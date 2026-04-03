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

    mainLayout->addWidget(m_tabWidget);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    resize(500, 600);
}

QWidget* SettingsDialog::createLLMTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_activeProviderCombo = new QComboBox(this);
    m_activeProviderCombo->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama")});
    
    auto *providerLayout = new QFormLayout();
    providerLayout->addRow(i18n("Active Provider:"), m_activeProviderCombo);
    layout->addLayout(providerLayout);

    // OpenAI Group
    auto *openaiGroup = new QGroupBox(i18n("OpenAI"), this);
    auto *openaiLayout = new QFormLayout(openaiGroup);
    m_openaiKeyEdit = new QLineEdit(this);
    m_openaiKeyEdit->setEchoMode(QLineEdit::Password);
    openaiLayout->addRow(i18n("API Key:"), m_openaiKeyEdit);
    m_openaiModelEdit = new QLineEdit(this);
    m_openaiModelEdit->setPlaceholderText(QStringLiteral("gpt-4o"));
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
    m_anthropicModelEdit->setPlaceholderText(QStringLiteral("claude-3-5-sonnet-20240620"));
    anthropicLayout->addRow(i18n("Default Model:"), m_anthropicModelEdit);
    layout->addWidget(anthropicGroup);

    // Ollama Group
    auto *ollamaGroup = new QGroupBox(i18n("Ollama"), this);
    auto *ollamaLayout = new QFormLayout(ollamaGroup);
    m_ollamaEndpointEdit = new QLineEdit(this);
    m_ollamaEndpointEdit->setPlaceholderText(QStringLiteral("http://localhost:11434/api/chat"));
    ollamaLayout->addRow(i18n("Local Endpoint:"), m_ollamaEndpointEdit);
    m_ollamaModelEdit = new QLineEdit(this);
    m_ollamaModelEdit->setPlaceholderText(QStringLiteral("llama3"));
    ollamaLayout->addRow(i18n("Default Model:"), m_ollamaModelEdit);
    layout->addWidget(ollamaGroup);

    layout->addStretch();
    return tab;
}

QWidget* SettingsDialog::createPromptsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_promptsList = new QListWidget(this);
    layout->addWidget(m_promptsList);

    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(i18n("Add Template..."), this);
    auto *removeBtn = new QPushButton(i18n("Remove"), this);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    layout->addLayout(btnLayout);

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

    return tab;
}

void SettingsDialog::load()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    m_activeProviderCombo->setCurrentIndex(settings.value(QStringLiteral("llm/provider"), 0).toInt());

    m_openaiModelEdit->setText(settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString());
    m_openaiEndpointEdit->setText(settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString());
    m_openaiKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::OpenAI));

    m_anthropicModelEdit->setText(settings.value(QStringLiteral("llm/anthropic/model"), QStringLiteral("claude-3-5-sonnet-20240620")).toString());
    m_anthropicKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Anthropic));

    m_ollamaModelEdit->setText(settings.value(QStringLiteral("llm/ollama/model"), QStringLiteral("llama3")).toString());
    m_ollamaEndpointEdit->setText(settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434/api/chat")).toString());

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

    settings.setValue(QStringLiteral("llm/provider"), m_activeProviderCombo->currentIndex());

    settings.setValue(QStringLiteral("llm/openai/model"), m_openaiModelEdit->text());
    settings.setValue(QStringLiteral("llm/openai/endpoint"), m_openaiEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::OpenAI, m_openaiKeyEdit->text());

    settings.setValue(QStringLiteral("llm/anthropic/model"), m_anthropicModelEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Anthropic, m_anthropicKeyEdit->text());

    settings.setValue(QStringLiteral("llm/ollama/model"), m_ollamaModelEdit->text());
    settings.setValue(QStringLiteral("llm/ollama/endpoint"), m_ollamaEndpointEdit->text());

    // Save Prompts
    QJsonObject obj;
    for (int i = 0; i < m_promptsList->count(); ++i) {
        auto *item = m_promptsList->item(i);
        obj[item->text()] = item->data(Qt::UserRole).toString();
    }
    settings.setValue(QStringLiteral("llm/prompts"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}
