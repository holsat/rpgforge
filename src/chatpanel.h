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

#ifndef CHATPANEL_H
#define CHATPANEL_H

#include <QWidget>
#include "llmservice.h"

class QWebEngineView;
class QTextEdit;
class QComboBox;
class QPushButton;
class QProgressBar;
class ChatPanel;

class ChatBridge : public QObject {
    Q_OBJECT
public:
    explicit ChatBridge(ChatPanel *panel);
    Q_INVOKABLE void handleInsert(const QString &text);
private:
    ChatPanel *m_panel;
};

class ChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPanel(QWidget *parent = nullptr);
    ~ChatPanel() override;

    /**
     * @brief Appends a prompt to the input area and focuses it.
     */
    void setPrompt(const QString &prompt);

    /**
     * @brief Sends a request to the LLM with the given user prompt.
     */
    void askAI(const QString &userPrompt, const QString &serviceName = QString());

    void insertRequested(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

public Q_SLOTS:
    void handleInsert(const QString &text);

private Q_SLOTS:
    void sendMessage();
    void onError(const QString &message);
    void clearChat();
    void updateModelList();
    void onProviderChanged();
    void onModelChanged(const QString &model);

Q_SIGNALS:
    void insertTextAtCursor(const QString &text);

private:
    void setupUi();
    void initChatView();
    QString currentModel() const;
    LLMProvider currentProvider() const;
    
    // JS helpers
    void appendMessageToView(const QString &role, const QString &text, const QString &rawText);
    void updateLastMessageInView(const QString &text, const QString &rawText);

    QWebEngineView *m_webView;
    QTextEdit *m_inputEdit;
    QComboBox *m_providerCombo;
    QComboBox *m_modelCombo;
    QPushButton *m_refreshModelsBtn;
    QPushButton *m_sendBtn;
    QPushButton *m_clearBtn;
    QProgressBar *m_progressBar;

    QString m_currentAiResponse;
    QString m_currentStreamId; // ID from requestStarted; filters out foreign streams
    QList<LLMMessage> m_history;
    ChatBridge *m_bridge = nullptr;
};

#endif // CHATPANEL_H
