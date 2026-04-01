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

#include "newprojectdialog.h"

#include <KLocalizedString>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

NewProjectDialog::NewProjectDialog(const QString &defaultDir, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Create New Project"));
    setupUi();
    m_dirEdit->setText(defaultDir);
}

NewProjectDialog::~NewProjectDialog() = default;

void NewProjectDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(i18n("My Awesome RPG"));
    form->addRow(i18n("Project Name:"), m_nameEdit);

    auto *dirLayout = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    dirLayout->addWidget(m_dirEdit);
    
    auto *browseBtn = new QPushButton(i18n("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &NewProjectDialog::browse);
    dirLayout->addWidget(browseBtn);
    
    form->addRow(i18n("Project Directory:"), dirLayout);

    layout->addLayout(form);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

void NewProjectDialog::browse()
{
    QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Project Directory"), m_dirEdit->text());
    if (!dir.isEmpty()) {
        m_dirEdit->setText(dir);
    }
}

QString NewProjectDialog::projectName() const { return m_nameEdit->text(); }
QString NewProjectDialog::projectDir() const { return m_dirEdit->text(); }
