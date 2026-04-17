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

#include "chatpanel.h"
#include "markdownparser.h"
#include "mainwindow.h"
#include <KTextEditor/Document>
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QKeyEvent>
#include <QWebChannel>

ChatBridge::ChatBridge(ChatPanel *panel) : QObject(panel), m_panel(panel) {}

void ChatBridge::handleInsert(const QString &text)
{
    m_panel->handleInsert(text);
}

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    initChatView();

    m_bridge = new ChatBridge(this);
    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("chatBridge"), m_bridge);
    m_webView->page()->setWebChannel(channel);

    connect(&LLMService::instance(), &LLMService::requestStarted, this, [this](const QString &requestId) {
        m_currentStreamId = requestId;
        m_progressBar->show();
        m_sendBtn->setEnabled(false);
        m_currentAiResponse.clear();
        appendMessageToView(QStringLiteral("assistant"), i18n("AI is thinking..."), QString());
    });

    connect(&LLMService::instance(), &LLMService::responseChunk, this, &ChatPanel::onResponseChunk);
    connect(&LLMService::instance(), &LLMService::responseFinished, this, &ChatPanel::onResponseFinished);
    connect(&LLMService::instance(), &LLMService::errorOccurred, this, &ChatPanel::onError);
}

ChatPanel::~ChatPanel() = default;

void ChatPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(5, 5, 5, 5);

    m_providerCombo = new QComboBox(this);
    // Use setItemData so the combo index is independent from the LLMProvider enum value.
    struct { const char *label; LLMProvider value; } providers[] = {
        {"OpenAI",    LLMProvider::OpenAI},
        {"Anthropic", LLMProvider::Anthropic},
        {"Ollama",    LLMProvider::Ollama},
        {"Grok",      LLMProvider::Grok},
        {"Google",    LLMProvider::Gemini},
        {"LM Studio", LLMProvider::LMStudio},
    };
    for (const auto &p : providers) {
        m_providerCombo->addItem(QLatin1String(p.label),
                                 QVariant::fromValue(static_cast<int>(p.value)));
    }
    m_providerCombo->setMinimumWidth(100);

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const int savedProvider = settings.value(QStringLiteral("llm/chat_provider"), 
                                             settings.value(QStringLiteral("llm/provider"), 0)).toInt();
    for (int i = 0; i < m_providerCombo->count(); ++i) {
        if (m_providerCombo->itemData(i).toInt() == savedProvider) {
            m_providerCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_providerCombo, &QComboBox::currentIndexChanged, this, &ChatPanel::onProviderChanged);
    toolbar->addWidget(m_providerCombo);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setMinimumWidth(250);
    updateModelList();
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &ChatPanel::onModelChanged);
    toolbar->addWidget(m_modelCombo);

    m_refreshModelsBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QString(), this);
    m_refreshModelsBtn->setToolTip(i18n("Refresh model list"));
    m_refreshModelsBtn->setAccessibleName(i18n("Refresh model list"));
    connect(m_refreshModelsBtn, &QPushButton::clicked, this, &ChatPanel::updateModelList);
    toolbar->addWidget(m_refreshModelsBtn);

    toolbar->addStretch();

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), this);
    m_clearBtn->setToolTip(i18n("Clear Chat"));
    m_clearBtn->setAccessibleName(i18n("Clear Chat"));
    connect(m_clearBtn, &QPushButton::clicked, this, &ChatPanel::clearChat);
    toolbar->addWidget(m_clearBtn);

    layout->addLayout(toolbar);

    // Chat History
    m_webView = new QWebEngineView(this);
    layout->addWidget(m_webView, 1);

    // Progress Bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(2);
    m_progressBar->hide();
    layout->addWidget(m_progressBar);

    // Input Area
    auto *inputContainer = new QWidget(this);
    auto *inputLayout = new QVBoxLayout(inputContainer);
    inputLayout->setContentsMargins(5, 5, 5, 5);

    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setPlaceholderText(i18n("Ask the AI about your RPG project..."));
    m_inputEdit->setMaximumHeight(100);
    m_inputEdit->installEventFilter(this);
    inputLayout->addWidget(m_inputEdit);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_sendBtn = new QPushButton(i18n("Send"), this);
    m_sendBtn->setIcon(QIcon::fromTheme(QStringLiteral("mail-send")));
    connect(m_sendBtn, &QPushButton::clicked, this, &ChatPanel::sendMessage);
    btnLayout->addWidget(m_sendBtn);
    inputLayout->addLayout(btnLayout);

    layout->addWidget(inputContainer);
}

