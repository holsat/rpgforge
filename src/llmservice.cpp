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

static void logRequest(const QUrl &url, const QJsonObject &body, const QNetworkRequest &netRequest) {
    QFile dbg(QStringLiteral("llm_debug.log"));
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
        case LLMProvider::Ollama: return; // No key for Ollama usually
    }

    connect(wallet, &KWallet::Wallet::walletOpened, this, [wallet, walletKey, key](bool opened) {
        if (opened) {
            if (!wallet->hasFolder(QStringLiteral("RPGForge"))) {
                wallet->createFolder(QStringLiteral("RPGForge"));
            }
            wallet->setFolder(QStringLiteral("RPGForge"));
            wallet->writePassword(walletKey, key);
        }
        wallet->deleteLater();
    });
}

QString LLMService::apiKey(LLMProvider provider) const
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) return QString();

    QString walletKey;
    switch (provider) {
        case LLMProvider::OpenAI: walletKey = QStringLiteral("OpenAI_Key"); break;
        case LLMProvider::Anthropic: walletKey = QStringLiteral("Anthropic_Key"); break;
        case LLMProvider::Ollama: delete wallet; return QString();
    }

    if (wallet->hasFolder(QStringLiteral("RPGForge"))) {
        wallet->setFolder(QStringLiteral("RPGForge"));
        QString key;
        if (wallet->readPassword(walletKey, key) == 0) {
            delete wallet;
            return key;
        }
    }
    delete wallet;
    return QString();
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

void LLMService::sendRequest(const LLMRequest &request)
{
    if (m_activeReply) {
        cancelRequest();
    }

    m_activeProvider = request.provider;
    m_fullResponse.clear();

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QJsonObject body;

    QString key = apiKey(request.provider);

    // Common Body Prep
    QJsonArray messagesArray;
    for (const auto &msg : request.messages) {
        QJsonObject m;
        m[QStringLiteral("role")] = msg.role;
        m[QStringLiteral("content")] = msg.content;
        messagesArray.append(m);
    }

    if (request.provider == LLMProvider::OpenAI) {
        QString endpoint = settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString();
        url = QUrl(endpoint);
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString() : request.model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = request.stream;
        body[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("max_tokens")] = request.maxTokens;
    } 
    else if (request.provider == LLMProvider::Anthropic) {
        QString endpoint = settings.value(QStringLiteral("llm/anthropic/endpoint"), QStringLiteral("https://api.anthropic.com/v1/messages")).toString().trimmed();
        if (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
        url = QUrl(endpoint);
        
        netRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        netRequest.setRawHeader("x-api-key", key.toUtf8());
        netRequest.setRawHeader("anthropic-version", "2023-06-01");
        netRequest.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (request.stream) {
            netRequest.setRawHeader("Accept", "text/event-stream");
        }
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/anthropic/model"), QStringLiteral("claude-sonnet-4-6")).toString() : request.model;
        
        // Anthropic: system message must be a single top-level string
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

        if (!systemParts.isEmpty()) {
            body[QStringLiteral("system")] = systemParts.join(QStringLiteral("\n\n"));
        }

        // Anthropic requires the first message to be "user"
        if (anthropicMessages.isEmpty()) {
             anthropicMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Hello")}});
        }
        
        body[QStringLiteral("messages")] = anthropicMessages;
        body[QStringLiteral("stream")] = request.stream;
        body[QStringLiteral("max_tokens")] = request.maxTokens > 0 ? request.maxTokens : 4096;
        body[QStringLiteral("temperature")] = request.temperature;
    }
    else if (request.provider == LLMProvider::Ollama) {
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/chat"));
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/ollama/model"), QStringLiteral("llama3")).toString() : request.model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = request.stream;
        QJsonObject options;
        options[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("options")] = options;
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    logRequest(url, body, netRequest);

    m_activeReply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    
    Q_EMIT requestStarted();

    connect(m_activeReply, &QNetworkReply::readyRead, this, &LLMService::handleReadyRead);
    connect(m_activeReply, &QNetworkReply::finished, this, &LLMService::handleFinished);
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

    QByteArray data = m_activeReply->readAll();
    
    switch (m_activeProvider) {
        case LLMProvider::OpenAI: processOpenAIChunk(data); break;
        case LLMProvider::Anthropic: processAnthropicChunk(data); break;
        case LLMProvider::Ollama: processOllamaChunk(data); break;
    }
}

void LLMService::handleFinished()
{
    if (!m_activeReply) return;

    if (m_activeReply->error() != QNetworkReply::NoError && m_activeReply->error() != QNetworkReply::OperationCanceledError) {
        QString errorMsg = m_activeReply->errorString();
        QByteArray serverResponse = m_activeReply->readAll();
        if (!serverResponse.isEmpty()) {
            errorMsg += QStringLiteral(" - ") + QString::fromUtf8(serverResponse);
        }
        handleError(errorMsg);
    } else if (m_activeReply->error() == QNetworkReply::NoError) {
        Q_EMIT responseFinished(m_fullResponse);
    }

    m_activeReply->deleteLater();
    m_activeReply = nullptr;
}

void LLMService::handleError(const QString &message)
{
    Q_EMIT errorOccurred(message);
}

void LLMService::processOpenAIChunk(const QByteArray &data)
{
    // OpenAI SSE format: "data: {...}\n\ndata: {...}"
    QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("data: "))) {
            QString jsonStr = line.mid(6).trimmed();
            if (jsonStr == QLatin1String("[DONE]")) continue;

            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
                if (!choices.isEmpty()) {
                    QString chunk = choices.at(0).toObject().value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
                    if (!chunk.isEmpty()) {
                        m_fullResponse += chunk;
                        Q_EMIT responseChunk(chunk);
                    }
                }
            }
        }
    }
}

