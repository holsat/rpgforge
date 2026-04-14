#include <QPointer>
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

#include "settingsdialog.h"
#include "prompteditordialog.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QSettings>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Configure RPG Forge"));
    setupUi();
    load();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    m_tabWidget->addTab(createLLMTab(), i18n("LLM Providers"));
    m_tabWidget->addTab(createAgentsTab(), i18n("AI Agents"));
    m_tabWidget->addTab(createPromptsTab(), i18n("System Prompts"));
    m_tabWidget->addTab(createAnalyzerTab(), i18n("Game Analyzer"));
    m_tabWidget->addTab(createEditorTab(), i18n("Editor"));

    mainLayout->addWidget(m_tabWidget);

    m_testProgressBar = new QProgressBar(this);
    m_testProgressBar->setRange(0, 0); // Indeterminate
    m_testProgressBar->hide();
    mainLayout->addWidget(m_testProgressBar);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    setMinimumSize(600, 650);
}

QWidget* SettingsDialog::createLLMTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_activeProviderCombo = new QComboBox(this);
    m_activeProviderCombo->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama"), QStringLiteral("Grok (xAI)"), QStringLiteral("Gemini (Google)")});
    
    auto *providerLayout = new QFormLayout();
    providerLayout->addRow(i18n("Active Provider:"), m_activeProviderCombo);
    // Provider agnostic
    m_embeddingModelEdit = new QLineEdit(this);
    m_embeddingModelEdit->setPlaceholderText(i18n("Select via provider model list"));
    providerLayout->addRow(i18n("Embedding Model:"), m_embeddingModelEdit);
    layout->addLayout(providerLayout);

    // OpenAI Group
    auto *openaiGroup = new QGroupBox(i18n("OpenAI"), this);
    auto *openaiLayout = new QFormLayout(openaiGroup);
    m_openaiKeyEdit = new QLineEdit(this);
    m_openaiKeyEdit->setEchoMode(QLineEdit::Password);
    openaiLayout->addRow(i18n("API Key:"), m_openaiKeyEdit);
    m_openaiModelEdit = new QLineEdit(this);
    m_openaiModelEdit->setPlaceholderText(i18n("Enter or fetch model name"));
    openaiLayout->addRow(i18n("Default Model:"), m_openaiModelEdit);
    m_openaiEndpointEdit = new QLineEdit(this);
    m_openaiEndpointEdit->setPlaceholderText(QStringLiteral("https://api.openai.com/v1/chat/completions"));
    openaiLayout->addRow(i18n("Endpoint:"), m_openaiEndpointEdit);
    layout->addWidget(openaiGroup);

    // Anthropic Group
    auto *anthropicGroup = new QGroupBox(i18n("Anthropic"), this);
    auto *anthropicLayout = new QFormLayout(anthropicGroup);
    m_anthropicKeyEdit = new QLineEdit(this);
    m_anthropicKeyEdit->setEchoMode(QLineEdit::Password);
    anthropicLayout->addRow(i18n("API Key:"), m_anthropicKeyEdit);
    m_anthropicModelEdit = new QLineEdit(this);
    m_anthropicModelEdit->setPlaceholderText(i18n("Enter or fetch model name"));
    anthropicLayout->addRow(i18n("Default Model:"), m_anthropicModelEdit);
    m_anthropicEndpointEdit = new QLineEdit(this);
    m_anthropicEndpointEdit->setPlaceholderText(QStringLiteral("https://api.anthropic.com/v1/messages"));
    anthropicLayout->addRow(i18n("Endpoint:"), m_anthropicEndpointEdit);
    layout->addWidget(anthropicGroup);

    // Ollama Group
    auto *ollamaGroup = new QGroupBox(i18n("Ollama"), this);
    auto *ollamaLayout = new QFormLayout(ollamaGroup);
    m_ollamaEndpointEdit = new QLineEdit(this);
    m_ollamaEndpointEdit->setPlaceholderText(QStringLiteral("http://localhost:11434/api/chat"));
    ollamaLayout->addRow(i18n("Local Endpoint:"), m_ollamaEndpointEdit);
    m_ollamaModelEdit = new QLineEdit(this);
    m_ollamaModelEdit->setPlaceholderText(i18n("Enter or fetch model name"));
    ollamaLayout->addRow(i18n("Default Model:"), m_ollamaModelEdit);
    layout->addWidget(ollamaGroup);

    // Grok Group
    auto *grokGroup = new QGroupBox(i18n("Grok (xAI)"), this);
    auto *grokLayout = new QFormLayout(grokGroup);
    m_grokKeyEdit = new QLineEdit(this);
    m_grokKeyEdit->setEchoMode(QLineEdit::Password);
    grokLayout->addRow(i18n("API Key:"), m_grokKeyEdit);
    m_grokModelEdit = new QLineEdit(this);
    m_grokModelEdit->setPlaceholderText(i18n("Enter or fetch model name"));
    grokLayout->addRow(i18n("Default Model:"), m_grokModelEdit);
    m_grokEndpointEdit = new QLineEdit(this);
    m_grokEndpointEdit->setPlaceholderText(QStringLiteral("https://api.x.ai/v1/chat/completions"));
    grokLayout->addRow(i18n("Endpoint:"), m_grokEndpointEdit);
    layout->addWidget(grokGroup);

    // Gemini Group
    auto *geminiGroup = new QGroupBox(i18n("Gemini (Google)"), this);
    auto *geminiLayout = new QFormLayout(geminiGroup);
    m_geminiKeyEdit = new QLineEdit(this);
    m_geminiKeyEdit->setEchoMode(QLineEdit::Password);
    geminiLayout->addRow(i18n("API Key:"), m_geminiKeyEdit);
    m_geminiModelEdit = new QLineEdit(this);
    m_geminiModelEdit->setPlaceholderText(i18n("Enter or fetch model name"));
    geminiLayout->addRow(i18n("Default Model:"), m_geminiModelEdit);
    m_geminiEndpointEdit = new QLineEdit(this);
    m_geminiEndpointEdit->setPlaceholderText(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
    geminiLayout->addRow(i18n("Endpoint:"), m_geminiEndpointEdit);
    layout->addWidget(geminiGroup);

    layout->addStretch();
    return tab;
}

