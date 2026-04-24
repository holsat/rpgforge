/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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

#include "projectsettingsdialog.h"
#include "agentgatekeeper.h"
#include "projectmanager.h"
#include "lorekeeperservice.h"
#include "llmservice.h"
#include "toggleswitch.h"

#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>

#include <KLocalizedString>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>

ProjectSettingsDialog::ProjectSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Project Settings"));
    setupUi();
    load();
}

ProjectSettingsDialog::~ProjectSettingsDialog() = default;

void ProjectSettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    // --- General Tab ---
    auto *generalTab = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(generalTab);

    // Project Metadata
    auto *metaGroup = new QGroupBox(i18n("Project Metadata"), this);
    auto *metaLayout = new QFormLayout(metaGroup);
    m_nameEdit = new QLineEdit(this);
    metaLayout->addRow(i18n("Project Name:"), m_nameEdit);
    m_authorEdit = new QLineEdit(this);
    metaLayout->addRow(i18n("Author:"), m_authorEdit);
    generalLayout->addWidget(metaGroup);

    // PDF Settings
    auto *pdfGroup = new QGroupBox(i18n("PDF & Print Settings"), this);
    auto *pdfLayout = new QFormLayout(pdfGroup);
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItems({QStringLiteral("A4"), QStringLiteral("Letter"), QStringLiteral("A5"), QStringLiteral("Legal")});
    pdfLayout->addRow(i18n("Page Size:"), m_pageSizeCombo);
    m_marginLeftSpin = new QDoubleSpinBox(this);
    m_marginLeftSpin->setRange(0, 100); m_marginLeftSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Left Margin:"), m_marginLeftSpin);
    m_marginRightSpin = new QDoubleSpinBox(this);
    m_marginRightSpin->setRange(0, 100); m_marginRightSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Right Margin:"), m_marginRightSpin);
    m_marginTopSpin = new QDoubleSpinBox(this);
    m_marginTopSpin->setRange(0, 100); m_marginTopSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Top Margin:"), m_marginTopSpin);
    m_marginBottomSpin = new QDoubleSpinBox(this);
    m_marginBottomSpin->setRange(0, 100); m_marginBottomSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Bottom Margin:"), m_marginBottomSpin);
    m_showPageNumbersCheck = new QCheckBox(i18n("Show Page Numbers"), this);
    pdfLayout->addRow(QString(), m_showPageNumbersCheck);
    generalLayout->addWidget(pdfGroup);

    // Appearance
    auto *styleGroup = new QGroupBox(i18n("Appearance"), this);
    auto *styleLayout = new QFormLayout(styleGroup);
    m_stylesheetEdit = new QLineEdit(this);
    m_stylesheetEdit->setPlaceholderText(QStringLiteral("style.css"));
    styleLayout->addRow(i18n("Project Stylesheet:"), m_stylesheetEdit);
    generalLayout->addWidget(styleGroup);
    generalLayout->addStretch();

    tabs->addTab(generalTab, i18n("General"));

    // --- LoreKeeper Tab ---
    auto *lkTab = new QWidget(this);
    auto *lkLayout = new QVBoxLayout(lkTab);

    auto *lkInfo = new QLabel(i18n("Configure entity categories for the LoreKeeper AI to track and summarize from your manuscript."), this);
    lkInfo->setWordWrap(true);
    lkLayout->addWidget(lkInfo);

    auto *tableHLayout = new QHBoxLayout();
    m_lkTable = new QTableWidget(0, 1, this);
    m_lkTable->setHorizontalHeaderLabels({i18n("Category Name")});
    m_lkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_lkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lkTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tableHLayout->addWidget(m_lkTable);

    auto *tableButtons = new QVBoxLayout();
    auto *addBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QString(), this);
    auto *remBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QString(), this);
    tableButtons->addWidget(addBtn);
    tableButtons->addWidget(remBtn);
    tableButtons->addStretch();
    tableHLayout->addLayout(tableButtons);
    lkLayout->addLayout(tableHLayout);

    auto *promptHeader = new QHBoxLayout();
    promptHeader->addWidget(new QLabel(i18n("Lore Extraction Prompt:"), this));
    promptHeader->addStretch();
    m_lkEnhanceBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("tools-wizard")),
        i18n("Enhance Prompt"), this);
    m_lkEnhanceBtn->setToolTip(i18n(
        "Use the configured LoreKeeper LLM to rewrite and strengthen the "
        "current prompt following best-practice prompt-engineering guidelines."));
    promptHeader->addWidget(m_lkEnhanceBtn);
    lkLayout->addLayout(promptHeader);

    m_lkPromptEdit = new QPlainTextEdit(this);
    lkLayout->addWidget(m_lkPromptEdit);

    connect(m_lkEnhanceBtn, &QPushButton::clicked, this, &ProjectSettingsDialog::enhanceCurrentPrompt);

    // Table Logic
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        int row = m_lkTable->rowCount();
        m_lkTable->insertRow(row);
        auto *item = new QTableWidgetItem(i18n("New Category"));
        item->setData(Qt::UserRole, QString()); // Prompt
        m_lkTable->setItem(row, 0, item);
        m_lkTable->setCurrentItem(item);
    });

    connect(remBtn, &QPushButton::clicked, this, [this]() {
        m_lkTable->removeRow(m_lkTable->currentRow());
    });

    connect(m_lkTable, &QTableWidget::currentItemChanged, this, [this](QTableWidgetItem *current, QTableWidgetItem *previous) {
        if (previous) {
            previous->setData(Qt::UserRole, m_lkPromptEdit->toPlainText());
        }
        if (current) {
            m_lkPromptEdit->setPlainText(current->data(Qt::UserRole).toString());
            m_lkPromptEdit->setEnabled(true);
        } else {
            m_lkPromptEdit->clear();
            m_lkPromptEdit->setEnabled(false);
        }
    });

    tabs->addTab(lkTab, i18n("LoreKeeper"));

    // --- AI Services Tab ---
    auto *aiTab = new QWidget(this);
    auto *aiLayout = new QVBoxLayout(aiTab);

    auto *aiInfo = new QLabel(i18n(
        "Enable or disable individual AI agent services that are used for "
        "this project. Disabled agents will not activate when a project is "
        "opened, or file contents are changed. This is useful when working "
        "offline or when you want to manage token costs for services you do "
        "not want to use. These settings are saved on a per project basis."),
        this);
    aiInfo->setWordWrap(true);
    aiLayout->addWidget(aiInfo);

    auto *aiGroup = new QGroupBox(i18n("Background AI Services"), this);
    auto *aiGroupLayout = new QVBoxLayout(aiGroup);

    auto addToggleRow = [&](ToggleSwitch *&outToggle, const QString &label) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        outToggle = new ToggleSwitch(this);
        auto *text = new QLabel(label, this);
        text->setWordWrap(true);
        text->setBuddy(outToggle);
        row->addWidget(outToggle, 0, Qt::AlignTop);
        row->addSpacing(8);
        row->addWidget(text, 1);
        aiGroupLayout->addLayout(row);
    };

    addToggleRow(m_aiAnalyzerToggle, i18n(
        "Game Analyzer — checks rules for conflicts and ambiguities (runs per edit)"));
    addToggleRow(m_aiLoreKeeperToggle, i18n(
        "LoreKeeper — auto-generates character/setting dossiers (runs at project open and per doc)"));
    addToggleRow(m_aiSynopsisToggle, i18n(
        "Synopsis Generator — writes short summaries per file (runs per text change)"));
    addToggleRow(m_aiLibrarianToggle, i18n(
        "Variable Librarian — extracts stats/variables from tables and lists"));
    addToggleRow(m_aiRagAssistToggle, i18n(
        "AI Writing Assistant — on-demand AI for expanding, rewriting, and drafting"));

    aiLayout->addWidget(aiGroup);
    aiLayout->addStretch();

    tabs->addTab(aiTab, i18n("AI Services"));

    // --- Version Control Tab ---
    auto *vcTab = new QWidget(this);
    auto *vcLayout = new QVBoxLayout(vcTab);

    auto *vcInfo = new QLabel(i18n(
        "Control how RPG Forge interacts with the project's Git repository. "
        "Auto-Sync commits every saved change immediately, giving you a "
        "complete history at the cost of many small commits. Turn it off to "
        "batch changes manually via the Sync Project button; note that files "
        "modified while Auto-Sync is off will not be restorable until you "
        "sync. These settings are saved on a per project basis."), this);
    vcInfo->setWordWrap(true);
    vcLayout->addWidget(vcInfo);

    auto *vcGroup = new QGroupBox(i18n("Seamless Version Control"), this);
    auto *vcGroupLayout = new QVBoxLayout(vcGroup);

    auto *autoSyncRow = new QHBoxLayout();
    autoSyncRow->setContentsMargins(0, 0, 0, 0);
    m_autoSyncToggle = new ToggleSwitch(this);
    auto *autoSyncLabel = new QLabel(i18n(
        "Auto-Sync — commit every saved change to Git automatically"), this);
    autoSyncLabel->setWordWrap(true);
    autoSyncLabel->setBuddy(m_autoSyncToggle);
    autoSyncRow->addWidget(m_autoSyncToggle, 0, Qt::AlignTop);
    autoSyncRow->addSpacing(8);
    autoSyncRow->addWidget(autoSyncLabel, 1);
    vcGroupLayout->addLayout(autoSyncRow);

    vcLayout->addWidget(vcGroup);
    vcLayout->addStretch();

    tabs->addTab(vcTab, i18n("Version Control"));

    mainLayout->addWidget(tabs);

    // --- Buttons ---
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    resize(600, 500);
}

