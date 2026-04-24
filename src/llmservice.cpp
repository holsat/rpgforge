/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "llmservice.h"
#include <kwallet.h>
#include <KLocalizedString>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QPointer>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>

#ifdef QT_DEBUG
static void logRequest(const QUrl &url, const QJsonObject &body, const QNetworkRequest &netRequest) {
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    if (!settings.value(QStringLiteral("llm/enable_logging"), false).toBool()) {
        return;
    }

    const QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                            + QLatin1String("/llm_debug.log");
    QFile dbg(logPath);
    if (dbg.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&dbg);
        out << "\n--- NEW REQUEST ---\n";
        out << "URL: " << url.toString() << "\n";
        out << "HEADERS:\n";
        for (const auto &header : netRequest.rawHeaderList()) {
            if (header.toLower().contains("key") || header.toLower().contains("auth")) {
                out << "  " << header << ": [REDACTED]\n";
            } else {
                out << "  " << header << ": " << netRequest.rawHeader(header) << "\n";
            }
        }
        out << "BODY: " << QJsonDocument(body).toJson() << "\n";
    }
}
#endif

LLMService& LLMService::instance()
{
    static LLMService s_instance;
    return s_instance;
}

LLMService::LLMService(QObject *parent)
    : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
}

LLMService::~LLMService() = default;

void LLMService::setApiKey(LLMProvider provider, const QString &key)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Asynchronous);
    if (!wallet) return;

    QString walletKey;
    switch (provider) {
        case LLMProvider::OpenAI: walletKey = QStringLiteral("OpenAI_Key"); break;
        case LLMProvider::Anthropic: walletKey = QStringLiteral("Anthropic_Key"); break;
        case LLMProvider::Grok: walletKey = QStringLiteral("Grok_Key"); break;
        case LLMProvider::Gemini: walletKey = QStringLiteral("Gemini_Key"); break;
        case LLMProvider::LMStudio: walletKey = QStringLiteral("LMStudio_Key"); break;
        case LLMProvider::Ollama: return; // No key for Ollama usually
    }

    connect(wallet, &KWallet::Wallet::walletOpened, this, [this, wallet, walletKey, key, provider](bool opened) {
        if (opened) {
            if (!wallet->hasFolder(QStringLiteral("RPGForge"))) {
                wallet->createFolder(QStringLiteral("RPGForge"));
            }
            wallet->setFolder(QStringLiteral("RPGForge"));
            wallet->writePassword(walletKey, key);
        }
        wallet->deleteLater();
        // Invalidate cache so next read fetches the new value
        m_apiKeyCache.remove(provider);
    });
}

QString LLMService::apiKey(LLMProvider provider) const
{
    if (provider == LLMProvider::Ollama) return QString();

    // Return cached key if available, avoiding a synchronous KWallet open
    auto it = m_apiKeyCache.constFind(provider);
    if (it != m_apiKeyCache.constEnd()) return it.value();

    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) return QString();

    QString walletKey;
    switch (provider) {
        case LLMProvider::OpenAI: walletKey = QStringLiteral("OpenAI_Key"); break;
        case LLMProvider::Anthropic: walletKey = QStringLiteral("Anthropic_Key"); break;
        case LLMProvider::Grok: walletKey = QStringLiteral("Grok_Key"); break;
        case LLMProvider::Gemini: walletKey = QStringLiteral("Gemini_Key"); break;
        case LLMProvider::LMStudio: walletKey = QStringLiteral("LMStudio_Key"); break;
        case LLMProvider::Ollama: delete wallet; return QString();
    }

    QString key;
    if (wallet->hasFolder(QStringLiteral("RPGForge"))) {
        wallet->setFolder(QStringLiteral("RPGForge"));
        if (wallet->readPassword(walletKey, key) == 0) {
            m_apiKeyCache.insert(provider, key);
        }
    }
    delete wallet;
    return key;
}

static QUrl normalizeOllamaUrl(const QString &rawEndpoint, const QString &targetPath) {
    QString endpoint = rawEndpoint.trimmed();
    
    // Target path is usually "/api/chat" or "/api/embeddings"
    
    // If it's just a base URL like http://localhost:11434
    if (!endpoint.contains(QStringLiteral("/api/"))) {
        if (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
        return QUrl(endpoint + targetPath);
    }
    
    // If it has /api/... but it's the wrong one
    if (endpoint.contains(QStringLiteral("/api/generate"))) {
        endpoint.replace(QStringLiteral("/api/generate"), targetPath);
    } else if (endpoint.contains(QStringLiteral("/api/chat")) && targetPath != QStringLiteral("/api/chat")) {
        endpoint.replace(QStringLiteral("/api/chat"), targetPath);
    } else if (endpoint.contains(QStringLiteral("/api/embeddings")) && targetPath != QStringLiteral("/api/embeddings")) {
        endpoint.replace(QStringLiteral("/api/embeddings"), targetPath);
    }
    
    return QUrl(endpoint);
}

// ---------------------------------------------------------------------------
// Helper: map provider → settings key prefix
// ---------------------------------------------------------------------------
QString LLMService::providerSettingsKey(LLMProvider p)
{
    return QStringLiteral("llm/") + providerKey(p);
}

QString LLMService::providerKey(LLMProvider p)
{
    switch (p) {
        case LLMProvider::OpenAI:    return QStringLiteral("openai");
        case LLMProvider::Anthropic: return QStringLiteral("anthropic");
        case LLMProvider::Grok:      return QStringLiteral("grok");
        case LLMProvider::Gemini:    return QStringLiteral("gemini");
        case LLMProvider::Ollama:    return QStringLiteral("ollama");
        case LLMProvider::LMStudio:  return QStringLiteral("lmstudio");
    }
    return QStringLiteral("openai");
}

bool LLMService::providerFromKey(const QString &key, LLMProvider *out)
{
    static const QHash<QString, LLMProvider> map = {
        {QStringLiteral("openai"),    LLMProvider::OpenAI},
        {QStringLiteral("anthropic"), LLMProvider::Anthropic},
        {QStringLiteral("grok"),      LLMProvider::Grok},
        {QStringLiteral("gemini"),    LLMProvider::Gemini},
        {QStringLiteral("ollama"),    LLMProvider::Ollama},
        {QStringLiteral("lmstudio"),  LLMProvider::LMStudio},
    };
    auto it = map.constFind(key);
    if (it == map.cend()) return false;
    if (out) *out = it.value();
    return true;
}

// Default provider order used when llm/provider_order is unset. Matches the
// historical hardcoded preference so existing installs behave identically
// until the user reorders in Settings.
static const LLMProvider kDefaultProviderOrder[] = {
    LLMProvider::Gemini, LLMProvider::Anthropic, LLMProvider::OpenAI,
    LLMProvider::Grok,   LLMProvider::Ollama,    LLMProvider::LMStudio,
};

QList<LLMProvider> LLMService::readProviderOrderFromSettings()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const QStringList stored = settings.value(QStringLiteral("llm/provider_order")).toStringList();

    QList<LLMProvider> result;
    if (stored.isEmpty()) {
        for (LLMProvider p : kDefaultProviderOrder) result.append(p);
        return result;
    }

    for (const QString &k : stored) {
        LLMProvider p;
        if (providerFromKey(k.trimmed(), &p) && !result.contains(p)) {
            result.append(p);
        }
    }
    // Append anything missing (new enum values added after the user's last
    // save) at the tail so upgrades don't silently hide a provider.
    for (LLMProvider p : kDefaultProviderOrder) {
        if (!result.contains(p)) result.append(p);
    }
    return result;
}