QWidget* SettingsDialog::createAgentsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto *container = new QWidget(this);
    auto *form = new QFormLayout(container);

    auto createAgentRow = [&](const QString &id, const QString &label, const QString &prefix) {
        auto *prov = new QComboBox(this);
        prov->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama"), QStringLiteral("Grok"), QStringLiteral("Gemini")});
        
        auto *model = new QComboBox(this);
        model->setEditable(true);
        model->setMinimumWidth(200);
        
        auto *rowLayout = new QHBoxLayout();
        rowLayout->addWidget(prov);
        rowLayout->addWidget(model);
        
        form->addRow(label + QStringLiteral(":"), rowLayout);
        m_agentConfigs[id] = {prov, model, prefix};

        connect(prov, &QComboBox::currentIndexChanged, this, [this, id](int index) {
            updateModelCombos(static_cast<LLMProvider>(index));
        });
    };

    createAgentRow(QStringLiteral("analyzer"), i18n("Game Analyzer"), QStringLiteral("analyzer"));
    createAgentRow(QStringLiteral("synopsis_file"), i18n("File Synopsis"), QStringLiteral("synopsis"));
    createAgentRow(QStringLiteral("synopsis_folder"), i18n("Folder Synopsis"), QStringLiteral("synopsis"));
    createAgentRow(QStringLiteral("chargen"), i18n("Character Generator"), QStringLiteral("chargen"));
    createAgentRow(QStringLiteral("sim_arbiter"), i18n("Simulation Arbiter"), QStringLiteral("simulation"));
    createAgentRow(QStringLiteral("sim_griot"), i18n("Simulation Griot"), QStringLiteral("simulation"));
    createAgentRow(QStringLiteral("sim_actor"), i18n("Simulation Actor"), QStringLiteral("simulation"));

    scrollArea->setWidget(container);
    layout->addWidget(scrollArea);

    auto *testBtn = new QPushButton(i18n("Test All Agent Connections"), this);
    layout->addWidget(testBtn);
    connect(testBtn, &QPushButton::clicked, this, [this]() {
        m_testProgressBar->show();
        QStringList agentIds = m_agentConfigs.keys();
        auto *completed = new int(0);
        auto *failed = new int(0);
        
        for (const QString &id : agentIds) {
            LLMRequest req;
            req.provider = static_cast<LLMProvider>(m_agentConfigs[id].providerCombo->currentIndex());
            req.model = m_agentConfigs[id].modelCombo->currentText();
            LLMMessage msg;
            msg.role = QStringLiteral("user");
            msg.content = QStringLiteral("Respond with OK");
            req.messages << msg;
            req.stream = false;

            QPointer<SettingsDialog> weakThis(this);
            LLMService::instance().sendNonStreamingRequest(req, [weakThis, completed, failed, agentIds](const QString &response) {
                if (!weakThis) {
                    (*completed)++;
                    if (*completed == agentIds.size()) {
                        delete completed;
                        delete failed;
                    }
                    return;
                }
                (*completed)++;
                if (response.isEmpty()) (*failed)++;
                
                if (*completed == agentIds.size()) {
                    weakThis->m_testProgressBar->hide();
                    if (*failed > 0) {
                        QMessageBox::critical(weakThis, i18n("Connection Test Failed"), 
                            i18n("%1 agent(s) failed to connect. Please verify your API keys and model names.", *failed));
                    } else {
                        QMessageBox::information(weakThis, i18n("Connection Test Successful"), 
                            i18n("All agents connected successfully."));
                    }
                    delete completed;
                    delete failed;
                }
            });
        }
    });

    return tab;
}

