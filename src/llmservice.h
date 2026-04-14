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
    Gemini
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
     */
    void sendNonStreamingRequest(const LLMRequest &request, std::function<void(const QString&)> callback);

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

private:
    explicit LLMService(QObject *parent = nullptr);
    ~LLMService() override;

    LLMService(const LLMService&) = delete;
    LLMService& operator=(const LLMService&) = delete;

    void handleReadyRead();
    void handleFinished();
    void handleError(const QString &message);

    void processOpenAIChunk(QByteArray &buffer);
    void processAnthropicChunk(QByteArray &buffer);
    void processOllamaChunk(QByteArray &buffer);

    // Resolves the model from request.model or settings (no hardcoded defaults).
    QString resolvedModel(const LLMRequest &request) const;

    // Validates the model against the session-cached provider model list,
    // fetching it on first use. Dispatches the request if valid, otherwise
    // emits modelNotFound() and stores the request for retryWithModel().
    void validateModelThenDispatch(const LLMRequest &request,
                                   std::function<void(const QString&)> nonStreamCallback = nullptr);

    // Builds and POSTs the request with the already-resolved model.
    // nonStreamCallback == nullptr → streaming path; non-null → non-streaming path.
    void dispatchRequest(const LLMRequest &request, const QString &model,
                         std::function<void(const QString&)> nonStreamCallback);

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
        std::function<void(const QString&)> nonStreamCallback;
    };
    bool m_hasPendingRequest = false;
    PendingRequest m_pendingRequest;

    // API key cache: populated on first KWallet read, avoids synchronous wallet
    // opens on every request. Invalidated when setApiKey() is called.
    mutable QHash<LLMProvider, QString> m_apiKeyCache;
    bool m_isShowingModelDialog = false;
};

#endif // LLMSERVICE_H