bool LLMService::isProviderEnabled(LLMProvider provider)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    return settings.value(providerSettingsKey(provider) + QStringLiteral("/enabled"),
                          true).toBool();
}

QStringList LLMService::filterEmbeddingModels(LLMProvider /*provider*/,
                                              const QStringList &allModels)
{
    QStringList out;
    for (const QString &m : allModels) {
        if (m.contains(QStringLiteral("embed"), Qt::CaseInsensitive)
            || m.contains(QStringLiteral("similarity"), Qt::CaseInsensitive)) {
            out.append(m);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Cooldown tracker: records per-{provider, model} retry-after windows from
// 429 responses and lets dispatchers short-circuit before hitting the network.
// ---------------------------------------------------------------------------
QString LLMService::cooldownKey(LLMProvider provider, const QString &model)
{
    return LLMService::providerSettingsKey(provider) + QStringLiteral("|") + model;
}

bool LLMService::isCooledDown(LLMProvider provider, const QString &model,
                              qint64 *expiresAtMsOut, QString *reasonOut) const
{
    const QString key = cooldownKey(provider, model);
    auto it = m_cooldowns.constFind(key);
    if (it == m_cooldowns.cend()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (it.value().expiresAtMs <= now) {
        // Lazily drop expired entries so the hash doesn't grow unbounded.
        m_cooldowns.remove(key);
        return false;
    }
    if (expiresAtMsOut) *expiresAtMsOut = it.value().expiresAtMs;
    if (reasonOut)      *reasonOut      = it.value().reason;
    return true;
}

void LLMService::recordCooldown(LLMProvider provider, const QString &model,
                                int seconds, const QString &reason)
{
    if (seconds <= 0) return;
    CooldownEntry entry;
    entry.expiresAtMs = QDateTime::currentMSecsSinceEpoch()
                        + static_cast<qint64>(seconds) * 1000;
    entry.reason = reason;
    m_cooldowns.insert(cooldownKey(provider, model), entry);
    qWarning().noquote()
        << "LLM cooldown recorded:" << providerName(provider) << "/" << model
        << "for" << seconds << "seconds:" << reason;
}

void LLMService::clearCooldown(LLMProvider provider, const QString &model)
{
    m_cooldowns.remove(cooldownKey(provider, model));
}

// Parse protobuf-duration-ish strings: "11617s", "3h13m37.37s", "17m0.5s",
// "30.2s". Returns 0 on no match so callers can decide a default window.
int LLMService::parseRetryDelaySeconds(const QString &delayStr)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(?:(\d+)h)?(?:(\d+)m)?(?:([\d.]+)s)?\s*$)"));
    const QRegularExpressionMatch m = re.match(delayStr);
    if (!m.hasMatch()) return 0;
    const int hours   = m.captured(1).toInt();
    const int minutes = m.captured(2).toInt();
    const double seconds = m.captured(3).toDouble();
    const int total = hours * 3600 + minutes * 60
                      + static_cast<int>(std::ceil(seconds));
    return total;
}

// Google Cloud 429 responses carry a retryDelay either inline in
// error.details[].retryDelay (when the detail @type ends with "RetryInfo")
// or embedded in error.message as "Please retry in <dur>.". We parse both
// so cooldowns also apply to providers that only include the human text.
int LLMService::extractRetryDelaySecondsFromErrorBody(const QByteArray &body)
{
    if (body.isEmpty()) return 0;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return 0;
    const QJsonObject err = doc.object().value(QStringLiteral("error")).toObject();

    // Preferred: structured details[] entry of type google.rpc.RetryInfo.
    const QJsonArray details = err.value(QStringLiteral("details")).toArray();
    for (const QJsonValue &v : details) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("@type")).toString();
        if (!type.endsWith(QStringLiteral("RetryInfo"))) continue;
        const QString delay = o.value(QStringLiteral("retryDelay")).toString();
        const int s = parseRetryDelaySeconds(delay);
        if (s > 0) return s;
    }

    // Fallback: scrape the human-readable message.
    const QString msg = err.value(QStringLiteral("message")).toString();
    static const QRegularExpression msgRe(
        QStringLiteral(R"(retry in\s+([0-9hms.]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = msgRe.match(msg);
    if (m.hasMatch()) {
        const int s = parseRetryDelaySeconds(m.captured(1));
        if (s > 0) return s;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Is the user's configuration complete enough for requests to this provider
// to have a chance of succeeding? Cloud providers need API key + model;
// local providers need endpoint + model (API key is optional).
// ---------------------------------------------------------------------------
bool LLMService::isProviderConfigured(LLMProvider provider) const
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const QString prefix = providerSettingsKey(provider);
    const QString model = settings.value(prefix + QStringLiteral("/model")).toString().trimmed();
    if (model.isEmpty()) return false;

    switch (provider) {
        case LLMProvider::OpenAI:
        case LLMProvider::Anthropic:
        case LLMProvider::Grok:
        case LLMProvider::Gemini:
            // Cloud providers: require a stored API key.
            return !apiKey(provider).isEmpty();
        case LLMProvider::Ollama:
        case LLMProvider::LMStudio:
            // Local providers: require an endpoint (defaults exist, but the
            // user may have blanked them intentionally). API key optional.
            return !settings.value(prefix + QStringLiteral("/endpoint")).toString().trimmed().isEmpty();
    }
    return false;
}

// ---------------------------------------------------------------------------
// Compose a default fallback chain from the user's ordered provider list in
// QSettings (llm/provider_order). Respects the per-provider enable flag so
// users can temporarily pause a provider without deleting its credentials.
// The primary is excluded so the caller doesn't loop on the exhausted entry.
// ---------------------------------------------------------------------------
QList<QPair<LLMProvider, QString>> LLMService::composeDefaultFallbackChain(LLMProvider primary) const
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QList<QPair<LLMProvider, QString>> chain;
    for (LLMProvider p : readProviderOrderFromSettings()) {
        if (p == primary) continue;
        if (!isProviderEnabled(p)) continue;
        if (!isProviderConfigured(p)) continue;
        const QString model = settings.value(
            providerSettingsKey(p) + QStringLiteral("/model")).toString().trimmed();
        if (model.isEmpty()) continue;
        chain.append(qMakePair(p, model));
    }
    return chain;
}

// ---------------------------------------------------------------------------
// Try the next usable entry in the fallback chain (user-supplied or
// auto-composed). "Usable" = provider is configured AND the {provider, model}
// pair is not currently in a cooldown window. Re-dispatches the request with
// the swapped {provider, model} and a trimmed chain so the fallback can
// itself cascade. Streaming is not plumbed here; only non-streaming consumers
// (LoreKeeper dossiers, Analyzer, Synopsis) benefit today.
// ---------------------------------------------------------------------------
bool LLMService::tryFallback(const LLMRequest &request, const NonStreamCallback &nonStreamCallback,
                              const QString &previousError)
{
    // Use the caller-supplied chain if any, else auto-compose from configured
    // providers (excluding the primary).
    QList<QPair<LLMProvider, QString>> chain = request.fallbackChain;
    if (chain.isEmpty()) {
        chain = composeDefaultFallbackChain(request.provider);
    }
    if (chain.isEmpty()) return false;

    // Find the first entry that is both configured and not cooled down.
    while (!chain.isEmpty()) {
        const auto next = chain.takeFirst();
        if (next.second.isEmpty()) continue;                  // no model → skip
        if (!isProviderConfigured(next.first)) continue;      // user hasn't set it up
        if (isCooledDown(next.first, next.second)) continue;  // still rate-limited

        LLMRequest retried = request;
        retried.provider = next.first;
        retried.model = next.second;
        retried.fallbackChain = chain; // remainder, so we keep cascading
        const QString serviceLabel = retried.serviceName.isEmpty()
            ? providerName(retried.provider)
            : retried.serviceName;
        qWarning().noquote()
            << "LLM fallback: switching" << serviceLabel
            << "to" << providerName(retried.provider) << "/" << retried.model
            << "(previous error:" << previousError.left(160) << ")";

        // Both modes re-enter via validateModelThenDispatch. For streaming
        // (nonStreamCallback empty) the dispatch will re-emit requestStarted
        // with a fresh LLMService stream ID; RagAssistService's
        // responseChunk / errorOccurred listeners don't filter on that ID,
        // so they continue to route chunks/errors to the same RagAssist-
        // level request. The chat panel sees an uninterrupted turn.
        validateModelThenDispatch(retried, nonStreamCallback);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Resolve the model: prefer request.model, else read settings (no fallback).
// ---------------------------------------------------------------------------
QString LLMService::resolvedModel(const LLMRequest &request) const
{
    if (!request.model.isEmpty()) return request.model;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    return settings.value(LLMService::providerSettingsKey(request.provider) + QStringLiteral("/model")).toString();
}

// ---------------------------------------------------------------------------
// Thin public entry points — both delegate to validateModelThenDispatch.
// ---------------------------------------------------------------------------
void LLMService::sendRequest(const LLMRequest &request)
{
    validateModelThenDispatch(request, nullptr);
}

// ---------------------------------------------------------------------------
// Validate model against the session cache, fetching the list if needed.
// Dispatches immediately if valid; emits modelNotFound() and stores a pending
// request if the model is absent from the provider's current model list.
// ---------------------------------------------------------------------------
void LLMService::validateModelThenDispatch(const LLMRequest &request,
                                           NonStreamCallback nonStreamCallback)
{
    const QString model = resolvedModel(request);
    qDebug() << "LLM Service: Validating model" << model << "for provider" << static_cast<int>(request.provider) << "Service:" << request.serviceName;
    if (model.isEmpty()) {
        const QString err = i18n("No model configured. Open Settings and enter a model name for this provider.");
        if (nonStreamCallback) {
            qWarning() << "LLM: no model configured — aborting request";
            nonStreamCallback(QString(), err);
        } else {
            handleError(err);
        }
        return;
    }

    // Short-circuit: if this {provider, model} pair is in a recorded cooldown
    // window from a prior 429, don't hit the network at all. Emit the stored
    // reason and invoke the non-streaming callback with an empty response +
    // error string so the consumer's error path fires just as if the request
    // failed.
    {
        qint64 expiresAt = 0;
        QString reason;
        if (isCooledDown(request.provider, model, &expiresAt, &reason)) {
            const qint64 remainingMs = expiresAt - QDateTime::currentMSecsSinceEpoch();
            const int remainingSec = static_cast<int>(qMax<qint64>(1, (remainingMs + 999) / 1000));
            const QString when = QDateTime::fromMSecsSinceEpoch(expiresAt)
                                    .toString(QStringLiteral("HH:mm:ss"));
            const QString providerLabel = request.serviceName.isEmpty()
                ? providerName(request.provider)
                : request.serviceName;
            const QString msg = i18n(
                "%1: model '%2' is cooling down (retry after %3, in %4s). Reason: %5",
                providerLabel, model, when, remainingSec,
                reason.isEmpty() ? i18n("rate-limited by provider") : reason);
            qWarning().noquote() << "LLM cooldown short-circuit:" << msg;

            // Try fallback before notifying the caller. If a configured
            // alternative provider exists and isn't also cooled down, we
            // swap to it silently; the user sees an uninterrupted result.
            // Applies to both modes: non-streaming dossier generation and
            // streaming chat alike.
            if (tryFallback(request, nonStreamCallback, msg)) {
                return;
            }

            if (nonStreamCallback) {
                QPointer<LLMService> weakThis(this);
                QMetaObject::invokeMethod(this, [weakThis, msg, nonStreamCallback]() {
                    if (weakThis) Q_EMIT weakThis->errorOccurred(msg);
                    nonStreamCallback(QString(), msg);
                }, Qt::QueuedConnection);
            } else {
                handleError(msg);
            }
            return;
        }
    }

    const LLMProvider provider = request.provider;

    // Use cached list if available.
    if (m_modelCache.contains(provider)) {
        const QStringList &cached = m_modelCache[provider];
        if (cached.isEmpty() || cached.contains(model)) {
            // Empty cache = fetch failed previously; give the request a chance.
            dispatchRequest(request, model, nonStreamCallback);
        } else {
            // Model not in cache for this provider. Before bothering the
            // user with a picker dialog, try the fallback chain — if
            // another configured provider can take this request silently,
            // that's preferable.
            const QString msg = i18n("Model '%1' not available from %2; trying fallback.",
                                     model, providerName(provider));
            if (tryFallback(request, nonStreamCallback, msg)) {
                return;
            }
            if (!m_isShowingModelDialog) {
                m_hasPendingRequest = true;
                m_pendingRequest = {request, nonStreamCallback};
                m_isShowingModelDialog = true;
                Q_EMIT modelNotFound(provider, model, cached, request.serviceName);
            }
        }
        return;
    }

    // First use for this provider this session — fetch the model list.
    QPointer<LLMService> weakThis(this);
    fetchModels(provider, [weakThis, request, model, nonStreamCallback](const QStringList &available) {
        if (!weakThis) return;
        weakThis->m_modelCache[request.provider] = available; // cache even if empty

        if (available.isEmpty() || available.contains(model)) {
            weakThis->dispatchRequest(request, model, nonStreamCallback);
        } else {
            const QString msg = i18n("Model '%1' not available from %2; trying fallback.",
                                     model, providerName(request.provider));
            if (weakThis->tryFallback(request, nonStreamCallback, msg)) {
                return;
            }
            if (!weakThis->m_isShowingModelDialog) {
                weakThis->m_hasPendingRequest = true;
                weakThis->m_pendingRequest = {request, nonStreamCallback};
                weakThis->m_isShowingModelDialog = true;
                Q_EMIT weakThis->modelNotFound(request.provider, model, available, request.serviceName);
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Retry the pending request with a user-selected replacement model.
// Also persists the new model to settings so subsequent requests use it.
// ---------------------------------------------------------------------------
void LLMService::retryWithModel(const QString &newModel)
{
    if (!m_hasPendingRequest) return;
    PendingRequest pending = m_pendingRequest;
    m_hasPendingRequest = false;

    // Update cache so this model is considered valid going forward.
    if (!m_modelCache[pending.request.provider].contains(newModel))
        m_modelCache[pending.request.provider].append(newModel);

    // Persist to settings. Prefer specific settingsKey if provided, otherwise update global provider default.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString key = pending.request.settingsKey;
    if (key.isEmpty()) {
        key = LLMService::providerSettingsKey(pending.request.provider) + QStringLiteral("/model");
    }
    settings.setValue(key, newModel);
    settings.sync();

    LLMRequest updated = pending.request;
    updated.model = newModel;
    dispatchRequest(updated, newModel, pending.nonStreamCallback);
}

// ---------------------------------------------------------------------------
// Build and POST the request. nonStreamCallback==nullptr → streaming path.
// ---------------------------------------------------------------------------
void LLMService::dispatchRequest(const LLMRequest &request, const QString &model,
                                  NonStreamCallback nonStreamCallback,
                                  bool isRetry)
{
    const bool streaming = (nonStreamCallback == nullptr);

    if (streaming && !isRetry) {
        // Fresh streaming request: reset retry counter, clear accumulated
        // response / stream buffers, store the canonical request for any
        // subsequent retries. A retry re-enters this function with isRetry
        // set so these fields survive untouched.
        if (m_activeReply) cancelRequest();
        m_activeRequest = request;
        m_activeRequest.model = model;
        m_activeProvider = request.provider;
        m_retryCount = 0;
        m_fullResponse.clear();
        m_streamBuffer.clear();
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QJsonObject body;
    const QString key = apiKey(request.provider);

    QJsonArray messagesArray;
    for (const auto &msg : request.messages) {
        QJsonObject m;
        m[QStringLiteral("role")] = msg.role;
        m[QStringLiteral("content")] = msg.content;
        messagesArray.append(m);
    }

    if (request.provider == LLMProvider::Gemini) {
        // Native Gemini API (v1beta). Distinct request/response shape
        // from OpenAI — we used to go through Google's OpenAI-compat
        // shim (/v1beta/openai/chat/completions) but it was picky about
        // model naming and produced 404s for models the native API
        // accepts. Native endpoint structure:
        //
        //   POST /v1beta/models/<model>:generateContent?key=<key>         (non-streaming)
        //   POST /v1beta/models/<model>:streamGenerateContent?alt=sse&key=<key>  (streaming)
        //
        // Body:
        //   {
        //     "contents":          [ {"role": "user"|"model", "parts": [{"text": "..."}]} ],
        //     "systemInstruction": {"parts": [{"text": "..."}]},          (optional; OpenAI "system" → this)
        //     "generationConfig":  {"temperature": 0.7, "maxOutputTokens": 4096}
        //   }
        //
        // Response (non-streaming):
        //   { "candidates": [ {"content": {"parts": [{"text": "..."}]}} ] }
        //
        // Streaming: SSE, each "data: {...}" chunk is a partial
        // candidates object. Parsed by processGeminiChunk().
        const QString apiBase = settings.value(
            QStringLiteral("llm/gemini/api_base"),
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta")).toString();

        // Normalise model: Gemini's REST paths expect bare names. Strip
        // the "models/" prefix if present; we re-add it in the URL.
        QString bareModel = model;
        if (bareModel.startsWith(QLatin1String("models/"))) {
            bareModel = bareModel.mid(7);
        }
        if (bareModel.isEmpty()) {
            qWarning() << "LLM Gemini: no model configured — aborting";
            if (nonStreamCallback) nonStreamCallback(QString(), i18n("Gemini: no model configured"));
            return;
        }

        const QString method = streaming
            ? QStringLiteral("streamGenerateContent?alt=sse&key=")
            : QStringLiteral("generateContent?key=");
        url = QUrl(QStringLiteral("%1/models/%2:%3%4")
                       .arg(apiBase, bareModel, method, key));

        // Split messages into systemInstruction (the system role) plus
        // contents (user/assistant turns). Gemini uses "model" for the
        // assistant role; translate as we go.
        QString systemText;
        QJsonArray contents;
        for (const auto &msg : request.messages) {
            if (msg.role == QLatin1String("system")) {
                if (!systemText.isEmpty()) systemText += QLatin1Char('\n');
                systemText += msg.content;
                continue;
            }
            const QString role = (msg.role == QLatin1String("assistant"))
                ? QStringLiteral("model")
                : QStringLiteral("user");
            QJsonObject turn;
            turn[QStringLiteral("role")] = role;
            QJsonArray parts;
            QJsonObject partObj;
            partObj[QStringLiteral("text")] = msg.content;
            parts.append(partObj);
            turn[QStringLiteral("parts")] = parts;
            contents.append(turn);
        }

        body[QStringLiteral("contents")] = contents;
        if (!systemText.isEmpty()) {
            QJsonObject sys;
            QJsonArray sysParts;
            QJsonObject sysPart;
            sysPart[QStringLiteral("text")] = systemText;
            sysParts.append(sysPart);
            sys[QStringLiteral("parts")] = sysParts;
            body[QStringLiteral("systemInstruction")] = sys;
        }
        QJsonObject genConfig;
        genConfig[QStringLiteral("temperature")] = request.temperature;
        genConfig[QStringLiteral("maxOutputTokens")] = request.maxTokens;
        body[QStringLiteral("generationConfig")] = genConfig;

        // Log URL with the key redacted so we can debug endpoint issues
        // without leaking credentials into the debug log.
        QString loggedUrl = url.toString();
        static const QRegularExpression keyRe(QStringLiteral("([?&])key=[^&]+"));
        loggedUrl.replace(keyRe, QStringLiteral("\\1key=REDACTED"));
        qInfo().noquote() << "LLM chat dispatch: provider= 4 (Gemini-native)"
                          << "model=" << bareModel
                          << "service=" << request.serviceName
                          << "endpoint=" << loggedUrl;
    }
    else if (request.provider == LLMProvider::OpenAI
        || request.provider == LLMProvider::Grok
        || request.provider == LLMProvider::LMStudio) {
        const QString sk = LLMService::providerSettingsKey(request.provider);
        const QString defaultEndpoint =
            (request.provider == LLMProvider::Grok)
                ? QStringLiteral("https://api.x.ai/v1/chat/completions")
            : (request.provider == LLMProvider::LMStudio)
                ? QStringLiteral("http://localhost:1234/v1/chat/completions")
            : QStringLiteral("https://api.openai.com/v1/chat/completions");

        url = QUrl(settings.value(sk + QStringLiteral("/endpoint"), defaultEndpoint).toString());
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());

        body[QStringLiteral("model")] = model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = streaming;
        body[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("max_tokens")] = request.maxTokens;

        qInfo().noquote() << "LLM chat dispatch: provider="
                          << static_cast<int>(request.provider)
                          << "model=" << model
                          << "service=" << request.serviceName
                          << "endpoint=" << url.toString();
    }
    else if (request.provider == LLMProvider::Anthropic) {
        QString endpoint = settings.value(QStringLiteral("llm/anthropic/endpoint"),
            QStringLiteral("https://api.anthropic.com/v1/messages")).toString().trimmed();
        if (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
        url = QUrl(endpoint);

        netRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        netRequest.setRawHeader("x-api-key", key.toUtf8());
        netRequest.setRawHeader("anthropic-version", "2023-06-01");
        netRequest.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (streaming) netRequest.setRawHeader("Accept", "text/event-stream");

        body[QStringLiteral("model")] = model;
        QStringList systemParts;
        QJsonArray anthropicMessages;
        for (const auto &msg : request.messages) {
            if (msg.role == QLatin1String("system")) {
                QString content = msg.content.trimmed();
                if (!content.isEmpty()) systemParts << content;
            } else {
                QJsonObject m;
                m[QStringLiteral("role")] = msg.role;
                m[QStringLiteral("content")] = msg.content;
                anthropicMessages.append(m);
            }
        }
        if (!systemParts.isEmpty())
            body[QStringLiteral("system")] = systemParts.join(QStringLiteral("\n\n"));
        if (anthropicMessages.isEmpty())
            anthropicMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Hello")}});
        body[QStringLiteral("messages")] = anthropicMessages;
        body[QStringLiteral("stream")] = streaming;
        body[QStringLiteral("max_tokens")] = request.maxTokens > 0 ? request.maxTokens : 4096;
        body[QStringLiteral("temperature")] = request.temperature;
    }
    else if (request.provider == LLMProvider::Ollama) {
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"),
            QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/chat"));
        body[QStringLiteral("model")] = model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = streaming;
        QJsonObject options;
        options[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("options")] = options;
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
#ifdef QT_DEBUG
    logRequest(url, body, netRequest);
#endif

    QNetworkReply *reply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));

    if (streaming) {
        m_activeReply = reply;
        if (!isRetry) {
            // Only announce a new stream on the first attempt. Emitting
            // requestStarted on every retry caused the chat panel to
            // append a new "AI is thinking" placeholder per retry — which,
            // paired with the earlier retry-counter-reset bug, produced
            // hundreds of stacked placeholders on recurrent 4xx errors.
            m_currentStreamId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            Q_EMIT requestStarted(m_currentStreamId);
        }
        connect(reply, &QNetworkReply::readyRead, this, &LLMService::handleReadyRead);
        connect(reply, &QNetworkReply::finished, this, &LLMService::handleFinished);
    } else {
        QPointer<LLMService> weakThis(this);
        // Capture the full request so the error branch can attempt a
        // fallback-chain retry with a configured alternative provider.
        connect(reply, &QNetworkReply::finished, this, [weakThis, reply, nonStreamCallback, originalRequest = request, model = model]() {
            const LLMProvider provider = originalRequest.provider;
            const QString serviceName = originalRequest.serviceName;
            QString result;
            QString errorMessage;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (!doc.isNull() && doc.isObject()) {
                    if (provider == LLMProvider::Gemini) {
                        // Native Gemini shape: candidates[0].content.parts[0].text.
                        // Multiple parts can exist on tool-calling responses; for
                        // plain text generation there's just one.
                        QJsonArray candidates = doc.object().value(QStringLiteral("candidates")).toArray();
                        if (!candidates.isEmpty()) {
                            QJsonArray parts = candidates.at(0).toObject()
                                .value(QStringLiteral("content")).toObject()
                                .value(QStringLiteral("parts")).toArray();
                            QString combined;
                            for (const QJsonValue &p : parts) {
                                combined += p.toObject().value(QStringLiteral("text")).toString();
                            }
                            result = combined;
                        }
                    } else if (provider == LLMProvider::OpenAI
                        || provider == LLMProvider::Grok
                        || provider == LLMProvider::LMStudio) {
                        QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
                        if (!choices.isEmpty())
                            result = choices.at(0).toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
                    } else if (provider == LLMProvider::Anthropic) {
                        QJsonArray content = doc.object().value(QStringLiteral("content")).toArray();
                        if (!content.isEmpty())
                            result = content.at(0).toObject().value(QStringLiteral("text")).toString();
                    } else if (provider == LLMProvider::Ollama) {
                        result = doc.object().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
                    }
                }
            } else {
                // Build a user-facing error message with HTTP status + provider's
                // error body (truncated) so callers like the Enhance Prompt dialog
                // can show a meaningful failure reason instead of "empty response".
                const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = reply->readAll();
                QString bodyMsg;
                // Try to extract a structured error message from common provider
                // shapes (OpenAI: error.message, Gemini: error.message, Anthropic:
                // error.message). Fall back to raw body truncated to 500 chars.
                QJsonDocument edoc = QJsonDocument::fromJson(body);
                if (edoc.isObject()) {
                    QJsonObject eobj = edoc.object().value(QStringLiteral("error")).toObject();
                    bodyMsg = eobj.value(QStringLiteral("message")).toString();
                }
                if (bodyMsg.isEmpty()) {
                    bodyMsg = QString::fromUtf8(body).left(500);
                }
                const QString providerLabel = serviceName.isEmpty()
                    ? QStringLiteral("LLM")
                    : serviceName;
                errorMessage = QStringLiteral("%1 request failed")
                    .arg(providerLabel);
                if (httpStatus > 0) {
                    errorMessage += QStringLiteral(" (HTTP %1)").arg(httpStatus);
                }
                errorMessage += QStringLiteral(": %1").arg(reply->errorString());
                if (!bodyMsg.isEmpty()) {
                    errorMessage += QStringLiteral(" — %1").arg(bodyMsg);
                }
                qWarning() << "LLM Non-Streaming Error:" << errorMessage;

                // On HTTP 429, record a cooldown for {provider, model} so
                // subsequent requests within the retry window short-circuit
                // instead of hammering the quota-exhausted endpoint.
                if (weakThis && httpStatus == 429 && !model.isEmpty()) {
                    int seconds = extractRetryDelaySecondsFromErrorBody(body);
                    if (seconds <= 0) {
                        // Provider didn't give us a retry hint — fall back to a
                        // conservative 60s window so we don't spam but also
                        // recover quickly for transient per-minute limits.
                        seconds = 60;
                    }
                    const QString reasonText = bodyMsg.isEmpty()
                        ? QStringLiteral("HTTP 429 (rate-limited)")
                        : bodyMsg.left(240);
                    weakThis->recordCooldown(provider, model, seconds, reasonText);
                }

                // Attempt to fall back to a configured alternative provider
                // (auto-composed if the request didn't bring its own chain).
                // If a fallback dispatches, our callback is forwarded to it
                // and we must NOT also invoke it here with the error.
                if (weakThis && nonStreamCallback
                    && weakThis->tryFallback(originalRequest, nonStreamCallback, errorMessage))
                {
                    reply->deleteLater();
                    return;
                }

                if (weakThis) {
                    QMetaObject::invokeMethod(weakThis.data(), [weakThis, errorMessage]() {
                        if (weakThis)
                            Q_EMIT weakThis->errorOccurred(errorMessage);
                    }, Qt::QueuedConnection);
                }
            }

            if (nonStreamCallback) {
                if (weakThis) {
                    QMetaObject::invokeMethod(weakThis.data(), [nonStreamCallback, result, errorMessage]() {
                        nonStreamCallback(result, errorMessage);
                    }, Qt::QueuedConnection);
                } else {
                    // LLMService was destroyed, but we can still run the callback if it doesn't depend on LLMService state.
                    // However, most callbacks expect to update UI/state, so we skip if service is gone.
                }
            }
            reply->deleteLater();
        });
    }
}

void LLMService::cancelRequest()
{
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

void LLMService::handleReadyRead()
{
    if (!m_activeReply) return;

    // Append incoming bytes to the stream buffer. TCP may deliver partial SSE
    // lines, so we only process complete newline-terminated data below.
    m_streamBuffer.append(m_activeReply->readAll());

    switch (m_activeProvider) {
        case LLMProvider::OpenAI:
        case LLMProvider::Grok:
        case LLMProvider::LMStudio: processOpenAIChunk(m_streamBuffer); break;
        case LLMProvider::Anthropic: processAnthropicChunk(m_streamBuffer); break;
        case LLMProvider::Ollama: processOllamaChunk(m_streamBuffer); break;
        case LLMProvider::Gemini: processGeminiChunk(m_streamBuffer); break;
    }
}

void LLMService::handleFinished()
{
    if (!m_activeReply) return;

    if (m_activeReply->error() != QNetworkReply::NoError && m_activeReply->error() != QNetworkReply::OperationCanceledError) {
        // Read the server-side response body once; both the retry log and
        // the final error surface benefit from having the upstream's
        // actual rejection message (4xx bodies from Anthropic/OpenAI
        // contain the structured reason, e.g. "invalid model" or
        // "max_tokens too large").
        const QByteArray serverResponse = m_activeReply->readAll();
        const int httpStatus = m_activeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errorString = m_activeReply->errorString();

        // Don't retry client errors (4xx) — those are not transient. A
        // bad model name, malformed request, or invalid API key will fail
        // identically on every retry and only serve to spam the UI. 5xx
        // and connection errors still retry with the existing 3-attempt cap.
        const bool isClientError = (httpStatus >= 400 && httpStatus < 500);

        // On HTTP 429 record a cooldown for the active {provider, model}
        // before handing off to the error path; this blocks subsequent
        // requests in the retry-after window (see validateModelThenDispatch).
        if (httpStatus == 429 && !m_activeRequest.model.isEmpty()) {
            int seconds = extractRetryDelaySecondsFromErrorBody(serverResponse);
            if (seconds <= 0) seconds = 60;
            // Prefer the provider's structured error.message as the reason.
            QString reason;
            const QJsonDocument edoc = QJsonDocument::fromJson(serverResponse);
            if (edoc.isObject()) {
                reason = edoc.object().value(QStringLiteral("error")).toObject()
                             .value(QStringLiteral("message")).toString().left(240);
            }
            if (reason.isEmpty()) reason = QStringLiteral("HTTP 429 (rate-limited)");
            recordCooldown(m_activeRequest.provider, m_activeRequest.model, seconds, reason);
        }

        if (!isClientError && m_retryCount < 3) {
            m_retryCount++;
            qWarning().noquote() << "LLM Request failed, retrying" << m_retryCount << "... HTTP" << httpStatus
                                 << errorString
                                 << "- body:" << QString::fromUtf8(serverResponse).left(512);
            m_activeReply->deleteLater();
            m_activeReply = nullptr;
            // Call dispatchRequest directly with isRetry=true so the retry
            // doesn't reset m_retryCount or re-emit requestStarted (which
            // would spawn another chat placeholder).
            dispatchRequest(m_activeRequest, m_activeRequest.model, nullptr, /*isRetry=*/true);
            return;
        }

        QString errorMsg = QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(errorString);
        if (!serverResponse.isEmpty()) {
            errorMsg += QStringLiteral("\n") + QString::fromUtf8(serverResponse);
        }
        qWarning().noquote() << "LLM request terminal failure:" << errorMsg;

        // Streaming fallback: if nothing has streamed yet, silently try
        // the next configured provider. Skip the fallback once chunks
        // have been delivered — splicing output from two providers would
        // produce confusing double-text, and the user has already
        // received partial value from the primary.
        const bool noContentStreamed = m_fullResponse.isEmpty();
        if (noContentStreamed) {
            // Must null m_activeReply before dispatching the fallback so
            // the fallback's fresh stream setup doesn't see a stale reply
            // pointer and invoke cancelRequest() on the already-finished
            // reply we're about to delete.
            QNetworkReply *dyingReply = m_activeReply;
            m_activeReply = nullptr;
            LLMRequest reqCopy = m_activeRequest; // capture before dispatch mutates state
            if (tryFallback(reqCopy, nullptr, errorMsg)) {
                dyingReply->deleteLater();
                return;
            }
            // Fallback chain had nothing usable — restore state and fall
            // through to the error-surfacing path below.
            m_activeReply = dyingReply;
        }

        handleError(errorMsg);
    } else if (m_activeReply->error() == QNetworkReply::NoError) {
        Q_EMIT responseFinished(m_currentStreamId, m_fullResponse);
    }

    m_activeReply->deleteLater();
    m_activeReply = nullptr;
}

void LLMService::handleError(const QString &message)
{
    Q_EMIT errorOccurred(message);
}

void LLMService::processOpenAIChunk(QByteArray &buffer)
{
    // OpenAI SSE format: "data: {...}\n" lines. Process only complete lines;
    // leave any incomplete trailing data in the buffer for the next readyRead().
    while (true) {
        int nl = buffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = buffer.left(nl).trimmed();
        buffer.remove(0, nl + 1);
        if (!line.startsWith("data: ")) continue;
        QByteArray jsonBytes = line.mid(6).trimmed();
        if (jsonBytes == "[DONE]") continue;
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (!doc.isNull() && doc.isObject()) {
            QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                QString chunk = choices.at(0).toObject()
                    .value(QStringLiteral("delta")).toObject()
                    .value(QStringLiteral("content")).toString();
                if (!chunk.isEmpty()) {
                    m_fullResponse += chunk;
                    Q_EMIT responseChunk(m_currentStreamId, chunk);
                }
            }
        }
    }
}

void LLMService::processAnthropicChunk(QByteArray &buffer)
{
    // Anthropic SSE format: multi-line events separated by "\n\n".
    // Process only complete events (those ending with \n\n).
    while (true) {
        int sep = buffer.indexOf("\n\n");
        if (sep < 0) break;
        QByteArray event = buffer.left(sep).trimmed();
        buffer.remove(0, sep + 2);
        if (!event.contains("content_block_delta")) continue;
        int dataIdx = event.indexOf("data: ");
        if (dataIdx < 0) continue;
        QByteArray jsonBytes = event.mid(dataIdx + 6).trimmed();
        // Trim any trailing event lines after the JSON
        int nl = jsonBytes.indexOf('\n');
        if (nl >= 0) jsonBytes = jsonBytes.left(nl).trimmed();
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (!doc.isNull() && doc.isObject()) {
            QString chunk = doc.object().value(QStringLiteral("delta")).toObject()
                .value(QStringLiteral("text")).toString();
            if (!chunk.isEmpty()) {
                m_fullResponse += chunk;
                Q_EMIT responseChunk(m_currentStreamId, chunk);
            }
        }
    }
}

void LLMService::processGeminiChunk(QByteArray &buffer)
{
    // Gemini SSE streaming: each event is a single "data: {...}" line
    // followed by "\n\n". The JSON is a full candidates-list snapshot
    // whose parts[].text contains the chunk we want to append.
    //
    // Sample event:
    //   data: {"candidates":[{"content":{"parts":[{"text":"Hello"}],
    //          "role":"model"}}],"modelVersion":"gemini-2.5-pro"}
    //
    // (One-line in reality; wrapped here for readability.)
    while (true) {
        const int sep = buffer.indexOf("\n\n");
        if (sep < 0) break;
        QByteArray event = buffer.left(sep).trimmed();
        buffer.remove(0, sep + 2);

        const int dataIdx = event.indexOf("data: ");
        if (dataIdx < 0) continue;
        QByteArray jsonBytes = event.mid(dataIdx + 6).trimmed();

        const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (doc.isNull() || !doc.isObject()) continue;

        const QJsonArray candidates = doc.object().value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) continue;
        const QJsonArray parts = candidates.at(0).toObject()
            .value(QStringLiteral("content")).toObject()
            .value(QStringLiteral("parts")).toArray();

        QString chunk;
        for (const QJsonValue &p : parts) {
            chunk += p.toObject().value(QStringLiteral("text")).toString();
        }
        if (!chunk.isEmpty()) {
            m_fullResponse += chunk;
            Q_EMIT responseChunk(m_currentStreamId, chunk);
        }
    }
}

void LLMService::processOllamaChunk(QByteArray &buffer)
{
    // Ollama native format: newline-delimited JSON objects.
    // Process only complete lines; leave any partial line in the buffer.
    while (true) {
        int nl = buffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = buffer.left(nl).trimmed();
        buffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject()) {
            QString chunk = doc.object().value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("content")).toString();
            if (!chunk.isEmpty()) {
                m_fullResponse += chunk;
                Q_EMIT responseChunk(m_currentStreamId, chunk);
            }
        }
    }
}

void LLMService::generateEmbedding(LLMProvider provider, const QString &model, const QString &text, std::function<void(const QVector<float>&)> callback)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QJsonObject body;

    QString key = apiKey(provider);

    if (provider == LLMProvider::OpenAI || provider == LLMProvider::LMStudio) {
        const QString embModel = model.isEmpty()
            ? settings.value(QStringLiteral("llm/embedding_model")).toString()
            : model;
        if (embModel.isEmpty()) {
            qWarning() << "LLM: no embedding model configured. Set one in Settings → Embedding Model.";
            if (callback) callback(QVector<float>());
            return;
        }
        
        if (provider == LLMProvider::OpenAI) {
            url = QUrl(QStringLiteral("https://api.openai.com/v1/embeddings"));
            netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        } else {
            // LM Studio
            QString endpoint = settings.value(QStringLiteral("llm/lmstudio/endpoint"), 
                                              QStringLiteral("http://localhost:1234/v1/chat/completions")).toString();
            if (endpoint.contains(QStringLiteral("/chat/completions"))) {
                endpoint.replace(QStringLiteral("/chat/completions"), QStringLiteral("/embeddings"));
            } else if (!endpoint.contains(QStringLiteral("/embeddings"))) {
                endpoint += QStringLiteral("/embeddings");
            }
            url = QUrl(endpoint);
            if (!key.isEmpty()) netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        }
        body[QStringLiteral("model")] = embModel;
        body[QStringLiteral("input")] = text;
    } else if (provider == LLMProvider::Ollama) {
        const QString embModel = model.isEmpty()
            ? settings.value(QStringLiteral("llm/embedding_model")).toString()
            : model;
        if (embModel.isEmpty()) {
            qWarning() << "LLM: no embedding model configured. Set one in Settings → Embedding Model.";
            if (callback) callback(QVector<float>());
            return;
        }
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/embeddings"));
        body[QStringLiteral("model")] = embModel;
        body[QStringLiteral("prompt")] = text;
    } else if (provider == LLMProvider::Gemini) {
        // Gemini uses a dedicated embeddings endpoint with a fixed URL
        // structure. The user's `llm/gemini/endpoint` setting is their
        // CHAT endpoint (typically ".../v1beta/openai/chat/completions"
        // or ".../v1beta"), which we must NOT concatenate with
        // "/models/...:embedContent" — that produces the bogus URL
        // ".../openai/chat/completions/models/X:embedContent" that
        // Google returns 404 for.
        //
        // Instead: pin the embeddings base to Google's canonical URL.
        // If Google ever changes it, one hardcode update fixes every
        // deployment; users don't need to (and shouldn't have to)
        // customize this.
        const QString embedBase = QStringLiteral(
            "https://generativelanguage.googleapis.com/v1beta");

        QString embModel = settings.value(QStringLiteral("llm/embedding_model")).toString();
        if (embModel.isEmpty()) embModel = model;
        if (embModel.isEmpty()
            || (!embModel.contains(QLatin1String("embedding"), Qt::CaseInsensitive)
                && !embModel.contains(QLatin1String("embed"), Qt::CaseInsensitive))) {
            // User's model was a chat model. Force the current production
            // embedding model. text-embedding-004 is the default general-
            // purpose embedding endpoint Gemini exposes.
            embModel = QStringLiteral("models/text-embedding-004");
        }
        if (!embModel.startsWith(QLatin1String("models/"))) {
            embModel = QStringLiteral("models/") + embModel;
        }

        url = QUrl(QStringLiteral("%1/%2:embedContent?key=%3").arg(embedBase, embModel, key));

        QJsonObject contentObj;
        QJsonArray parts;
        QJsonObject part;
        part[QStringLiteral("text")] = text;
        parts.append(part);
        contentObj[QStringLiteral("parts")] = parts;
        body[QStringLiteral("content")] = contentObj;
        body[QStringLiteral("model")] = embModel;
    } else {
        // Anthropic, Grok: no embedding API available.
        qWarning() << "LLM: provider" << static_cast<int>(provider)
                   << "does not have an embeddings API — returning empty.";
        if (callback) callback(QVector<float>());
        return;
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QPointer<LLMService> weakThis(this);
    connect(reply, &QNetworkReply::finished, this, [weakThis, reply, callback, provider, model]() {
        QVector<float> embedding;
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isNull() && doc.isObject()) {
                if (provider == LLMProvider::OpenAI || provider == LLMProvider::LMStudio) {
                    QJsonArray dataArray = doc.object().value(QStringLiteral("data")).toArray();
                    if (!dataArray.isEmpty()) {
                        QJsonArray embeddingArray = dataArray.at(0).toObject().value(QStringLiteral("embedding")).toArray();
                        for (const QJsonValue &val : embeddingArray) {
                            embedding.append(val.toDouble());
                        }
                    }
                } else if (provider == LLMProvider::Ollama) {
                    QJsonArray embeddingArray = doc.object().value(QStringLiteral("embedding")).toArray();
                    for (const QJsonValue &val : embeddingArray) {
                        embedding.append(val.toDouble());
                    }
                } else if (provider == LLMProvider::Gemini) {
                    // Gemini's embedContent returns { "embedding": { "values": [...] } }
                    QJsonObject embObj = doc.object().value(QStringLiteral("embedding")).toObject();
                    QJsonArray values = embObj.value(QStringLiteral("values")).toArray();
                    for (const QJsonValue &val : values) {
                        embedding.append(val.toDouble());
                    }
                }
            }
            if (embedding.isEmpty()) {
                qWarning().noquote() << "LLM embedding: 200 OK but response parse produced empty vector. Body:"
                                      << QString::fromUtf8(body).left(512);
            }
        } else {
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // Redact any ?key=... query string from the error string —
            // Gemini's embedding endpoint puts the API key there, so the
            // raw errorString() leaks it into the log.
            QString err = reply->errorString();
            static const QRegularExpression keyRe(
                QStringLiteral("([?&])key=[^&\\s]+"));
            err.replace(keyRe, QStringLiteral("\\1key=REDACTED"));
            qWarning().noquote() << "LLM embedding failed: provider=" << static_cast<int>(provider)
                                  << "HTTP" << httpStatus
                                  << err
                                  << "- body:" << QString::fromUtf8(body).left(512);

            // Record a cooldown on 429 so KnowledgeBase::generateEmbeddingWithFallback
            // skips this {provider, model} pair until the retry window passes,
            // the same contract the chat paths use.
            if (weakThis && httpStatus == 429 && !model.isEmpty()) {
                int seconds = extractRetryDelaySecondsFromErrorBody(body);
                if (seconds <= 0) seconds = 60;
                QString reasonText;
                QJsonDocument edoc = QJsonDocument::fromJson(body);
                if (edoc.isObject()) {
                    reasonText = edoc.object().value(QStringLiteral("error"))
                                     .toObject().value(QStringLiteral("message"))
                                     .toString().left(240);
                }
                if (reasonText.isEmpty())
                    reasonText = QStringLiteral("HTTP 429 (embedding rate-limited)");
                weakThis->recordCooldown(provider, model, seconds, reasonText);
            }
        }
        if (callback) {
            callback(embedding);
        }
        reply->deleteLater();
    });
}

void LLMService::sendNonStreamingRequest(const LLMRequest &request, std::function<void(const QString&)> callback)
{
    // Legacy 1-arg callback: adapt to the detailed 2-arg form by dropping the
    // error string. New code should prefer sendNonStreamingRequestDetailed()
    // so it can distinguish "empty response" from "provider returned 429".
    NonStreamCallback adapter = [callback = std::move(callback)](const QString &response, const QString &) {
        if (callback) callback(response);
    };
    validateModelThenDispatch(request, std::move(adapter));
}

void LLMService::sendNonStreamingRequestDetailed(const LLMRequest &request, NonStreamCallback callback)
{
    validateModelThenDispatch(request, std::move(callback));
}

void LLMService::pingAnthropic(std::function<void(bool, const QString&)> callback)
{
    // Fetch the live model list first — if that succeeds the API key is valid,
    // and we use the first available model for a 1-token ping (no hardcoded names).
    fetchModels(LLMProvider::Anthropic, [this, callback](const QStringList &models) {
        if (models.isEmpty()) {
            callback(false, i18n("API test failed — could not retrieve model list. Check your API key."));
            return;
        }
        LLMRequest req;
        req.provider = LLMProvider::Anthropic;
        req.model = models.first(); // guaranteed valid — just fetched
        req.maxTokens = 1;
        req.stream = false;
        req.messages.append({QStringLiteral("user"), QStringLiteral("ping")});

        // Bypass the cache-validation step — we already know the model is live.
        dispatchRequest(req, req.model, [callback](const QString &result, const QString &error) {
            if (!result.isEmpty())
                callback(true, QString());
            else if (!error.isEmpty())
                callback(false, error);
            else
                callback(false, i18n("API test failed. Check your key and logs."));
        });
    });
}

void LLMService::fetchModels(LLMProvider provider, std::function<void(const QStringList&)> callback)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QString key = apiKey(provider);

    if (provider == LLMProvider::Gemini) {
        // Native Gemini models endpoint: GET /v1beta/models?key=<key>.
        // No Bearer header — auth is the query param, same as generation.
        const QString apiBase = settings.value(
            QStringLiteral("llm/gemini/api_base"),
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta")).toString();
        url = QUrl(QStringLiteral("%1/models?key=%2").arg(apiBase, key));
        // No netRequest.setRawHeader — Gemini wants the key in the URL only.
    } else if (provider == LLMProvider::OpenAI
        || provider == LLMProvider::Grok
        || provider == LLMProvider::LMStudio) {
        QString settingsKey = (provider == LLMProvider::Grok) ? QStringLiteral("llm/grok")
                            : (provider == LLMProvider::LMStudio) ? QStringLiteral("llm/lmstudio")
                            : QStringLiteral("llm/openai");
        QString fallback = (provider == LLMProvider::Grok) ? QStringLiteral("https://api.x.ai/v1/chat/completions")
                         : (provider == LLMProvider::LMStudio) ? QStringLiteral("http://localhost:1234/v1/chat/completions")
                         : QStringLiteral("https://api.openai.com/v1/chat/completions");
        QString endpoint = settings.value(settingsKey + QStringLiteral("/endpoint"), fallback).toString().trimmed();
        if (endpoint.endsWith(QStringLiteral("/chat/completions"))) {
            endpoint.replace(QStringLiteral("/chat/completions"), QStringLiteral("/models"));
        } else if (!endpoint.contains(QStringLiteral("/models"))) {
            endpoint = fallback.replace(QStringLiteral("/chat/completions"), QStringLiteral("/models"));
        }
        url = QUrl(endpoint);
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    } else if (provider == LLMProvider::Anthropic) {
        // Base is api.anthropic.com/v1/models
        QString endpoint = settings.value(QStringLiteral("llm/anthropic/endpoint"), QStringLiteral("https://api.anthropic.com/v1/messages")).toString().trimmed();
        if (endpoint.endsWith(QStringLiteral("/messages"))) {
            endpoint.replace(QStringLiteral("/messages"), QStringLiteral("/models"));
        } else if (!endpoint.contains(QStringLiteral("/models"))) {
            endpoint = QStringLiteral("https://api.anthropic.com/v1/models");
        }
        url = QUrl(endpoint);
        netRequest.setRawHeader("x-api-key", key.toUtf8());
        netRequest.setRawHeader("anthropic-version", "2023-06-01");
    } else if (provider == LLMProvider::Ollama) {
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/tags"));
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    netRequest.setRawHeader("User-Agent", "Mozilla/5.0 RPGForge/1.0");

#ifdef QT_DEBUG
    logRequest(url, QJsonObject(), netRequest);
#endif

    QNetworkReply *reply = m_networkManager->get(netRequest);
    connect(reply, &QNetworkReply::finished, this, [reply, callback, provider, url]() {
        QStringList models;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                if (provider == LLMProvider::Gemini) {
                    // Native Gemini /models response: {"models":[{"name":"models/gemini-...","supportedGenerationMethods":[...]}]}
                    // We want IDs usable for generation. Filter to entries that
                    // advertise generateContent and strip the "models/" prefix
                    // so downstream code sees bare names consistently.
                    QJsonArray ms = doc.object().value(QStringLiteral("models")).toArray();
                    for (const QJsonValue &v : ms) {
                        const QJsonObject m = v.toObject();
                        const QJsonArray methods = m.value(
                            QStringLiteral("supportedGenerationMethods")).toArray();
                        bool supportsGenerate = false;
                        for (const QJsonValue &mv : methods) {
                            if (mv.toString() == QLatin1String("generateContent")) {
                                supportsGenerate = true;
                                break;
                            }
                        }
                        if (!supportsGenerate) continue;
                        QString name = m.value(QStringLiteral("name")).toString();
                        if (name.startsWith(QLatin1String("models/"))) name = name.mid(7);
                        if (!name.isEmpty()) models << name;
                    }
                } else if (provider == LLMProvider::OpenAI
                    || provider == LLMProvider::Anthropic
                    || provider == LLMProvider::Grok
                    || provider == LLMProvider::LMStudio) {
                    QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
                    for (const QJsonValue &v : data) {
                        QString id = v.toObject().value(QStringLiteral("id")).toString();
                        if (!id.isEmpty()) models << id;
                    }
                } else if (provider == LLMProvider::Ollama) {
                    QJsonArray ms = doc.object().value(QStringLiteral("models")).toArray();
                    for (const QJsonValue &v : ms) {
                        QString name = v.toObject().value(QStringLiteral("name")).toString();
                        if (!name.isEmpty()) models << name;
                    }
                }
            }
        } else {
            qWarning() << "Fetch Models Error (" << url.toString() << "):" << reply->errorString();
        }
        
        // Sort models alphabetically
        models.sort(Qt::CaseInsensitive);
        
        if (callback) callback(models);
        reply->deleteLater();
    });
}

void LLMService::pullModel(const QString &modelName, 
                          std::function<void(double progress, const QString &status)> progressCallback, 
                          std::function<void(bool success, const QString &error)> completionCallback)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
    QUrl url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/pull"));
    
    QNetworkRequest netRequest(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body[QStringLiteral("name")] = modelName;
    body[QStringLiteral("stream")] = true;

    QNetworkReply *reply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    
    connect(reply, &QNetworkReply::readyRead, this, [reply, progressCallback]() {
        while (reply->canReadLine()) {
            QByteArray line = reply->readLine();
            QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject root = doc.object();
                QString status = root.value(QStringLiteral("status")).toString();
                double total = root.value(QStringLiteral("total")).toDouble();
                double completed = root.value(QStringLiteral("completed")).toDouble();
                
                if (total > 0) {
                    progressCallback(completed / total, status);
                } else {
                    progressCallback(0, status);
                }
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, completionCallback]() {
        if (reply->error() == QNetworkReply::NoError) {
            completionCallback(true, QString());
        } else {
            completionCallback(false, reply->errorString());
        }
        reply->deleteLater();
    });
}




QString LLMService::providerName(LLMProvider provider)
{
    switch (provider) {
        case LLMProvider::OpenAI:    return QStringLiteral("OpenAI");
        case LLMProvider::Anthropic: return QStringLiteral("Anthropic");
        case LLMProvider::Ollama:    return QStringLiteral("Ollama");
        case LLMProvider::Grok:      return QStringLiteral("Grok (xAI)");
        case LLMProvider::Gemini:    return QStringLiteral("Google");
        case LLMProvider::LMStudio:  return QStringLiteral("LM Studio");
    }
    return QStringLiteral("Unknown");
}
