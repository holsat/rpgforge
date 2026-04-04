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
#include <QList>
#include <QPair>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

enum class LLMProvider {
    OpenAI,
    Anthropic,
    Ollama
};

struct LLMMessage {
    QString role;    // "system", "user", "assistant"
    QString content;
};

struct LLMRequest {
    LLMProvider provider;
    QString model;
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

Q_SIGNALS:
    void requestStarted();
    void responseChunk(const QString &text);
    void responseFinished(const QString &fullText);
    void errorOccurred(const QString &message);

private:
    explicit LLMService(QObject *parent = nullptr);
    ~LLMService() override;

    LLMService(const LLMService&) = delete;
    LLMService& operator=(const LLMService&) = delete;

    void handleReadyRead();
    void handleFinished();
    void handleError(const QString &message);

    void processOpenAIChunk(const QByteArray &data);
    void processAnthropicChunk(const QByteArray &data);
    void processOllamaChunk(const QByteArray &data);

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_activeReply = nullptr;
    QString m_fullResponse;
    LLMProvider m_activeProvider;
};

#endif // LLMSERVICE_H