void LLMService::processAnthropicChunk(const QByteArray &data)
{
    // Anthropic SSE format: "event: content_block_delta\ndata: {...}\n\n"
    QString content = QString::fromUtf8(data);
    QStringList events = content.split(QStringLiteral("\n\n"));

    for (const QString &event : events) {
        if (event.contains(QLatin1String("content_block_delta"))) {
            int dataIdx = event.indexOf(QLatin1String("data: "));
            if (dataIdx != -1) {
                QString jsonStr = event.mid(dataIdx + 6).trimmed();
                QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    QString chunk = doc.object().value(QStringLiteral("delta")).toObject().value(QStringLiteral("text")).toString();
                    if (!chunk.isEmpty()) {
                        m_fullResponse += chunk;
                        Q_EMIT responseChunk(chunk);
                    }
                }
            }
        }
    }
}

void LLMService::processOllamaChunk(const QByteArray &data)
{
    // Ollama native format: {"model":"...", "message":{"content":"..."}} \n ...
    QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isNull() && doc.isObject()) {
            QString chunk = doc.object().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
            if (!chunk.isEmpty()) {
                m_fullResponse += chunk;
                Q_EMIT responseChunk(chunk);
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
        url = QUrl(QStringLiteral("https://api.openai.com/v1/embeddings"));
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        body[QStringLiteral("model")] = model.isEmpty() ? QStringLiteral("text-embedding-3-small") : model;
        body[QStringLiteral("input")] = text;
    } else if (provider == LLMProvider::Ollama) {
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/embeddings"));
        body[QStringLiteral("model")] = model.isEmpty() ? QStringLiteral("nomic-embed-text") : model;
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
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QJsonObject body;

    QString key = apiKey(request.provider);

    QJsonArray messagesArray;
    for (const auto &msg : request.messages) {
        QJsonObject m;
        m[QStringLiteral("role")] = msg.role;
        m[QStringLiteral("content")] = msg.content;
        messagesArray.append(m);
    }

    if (request.provider == LLMProvider::OpenAI) {
        QString endpoint = settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString();
        url = QUrl(endpoint);
        netRequest.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString() : request.model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = false;
        body[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("max_tokens")] = request.maxTokens;
    } 
    else if (request.provider == LLMProvider::Anthropic) {
        QString endpoint = settings.value(QStringLiteral("llm/anthropic/endpoint"), QStringLiteral("https://api.anthropic.com/v1/messages")).toString().trimmed();
        if (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
        url = QUrl(endpoint);
        
        netRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        netRequest.setRawHeader("x-api-key", key.toUtf8());
        netRequest.setRawHeader("anthropic-version", "2023-06-01");
        netRequest.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/anthropic/model"), QStringLiteral("claude-sonnet-4-6")).toString() : request.model;
        
        // Anthropic: system message must be a single top-level string
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

        if (!systemParts.isEmpty()) {
            body[QStringLiteral("system")] = systemParts.join(QStringLiteral("\n\n"));
        }

        // Anthropic requires the first message to be "user"
        if (anthropicMessages.isEmpty()) {
             anthropicMessages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Hello")}});
        }
        
        body[QStringLiteral("messages")] = anthropicMessages;
        body[QStringLiteral("stream")] = false;
        body[QStringLiteral("max_tokens")] = request.maxTokens > 0 ? request.maxTokens : 4096;
        body[QStringLiteral("temperature")] = request.temperature;
    }
    else if (request.provider == LLMProvider::Ollama) {
        QString endpoint = settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434")).toString();
        url = normalizeOllamaUrl(endpoint, QStringLiteral("/api/chat"));
        
        body[QStringLiteral("model")] = request.model.isEmpty() ? settings.value(QStringLiteral("llm/ollama/model"), QStringLiteral("llama3")).toString() : request.model;
        body[QStringLiteral("messages")] = messagesArray;
        body[QStringLiteral("stream")] = false;
        QJsonObject options;
        options[QStringLiteral("temperature")] = request.temperature;
        body[QStringLiteral("options")] = options;
    }

    netRequest.setUrl(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    logRequest(url, body, netRequest);

    QNetworkReply *reply = m_networkManager->post(netRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, provider = request.provider]() {
        QString result;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                if (provider == LLMProvider::OpenAI) {
                    QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
                    if (!choices.isEmpty()) {
                        result = choices.at(0).toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
                    }
                } else if (provider == LLMProvider::Anthropic) {
                    QJsonArray contentArray = doc.object().value(QStringLiteral("content")).toArray();
                    if (!contentArray.isEmpty()) {
                        result = contentArray.at(0).toObject().value(QStringLiteral("text")).toString();
                    }
                } else if (provider == LLMProvider::Ollama) {
                    result = doc.object().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
                }
            }
        } else {
            qWarning() << "LLM Non-Streaming Error:" << reply->errorString() << reply->readAll();
        }
        if (callback) {
            callback(result);
        }
        reply->deleteLater();
    });
}

