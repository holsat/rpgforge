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
#include "projecttreemodel.h"

#include "gitservice.h"
#include "mainwindow.h"
#include "projectmanager.h"
#include "sidebar.h"

#include <KTextEditor/Document>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QRect>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

// Helper: is a project currently loaded?
namespace {
bool projectOpen()
{
    return ProjectManager::instance().isProjectOpen();
}
QString projectRoot()
{
    return ProjectManager::instance().projectPath();
}
} // namespace

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
    return projectOpen();
}

QString RpgForgeDBus::currentProjectPath() const
{
    if (!projectOpen()) {
        return QString();
    }
    return projectRoot();
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
    // ProjectManager emits projectOpened() from openProject(), so panels that
    // subscribed to that signal (ProjectTreePanel, GitPanel, ExplorationsPanel)
    // update themselves automatically.
    return true;
}

bool RpgForgeDBus::closeProject()
{
    if (!projectOpen()) {
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

// -------- UI state queries --------

bool RpgForgeDBus::mainWindowVisible() const
{
    return m_window && m_window->isVisible();
}

QList<int> RpgForgeDBus::mainWindowGeometry() const
{
    if (!m_window) {
        return QList<int>{0, 0, 0, 0};
    }
    const QRect g = m_window->geometry();
    return QList<int>{g.x(), g.y(), g.width(), g.height()};
}

bool RpgForgeDBus::currentDocumentModified() const
{
    if (!m_window) {
        return false;
    }
    auto *doc = m_window->currentDocument();
    if (!doc) {
        return false;
    }
    return doc->isModified();
}

bool RpgForgeDBus::autoSyncEnabled() const
{
    return ProjectManager::instance().autoSync();
}

bool RpgForgeDBus::setAutoSyncEnabled(bool enabled)
{
    ProjectManager::instance().setAutoSync(enabled);
    return ProjectManager::instance().autoSync();
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

namespace {
// ProjectTreeModel serializes node type as either a string ("file"/"folder")
// or, in legacy data, as an integer (1 == file, 0 == folder). Accept both.
bool nodeIsFile(const QJsonObject &node)
{
    const QJsonValue v = node.value(QStringLiteral("type"));
    if (v.isString()) return v.toString() == QStringLiteral("file");
    return v.toInt() == 1;
}

bool nodeIsFolder(const QJsonObject &node)
{
    const QJsonValue v = node.value(QStringLiteral("type"));
    if (v.isString()) return v.toString() == QStringLiteral("folder");
    return v.toInt() == 0;
}

void collectFilesFromTree(const QJsonObject &node, QStringList &out)
{
    if (nodeIsFile(node)) {
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

void collectFoldersFromTree(const QJsonObject &node, QStringList &out, bool isRoot)
{
    if (!isRoot && nodeIsFolder(node)) {
        const QString rel = node.value(QStringLiteral("path")).toString();
        if (!rel.isEmpty()) {
            out.append(rel);
        }
    }
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const auto &child : children) {
        collectFoldersFromTree(child.toObject(), out, false);
    }
}
} // namespace

QStringList RpgForgeDBus::projectFiles() const
{
    if (!projectOpen()) {
        return {};
    }
    const QJsonObject root = ProjectManager::instance().model()->projectData();
    QStringList paths;
    collectFilesFromTree(root, paths);
    return paths;
}

QStringList RpgForgeDBus::projectFilesAbsolute() const
{
    if (!projectOpen()) {
        return {};
    }
    const QString root = projectRoot();
    const QStringList rel = projectFiles();
    QStringList abs;
    abs.reserve(rel.size());
    for (const QString &r : rel) {
        abs.append(QDir(root).absoluteFilePath(r));
    }
    return abs;
}

QStringList RpgForgeDBus::projectFolders() const
{
    if (!projectOpen()) {
        return {};
    }
    const QJsonObject root = ProjectManager::instance().model()->projectData();
    QStringList folders;
    collectFoldersFromTree(root, folders, true);
    return folders;
}

bool RpgForgeDBus::projectContains(const QString &relativePath) const
{
    if (!projectOpen() || relativePath.isEmpty()) {
        return false;
    }
    return projectFiles().contains(relativePath)
        || projectFolders().contains(relativePath);
}

// -------- Explorations / Git queries --------

QStringList RpgForgeDBus::explorationNames() const
{
    if (!projectOpen()) {
        return {};
    }
    return GitService::instance().listBranches(projectRoot());
}

QString RpgForgeDBus::currentExploration() const
{
    if (!projectOpen()) {
        return QString();
    }
    return GitService::instance().currentBranch(projectRoot());
}

bool RpgForgeDBus::hasUncommittedChanges() const
{
    if (!projectOpen()) {
        return false;
    }
    return GitService::instance().hasUncommittedChanges(projectRoot());
}

QVariantList RpgForgeDBus::graphNodes() const
{
    QVariantList out;
    if (!projectOpen()) {
        return out;
    }
    // Blocks briefly — acceptable for a test-facing API.
    const QList<ExplorationNode> nodes =
        GitService::instance().getExplorationGraph(projectRoot()).result();
    out.reserve(nodes.size());
    for (const ExplorationNode &n : nodes) {
        QVariantMap m;
        m.insert(QStringLiteral("hash"), n.hash);
        m.insert(QStringLiteral("branchName"), n.branchName);
        m.insert(QStringLiteral("message"), n.message);
        m.insert(QStringLiteral("date"), n.date.toString(Qt::ISODate));
        m.insert(QStringLiteral("tags"), n.tags);
        m.insert(QStringLiteral("wordCount"), n.wordCount);
        m.insert(QStringLiteral("wordCountDelta"), n.wordCountDelta);
        m.insert(QStringLiteral("primaryParentHash"), n.primaryParentHash);
        m.insert(QStringLiteral("mergeParentHash"), n.mergeParentHash);
        out.append(m);
    }
    return out;
}

QVariantList RpgForgeDBus::recentCommits(int limit) const
{
    QVariantList out;
    if (!projectOpen() || limit <= 0) {
        return out;
    }
    // GitService::getHistory works per-file; for "recent commits on the current
    // branch" we drive it off the exploration graph filtered to the current
    // branch. Graph is already ordered newest-first within each lane.
    const QList<ExplorationNode> nodes =
        GitService::instance().getExplorationGraph(projectRoot()).result();
    const QString current = GitService::instance().currentBranch(projectRoot());

    int count = 0;
    for (const ExplorationNode &n : nodes) {
        if (count >= limit) break;
        if (!current.isEmpty() && n.branchName != current) continue;
        QVariantMap m;
        m.insert(QStringLiteral("hash"), n.hash);
        m.insert(QStringLiteral("message"), n.message);
        m.insert(QStringLiteral("date"), n.date.toString(Qt::ISODate));
        out.append(m);
        ++count;
    }
    return out;
}

QStringList RpgForgeDBus::landmarkNames() const
{
    if (!projectOpen()) {
        return {};
    }
    const QList<ExplorationNode> nodes =
        GitService::instance().getExplorationGraph(projectRoot()).result();
    QSet<QString> seen;
    for (const ExplorationNode &n : nodes) {
        for (const QString &tag : n.tags) {
            if (!tag.isEmpty()) seen.insert(tag);
        }
    }
    QStringList out(seen.begin(), seen.end());
    out.sort();
    return out;
}

QVariantList RpgForgeDBus::parkedChanges() const
{
    QVariantList out;
    if (!projectOpen()) {
        return out;
    }
    const QList<StashEntry> entries =
        GitService::instance().listStashes(projectRoot());
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

// -------- Explorations / Git actions --------

bool RpgForgeDBus::createExploration(const QString &name)
{
    if (name.isEmpty() || !projectOpen()) {
        return false;
    }
    return GitService::instance().createExploration(projectRoot(), name);
}

bool RpgForgeDBus::switchExploration(const QString &name)
{
    if (name.isEmpty() || !projectOpen()) {
        return false;
    }
    // Blocks briefly; acceptable for a test-facing API.
    auto future = GitService::instance().switchExploration(projectRoot(), name);
    return future.result();
}

bool RpgForgeDBus::saveAll()
{
    if (!m_window) return false;
    return m_window->saveAllDocuments();
}

bool RpgForgeDBus::commitAll(const QString &message)
{
    if (!projectOpen() || message.isEmpty()) {
        return false;
    }
    return GitService::instance().commitAll(projectRoot(), message).result();
}

bool RpgForgeDBus::parkChanges(const QString &message)
{
    if (!projectOpen() || message.isEmpty()) {
        return false;
    }
    return GitService::instance().stashChanges(projectRoot(), message).result();
}

bool RpgForgeDBus::restoreParkedChanges(int stashIndex)
{
    if (!projectOpen() || stashIndex < 0) {
        return false;
    }
    return GitService::instance().applyStash(projectRoot(), stashIndex).result();
}

bool RpgForgeDBus::discardParkedChanges(int stashIndex)
{
    if (!projectOpen() || stashIndex < 0) {
        return false;
    }
    return GitService::instance().dropStash(projectRoot(), stashIndex).result();
}

bool RpgForgeDBus::integrateExploration(const QString &sourceBranch)
{
    if (!projectOpen() || sourceBranch.isEmpty()) {
        return false;
    }
    return GitService::instance().integrateExploration(projectRoot(), sourceBranch).result();
}

QStringList RpgForgeDBus::conflictingFiles() const
{
    if (!projectOpen()) {
        return {};
    }
    const QList<ConflictFile> conflicts =
        GitService::instance().getConflictingFiles(projectRoot()).result();
    QStringList out;
    out.reserve(conflicts.size());
    for (const ConflictFile &c : conflicts) {
        out.append(c.path);
    }
    return out;
}

bool RpgForgeDBus::createLandmark(const QString &commitHash,
                                  const QString &landmarkName)
{
    if (!projectOpen() || commitHash.isEmpty() || landmarkName.isEmpty()) {
        return false;
    }
    return GitService::instance().createTag(projectRoot(), landmarkName, commitHash);
}

bool RpgForgeDBus::recallVersion(const QString &filePath, const QString &commitHash)
{
    if (!m_window || !projectOpen() || filePath.isEmpty() || commitHash.isEmpty()) {
        return false;
    }
    m_window->invokeVersionRecall(filePath, commitHash);
    // The recall pipeline is asynchronous (commit -> extract -> install);
    // we return true to indicate the request was dispatched. Callers that
    // need to observe completion should poll the file content or the
    // exploration graph.
    return true;
}

// -------- Conflict state --------

QString RpgForgeDBus::activeConflictFile() const
{
    if (!m_window) return QString();
    return m_window->activeConflictFile();
}

QList<int> RpgForgeDBus::conflictProgress() const
{
    if (!m_window) return QList<int>{0, 0};
    return m_window->conflictProgress();
}

// -------- Dialog introspection + interaction --------

namespace {
QList<QDialog*> visibleDialogs()
{
    QList<QDialog*> out;
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (!w || !w->isVisible()) continue;
        if (auto *d = qobject_cast<QDialog*>(w)) {
            out.append(d);
        }
    }
    return out;
}

QDialog* findDialogByTitle(const QString &windowTitle)
{
    for (QDialog *d : visibleDialogs()) {
        if (d->windowTitle() == windowTitle) {
            return d;
        }
    }
    return nullptr;
}
} // namespace

QStringList RpgForgeDBus::openDialogTitles() const
{
    QStringList titles;
    const QList<QDialog*> dialogs = visibleDialogs();
    titles.reserve(dialogs.size());
    for (QDialog *d : dialogs) {
        titles.append(d->windowTitle());
    }
    return titles;
}

bool RpgForgeDBus::acceptDialog(const QString &windowTitle)
{
    QDialog *d = findDialogByTitle(windowTitle);
    if (!d) return false;
    d->accept();
    return true;
}

bool RpgForgeDBus::rejectDialog(const QString &windowTitle)
{
    QDialog *d = findDialogByTitle(windowTitle);
    if (!d) return false;
    d->reject();
    return true;
}

bool RpgForgeDBus::fillDialogLineEdit(const QString &windowTitle,
                                      const QString &objectName,
                                      const QString &value)
{
    QDialog *d = findDialogByTitle(windowTitle);
    if (!d || objectName.isEmpty()) return false;
    auto *edit = d->findChild<QLineEdit*>(objectName);
    if (!edit) return false;
    edit->setText(value);
    return true;
}
