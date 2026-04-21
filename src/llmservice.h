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

#ifndef LLMSERVICE_H
#define LLMSERVICE_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <functional>
#include <QUuid>

class QNetworkAccessManager;
class QNetworkReply;

enum class LLMProvider {
    OpenAI,
    Anthropic,
    Ollama,
    Grok,
    Gemini,
    LMStudio
};

struct LLMMessage {
    QString role;    // "system", "user", "assistant"
    QString content;
};

struct LLMRequest {
    LLMProvider provider;
    QString model;
    QString serviceName; // e.g. "Game Analyzer", "Character Generator"
    QString settingsKey; // The settings path to update if the model is replaced (e.g. "analyzer/model")
    QList<LLMMessage> messages;
    bool stream = true;
    double temperature = 0.7;
    int maxTokens = 1024;

    // Optional ordered list of {provider, model} pairs to try if the primary
    // {provider, model} above fails (cooldown / 429 / model-not-found). Only
    // honoured by the non-streaming path today. If empty, LLMService composes
    // a default chain from the user's configured providers.
    QList<QPair<LLMProvider, QString>> fallbackChain;
};

/**
 * @brief Singleton service for communicating with various LLM providers.
 */
class LLMService : public QObject
{
    Q_OBJECT

public:
    static LLMService& instance();

    /**
     * @brief Sends a request to the configured LLM provider.
     */
    void sendRequest(const LLMRequest &request);

    /**
     * @brief Cancels the current active request.
     */
    void cancelRequest();

    /**
     * @brief Returns true if a request is currently in progress.
     */
    bool isRequestActive() const { return m_activeReply != nullptr; }

    /**
     * @brief Securely stores an API key in KWallet.
     */
    void setApiKey(LLMProvider provider, const QString &key);

    /**
     * @brief Retrieves an API key from KWallet.
     */
    QString apiKey(LLMProvider provider) const;

    /**
     * @brief Generates an embedding for the given text.
     */
    void generateEmbedding(LLMProvider provider, const QString &model, const QString &text, std::function<void(const QVector<float>&)> callback);

    /**
     * @brief Sends a request to the LLM without interrupting the active stream, useful for background tasks.
     *
     * Callback receives the raw response body on success, or an empty string
     * on any failure. Consumers that need to distinguish "empty response"
     * from "request failed with HTTP 429/401/..." should use the detailed
     * overload below.
     */
    virtual void sendNonStreamingRequest(const LLMRequest &request, std::function<void(const QString&)> callback);

    /**
     * @brief Detailed variant of sendNonStreamingRequest. The callback receives
     * both the response body (empty on failure) and an error message (empty
     * on success). Lets callers show the real provider error — e.g. Google's
     * "You exceeded your current quota (retry in 3h13m)" — instead of a
     * generic placeholder like "Empty response from LLM".
     */
    using NonStreamCallback = std::function<void(const QString &response, const QString &error)>;
    virtual void sendNonStreamingRequestDetailed(const LLMRequest &request, NonStreamCallback callback);

    /**
     * @brief Pings the Anthropic API to verify connectivity.
     */
    void pingAnthropic(std::function<void(bool, const QString&)> callback);

    /**
     * @brief Fetches available models from the provider.
     */
    void fetchModels(LLMProvider provider, std::function<void(const QStringList&)> callback);

    /**
     * @brief Pulls (installs) a model from Ollama.
     * Only applicable for Ollama provider.
     */
    void pullModel(const QString &modelName, std::function<void(double progress, const QString &status)> progressCallback, std::function<void(bool success, const QString &error)> completionCallback);

    /**
     * @brief Retries the last request that was blocked due to an invalid model.
     * Call this after the user has selected a replacement model from the picker.
     */
    void retryWithModel(const QString &newModel);

    /**
     * @brief Returns a user-friendly name for the given provider.
     */
    static QString providerName(LLMProvider provider);

    bool isShowingModelDialog() const { return m_isShowingModelDialog; }
    void setShowingModelDialog(bool showing) { m_isShowingModelDialog = showing; }

    /**
     * @brief Returns the settings key prefix for the given provider (e.g. "llm/openai").
     */
    static QString providerSettingsKey(LLMProvider provider);

    /**
     * @brief Returns the short key for the given provider ("openai",
     * "anthropic", …). Used when storing provider identities in
     * QStringList-shaped settings like llm/provider_order, where a
     * human-readable key ages better than an int enum index.
     */
    static QString providerKey(LLMProvider provider);