void SettingsDialog::updateModelCombos(LLMProvider provider)
{
    if (m_modelCache.contains(provider)) {
        for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
            auto &config = it.value();
            if (static_cast<LLMProvider>(config.providerCombo->currentIndex()) == provider) {
                QString current = config.modelCombo->currentText();
                config.modelCombo->clear();
                config.modelCombo->addItems(m_modelCache[provider]);
                config.modelCombo->setEditText(current);
            }
        }
        
        // Handle Analyzer Model Combo
        if (static_cast<LLMProvider>(m_analyzerProviderCombo->currentIndex()) == provider) {
            QString current = m_analyzerModelCombo->currentText();
            m_analyzerModelCombo->clear();
            m_analyzerModelCombo->addItems(m_modelCache[provider]);
            m_analyzerModelCombo->setEditText(current);
        }
        return;
    }

    LLMService::instance().fetchModels(provider, [this, provider](const QStringList &models) {
        m_modelCache[provider] = models;
        updateModelCombos(provider);
    });
}

QWidget* SettingsDialog::createPromptsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    // Reusable Templates Section
    auto *templatesGroup = new QGroupBox(i18n("Editor Prompt Templates"), this);
    auto *templatesLayout = new QVBoxLayout(templatesGroup);
    m_promptsList = new QListWidget(this);
    m_promptsList->setMaximumHeight(150);
    templatesLayout->addWidget(m_promptsList);

    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(i18n("Add Template..."), this);
    auto *viewTemplateBtn = new QPushButton(i18n("View"), this);
    auto *editTemplateBtn = new QPushButton(i18n("Edit"), this);
    auto *removeBtn = new QPushButton(i18n("Remove"), this);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(viewTemplateBtn);
    btnLayout->addWidget(editTemplateBtn);
    btnLayout->addWidget(removeBtn);
    templatesLayout->addLayout(btnLayout);
    layout->addWidget(templatesGroup);

    // Core System Prompts Section
    auto *coreGroup = new QGroupBox(i18n("Core Engine System Prompts"), this);
    auto *coreLayout = new QFormLayout(coreGroup);

    setupEnginePromptRow(coreLayout, QStringLiteral("analyzer"), i18n("Game Analyzer"));
    setupEnginePromptRow(coreLayout, QStringLiteral("synopsis_file"), i18n("File Synopsis"));
    setupEnginePromptRow(coreLayout, QStringLiteral("synopsis_folder"), i18n("Folder Synopsis"));
    setupEnginePromptRow(coreLayout, QStringLiteral("chargen"), i18n("Character Generator"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_arbiter"), i18n("Simulation Arbiter"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_griot"), i18n("Simulation Griot"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_actor"), i18n("Simulation Actor"));

    layout->addWidget(coreGroup);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, i18n("Add Template"), i18n("Template Name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty()) {
            PromptEditorDialog dialog(name, QString(), this);
            if (dialog.exec() == QDialog::Accepted) {
                auto *item = new QListWidgetItem(name, m_promptsList);
                item->setData(Qt::UserRole, dialog.content());
            }
        }
    });

    connect(viewTemplateBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_promptsList->currentItem();
        if (item) {
            QMessageBox::information(this, item->text(), item->data(Qt::UserRole).toString());
        }
    });

    connect(editTemplateBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_promptsList->currentItem();
        if (item) {
            PromptEditorDialog dialog(item->text(), item->data(Qt::UserRole).toString(), this);
            if (dialog.exec() == QDialog::Accepted) {
                item->setData(Qt::UserRole, dialog.content());
            }
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        delete m_promptsList->currentItem();
    });

    layout->addStretch();
    return tab;
}

