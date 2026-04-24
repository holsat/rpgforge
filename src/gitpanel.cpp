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

#include "gitpanel.h"
#include "gitservice.h"
#include "projectmanager.h"

#include <KLocalizedString>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFont>

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    // Refresh whenever a project opens or the user commits
    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, &GitPanel::refresh);
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, [this]() {
        m_branchLabel->setText(i18n("No project open"));
        m_changesLabel->clear();
        m_lastCommitLabel->clear();
        m_commitBtn->setEnabled(false);
        m_pushBtn->setEnabled(false);
    });
}

GitPanel::~GitPanel() = default;

void GitPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_branchLabel = new QLabel(i18n("No project open"), this);
    QFont bold = m_branchLabel->font();
    bold.setBold(true);
    m_branchLabel->setFont(bold);
    m_branchLabel->setWordWrap(true);
    layout->addWidget(m_branchLabel);

    m_changesLabel = new QLabel(this);
    m_changesLabel->setWordWrap(true);
    layout->addWidget(m_changesLabel);

    m_lastCommitLabel = new QLabel(this);
    m_lastCommitLabel->setWordWrap(true);
    m_lastCommitLabel->setEnabled(false); // Grey out — informational only
    layout->addWidget(m_lastCommitLabel);

    layout->addStretch();

    m_commitBtn = new QPushButton(i18n("Commit All Changes"), this);
    m_commitBtn->setEnabled(false);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::onCommitAll);
    layout->addWidget(m_commitBtn);

    m_pushBtn = new QPushButton(i18n("Push to Remote"), this);
    m_pushBtn->setEnabled(false);
    connect(m_pushBtn, &QPushButton::clicked, this, &GitPanel::onPush);
    layout->addWidget(m_pushBtn);
}

void GitPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    refresh();
}

void GitPanel::refresh()
{
    if (m_rootPath.isEmpty()) {
        if (ProjectManager::instance().isProjectOpen()) {
            m_rootPath = ProjectManager::instance().projectPath();
        } else {
            return;
        }
    }

    if (!GitService::instance().isRepo(m_rootPath)) {
        m_branchLabel->setText(i18n("Not a git repository"));
        m_changesLabel->clear();
        m_lastCommitLabel->clear();
        m_commitBtn->setEnabled(false);
        m_pushBtn->setEnabled(false);
        return;
    }

    const QString branch = GitService::instance().currentBranch(m_rootPath);
    m_branchLabel->setText(i18n("Branch: %1", branch.isEmpty() ? i18n("(detached)") : branch));

    const bool dirty = GitService::instance().hasUncommittedChanges(m_rootPath);
    m_changesLabel->setText(dirty ? i18n("Uncommitted changes present") : i18n("Working tree clean"));
    m_commitBtn->setEnabled(dirty);
    m_pushBtn->setEnabled(true);

    // Show last commit message from history
    GitService::instance().getHistory(m_rootPath + QStringLiteral("/.git"))
        .then(this, [this](const QList<VersionInfo> &history) {
        if (!history.isEmpty()) {
            m_lastCommitLabel->setText(i18n("Last: %1", history.first().message));
        }
    });
}

void GitPanel::onCommitAll()
{
    if (m_rootPath.isEmpty()) return;
    m_commitBtn->setEnabled(false);
    GitService::instance().commitAll(m_rootPath, QStringLiteral("Manual commit from RPG Forge"))
        .then(this, [this](bool) {
        refresh();
    });
}

void GitPanel::onPush()
{
    if (m_rootPath.isEmpty()) return;
    m_pushBtn->setEnabled(false);
    GitService::instance().push(m_rootPath)
        .then(this, [this](bool) {
        refresh();
    });
}
