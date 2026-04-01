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

#include <KLocalizedString>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QGroupBox>

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

    // Project Metadata
    auto *metaGroup = new QGroupBox(i18n("Project Metadata"), this);
    auto *metaLayout = new QFormLayout(metaGroup);
    
    m_nameEdit = new QLineEdit(this);
    metaLayout->addRow(i18n("Project Name:"), m_nameEdit);
    
    m_authorEdit = new QLineEdit(this);
    metaLayout->addRow(i18n("Author:"), m_authorEdit);
    
    mainLayout->addWidget(metaGroup);

    // PDF Settings
    auto *pdfGroup = new QGroupBox(i18n("PDF & Print Settings"), this);
    auto *pdfLayout = new QFormLayout(pdfGroup);
    
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItems({QStringLiteral("A4"), QStringLiteral("Letter"), QStringLiteral("A5"), QStringLiteral("Legal")});
    pdfLayout->addRow(i18n("Page Size:"), m_pageSizeCombo);
    
    m_marginLeftSpin = new QDoubleSpinBox(this);
    m_marginLeftSpin->setRange(0, 100);
    m_marginLeftSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Left Margin:"), m_marginLeftSpin);
    
    m_marginRightSpin = new QDoubleSpinBox(this);
    m_marginRightSpin->setRange(0, 100);
    m_marginRightSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Right Margin:"), m_marginRightSpin);
    
    m_marginTopSpin = new QDoubleSpinBox(this);
    m_marginTopSpin->setRange(0, 100);
    m_marginTopSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Top Margin:"), m_marginTopSpin);
    
    m_marginBottomSpin = new QDoubleSpinBox(this);
    m_marginBottomSpin->setRange(0, 100);
    m_marginBottomSpin->setSuffix(QStringLiteral(" mm"));
    pdfLayout->addRow(i18n("Bottom Margin:"), m_marginBottomSpin);
    
    m_showPageNumbersCheck = new QCheckBox(i18n("Show Page Numbers"), this);
    pdfLayout->addRow(QString(), m_showPageNumbersCheck);
    
    mainLayout->addWidget(pdfGroup);

    // Stylesheet
    auto *styleGroup = new QGroupBox(i18n("Appearance"), this);
    auto *styleLayout = new QFormLayout(styleGroup);
    
    m_stylesheetEdit = new QLineEdit(this);
    m_stylesheetEdit->setPlaceholderText(QStringLiteral("style.css"));
    styleLayout->addRow(i18n("Project Stylesheet:"), m_stylesheetEdit);
    
    mainLayout->addWidget(styleGroup);

    // Buttons
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
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
    
    pm.saveProject();
}
