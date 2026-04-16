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

#include "projectsettingsdialog.h"
#include "projectmanager.h"
#include "lorekeeperservice.h"

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

    lkLayout->addWidget(new QLabel(i18n("Lore Extraction Prompt:"), this));
    m_lkPromptEdit = new QPlainTextEdit(this);
    lkLayout->addWidget(m_lkPromptEdit);

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
    
    // Save LoreKeeper Config
    if (auto *current = m_lkTable->currentItem()) {
        current->setData(Qt::UserRole, m_lkPromptEdit->toPlainText());
    }

    QJsonObject lkConfig;
    QJsonArray categories;
    for (int i = 0; i < m_lkTable->rowCount(); ++i) {
        QJsonObject cat;
        cat[QStringLiteral("name")] = m_lkTable->item(i, 0)->text();
        cat[QStringLiteral("prompt")] = m_lkTable->item(i, 0)->data(Qt::UserRole).toString();
        categories.append(cat);
    }
    lkConfig[QStringLiteral("categories")] = categories;
    pm.setLoreKeeperConfig(lkConfig);
    
    pm.saveProject();
    
    // Notify LoreKeeper Service
    LoreKeeperService::instance().updateConfig(lkConfig);
}
