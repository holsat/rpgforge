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

#include "metadatadialog.h"

#include <KLocalizedString>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>

MetadataDialog::MetadataDialog(const QString &title, const QString &status, const QString &synopsis, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Edit Metadata"));
    setupUi();
    
    m_titleEdit->setText(title);
    
    // Set current status in combo
    int idx = m_statusCombo->findText(status);
    if (idx != -1) {
        m_statusCombo->setCurrentIndex(idx);
    } else if (!status.isEmpty()) {
        // Handle existing custom status if any
        m_statusCombo->addItem(status);
        m_statusCombo->setCurrentText(status);
    }
    
    m_synopsisEdit->setPlainText(synopsis);
}

MetadataDialog::~MetadataDialog() = default;

void MetadataDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_titleEdit = new QLineEdit(this);
    form->addRow(i18n("Title:"), m_titleEdit);

    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItems({
        i18n("Draft"),
        i18n("Work in Progress"),
        i18n("In Review"),
        i18n("Final")
    });
    form->addRow(i18n("Status:"), m_statusCombo);

    m_synopsisEdit = new QTextEdit(this);
    m_synopsisEdit->setAcceptRichText(false);
    form->addRow(i18n("Synopsis:"), m_synopsisEdit);

    layout->addLayout(form);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

QString MetadataDialog::title() const { return m_titleEdit->text(); }
QString MetadataDialog::status() const { return m_statusCombo->currentText(); }
QString MetadataDialog::synopsis() const { return m_synopsisEdit->toPlainText(); }
