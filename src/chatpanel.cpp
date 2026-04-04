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

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    initChatView();

    connect(&LLMService::instance(), &LLMService::requestStarted, this, [this]() {
        m_progressBar->show();
        m_sendBtn->setEnabled(false);
        m_currentAiResponse.clear();
        appendMessageToView(QStringLiteral("assistant"), i18n("AI is thinking..."));
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

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setMinimumWidth(120);
    updateModelList();
    toolbar->addWidget(m_modelCombo);

    toolbar->addStretch();

    m_clearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), this);
    m_clearBtn->setToolTip(i18n("Clear Chat"));
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
            <script>
                function appendMessage(role, html) {
                    const div = document.createElement('div');
                    div.className = 'message ' + role;
                    div.innerHTML = html;
                    document.getElementById('chat-history').appendChild(div);
                    window.scrollTo(0, document.body.scrollHeight);
                }
                function updateLastMessage(html) {
                    const messages = document.getElementsByClassName('message assistant');
                    if (messages.length > 0) {
                        messages[messages.length - 1].innerHTML = html;
                        window.scrollTo(0, document.body.scrollHeight);
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

void ChatPanel::updateModelList()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    m_modelCombo->clear();
    
    // For now, we just show the active provider's default model and maybe some hardcoded ones
    int providerIdx = settings.value(QStringLiteral("llm/provider"), 0).toInt();
    if (providerIdx == 0) { // OpenAI
        m_modelCombo->addItem(settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString());
        m_modelCombo->addItem(QStringLiteral("gpt-4-turbo"));
        m_modelCombo->addItem(QStringLiteral("gpt-3.5-turbo"));
    } else if (providerIdx == 1) { // Anthropic
        m_modelCombo->addItem(settings.value(QStringLiteral("llm/anthropic/model"), QStringLiteral("claude-3-5-sonnet-20240620")).toString());
        m_modelCombo->addItem(QStringLiteral("claude-3-opus-20240229"));
        m_modelCombo->addItem(QStringLiteral("claude-3-haiku-20240307"));
    } else { // Ollama
        m_modelCombo->addItem(settings.value(QStringLiteral("llm/ollama/model"), QStringLiteral("llama3")).toString());
        m_modelCombo->addItem(QStringLiteral("mistral"));
        m_modelCombo->addItem(QStringLiteral("phi3"));
    }
}

void ChatPanel::sendMessage()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    askAI(text);
    m_inputEdit->clear();
}

void ChatPanel::askAI(const QString &userPrompt)
{
    MarkdownParser parser;
    QString html = parser.renderHtml(userPrompt);
    appendMessageToView(QStringLiteral("user"), html);
    
    m_history.append({QStringLiteral("user"), userPrompt});
    
    LLMRequest request;
    request.provider = currentProvider();
    request.model = m_modelCombo->currentText();
    request.messages = m_history;
    
    LLMService::instance().sendRequest(request);
}

void ChatPanel::onResponseChunk(const QString &chunk)
{
    m_currentAiResponse += chunk;
    
    MarkdownParser parser;
    QString html = parser.renderHtml(m_currentAiResponse);
    updateLastMessageInView(html);
}

void ChatPanel::onResponseFinished(const QString &fullText)
{
    m_progressBar->hide();
    m_sendBtn->setEnabled(true);
    m_history.append({QStringLiteral("assistant"), fullText});
    
    MarkdownParser parser;
    QString html = parser.renderHtml(fullText);
    updateLastMessageInView(html);
}

void ChatPanel::onError(const QString &message)
{
    m_progressBar->hide();
    m_sendBtn->setEnabled(true);
    updateLastMessageInView(i18n("<b>Error:</b> %1").arg(message));
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
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    return static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
}

void ChatPanel::appendMessageToView(const QString &role, const QString &text)
{
    // text is already HTML
    QString js = QStringLiteral("appendMessage('%1', %2)").arg(role, QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), text}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1));
    m_webView->page()->runJavaScript(js);
}

void ChatPanel::updateLastMessageInView(const QString &text)
{
    // text is already HTML
    QString js = QStringLiteral("updateLastMessage(%1)").arg(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), text}}).toJson(QJsonDocument::Compact)).mid(5).chopped(1));
    m_webView->page()->runJavaScript(js);
}
