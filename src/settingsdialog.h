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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "llmservice.h"

class QLineEdit;
class QComboBox;
class QTabWidget;
class QListWidget;
class QCheckBox;
class QProgressBar;
class QLabel;
class QFormLayout;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void save();

private:
    void setupUi();
    void load();
    QWidget* createLLMTab();
    QWidget* createPromptsTab();
    QWidget* createAnalyzerTab();
    QWidget* createAgentsTab();
    QWidget* createEditorTab();

    struct AgentConfig {
        QComboBox *providerCombo;
        QComboBox *modelCombo;
        QString keyPrefix; // e.g. "analyzer", "synopsis_file"
    };
    QHash<QString, AgentConfig> m_agentConfigs;

    void updateModelCombos(LLMProvider provider);
    void testAgentConnection(const QString &agentId);

    QHash<LLMProvider, QStringList> m_modelCache;
    QProgressBar *m_testProgressBar = nullptr;

    QTabWidget *m_tabWidget = nullptr;

    // Editor settings
    QCheckBox *m_typewriterScrollingCheck = nullptr;

    // Provider fallback order: draggable list of providers with per-row
    // enable checkbox. Row order persists to QSettings llm/provider_order
    // and check state to llm/{provider}/enabled. Used by LLMService's
    // composeDefaultFallbackChain.
    QListWidget *m_providerOrderList = nullptr;
    void buildProviderOrderList();
    void saveProviderOrderList();

    // LLM settings
    QComboBox *m_activeProviderCombo;
    QLineEdit *m_openaiKeyEdit;
    QLineEdit *m_openaiModelEdit;
    QLineEdit *m_openaiEndpointEdit;
    QLineEdit *m_anthropicKeyEdit;
    QLineEdit *m_anthropicModelEdit;
    QLineEdit *m_anthropicEndpointEdit;
    QLineEdit *m_ollamaModelEdit;
    QLineEdit *m_ollamaEndpointEdit;
    QLineEdit *m_grokKeyEdit;
    QLineEdit *m_grokModelEdit;
    QLineEdit *m_grokEndpointEdit;
    QLineEdit *m_geminiKeyEdit;
    QLineEdit *m_geminiModelEdit;
    QLineEdit *m_geminiEndpointEdit;
    QLineEdit *m_lmstudioKeyEdit;
    QLineEdit *m_lmstudioModelEdit;
    QLineEdit *m_lmstudioEndpointEdit;
    QLineEdit *m_embeddingModelEdit;

    // Prompts
    QListWidget *m_promptsList;
    
    struct EnginePrompt {
        QString content;
        QLabel *statusLabel;
    };
    QHash<QString, EnginePrompt> m_enginePrompts;

    void setupEnginePromptRow(QFormLayout *layout, const QString &id, const QString &label);
    void openPromptEditor(const QString &id);

    // Analyzer
    QComboBox *m_analyzerRunModeCombo;
    QComboBox *m_analyzerProviderCombo;
    QComboBox *m_analyzerModelCombo; // Changed from QLineEdit
};

#endif // SETTINGSDIALOG_H
