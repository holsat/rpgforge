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
    QComboBox *m_openaiModelCombo;
    QLineEdit *m_openaiEndpointEdit;
    QLineEdit *m_anthropicKeyEdit;
    QComboBox *m_anthropicModelCombo;
    QLineEdit *m_anthropicEndpointEdit;
    QComboBox *m_ollamaModelCombo;
    QLineEdit *m_ollamaEndpointEdit;
    QLineEdit *m_grokKeyEdit;
    QComboBox *m_grokModelCombo;
    QLineEdit *m_grokEndpointEdit;
    QLineEdit *m_geminiKeyEdit;
    QComboBox *m_geminiModelCombo;
    QLineEdit *m_geminiEndpointEdit;
    QLineEdit *m_lmstudioKeyEdit;
    QComboBox *m_lmstudioModelCombo;
    QLineEdit *m_lmstudioEndpointEdit;
    QLineEdit *m_embeddingModelEdit;

    // Per-provider inline status label under the API key / endpoint row.
    // Updated by the API-key-test flow to show "Connected — N models" on
    // success or the HTTP error on failure. Null keys are skipped.
    QHash<LLMProvider, QLabel*> m_providerStatusLabels;

    // Map LLMProvider -> its default-model combo in the LLM tab, used by
    // updateModelCombos so one function populates both tabs' combos.
    QHash<LLMProvider, QComboBox*> m_providerModelCombos;

    // Trigger an API-key-test + model-fetch for the given provider. Called
    // when the user finishes editing the key/endpoint field, and during
    // load() to pre-populate combos on dialog open.
    void testProviderConnection(LLMProvider provider);

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
