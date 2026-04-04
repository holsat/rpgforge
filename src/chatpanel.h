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
    void askAI(const QString &userPrompt);

private Q_SLOTS:
    void sendMessage();
    void onResponseChunk(const QString &chunk);
    void onResponseFinished(const QString &fullText);
    void onError(const QString &message);
    void clearChat();
    void updateModelList();

private:
    void setupUi();
    void initChatView();
    QString currentModel() const;
    LLMProvider currentProvider() const;
    
    // JS helpers
    void appendMessageToView(const QString &role, const QString &text);
    void updateLastMessageInView(const QString &text);

    QWebEngineView *m_webView;
    QTextEdit *m_inputEdit;
    QComboBox *m_modelCombo;
    QPushButton *m_sendBtn;
    QPushButton *m_clearBtn;
    QProgressBar *m_progressBar;

    QString m_currentAiResponse;
    QList<LLMMessage> m_history;
};

#endif // CHATPANEL_H