void ProjectSettingsDialog::load()
{
    auto &pm = ProjectManager::instance();
    m_nameEdit->setText(pm.projectName());
    m_authorEdit->setText(pm.author());
    m_pageSizeCombo->setCurrentText(pm.pageSize());
    m_marginLeftSpin->setValue(pm.marginLeft());
    m_marginRightSpin->setValue(pm.marginRight());
    m_marginTopSpin->setValue(pm.marginTop());
    m_marginBottomSpin->setValue(pm.marginBottom());
    m_showPageNumbersCheck->setChecked(pm.showPageNumbers());
    m_stylesheetEdit->setText(pm.stylesheetPath());

    // LoreKeeper
    QJsonObject lkConfig = pm.loreKeeperConfig();
    QJsonArray categories = lkConfig.value(QStringLiteral("categories")).toArray();
    
    m_lkTable->setRowCount(0);
    for (const QJsonValue &cv : categories) {
        QJsonObject cat = cv.toObject();
        int row = m_lkTable->rowCount();
        m_lkTable->insertRow(row);
        auto *item = new QTableWidgetItem(cat.value(QStringLiteral("name")).toString());
        item->setData(Qt::UserRole, cat.value(QStringLiteral("prompt")).toString());
        m_lkTable->setItem(row, 0, item);
    }
    
    if (m_lkTable->rowCount() > 0) {
        m_lkTable->setCurrentCell(0, 0);
    } else {
        m_lkPromptEdit->setEnabled(false);
    }

    m_aiAnalyzerToggle->setChecked(pm.aiAnalyzerEnabled());
    m_aiLoreKeeperToggle->setChecked(pm.aiLoreKeeperEnabled());
    m_aiSynopsisToggle->setChecked(pm.aiSynopsisEnabled());
    m_aiLibrarianToggle->setChecked(pm.aiLibrarianEnabled());
    m_aiRagAssistToggle->setChecked(pm.aiRagAssistEnabled());

    m_autoSyncToggle->setChecked(pm.autoSync());
}