void ChatPanel::initChatView()
{
    QString html = QStringLiteral(R"(
        <!DOCTYPE html>
        <html>
        <head>
            <style>
                body {
                    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
                    font-size: 13px;
                    line-height: 1.5;
                    padding: 10px;
                    background-color: transparent;
                    color: palette(text);
                }
                .message {
                    margin-bottom: 15px;
                    padding: 10px;
                    border-radius: 8px;
                    max-width: 90%;
                }
                .user {
                    background-color: palette(highlight);
                    color: palette(highlighted-text);
                    margin-left: auto;
                    border-bottom-right-radius: 2px;
                }
                .assistant {
                    background-color: palette(button);
                    color: palette(button-text);
                    margin-right: auto;
                    border-bottom-left-radius: 2px;
                    border: 1px solid palette(mid);
                }
                .insert-btn {
                    margin-top: 8px;
                    padding: 4px 8px;
                    background-color: palette(highlight);
                    color: palette(highlighted-text);
                    border: none;
                    border-radius: 4px;
                    font-size: 11px;
                    cursor: pointer;
                }
                .insert-btn:hover {
                    opacity: 0.8;
                }
                pre {
                    background-color: rgba(0,0,0,0.1);
                    padding: 8px;
                    border-radius: 4px;
                    overflow-x: auto;
                }
                code {
                    font-family: monospace;
                }
            </style>
        </head>
        <body>
            <div id="chat-history"></div>
            <script src="qrc:///qtwebchannel/qwebchannel.js"></script>
            <script>
                var backend;
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    backend = channel.objects.chatBridge;
                });

                function appendMessage(role, html, rawText) {
                    const div = document.createElement('div');
                    div.className = 'message ' + role;
                    div.setAttribute('data-raw', rawText || '');
                    
                    let content = html;
                    if (role === 'assistant' && rawText) {
                        content += '<div style=\"border-top: 1px solid rgba(0,0,0,0.1); margin-top: 5px;\">' +
                                   '<button onclick=\"insertText(this.parentNode.parentNode.getAttribute(\'data-raw\'))\" class=\"insert-btn\">' +
                                   'Insert at Cursor' +
                                   '</button></div>';
                    }
                    
                    div.innerHTML = content;
                    document.getElementById('chat-history').appendChild(div);
                    window.scrollTo(0, document.body.scrollHeight);
                }

                function updateLastMessage(html, rawText) {
                    const messages = document.getElementsByClassName('message assistant');
                    if (messages.length > 0) {
                        const msg = messages[messages.length - 1];
                        msg.setAttribute('data-raw', rawText || '');
                        let content = html;
                        if (rawText) {
                            content += '<div style=\"border-top: 1px solid rgba(0,0,0,0.1); margin-top: 5px;\">' +
                                       '<button onclick=\"insertText(this.parentNode.parentNode.getAttribute(\'data-raw\'))\" class=\"insert-btn\">' +
                                       'Insert at Cursor' +
                                       '</button></div>';
                        }
                        msg.innerHTML = content;
                        window.scrollTo(0, document.body.scrollHeight);
                    }
                }

                function insertText(text) {
                    if (backend && text) {
                        backend.handleInsert(text);
                    }
                }

                function clearHistory() {
                    document.getElementById('chat-history').innerHTML = '';
                }
            </script>
        </body>
        </html>
    )");

    m_webView->setHtml(html);
}

bool ChatPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                m_inputEdit->insertPlainText(QStringLiteral("\n"));
            } else {
                sendMessage();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatPanel::onProviderChanged()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("llm/chat_provider"), static_cast<int>(currentProvider()));
    updateModelList();
}

