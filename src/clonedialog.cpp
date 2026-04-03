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

#include "clonedialog.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>

CloneDialog::CloneDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Clone Project from Git"));
    setupUi();
}

CloneDialog::~CloneDialog() = default;

void CloneDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *form = new QVBoxLayout();
    
    form->addWidget(new QLabel(i18n("Git Repository URL:"), this));
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://github.com/user/project.git"));
    form->addWidget(m_urlEdit);

    form->addWidget(new QLabel(i18n("Local Directory:"), this));
    auto *pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setText(QDir::homePath() + QStringLiteral("/RPGProjects"));
    pathLayout->addWidget(m_pathEdit);
    
    auto *browseBtn = new QPushButton(i18n("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Local Directory"), m_pathEdit->text());
        if (!dir.isEmpty()) m_pathEdit->setText(dir);
    });
    pathLayout->addWidget(browseBtn);
    form->addLayout(pathLayout);

    layout->addLayout(form);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    auto *cancelBtn = new QPushButton(i18n("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancelBtn);

    auto *cloneBtn = new QPushButton(i18n("Clone"), this);
    cloneBtn->setDefault(true);
    connect(cloneBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(cloneBtn);

    layout->addLayout(buttons);

    // Auto-suggest folder name from URL
    connect(m_urlEdit, &QLineEdit::textChanged, this, [this](const QString &url) {
        if (url.isEmpty()) return;
        QString name = QFileInfo(url).completeBaseName();
        if (name.endsWith(QLatin1String(".git"))) name.chop(4);
        
        QFileInfo fi(m_pathEdit->text());
        m_pathEdit->setText(fi.absolutePath() + QDir::separator() + name);
    });
}

QString CloneDialog::url() const { return m_urlEdit->text(); }
QString CloneDialog::localPath() const { return m_pathEdit->text(); }