void LLMService::pingAnthropic(std::function<void(bool, const QString&)> callback)
{
    LLMRequest req;
    req.provider = LLMProvider::Anthropic;
    req.model = QStringLiteral("claude-haiku-4-5"); // Smallest/cheapest for ping
    req.maxTokens = 1;
    req.messages.append({QStringLiteral("user"), QStringLiteral("ping")});
    req.stream = false;

    sendNonStreamingRequest(req, [callback](const QString &result) {
        if (!result.isEmpty()) {
            callback(true, QString());
        } else {
            callback(false, i18n("API test failed. Check your key and logs."));
        }
    });
}

void LLMService::fetchModels(LLMProvider provider, std::function<void(const QStringList&)> callback)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QUrl url;
    QNetworkRequest netRequest;
    QString key = apiKey(provider);

    if (provider == LLMProvider::OpenAI) {
        QString endpoint = settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString().trimmed();
        // Models endpoint is usually /v1/models
        if (endpoint.endsWith(QStringLiteral("/chat/completions"))) {
            endpoint.replace(QStringLiteral("/chat/completions"), QStringLiteral("/models"));
        } else if (!endpoint.contains(QStringLiteral("/models"))) {
            // best effort fallback
            endpoint = QStringLiteral("https://api.openai.com/v1/models");
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

    logRequest(url, QJsonObject(), netRequest);

    QNetworkReply *reply = m_networkManager->get(netRequest);
    connect(reply, &QNetworkReply::finished, this, [reply, callback, provider, url]() {
        QStringList models;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                if (provider == LLMProvider::OpenAI || provider == LLMProvider::Anthropic) {
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