void ChatPanel::updateModelList()
{
    m_modelCombo->clear();
    m_modelCombo->addItem(i18n("Loading models..."));
    m_modelCombo->setEnabled(false);
    
    LLMProvider provider = currentProvider();

    LLMService::instance().fetchModels(provider, [this, provider](const QStringList &models) {
        m_modelCombo->clear();
        m_modelCombo->setEnabled(true);

        QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

        if (!models.isEmpty()) {
            m_modelCombo->addItems(models);
            
            // Try to select the configured model from settings
            QString settingsKey;
            if (provider == LLMProvider::OpenAI) settingsKey = QStringLiteral("llm/openai/model");
            else if (provider == LLMProvider::Anthropic) settingsKey = QStringLiteral("llm/anthropic/model");
            else if (provider == LLMProvider::Grok) settingsKey = QStringLiteral("llm/grok/model");
            else if (provider == LLMProvider::Gemini) settingsKey = QStringLiteral("llm/gemini/model");
            else if (provider == LLMProvider::LMStudio) settingsKey = QStringLiteral("llm/lmstudio/model");
            else settingsKey = QStringLiteral("llm/ollama/model");

            const QString configuredModel = settings.value(QStringLiteral("llm/chat_model"), 
                                                           settings.value(settingsKey)).toString();
            int idx = m_modelCombo->findText(configuredModel);
            if (idx != -1) m_modelCombo->setCurrentIndex(idx);
        } else {
            // Fetch failed — show configured model if set, otherwise leave empty
            // (suggestions would be hardcoded model names which we must not inject into the request path)
            if (provider == LLMProvider::OpenAI) {
                const QString m = settings.value(QStringLiteral("llm/openai/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            } else if (provider == LLMProvider::Anthropic) {
                const QString m = settings.value(QStringLiteral("llm/anthropic/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            } else if (provider == LLMProvider::Grok) {
                const QString m = settings.value(QStringLiteral("llm/grok/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            } else if (provider == LLMProvider::Gemini) {
                const QString m = settings.value(QStringLiteral("llm/gemini/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            } else if (provider == LLMProvider::LMStudio) {
                const QString m = settings.value(QStringLiteral("llm/lmstudio/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            } else { // Ollama
                const QString m = settings.value(QStringLiteral("llm/ollama/model")).toString();
                if (!m.isEmpty()) m_modelCombo->addItem(m);
            }
        }
    });
}

void ChatPanel::onModelChanged(const QString &model)
{
    if (model.isEmpty() || model == i18n("Loading models...")) return;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("llm/chat_model"), model);
}

void ChatPanel::sendMessage()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    askAI(text);
    m_inputEdit->clear();
}

void ChatPanel::askAI(const QString &userPrompt, const QString &serviceName)
{
    QString actualServiceName = serviceName.isEmpty() ? i18n("AI Chat") : serviceName;
    MarkdownParser parser;
    QString html = parser.renderHtml(userPrompt);
    appendMessageToView(QStringLiteral("user"), html, userPrompt);
    
    // Add current document context if history is empty
    if (m_history.isEmpty()) {
        auto *mw = qobject_cast<MainWindow*>(window());
        if (!mw) {
            QWidget *p = parentWidget();
            while (p && !mw) {
                mw = qobject_cast<MainWindow*>(p);
                p = p->parentWidget();
            }
        }

        if (mw && mw->editorDocument()) {
            QString docText = mw->editorDocument()->text();
            QString filePath = mw->editorDocument()->url().toLocalFile();

            if (docText.length() < 32000) {
                // File is small enough to send in full
                m_history.append({QStringLiteral("system"), 
                    QStringLiteral("The user is currently working on an RPG project. Here is the content of the active document for context:\n\n%1").arg(docText)});
                
                // Proceed immediately
                m_history.append({QStringLiteral("user"), userPrompt});
                LLMRequest request;
                request.provider = currentProvider();
                request.model = m_modelCombo->currentText();
                request.serviceName = actualServiceName;
                request.settingsKey = LLMService::providerSettingsKey(request.provider) + QStringLiteral("/model");
                request.messages = m_history;
                LLMService::instance().sendRequest(request);
                return;
            } else {
                // File is LARGE. Use RAG to pull relevant snippets from this specific file.
                // Note: We search the KnowledgeBase but filter results to this file (or let it pull from others too if useful)
                // For now, let's pull the top 10 relevant chunks from the project.
                KnowledgeBase::instance().search(userPrompt, 10, QString(), [this, userPrompt, actualServiceName](const QList<SearchResult> &results) {
                    QString context;
                    for (const auto &res : results) {
                        context += QStringLiteral("--- Source: %1 (Heading: %2) ---\n%3\n\n").arg(res.filePath, res.heading, res.content);
                    }

                    m_history.append({QStringLiteral("system"), 
                        QStringLiteral("The user is working on a large RPG document. Here are the most relevant snippets found in the project for this query:\n\n%1").arg(context)});
                    
                    m_history.append({QStringLiteral("user"), userPrompt});
                    
                    LLMRequest request;
                    request.provider = currentProvider();
                    request.model = m_modelCombo->currentText().trimmed();
                    request.serviceName = actualServiceName;
                    request.settingsKey = LLMService::providerSettingsKey(request.provider) + QStringLiteral("/model");
                    request.messages = m_history;
                    request.stream = true;
                    LLMService::instance().sendRequest(request);
                });
                return;
            }
        }
    }

    m_history.append({QStringLiteral("user"), userPrompt});
    
    LLMRequest request;
    request.provider = currentProvider();
    request.model = m_modelCombo->currentText().trimmed();
    request.serviceName = actualServiceName;
    request.settingsKey = LLMService::providerSettingsKey(request.provider) + QStringLiteral("/model");
    request.messages = m_history;
    request.stream = true;
    
    LLMService::instance().sendRequest(request);
}

void ChatPanel::onResponseChunk(const QString &requestId, const QString &chunk)
{
    if (requestId != m_currentStreamId) return; // not our request
    m_currentAiResponse += chunk;

    MarkdownParser parser;
    QString html = parser.renderHtml(m_currentAiResponse);
    updateLastMessageInView(html, m_currentAiResponse);
}

void ChatPanel::onResponseFinished(const QString &requestId, const QString &fullText)
{
    if (requestId != m_currentStreamId) return; // not our request
    m_progressBar->hide();
    m_sendBtn->setEnabled(true);
    m_history.append({QStringLiteral("assistant"), fullText});
    
    MarkdownParser parser;
    QString html = parser.renderHtml(fullText);
    updateLastMessageInView(html, fullText);
}

void ChatPanel::onError(const QString &message)
{
    m_progressBar->hide();
    m_sendBtn->setEnabled(true);
    // Use a literal string for the HTML tags to avoid i18n placeholder conflicts
    updateLastMessageInView(QStringLiteral("<b>") + i18n("Error") + QStringLiteral(":</b> ") + message, QString());
}

void ChatPanel::clearChat()
{
    m_history.clear();
    m_currentAiResponse.clear();
    m_webView->page()->runJavaScript(QStringLiteral("clearHistory()"));
}

void ChatPanel::setPrompt(const QString &prompt)
{
    m_inputEdit->setPlainText(prompt);
    m_inputEdit->setFocus();
}

LLMProvider ChatPanel::currentProvider() const
{
    return static_cast<LLMProvider>(m_providerCombo->currentData().toInt());
}

void ChatPanel::appendMessageToView(const QString &role, const QString &text, const QString &rawText)
{
    // text is already HTML
    QString js = QStringLiteral("appendMessage('%1', %2, %3)").arg(role, 
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), text}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1),
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("r"), rawText}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1));
    m_webView->page()->runJavaScript(js);
}

void ChatPanel::updateLastMessageInView(const QString &text, const QString &rawText)
{
    // text is already HTML
    QString js = QStringLiteral("updateLastMessage(%1, %2)").arg(
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), text}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1),
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("r"), rawText}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1));
    m_webView->page()->runJavaScript(js);
}

void ChatPanel::handleInsert(const QString &text)
{
    Q_EMIT insertTextAtCursor(text);
}