    /**
     * @brief Reverse of providerKey(). Returns true and writes the matching
     * enum to @p out on success; returns false if the key is unknown.
     */
    static bool providerFromKey(const QString &key, LLMProvider *out);

    /**
     * @brief Returns the user's ordered list of providers as stored in
     * QSettings "llm/provider_order". On first run (or when the key is
     * missing), seeds the list with the historical preference order
     * (Gemini > Anthropic > OpenAI > Grok > Ollama > LMStudio). Unknown
     * keys in storage are dropped; providers added to the enum after
     * the user first saved are appended at the tail so upgrades don't
     * silently lose them.
     */
    static QList<LLMProvider> readProviderOrderFromSettings();

    /**
     * @brief Returns true if the user has the given provider enabled for
     * fallback participation. Default is true — existing installs that
     * haven't written the key continue to behave as before.
     */
    static bool isProviderEnabled(LLMProvider provider);

    /**
     * @brief Given the full chat-model list returned by fetchModels, extract
     * just the ones that look like embedding models. Name-based heuristic:
     * anything containing "embed" or "similarity" (case-insensitive) —
     * covers OpenAI's text-embedding-3-*, Gemini's text-embedding-004 /
     * gemini-embedding-001, Ollama's nomic-embed-text / mxbai-embed-large,
     * and LMStudio's OpenAI-compatible embedding models. Anthropic / Grok
     * don't publish embedding endpoints, so their filtered list comes back
     * empty — callers should treat that as "embeddings not available for
     * this provider" and grey out / skip the entry.
     */
    static QStringList filterEmbeddingModels(LLMProvider provider,
                                             const QStringList &allModels);

    /**
     * @brief Returns true if the given {provider, model} pair is currently in
     * a cooldown window (recorded from a prior 429 with retry-after). If @p
     * expiresAtMsOut is non-null, the wall-clock millisecond epoch when the
     * cooldown ends is written there; if @p reasonOut is non-null, a short
     * human-readable reason is written there.
     *
     * Expired cooldowns are lazily cleaned up on check.
     */
    bool isCooledDown(LLMProvider provider, const QString &model,
                      qint64 *expiresAtMsOut = nullptr,
                      QString *reasonOut = nullptr) const;

    /**
     * @brief Records a cooldown for the given {provider, model} pair. Further
     * requests to that pair within the window will short-circuit with an
     * errorOccurred() and an empty callback result (no network traffic).
     */
    void recordCooldown(LLMProvider provider, const QString &model,
                        int seconds, const QString &reason);

    /**
     * @brief Clears any active cooldown for the given pair (used by tests and
     * by manual retry after the user confirms quota restoration).
     */
    void clearCooldown(LLMProvider provider, const QString &model);

    /**
     * @brief Returns true if the user has enough of the given provider's
     * settings filled in for a request to have any chance of succeeding.
     *
     * - Cloud providers (OpenAI, Anthropic, Gemini, Grok): require both an
     *   API key in KWallet AND a non-empty default model.
     * - Local providers (Ollama, LMStudio): require an endpoint AND a
     *   non-empty default model. API key is optional.
     *
     * Used by the fallback-chain logic to skip unconfigured entries so we
     * don't attempt requests that would just 401/404 on the first byte.
     */
    bool isProviderConfigured(LLMProvider provider) const;

    /**
     * @brief Composes a default fallback chain for the given primary
     * provider by iterating every configured provider (other than the
     * primary) in a fixed preference order. Each entry uses the provider's
     * configured default model from settings. Returned list is empty if
     * the user hasn't configured any other providers — callers then
     * surface the primary failure as-is.
     */
    QList<QPair<LLMProvider, QString>> composeDefaultFallbackChain(LLMProvider primary) const;

Q_SIGNALS:
    /// Emitted when a new streaming request begins. requestId is a UUID that
    /// callers can store and use to filter responseChunk/responseFinished.
    void requestStarted(const QString &requestId);
    void responseChunk(const QString &requestId, const QString &text);
    void responseFinished(const QString &requestId, const QString &fullText);
    void errorOccurred(const QString &message);

    /**
     * @brief Emitted when the configured model is not in the provider's current model list.
     * Connect to this to show a model selection UI, then call retryWithModel().
     */
    void modelNotFound(LLMProvider provider, const QString &invalidModel, const QStringList &available, const QString &serviceName);

protected:
    explicit LLMService(QObject *parent = nullptr);
    virtual ~LLMService();

private:

    LLMService(const LLMService&) = delete;
    LLMService& operator=(const LLMService&) = delete;

    void handleReadyRead();
    void handleFinished();
    void handleError(const QString &message);

    void processOpenAIChunk(QByteArray &buffer);
    void processAnthropicChunk(QByteArray &buffer);
    void processOllamaChunk(QByteArray &buffer);
    /// Parse Gemini's native streamGenerateContent SSE format. Each
    /// event is a single "data: {...}" line terminated by "\n\n"; the
    /// JSON contains candidates[0].content.parts[].text with the chunk
    /// to append.
    void processGeminiChunk(QByteArray &buffer);

    // Resolves the model from request.model or settings (no hardcoded defaults).
    QString resolvedModel(const LLMRequest &request) const;

    // Validates the model against the session-cached provider model list,
    // fetching it on first use. Dispatches the request if valid, otherwise
    // emits modelNotFound() and stores the request for retryWithModel().
    // Passing a null nonStreamCallback means streaming mode.
    void validateModelThenDispatch(const LLMRequest &request,
                                   NonStreamCallback nonStreamCallback = nullptr);

    // Builds and POSTs the request with the already-resolved model.
    // nonStreamCallback == nullptr → streaming path; non-null → non-streaming path.
    // isRetry=true skips the per-request state reset and the requestStarted
    // emission so the chat UI doesn't get spammed with fresh "AI is thinking"
    // placeholders on every network retry. Also skips model revalidation.
    void dispatchRequest(const LLMRequest &request, const QString &model,
                         NonStreamCallback nonStreamCallback,
                         bool isRetry = false);

    // If the non-streaming request has a (user-supplied or auto-composed)
    // fallback chain, try the next usable entry (skipping unconfigured
    // providers and currently-cooled-down {provider, model} pairs) and
    // re-dispatch. Returns true if a fallback was dispatched (caller's
    // callback is now owned by the fallback dispatch), false if the chain
    // had nothing to try — in that case the caller's callback is untouched
    // and the caller must surface the error. Streaming mode is not yet
    // plumbed through here.
    bool tryFallback(const LLMRequest &request, const NonStreamCallback &nonStreamCallback,
                     const QString &previousError);

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_activeReply = nullptr;
    QString m_fullResponse;
    LLMProvider m_activeProvider;
    LLMRequest m_activeRequest;
    int m_retryCount = 0;

    // Per-stream accumulation buffer. TCP readyRead() may deliver partial SSE
    // lines; this buffer holds incomplete data until a full line arrives.
    QByteArray m_streamBuffer;

    // Unique ID for the current streaming request. Emitted with requestStarted
    // and included in responseChunk/responseFinished so callers can filter out
    // chunks that don't belong to their own request.
    QString m_currentStreamId;

    // Session model cache — populated on first validateModelThenDispatch() per provider.
    QHash<LLMProvider, QStringList> m_modelCache;

    // Pending request held while the user selects a replacement model.
    struct PendingRequest {
        LLMRequest request;
        NonStreamCallback nonStreamCallback;
    };
    bool m_hasPendingRequest = false;
    PendingRequest m_pendingRequest;

    // API key cache: populated on first KWallet read, avoids synchronous wallet
    // opens on every request. Invalidated when setApiKey() is called.
    mutable QHash<LLMProvider, QString> m_apiKeyCache;
    bool m_isShowingModelDialog = false;

    // Per-{provider, model} cooldown tracker. When a provider returns 429
    // with a retry-after window, the pair is blocked until wall-clock time
    // reaches the recorded expiry. Subsequent requests during the window
    // short-circuit with errorOccurred() and an empty callback result so
    // we don't keep hammering a quota-exhausted endpoint.
    struct CooldownEntry {
        qint64 expiresAtMs = 0;
        QString reason;
    };
    static QString cooldownKey(LLMProvider provider, const QString &model);
    mutable QHash<QString, CooldownEntry> m_cooldowns;

    // Parses Google's protobuf-duration strings ("11617s", "3h13m37.37s",
    // "17m0.5s") into seconds. Returns 0 on parse failure.
    static int parseRetryDelaySeconds(const QString &delayStr);

    // Scans a raw provider error body for a retry-after hint. Understands
    // Google's error.details[].RetryInfo.retryDelay shape and, as a
    // fallback, "Please retry in ..." substrings in error.message.
    // Returns 0 if no hint is present.
    static int extractRetryDelaySecondsFromErrorBody(const QByteArray &body);
};

#endif // LLMSERVICE_H
