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

    m_tabWidget->addTab(createLLMTab(), i18n("LLM Integration"));
    m_tabWidget->addTab(createPromptsTab(), i18n("Prompt Templates"));
    m_tabWidget->addTab(createAnalyzerTab(), i18n("Game Analyzer"));
    m_tabWidget->addTab(createEditorTab(), i18n("Editor"));

    mainLayout->addWidget(m_tabWidget);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    setMinimumSize(450, 550);
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
    auto *removeBtn = new QPushButton(i18n("Remove"), this);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    templatesLayout->addLayout(btnLayout);
    layout->addWidget(templatesGroup);

    // Core System Prompts Section
    auto *coreGroup = new QGroupBox(i18n("Core Engine System Prompts"), this);
    auto *coreLayout = new QFormLayout(coreGroup);

    m_analyzerPromptEdit = new QLineEdit(this);
    m_synopsisFilePromptEdit = new QLineEdit(this);
    m_synopsisFolderPromptEdit = new QLineEdit(this);
    m_charGenPromptEdit = new QLineEdit(this);
    m_simArbiterPromptEdit = new QLineEdit(this);
    m_simGriotPromptEdit = new QLineEdit(this);
    m_simActorPromptEdit = new QLineEdit(this);

    coreLayout->addRow(i18n("Game Analyzer:"), m_analyzerPromptEdit);
    coreLayout->addRow(i18n("File Synopsis:"), m_synopsisFilePromptEdit);
    coreLayout->addRow(i18n("Folder Synopsis:"), m_synopsisFolderPromptEdit);
    coreLayout->addRow(i18n("Character Generator:"), m_charGenPromptEdit);
    coreLayout->addRow(i18n("Simulation Arbiter:"), m_simArbiterPromptEdit);
    coreLayout->addRow(i18n("Simulation Griot:"), m_simGriotPromptEdit);
    coreLayout->addRow(i18n("Simulation Actor:"), m_simActorPromptEdit);

    layout->addWidget(coreGroup);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, i18n("Add Template"), i18n("Template Name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty()) {
            QString content = QInputDialog::getMultiLineText(this, i18n("Add Template"), i18n("Prompt Content:"), QString(), &ok);
            if (ok) {
                auto *item = new QListWidgetItem(name, m_promptsList);
                item->setData(Qt::UserRole, content);
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

    m_analyzerModelEdit = new QLineEdit(this);
    m_analyzerModelEdit->setPlaceholderText(i18n("Leave blank to use provider default model"));
    layout->addRow(i18n("Analyzer Model:"), m_analyzerModelEdit);

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
    m_analyzerModelEdit->setText(settings.value(QStringLiteral("analyzer/model")).toString());

    // Load Core System Prompts
    m_analyzerPromptEdit->setText(settings.value(QStringLiteral("analyzer/system_prompt"),
        QStringLiteral("You are an expert RPG game design analyzer.\n"
                       "Analyze the provided document for rule conflicts, ambiguities, and completeness gaps.\n"
                       "You must output ONLY a valid JSON array of objects. Do not include markdown code blocks or conversational text.\n"
                       "Format: [{\"line\": 0, \"severity\": \"error|warning|info\", \"message\": \"...\", \"references\": [{\"filePath\": \"...\", \"line\": 0}]}]")).toString());

    m_synopsisFilePromptEdit->setText(settings.value(QStringLiteral("synopsis/file_prompt"),
        QStringLiteral("You are a senior RPG editor. Write a one-sentence hook/synopsis for this scene or document. Be atmospheric and concise.")).toString());

    m_synopsisFolderPromptEdit->setText(settings.value(QStringLiteral("synopsis/folder_prompt"),
        QStringLiteral("You are an RPG project manager. Write a one-sentence summary for this folder (e.g. 'A collection of character backgrounds' or 'The core mechanics of combat').")).toString());

    m_charGenPromptEdit->setText(settings.value(QStringLiteral("chargen/system_prompt"),
        QStringLiteral("You are an expert RPG character generator. Your goal is to create a character sheet that strictly follows the PROJECT RULES provided below.\n\n"
                       "PROJECT RULES:\n%1\n\n"
                       "TASK:\n"
                       "1. Output a valid JSON object representing the character sheet.\n"
                       "2. The JSON must include 'name', 'concept', 'stats', 'skills', 'equipment', and 'biography'.")).toString());

    m_simArbiterPromptEdit->setText(settings.value(QStringLiteral("simulation/arbiter_prompt"),
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
                       "4. Return ONLY a JSON object with 'logical_patch' and 'mechanical_log'.")).toString());

    m_simGriotPromptEdit->setText(settings.value(QStringLiteral("simulation/griot_prompt"),
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
                       "- No mechanical jargon.")).toString());

    m_simActorPromptEdit->setText(settings.value(QStringLiteral("simulation/actor_prompt"),
        QStringLiteral("You are an autonomous agent in a tabletop RPG simulation.\n"
                       "Name: %1\n"
                       "Motive: %2\n"
                       "Your Character Sheet (JSON): %3\n\n"
                       "TACTICAL AGGRESSION (Level %4): %5\n\n"
                       "CURRENT WORLD STATE (JSON): %6\n\n"
                       "RULES CONTEXT:\n%7\n\n"
                       "TASK:\n"
                       "Decide your next action based on your motive and the current state.\n"
                       "Return ONLY a JSON object: {\"intent\": \"...\", \"reasoning\": \"...\"}")).toString());

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
    settings.setValue(QStringLiteral("analyzer/model"), m_analyzerModelEdit->text());

    // Save Core System Prompts
    settings.setValue(QStringLiteral("analyzer/system_prompt"), m_analyzerPromptEdit->text());
    settings.setValue(QStringLiteral("synopsis/file_prompt"), m_synopsisFilePromptEdit->text());
    settings.setValue(QStringLiteral("synopsis/folder_prompt"), m_synopsisFolderPromptEdit->text());
    settings.setValue(QStringLiteral("chargen/system_prompt"), m_charGenPromptEdit->text());
    settings.setValue(QStringLiteral("simulation/arbiter_prompt"), m_simArbiterPromptEdit->text());
    settings.setValue(QStringLiteral("simulation/griot_prompt"), m_simGriotPromptEdit->text());
    settings.setValue(QStringLiteral("simulation/actor_prompt"), m_simActorPromptEdit->text());

    // Save Prompts
    QJsonObject obj;
    for (int i = 0; i < m_promptsList->count(); ++i) {
        auto *item = m_promptsList->item(i);
        obj[item->text()] = item->data(Qt::UserRole).toString();
    }
    settings.setValue(QStringLiteral("llm/prompts"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}
