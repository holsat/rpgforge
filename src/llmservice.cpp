/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

#ifdef QT_DEBUG
static void logRequest(const QUrl &url, const QJsonObject &body, const QNetworkRequest &netRequest) {
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
static QString providerSettingsKey(LLMProvider p)
{
    switch (p) {
        case LLMProvider::OpenAI:    return QStringLiteral("llm/openai");
        case LLMProvider::Anthropic: return QStringLiteral("llm/anthropic");
        case LLMProvider::Grok:      return QStringLiteral("llm/grok");
        case LLMProvider::Gemini:    return QStringLiteral("llm/gemini");
        case LLMProvider::Ollama:    return QStringLiteral("llm/ollama");
    }
    return QStringLiteral("llm/openai");
}

// ---------------------------------------------------------------------------
// Resolve the model: prefer request.model, else read settings (no fallback).
// ---------------------------------------------------------------------------
QString LLMService::resolvedModel(const LLMRequest &request) const
{
    if (!request.model.isEmpty()) return request.model;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    return settings.value(providerSettingsKey(request.provider) + QStringLiteral("/model")).toString();
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
                                           std::function<void(const QString&)> nonStreamCallback)
{
    const QString model = resolvedModel(request);
    if (model.isEmpty()) {
        if (nonStreamCallback) {
            qWarning() << "LLM: no model configured — aborting request";
            nonStreamCallback(QString());
        } else {
            handleError(i18n("No model configured. Open Settings and enter a model name for this provider."));
        }
        return;
    }

    const LLMProvider provider = request.provider;

    // Use cached list if available.
    if (m_modelCache.contains(provider)) {
        const QStringList &cached = m_modelCache[provider];
        if (cached.isEmpty() || cached.contains(model)) {
            // Empty cache = fetch failed previously; give the request a chance.
            dispatchRequest(request, model, nonStreamCallback);
        } else {
            m_hasPendingRequest = true;
            m_pendingRequest = {request, nonStreamCallback};
            Q_EMIT modelNotFound(provider, model, cached);
        }
        return;
    }

    // First use for this provider this session — fetch the model list.
    fetchModels(provider, [this, request, model, nonStreamCallback](const QStringList &available) {
        m_modelCache[request.provider] = available; // cache even if empty

        if (available.isEmpty() || available.contains(model)) {
            dispatchRequest(request, model, nonStreamCallback);
        } else {
            m_hasPendingRequest = true;
            m_pendingRequest = {request, nonStreamCallback};
            Q_EMIT modelNotFound(request.provider, model, available);
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

    // Persist to settings — this becomes the new source of truth.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(providerSettingsKey(pending.request.provider) + QStringLiteral("/model"), newModel);

    LLMRequest updated = pending.request;
    updated.model = newModel;
    dispatchRequest(updated, newModel, pending.nonStreamCallback);
}

// ---------------------------------------------------------------------------
// Build and POST the request. nonStreamCallback==nullptr → streaming path.
// ---------------------------------------------------------------------------
void LLMService::dispatchRequest(const LLMRequest &request, const QString &model,
                                  std::function<void(const QString&)> nonStreamCallback)
{
    const bool streaming = (nonStreamCallback == nullptr);

    if (streaming) {
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

    if (request.provider == LLMProvider::OpenAI
        || request.provider == LLMProvider::Grok
        || request.provider == LLMProvider::Gemini) {
        const QString sk = providerSettingsKey(request.provider);
        const QString defaultEndpoint =
            (request.provider == LLMProvider::Grok)
                ? QStringLiteral("https://api.x.ai/v1/chat/completions")
            : (request.provider == LLMProvider::Gemini)
                ? QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions")
            : QStringLiteral("https://api.openai.com/v1/chat/completions");

        url = QUrl(settings.value(sk + QStringLiteral("/endpoint"), defaultEndpoint).toString());
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        body[QStringLiteral("model")] = model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = streaming;
        body[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("max_tokens")] = request.maxTokens;
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
        m_currentStreamId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Q_EMIT requestStarted(m_currentStreamId);
        connect(reply, &QNetworkReply::readyRead, this, &LLMService::handleReadyRead);
        connect(reply, &QNetworkReply::finished, this, &LLMService::handleFinished);
    } else {
        connect(reply, &QNetworkReply::finished, this, [reply, nonStreamCallback, provider = request.provider]() {
            QString result;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (!doc.isNull() && doc.isObject()) {
                    if (provider == LLMProvider::OpenAI
                        || provider == LLMProvider::Grok
                        || provider == LLMProvider::Gemini) {
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
                qWarning() << "LLM Non-Streaming Error:" << reply->errorString() << reply->readAll();
            }
            if (nonStreamCallback) nonStreamCallback(result);
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
        case LLMProvider::Gemini: processOpenAIChunk(m_streamBuffer); break;
        case LLMProvider::Anthropic: processAnthropicChunk(m_streamBuffer); break;
        case LLMProvider::Ollama: processOllamaChunk(m_streamBuffer); break;
    }
}

void LLMService::handleFinished()
{
    if (!m_activeReply) return;

    if (m_activeReply->error() != QNetworkReply::NoError && m_activeReply->error() != QNetworkReply::OperationCanceledError) {
        if (m_retryCount < 3) {
            m_retryCount++;
            qWarning() << "LLM Request failed, retrying" << m_retryCount << "..." << m_activeReply->errorString();
            m_activeReply->deleteLater();
            m_activeReply = nullptr;
            sendRequest(m_activeRequest);
            return;
        }

        QString errorMsg = m_activeReply->errorString();
        QByteArray serverResponse = m_activeReply->readAll();
        if (!serverResponse.isEmpty()) {
            errorMsg += QStringLiteral(" - ") + QString::fromUtf8(serverResponse);
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

    if (provider == LLMProvider::OpenAI) {
        const QString embModel = model.isEmpty()
            ? settings.value(QStringLiteral("llm/embedding_model")).toString()
            : model;
        if (embModel.isEmpty()) {
            qWarning() << "LLM: no embedding model configured. Set one in Settings → Embedding Model.";
            if (callback) callback(QVector<float>());
            return;
        }
        url = QUrl(QStringLiteral("https://api.openai.com/v1/embeddings"));
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
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
    } else {
        // Anthropic doesn't have an embedding API currently
        if (callback) callback(QVector<float>());
        return;
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, provider]() {
        QVector<float> embedding;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray dataArray;
                if (provider == LLMProvider::OpenAI) {
                    dataArray = doc.object().value(QStringLiteral("data")).toArray();
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
                }
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
    validateModelThenDispatch(request, callback);
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
        dispatchRequest(req, req.model, [callback](const QString &result) {
            if (!result.isEmpty())
                callback(true, QString());
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

    if (provider == LLMProvider::OpenAI
        || provider == LLMProvider::Grok
        || provider == LLMProvider::Gemini) {
        QString settingsKey = (provider == LLMProvider::Grok) ? QStringLiteral("llm/grok")
                            : (provider == LLMProvider::Gemini) ? QStringLiteral("llm/gemini")
                            : QStringLiteral("llm/openai");
        QString fallback = (provider == LLMProvider::Grok) ? QStringLiteral("https://api.x.ai/v1/chat/completions")
                         : (provider == LLMProvider::Gemini) ? QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions")
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
                if (provider == LLMProvider::OpenAI || provider == LLMProvider::Anthropic
                    || provider == LLMProvider::Grok || provider == LLMProvider::Gemini) {
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