QWidget* SettingsDialog::createAnalyzerTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);

    m_analyzerRunModeCombo = new QComboBox(this);
    m_analyzerRunModeCombo->addItems({i18n("Continuous (On Save)"), i18n("On-Demand"), i18n("Paused")});
    layout->addRow(i18n("Run Mode:"), m_analyzerRunModeCombo);

    m_analyzerProviderCombo = new QComboBox(this);
    m_analyzerProviderCombo->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama")});
    layout->addRow(i18n("Analyzer Provider:"), m_analyzerProviderCombo);

    m_analyzerModelCombo = new QComboBox(this);
    m_analyzerModelCombo->setEditable(true);
    m_analyzerModelCombo->setMinimumWidth(250);
    layout->addRow(i18n("Analyzer Model:"), m_analyzerModelCombo);

    connect(m_analyzerProviderCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        updateModelCombos(static_cast<LLMProvider>(index));
    });

    return tab;
}

QWidget* SettingsDialog::createEditorTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_typewriterScrollingCheck = new QCheckBox(i18n("Enable Typewriter Scrolling (center cursor)"), this);
    layout->addWidget(m_typewriterScrollingCheck);

    layout->addStretch();
    return tab;
}

void SettingsDialog::load()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    m_typewriterScrollingCheck->setChecked(settings.value(QStringLiteral("editor/typewriterScrolling"), false).toBool());

    m_activeProviderCombo->setCurrentIndex(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    // Model fields: no hardcoded defaults — empty means "not configured yet"
    // Placeholder text in the widget gives the user a hint without seeding QSettings.
    m_embeddingModelEdit->setText(settings.value(QStringLiteral("llm/embedding_model")).toString());

    m_openaiModelEdit->setText(settings.value(QStringLiteral("llm/openai/model")).toString());
    m_openaiEndpointEdit->setText(settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString());
    m_openaiKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::OpenAI));

    m_anthropicModelEdit->setText(settings.value(QStringLiteral("llm/anthropic/model")).toString());
    m_anthropicEndpointEdit->setText(settings.value(QStringLiteral("llm/anthropic/endpoint"), QStringLiteral("https://api.anthropic.com/v1/messages")).toString());
    m_anthropicKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Anthropic));

    m_ollamaModelEdit->setText(settings.value(QStringLiteral("llm/ollama/model")).toString());
    m_ollamaEndpointEdit->setText(settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434/api/chat")).toString());

    m_grokModelEdit->setText(settings.value(QStringLiteral("llm/grok/model"), QString()).toString());
    m_grokEndpointEdit->setText(settings.value(QStringLiteral("llm/grok/endpoint"), QStringLiteral("https://api.x.ai/v1/chat/completions")).toString());
    m_grokKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Grok));

    m_geminiModelEdit->setText(settings.value(QStringLiteral("llm/gemini/model"), QString()).toString());
    m_geminiEndpointEdit->setText(settings.value(QStringLiteral("llm/gemini/endpoint"), QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions")).toString());
    m_geminiKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Gemini));

    m_analyzerRunModeCombo->setCurrentIndex(settings.value(QStringLiteral("analyzer/run_mode"), 2).toInt());
    m_analyzerProviderCombo->setCurrentIndex(settings.value(QStringLiteral("analyzer/provider"), 0).toInt());
    m_analyzerModelCombo->setEditText(settings.value(QStringLiteral("analyzer/model")).toString());
    updateModelCombos(static_cast<LLMProvider>(m_analyzerProviderCombo->currentIndex()));

    // Load Agent Configurations
    for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
        const QString &id = it.key();
        auto &config = it.value();
        
        int providerIdx = settings.value(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_provider"), 
                                        settings.value(QStringLiteral("llm/provider"), 0)).toInt();
        config.providerCombo->setCurrentIndex(providerIdx);
        
        // Populate model combo for this provider
        updateModelCombos(static_cast<LLMProvider>(providerIdx));
        
        QString defaultModel;
        switch (static_cast<LLMProvider>(providerIdx)) {
            case LLMProvider::OpenAI: defaultModel = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
            case LLMProvider::Anthropic: defaultModel = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
            case LLMProvider::Ollama: defaultModel = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
            case LLMProvider::Grok: defaultModel = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
            case LLMProvider::Gemini: defaultModel = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
        }

        config.modelCombo->setEditText(settings.value(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_model"), defaultModel).toString());
    }

    // Load Core System Prompts
    m_enginePrompts[QStringLiteral("analyzer")].content = settings.value(QStringLiteral("analyzer/system_prompt"),
        QStringLiteral("You are an expert RPG game design analyzer.\n"
                       "Analyze the provided document for rule conflicts, ambiguities, and completeness gaps.\n"
                       "You must output ONLY a valid JSON array of objects. Do not include markdown code blocks or conversational text.\n"
                       "Format: [{\"line\": 0, \"severity\": \"error|warning|info\", \"message\": \"...\", \"references\": [{\"filePath\": \"...\", \"line\": 0}]}]")).toString();

    m_enginePrompts[QStringLiteral("synopsis_file")].content = settings.value(QStringLiteral("synopsis/file_prompt"),
        QStringLiteral("You are a senior RPG editor. Write a one-sentence hook/synopsis for this scene or document. Be atmospheric and concise.")).toString();

    m_enginePrompts[QStringLiteral("synopsis_folder")].content = settings.value(QStringLiteral("synopsis/folder_prompt"),
        QStringLiteral("You are an RPG project manager. Write a one-sentence summary for this folder (e.g. 'A collection of character backgrounds' or 'The core mechanics of combat').")).toString();

    m_enginePrompts[QStringLiteral("chargen")].content = settings.value(QStringLiteral("chargen/system_prompt"),
        QStringLiteral("You are an expert RPG character generator. Your goal is to create a character sheet that strictly follows the PROJECT RULES provided below.\n\n"
                       "PROJECT RULES:\n%1\n\n"
                       "TASK:\n"
                       "1. Output a valid JSON object representing the character sheet.\n"
                       "2. The JSON must include 'name', 'concept', 'stats', 'skills', 'equipment', and 'biography'.")).toString();

    m_enginePrompts[QStringLiteral("sim_arbiter")].content = settings.value(QStringLiteral("simulation/arbiter_prompt"),
        QStringLiteral("You are the Arbiter of a tabletop RPG simulation. Your job is to enforce the rules and update the world state.\n\n"
                       "SCENARIO CONTEXT:\n%1\n\n"
                       "RELEVANT RULES:\n%2\n\n"
                       "CURRENT SITUATION:\n"
                       "- Actor: %3\n"
                       "- Intent: %4\n"
                       "- World State: %5\n\n"
                       "TASK:\n"
                       "1. Evaluate the intent based on rules and context.\n"
                       "2. Generate a 'logical_patch' (JSON) to update the world state.\n"
                       "3. Write a 'mechanical_log' explaining the result.\n"
                       "4. Return ONLY a JSON object with 'logical_patch' and 'mechanical_log'.")).toString();

    m_enginePrompts[QStringLiteral("sim_griot")].content = settings.value(QStringLiteral("simulation/griot_prompt"),
        QStringLiteral("You are the Griot, an immersive storyteller for an RPG simulation.\n"
                       "Your task is to take dry mechanical results and turn them into cinematic prose.\n\n"
                       "INPUTS:\n"
                       "- Actor: %1\n"
                       "- Intent: %2\n"
                       "- Mechanical Result: %3\n"
                       "- World Changes: %4\n"
                       "- Current State: %5\n\n"
                       "STYLE:\n"
                       "- Evocative but concise.\n"
                       "- Second-person perspective if appropriate, or third-person cinematic.\n"
                       "- No mechanical jargon.")).toString();

    m_enginePrompts[QStringLiteral("sim_actor")].content = settings.value(QStringLiteral("simulation/actor_prompt"),
        QStringLiteral("You are an autonomous agent in a tabletop RPG simulation.\n"
                       "Name: %1\n"
                       "Motive: %2\n"
                       "Your Character Sheet (JSON): %3\n\n"
                       "TACTICAL AGGRESSION (Level %4): %5\n\n"
                       "CURRENT WORLD STATE (JSON): %6\n\n"
                       "RULES CONTEXT:\n%7\n\n"
                       "TASK:\n"
                       "Decide your next action based on your motive and the current state.\n"
                       "Return ONLY a JSON object: {\"intent\": \"...\", \"reasoning\": \"...\"}")).toString();

    // Load Prompts
    QString promptsJson = settings.value(QStringLiteral("llm/prompts")).toString();
    if (!promptsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(promptsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                auto *item = new QListWidgetItem(it.key(), m_promptsList);
                item->setData(Qt::UserRole, it.value().toString());
            }
        }
    } else {
        // Defaults
        auto *expand = new QListWidgetItem(i18n("Expand"), m_promptsList);
        expand->setData(Qt::UserRole, i18n("Please expand on the following worldbuilding text, adding more detail and lore."));
        auto *rewrite = new QListWidgetItem(i18n("Rewrite"), m_promptsList);
        rewrite->setData(Qt::UserRole, i18n("Please rewrite the following text for better flow and impact."));
        auto *summarize = new QListWidgetItem(i18n("Summarize"), m_promptsList);
        summarize->setData(Qt::UserRole, i18n("Please summarize the following rules or worldbuilding text."));
    }
}

