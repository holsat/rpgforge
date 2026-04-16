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

#include "rpgforgedbus.h"

#include "gitservice.h"
#include "mainwindow.h"
#include "projectmanager.h"
#include "sidebar.h"

#include <KTextEditor/Document>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>

RpgForgeDBus::RpgForgeDBus(MainWindow *window)
    : QDBusAbstractAdaptor(window)
    , m_window(window)
{
    setAutoRelaySignals(false);
}

// -------- Application lifecycle --------

QString RpgForgeDBus::version() const
{
    return QCoreApplication::applicationVersion();
}

bool RpgForgeDBus::isProjectOpen() const
{
    return ProjectManager::instance().isProjectOpen();
}

QString RpgForgeDBus::currentProjectPath() const
{
    if (!ProjectManager::instance().isProjectOpen()) {
        return QString();
    }
    return ProjectManager::instance().projectPath();
}

bool RpgForgeDBus::openProject(const QString &path)
{
    if (path.isEmpty() || !m_window) {
        return false;
    }
    if (!QFileInfo(path).isFile()) {
        return false;
    }
    if (!ProjectManager::instance().openProject(path)) {
        return false;
    }
    // Re-run the window-side refresh that the normal openProject() action does.
    // We cannot call MainWindow::openProject() because it shows a file dialog,
    // so we replicate the post-open updates here via the public openFileFromUrl
    // path — instead, we just trigger the project-opened signal consumers that
    // ProjectManager already emitted, and ensure the file explorer is pointed
    // at the new path via the public accessor chain. ProjectManager emits
    // projectOpened() from openProject(), so panels that subscribed to that
    // signal (ProjectTreePanel, GitPanel, ExplorationsPanel) will have
    // updated themselves automatically.
    return true;
}

bool RpgForgeDBus::closeProject()
{
    if (!ProjectManager::instance().isProjectOpen()) {
        return false;
    }
    ProjectManager::instance().closeProject();
    return true;
}

void RpgForgeDBus::quit()
{
    if (qApp) {
        qApp->quit();
    }
}

// -------- Sidebar --------

QStringList RpgForgeDBus::sidebarPanels() const
{
    if (!m_window || !m_window->sidebar()) {
        return {};
    }
    return m_window->sidebar()->panelNames();
}

QString RpgForgeDBus::activeSidebarPanel() const
{
    if (!m_window || !m_window->sidebar()) {
        return QString();
    }
    const int id = m_window->sidebar()->currentPanel();
    if (id < 0) {
        return QString();
    }
    return m_window->sidebar()->panelName(id);
}

bool RpgForgeDBus::showSidebarPanel(const QString &name)
{
    if (!m_window || !m_window->sidebar() || name.isEmpty()) {
        return false;
    }
    const int id = m_window->sidebar()->panelIdFromName(name);
    if (id < 0) {
        return false;
    }
    m_window->sidebar()->showPanel(id);
    return true;
}

// -------- Editor --------

QString RpgForgeDBus::currentEditorFilePath() const
{
    if (!m_window) {
        return QString();
    }
    auto *doc = m_window->currentDocument();
    if (!doc) {
        return QString();
    }
    const QUrl url = doc->url();
    if (!url.isLocalFile()) {
        return QString();
    }
    return url.toLocalFile();
}

QString RpgForgeDBus::currentCentralView() const
{
    if (!m_window) {
        return QString();
    }
    return m_window->currentViewId();
}

bool RpgForgeDBus::openFile(const QString &absolutePath)
{
    if (!m_window || absolutePath.isEmpty()) {
        return false;
    }
    const QFileInfo fi(absolutePath);
    if (!fi.isAbsolute() || !fi.isFile()) {
        return false;
    }
    m_window->openFileFromUrl(QUrl::fromLocalFile(absolutePath));
    return true;
}

// -------- Project tree --------

static void collectFilesFromTree(const QJsonObject &node, QStringList &out)
{
    const int type = node.value(QStringLiteral("type")).toInt();
    if (type == 1) { // File
        const QString rel = node.value(QStringLiteral("path")).toString();
        if (!rel.isEmpty()) {
            out.append(rel);
        }
    }
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const auto &child : children) {
        collectFilesFromTree(child.toObject(), out);
    }
}

QStringList RpgForgeDBus::projectFiles() const
{
    if (!ProjectManager::instance().isProjectOpen()) {
        return {};
    }
    const QJsonObject root = ProjectManager::instance().tree();
    QStringList paths;
    collectFilesFromTree(root, paths);
    return paths;
}

// -------- Explorations --------

QStringList RpgForgeDBus::explorationNames() const
{
    if (!ProjectManager::instance().isProjectOpen()) {
        return {};
    }
    return GitService::instance().listBranches(ProjectManager::instance().projectPath());
}

QString RpgForgeDBus::currentExploration() const
{
    if (!ProjectManager::instance().isProjectOpen()) {
        return QString();
    }
    return GitService::instance().currentBranch(ProjectManager::instance().projectPath());
}

bool RpgForgeDBus::createExploration(const QString &name)
{
    if (name.isEmpty() || !ProjectManager::instance().isProjectOpen()) {
        return false;
    }
    return GitService::instance().createExploration(
        ProjectManager::instance().projectPath(), name);
}

bool RpgForgeDBus::switchExploration(const QString &name)
{
    if (name.isEmpty() || !ProjectManager::instance().isProjectOpen()) {
        return false;
    }
    // Blocks briefly; acceptable for a test-facing API.
    auto future = GitService::instance().switchExploration(
        ProjectManager::instance().projectPath(), name);
    return future.result();
}

QVariantList RpgForgeDBus::parkedChanges() const
{
    QVariantList out;
    if (!ProjectManager::instance().isProjectOpen()) {
        return out;
    }
    const QList<StashEntry> entries =
        GitService::instance().listStashes(ProjectManager::instance().projectPath());
    out.reserve(entries.size());
    for (const StashEntry &entry : entries) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), entry.index);
        m.insert(QStringLiteral("message"), entry.message);
        m.insert(QStringLiteral("onBranch"), entry.onBranch);
        m.insert(QStringLiteral("date"), entry.date.toString(Qt::ISODate));
        out.append(m);
    }
    return out;
}
