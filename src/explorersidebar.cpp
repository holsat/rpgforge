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

#include "explorersidebar.h"
#include "projecttreepanel.h"
#include "fileexplorer.h"

#include <KLocalizedString>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

ExplorerSidebar::ExplorerSidebar(ProjectTreePanel *projectTree, FileExplorer *fileExplorer, QWidget *parent)
    : QWidget(parent), m_projectTree(projectTree), m_fileExplorer(fileExplorer)
{
    setupUi();
}

ExplorerSidebar::~ExplorerSidebar() = default;

void ExplorerSidebar::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_projectTree);

    // Bottom pane: header strip with a toggle button, then the file
    // explorer tree. Hiding the file explorer leaves only the thin
    // header visible, so users can always re-open it without going
    // through a menu.
    auto *bottomPane = new QWidget(this);
    auto *bottomLayout = new QVBoxLayout(bottomPane);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    auto *header = new QWidget(bottomPane);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    m_toggleBtn = new QToolButton(header);
    m_toggleBtn->setCheckable(true);
    m_toggleBtn->setAutoRaise(true);
    m_toggleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toggleBtn->setToolTip(i18n("Show or hide the filesystem browser"));
    m_toggleBtn->setIcon(QIcon::fromTheme(QStringLiteral("folder-symbolic"),
        QIcon::fromTheme(QStringLiteral("folder"))));

    auto *titleLabel = new QLabel(i18n("Filesystem"), header);

    headerLayout->addWidget(m_toggleBtn);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    bottomLayout->addWidget(header);
    bottomLayout->addWidget(m_fileExplorer, /*stretch=*/1);

    m_splitter->addWidget(bottomPane);

    // When the file explorer is visible, give it a reasonable share of
    // the vertical space; when hidden, the collapsed bottom pane
    // naturally shrinks to just the header.
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    layout->addWidget(m_splitter);

    connect(m_toggleBtn, &QToolButton::toggled, this, &ExplorerSidebar::setFileExplorerVisible);

    // Restore persisted state. Default is hidden to match the reduced-
    // clutter opening view — most users don't need raw filesystem
    // access until they specifically ask for it.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const bool startVisible = settings.value(
        QStringLiteral("ui/filesystem_browser_visible"), false).toBool();
    m_toggleBtn->setChecked(startVisible);
    setFileExplorerVisible(startVisible);
}

void ExplorerSidebar::setFileExplorerVisible(bool visible)
{
    if (!m_fileExplorer) return;
    m_fileExplorer->setVisible(visible);

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("ui/filesystem_browser_visible"), visible);
}