void ProjectSettingsDialog::enhanceCurrentPrompt()
{
    // Ask the configured LoreKeeper LLM to rewrite the current prompt
    // following prompt-engineering best practices, then replace the
    // textarea content with the result. Preserves the user's intent
    // while strengthening structure, specificity, and output-format
    // instructions.
    const QString current = m_lkPromptEdit->toPlainText().trimmed();
    if (current.isEmpty()) {
        QMessageBox::information(this, i18n("Enhance Prompt"),
            i18n("The prompt is empty. Type a rough description of what you "
                 "want LoreKeeper to extract, then click Enhance Prompt again."));
        return;
    }

    const QString categoryName = m_lkTable->currentItem()
        ? m_lkTable->currentItem()->text()
        : QStringLiteral("Unknown");

    // Use the same provider/model settings LoreKeeper uses for its
    // generation pass — that way the enhanced prompt style matches
    // what will actually run.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMRequest req;
    req.provider = static_cast<LLMProvider>(
        settings.value(QStringLiteral("lorekeeper/lorekeeper_provider"),
                       settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    req.model = settings.value(QStringLiteral("lorekeeper/lorekeeper_model")).toString();
    if (req.model.isEmpty()) {
        const QString sk = LLMService::providerSettingsKey(req.provider);
        req.model = settings.value(sk + QStringLiteral("/model")).toString();
    }
    req.serviceName = i18n("LoreKeeper Prompt Enhancement");
    req.settingsKey = QStringLiteral("lorekeeper/lorekeeper_model");
    req.temperature = 0.4;
    req.maxTokens = 2048;
    req.stream = false;

    req.messages << LLMMessage{QStringLiteral("system"), QStringLiteral(
        "You are a prompt-engineering assistant. The user provides a rough "
        "extraction prompt that a world-building AI will use to synthesize "
        "a dossier for a single entity from a larger RPG writing project. "
        "Rewrite the prompt to be clearer, more specific, and more "
        "actionable following best-practice guidelines:\n\n"
        "  - State the role (\"You are an expert world-builder...\").\n"
        "  - Describe the target document type (e.g. a structured Markdown "
        "    dossier) and the sections it should contain.\n"
        "  - Specify how to handle missing information (infer vs. leave "
        "    blank vs. mark TBD).\n"
        "  - Instruct the model to preserve the author's established voice "
        "    and to cite source files inline using the exact paths provided.\n"
        "  - Keep the output under 400 words of prompt text.\n\n"
        "Return ONLY the rewritten prompt. No preamble, no explanations, "
        "no code fences, no meta-commentary.")};
    req.messages << LLMMessage{QStringLiteral("user"), i18n(
        "Category: %1\n\nCurrent extraction prompt to enhance:\n\n%2",
        categoryName, current)};

    m_lkEnhanceBtn->setEnabled(false);
    m_lkEnhanceBtn->setText(i18n("Enhancing..."));

    // Use the detailed callback: response is the enhanced prompt on success;
    // error is populated with the provider's actual error (quota, auth, etc.)
    // when the request fails, letting us surface a useful dialog instead of
    // the generic "empty response" placeholder.
    QPointer<ProjectSettingsDialog> weakThis(this);
    LLMService::instance().sendNonStreamingRequestDetailed(req,
        [weakThis](const QString &response, const QString &error) {
            if (!weakThis) return;
            weakThis->m_lkEnhanceBtn->setEnabled(true);
            weakThis->m_lkEnhanceBtn->setText(i18n("Enhance Prompt"));

            if (!error.isEmpty()) {
                QMessageBox::warning(weakThis, i18n("Enhance Prompt failed"),
                    i18n("The LLM provider returned an error:\n\n%1", error));
                return;
            }
            const QString cleaned = response.trimmed();
            if (cleaned.isEmpty()) {
                QMessageBox::warning(weakThis, i18n("Enhance Prompt"),
                    i18n("The LLM returned an empty response. Check that "
                         "your API key, provider, and model are configured "
                         "correctly, then try again."));
                return;
            }
            weakThis->m_lkPromptEdit->setPlainText(cleaned);
        });
}

void ProjectSettingsDialog::save()
{
    auto &pm = ProjectManager::instance();
    pm.setProjectName(m_nameEdit->text());
    pm.setAuthor(m_authorEdit->text());
    pm.setPageSize(m_pageSizeCombo->currentText());
    pm.setMarginLeft(m_marginLeftSpin->value());
    pm.setMarginRight(m_marginRightSpin->value());
    pm.setMarginTop(m_marginTopSpin->value());
    pm.setMarginBottom(m_marginBottomSpin->value());
    pm.setShowPageNumbers(m_showPageNumbersCheck->isChecked());
    pm.setStylesheetPath(m_stylesheetEdit->text());

    // Commit any in-progress table-cell edit back to the item model.
    // QTableWidget's inline editor does not auto-commit when the user
    // clicks OK without first tabbing away from the cell — without
    // this call, a freshly typed category name (or a rename of an
    // existing one) silently reverts to the previous value.
    if (auto *editor = m_lkTable->indexWidget(m_lkTable->currentIndex())) {
        m_lkTable->closePersistentEditor(m_lkTable->currentItem());
        Q_UNUSED(editor);
    }
    m_lkTable->setCurrentCell(m_lkTable->currentRow(), m_lkTable->currentColumn());  // nudge commit

    // Save LoreKeeper Config
    if (auto *current = m_lkTable->currentItem()) {
        current->setData(Qt::UserRole, m_lkPromptEdit->toPlainText());
    }

    QJsonObject lkConfig;
    QJsonArray categories;
    for (int i = 0; i < m_lkTable->rowCount(); ++i) {
        auto *cell = m_lkTable->item(i, 0);
        if (!cell) {
            qWarning() << "ProjectSettings: LoreKeeper row" << i
                        << "has no cell item — skipping (would have been dropped)";
            continue;
        }
        QJsonObject cat;
        cat[QStringLiteral("name")] = cell->text();
        cat[QStringLiteral("prompt")] = cell->data(Qt::UserRole).toString();
        categories.append(cat);
    }
    lkConfig[QStringLiteral("categories")] = categories;

    qInfo().noquote() << "ProjectSettings::save: writing"
                      << categories.size() << "LoreKeeper categor(ies):"
                      << QJsonDocument(categories).toJson(QJsonDocument::Compact);

    pm.setLoreKeeperConfig(lkConfig);

    // Apply AI toggles before the final save so we don't write the project
    // twice with a brief stale-flag window in between. setEnabled itself
    // persists + emits serviceEnabledChanged; subsequent saveProject() below
    // is idempotent.
    auto &gate = AgentGatekeeper::instance();
    gate.setEnabled(AgentGatekeeper::Service::Analyzer,   m_aiAnalyzerToggle->isChecked());
    gate.setEnabled(AgentGatekeeper::Service::LoreKeeper, m_aiLoreKeeperToggle->isChecked());
    gate.setEnabled(AgentGatekeeper::Service::Synopsis,   m_aiSynopsisToggle->isChecked());
    gate.setEnabled(AgentGatekeeper::Service::Librarian,  m_aiLibrarianToggle->isChecked());
    gate.setEnabled(AgentGatekeeper::Service::RagAssist,  m_aiRagAssistToggle->isChecked());

    pm.setAutoSync(m_autoSyncToggle->isChecked());

    pm.saveProject();

    // Notify LoreKeeper Service
    LoreKeeperService::instance().updateConfig(lkConfig);
}