void SettingsDialog::save()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("editor/typewriterScrolling"), m_typewriterScrollingCheck->isChecked());

    settings.setValue(QStringLiteral("llm/provider"), m_activeProviderCombo->currentIndex());
    settings.setValue(QStringLiteral("llm/embedding_model"), m_embeddingModelEdit->text());

    settings.setValue(QStringLiteral("llm/openai/model"), m_openaiModelEdit->text());
    settings.setValue(QStringLiteral("llm/openai/endpoint"), m_openaiEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::OpenAI, m_openaiKeyEdit->text());

    settings.setValue(QStringLiteral("llm/anthropic/model"), m_anthropicModelEdit->text());
    settings.setValue(QStringLiteral("llm/anthropic/endpoint"), m_anthropicEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Anthropic, m_anthropicKeyEdit->text());

    settings.setValue(QStringLiteral("llm/ollama/model"), m_ollamaModelEdit->text());
    settings.setValue(QStringLiteral("llm/ollama/endpoint"), m_ollamaEndpointEdit->text());

    settings.setValue(QStringLiteral("llm/grok/model"), m_grokModelEdit->text());
    settings.setValue(QStringLiteral("llm/grok/endpoint"), m_grokEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Grok, m_grokKeyEdit->text());

    settings.setValue(QStringLiteral("llm/gemini/model"), m_geminiModelEdit->text());
    settings.setValue(QStringLiteral("llm/gemini/endpoint"), m_geminiEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Gemini, m_geminiKeyEdit->text());

    settings.setValue(QStringLiteral("analyzer/run_mode"), m_analyzerRunModeCombo->currentIndex());
    settings.setValue(QStringLiteral("analyzer/provider"), m_analyzerProviderCombo->currentIndex());
    settings.setValue(QStringLiteral("analyzer/model"), m_analyzerModelCombo->currentText());

    // Save Agent Configurations
    for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
        const QString &id = it.key();
        auto &config = it.value();
        settings.setValue(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_provider"), config.providerCombo->currentIndex());
        settings.setValue(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_model"), config.modelCombo->currentText());
    }

    // Save Core System Prompts
    settings.setValue(QStringLiteral("analyzer/system_prompt"), m_enginePrompts[QStringLiteral("analyzer")].content);
    settings.setValue(QStringLiteral("synopsis/file_prompt"), m_enginePrompts[QStringLiteral("synopsis_file")].content);
    settings.setValue(QStringLiteral("synopsis/folder_prompt"), m_enginePrompts[QStringLiteral("synopsis_folder")].content);
    settings.setValue(QStringLiteral("chargen/system_prompt"), m_enginePrompts[QStringLiteral("chargen")].content);
    settings.setValue(QStringLiteral("simulation/arbiter_prompt"), m_enginePrompts[QStringLiteral("sim_arbiter")].content);
    settings.setValue(QStringLiteral("simulation/griot_prompt"), m_enginePrompts[QStringLiteral("sim_griot")].content);
    settings.setValue(QStringLiteral("simulation/actor_prompt"), m_enginePrompts[QStringLiteral("sim_actor")].content);

    // Save Prompts
    QJsonObject obj;
    for (int i = 0; i < m_promptsList->count(); ++i) {
        auto *item = m_promptsList->item(i);
        obj[item->text()] = item->data(Qt::UserRole).toString();
    }
    settings.setValue(QStringLiteral("llm/prompts"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void SettingsDialog::setupEnginePromptRow(QFormLayout *layout, const QString &id, const QString &label)
{
    auto *rowLayout = new QHBoxLayout();
    
    auto *viewBtn = new QPushButton(i18n("View"), this);
    auto *editBtn = new QPushButton(i18n("Edit"), this);
    
    auto *status = new QLabel(this);
    status->setText(i18n("(Configured)"));
    status->setStyleSheet(QStringLiteral("font-size: 9px; color: gray;"));
    
    rowLayout->addWidget(viewBtn);
    rowLayout->addWidget(editBtn);
    rowLayout->addWidget(status);
    rowLayout->addStretch();
    
    layout->addRow(label + QStringLiteral(":"), rowLayout);
    
    EnginePrompt ep;
    ep.content = QString();
    ep.statusLabel = status;
    m_enginePrompts[id] = ep;
    
    connect(viewBtn, &QPushButton::clicked, this, [this, id, label]() {
        QMessageBox::information(this, label, m_enginePrompts[id].content);
    });
    
    connect(editBtn, &QPushButton::clicked, this, [this, id, label]() {
        openPromptEditor(id);
    });
}

void SettingsDialog::openPromptEditor(const QString &id)
{
    PromptEditorDialog dialog(id, m_enginePrompts[id].content, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_enginePrompts[id].content = dialog.content();
    }
}
