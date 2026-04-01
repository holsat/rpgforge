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

#include "compiledialog.h"
#include "projectmanager.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>

CompileDialog::CompileDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Compile Project Settings"));
    setupUi();
}

CompileDialog::~CompileDialog() = default;

void CompileDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    // Status Filter
    m_filterCheck = new QCheckBox(i18n("Only include files of this status or above:"), this);
    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItems({
        i18n("Draft"),
        i18n("Work in Progress"),
        i18n("In Review"),
        i18n("Final")
    });
    m_statusCombo->setEnabled(false);
    connect(m_filterCheck, &QCheckBox::toggled, m_statusCombo, &QComboBox::setEnabled);
    
    form->addRow(m_filterCheck);
    form->addRow(i18n("Minimum Status:"), m_statusCombo);

    // Other options
    m_stylesheetCheck = new QCheckBox(i18n("Apply project stylesheet"), this);
    m_stylesheetCheck->setChecked(true);
    form->addRow(m_stylesheetCheck);

    m_tocCheck = new QCheckBox(i18n("Create table of contents"), this);
    form->addRow(m_tocCheck);

    m_pageNumbersCheck = new QCheckBox(i18n("Automatically add page numbers"), this);
    m_pageNumbersCheck->setChecked(ProjectManager::instance().showPageNumbers());
    form->addRow(m_pageNumbersCheck);

    layout->addLayout(form);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

CompileOptions CompileDialog::options() const
{
    CompileOptions opts;
    opts.filterByStatus = m_filterCheck->isChecked();
    opts.minStatus = m_statusCombo->currentText();
    opts.applyStylesheet = m_stylesheetCheck->isChecked();
    opts.createTOC = m_tocCheck->isChecked();
    opts.showPageNumbers = m_pageNumbersCheck->isChecked();
    return opts;
}
