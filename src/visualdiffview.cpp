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

#include "visualdiffview.h"
#include "gitservice.h"
#include "kompareinterface.h"

#include <KPluginFactory>
#include <KLocalizedString>
#include <KActionCollection>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QMessageBox>

VisualDiffView::VisualDiffView(QWidget *parent)
    : QWidget(parent)
{
    m_tempOld = QDir::tempPath() + QStringLiteral("/rpgforge_diff_old.md");
    m_tempNew = QDir::tempPath() + QStringLiteral("/rpgforge_diff_new.md");
    setupUi();
}

VisualDiffView::~VisualDiffView() = default;

void VisualDiffView::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(5, 5, 5, 5);
    
    auto *saveBtn = new QToolButton(this);
    saveBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    saveBtn->setText(i18n("Save Changes"));
    saveBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(saveBtn, &QToolButton::clicked, this, [this]() {
        if (m_part) {
            m_part->save();
            Q_EMIT saveRequested(m_file2);
        }
    });
    toolbar->addWidget(saveBtn);

    auto *swapBtn = new QToolButton(this);
    swapBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    swapBtn->setText(i18n("Swap Direction"));
    swapBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(swapBtn, &QToolButton::clicked, this, &VisualDiffView::swapDiff);
    toolbar->addWidget(swapBtn);

    toolbar->addStretch();
    
    auto *reloadBtn = new QToolButton(this);
    reloadBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh-symbolic")));
    reloadBtn->setText(i18n("Reload Original"));
    reloadBtn->setToolTip(i18n("Discard applied changes and reload from disk/Git"));
    connect(reloadBtn, &QToolButton::clicked, this, [this]() {
        if (!m_filePath.isEmpty()) {
            setDiff(m_filePath, m_oldHash, m_newHash);
            Q_EMIT reloadRequested(m_filePath);
        } else if (!m_file1.isEmpty()) {
            setFiles(m_file1, m_file2);
        }
    });
    toolbar->addWidget(reloadBtn);

    layout->addLayout(toolbar);

    loadKomparePart();

    if (m_part && m_part->widget()) {
        layout->addWidget(m_part->widget(), 1);
    } else {
        auto *errorLabel = new QLabel(i18n("Could not load Kompare plugin. Ensure 'kompare' is installed."), this);
        errorLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(errorLabel, 1);
    }
}

void VisualDiffView::loadKomparePart()
{
    auto result = KPluginFactory::instantiatePlugin<KParts::ReadWritePart>(
        KPluginMetaData(QStringLiteral("kf6/parts/komparepart")), this);

    if (result) {
        m_part = result.plugin;
        m_kompareInterface = qobject_cast<KompareInterface*>(m_part);
    }
}

void VisualDiffView::setDiff(const QString &filePath, const QString &oldHash, const QString &newHash)
{
    m_filePath = filePath;
    m_oldHash = oldHash;
    m_newHash = newHash;
    m_file1.clear();
    m_file2.clear();

    if (!m_kompareInterface) return;

    auto oldFuture = GitService::instance().extractVersion(filePath, oldHash, m_tempOld);
    
    oldFuture.then(this, [this, filePath, newHash](bool success) {
        if (!success) return;

        if (newHash.isEmpty()) {
            // Compare with working copy (which is editable)
            // We copy the working copy to m_tempNew so kompare doesn't accidentally
            // save halfway directly into the project without us intercepting it
            if (QFile::exists(m_tempNew)) QFile::remove(m_tempNew);
            QFile::copy(filePath, m_tempNew);
            QFile(m_tempNew).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

            m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
        } else {
            GitService::instance().extractVersion(filePath, newHash, m_tempNew).then(this, [this](bool success) {
                if (success) {
                    m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
                }
            });
        }
    });
}

void VisualDiffView::setFiles(const QString &file1, const QString &file2)
{
    m_file1 = file1;
    m_file2 = file2;
    m_filePath.clear();
    
    if (m_kompareInterface) {
        if (QFile::exists(m_tempOld)) QFile::remove(m_tempOld);
        if (QFile::exists(m_tempNew)) QFile::remove(m_tempNew);
        QFile::copy(file1, m_tempOld);
        QFile::copy(file2, m_tempNew);
        QFile(m_tempOld).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QFile(m_tempNew).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
    }
}

void VisualDiffView::swapDiff()
{
    m_swapped = !m_swapped;
    
    if (!m_filePath.isEmpty()) {
        QString oldH = m_oldHash;
        m_oldHash = m_newHash;
        m_newHash = oldH;
        setDiff(m_filePath, m_oldHash, m_newHash);
    } else {
        QString f1 = m_file1;
        m_file1 = m_file2;
        m_file2 = f1;
        setFiles(m_file1, m_file2);
    }
}

void VisualDiffView::saveChanges()
{
    if (!m_part) return;
    
    // Trigger Kompare's own save functionality first (saves applied hunks to m_tempNew)
    m_part->save();

    QString targetPath;
    if (!m_filePath.isEmpty()) {
        if (m_newHash.isEmpty()) {
            targetPath = m_filePath;
        } else {
            // Can't overwrite a commit in git, but maybe we let them save somewhere?
            // Actually, if m_newHash is not empty, it's history vs history, so read-only
            QMessageBox::information(this, i18n("Read Only"), i18n("Cannot save changes when comparing two historical commits."));
            return;
        }
    } else {
        targetPath = m_file2;
    }

    if (!targetPath.isEmpty()) {
        if (QFile::exists(targetPath)) QFile::remove(targetPath);
        QFile::copy(m_tempNew, targetPath);
        Q_EMIT saveRequested(targetPath);
    }
}
