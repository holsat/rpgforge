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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QRect>
#include <QSet>
#include <QTimer>
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

    // Cache the last reconciliationRequired payload so scripted tests can
    // read it via pendingReconciliation() without racing the UI dialog.
    QObject::connect(&ProjectManager::instance(),
                     &ProjectManager::reconciliationRequired,
                     this,
                     [this](const QList<ReconciliationEntry> &entries) {
        m_pendingReconciliation = entries;
    });
    QObject::connect(&ProjectManager::instance(),
                     &ProjectManager::projectClosed,
                     this,
                     [this]() { m_pendingReconciliation.clear(); });
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
    const QJsonObject root = ProjectManager::instance().treeData();
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
    const QJsonObject root = ProjectManager::instance().treeData();
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

// -------- Tree snapshot introspection (Phase 2) --------

namespace {
QVariantMap snapshotToVariant(const TreeNodeSnapshot &node)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), node.name);
    map.insert(QStringLiteral("path"), node.path);
    map.insert(QStringLiteral("synopsis"), node.synopsis);
    map.insert(QStringLiteral("status"), node.status);
    map.insert(QStringLiteral("type"), node.type);
    map.insert(QStringLiteral("category"), node.category);
    map.insert(QStringLiteral("diskPresent"), node.diskPresent);
    map.insert(QStringLiteral("isTransient"), node.isTransient);

    QVariantList kids;
    kids.reserve(node.children.size());
    for (const auto &child : node.children) {
        kids.append(snapshotToVariant(child));
    }
    map.insert(QStringLiteral("children"), kids);
    return map;
}
} // namespace

QVariantMap RpgForgeDBus::treeSnapshotJson()
{
    if (!projectOpen()) {
        return {};
    }
    return snapshotToVariant(ProjectManager::instance().treeSnapshot());
}

QVariantMap RpgForgeDBus::folderSnapshotJson(const QString &path)
{
    if (!projectOpen()) {
        return {};
    }
    const auto snap = ProjectManager::instance().folderSnapshot(path);
    if (!snap) return {};
    return snapshotToVariant(*snap);
}

bool RpgForgeDBus::pathExists(const QString &path)
{
    if (!projectOpen() || path.isEmpty()) {
        return false;
    }
    return ProjectManager::instance().pathExists(path);
}

// -------- Tree mutations (Phase 3) --------

bool RpgForgeDBus::addFolderAt(const QString &name, const QString &relPath, const QString &parentPath)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().addFolder(name, relPath, parentPath);
}

bool RpgForgeDBus::addFileAt(const QString &name, const QString &relPath, const QString &parentPath)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().addFile(name, relPath, parentPath);
}

bool RpgForgeDBus::renameItemAt(const QString &path, const QString &newName)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().renameItem(path, newName);
}

bool RpgForgeDBus::moveItemTo(const QString &path, const QString &newParentPath, int row)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().moveItem(path, newParentPath, row);
}

bool RpgForgeDBus::removeItemAt(const QString &path)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().removeItem(path);
}

// -------- Reconciliation (Phase 4) --------

QVariantList RpgForgeDBus::pendingReconciliation()
{
    QVariantList out;
    out.reserve(m_pendingReconciliation.size());
    for (const ReconciliationEntry &e : m_pendingReconciliation) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), e.path);
        m.insert(QStringLiteral("displayName"), e.displayName);
        m.insert(QStringLiteral("category"), e.category);
        m.insert(QStringLiteral("type"), e.type);
        m.insert(QStringLiteral("action"), static_cast<int>(e.action));
        m.insert(QStringLiteral("resolvedPath"), e.resolvedPath);
        m.insert(QStringLiteral("suggestedPath"), e.suggestedPath);
        out.append(m);
    }
    return out;
}

bool RpgForgeDBus::applyReconciliationLocate(const QString &oldPath, const QString &newPath)
{
    if (!projectOpen()) return false;
    if (oldPath.isEmpty() || newPath.isEmpty()) return false;
    if (oldPath == newPath) return true; // no-op

    ProjectManager &pm = ProjectManager::instance();
    const QString oldParent = QFileInfo(oldPath).path();
    const QString newParent = QFileInfo(newPath).path();
    const QString newBasename = QFileInfo(newPath).fileName();

    if (oldParent == newParent) {
        return pm.renameItem(oldPath, newBasename);
    }

    const QString sourceBasename = QFileInfo(oldPath).fileName();
    if (!pm.moveItem(oldPath, newParent)) return false;
    if (sourceBasename == newBasename) return true;

    const QString movedPath = newParent.isEmpty()
        ? sourceBasename
        : (newParent + QLatin1Char('/') + sourceBasename);
    return pm.renameItem(movedPath, newBasename);
}

bool RpgForgeDBus::applyReconciliationRemove(const QString &oldPath)
{
    if (!projectOpen()) return false;
    return ProjectManager::instance().removeItem(oldPath);
}

bool RpgForgeDBus::applyReconciliationRecreate(const QString &oldPath)
{
    if (!projectOpen()) return false;
    const QString absPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(oldPath);
    QFileInfo fi(absPath);
    if (fi.exists()) return true;
    if (!QDir().mkpath(fi.absolutePath())) return false;
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();
    return true;
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

// -------- External-change + filesystem watcher (Phase 5) --------

bool RpgForgeDBus::reloadFromDisk()
{
    return ProjectManager::instance().reloadFromDisk();
}

bool RpgForgeDBus::beginExternalChange()
{
    ProjectManager::instance().beginExternalChange();
    return true;
}

bool RpgForgeDBus::endExternalChange()
{
    ProjectManager::instance().endExternalChange();
    return true;
}

// -------- Disk authority (Phase 6) --------

QStringList RpgForgeDBus::diskSnapshot()
{
    // Walk the project directory using the same policy as
    // ProjectTreeModel::buildFromDisk so the returned list matches what
    // a tree rebuild would produce. Keeping the logic in-place (instead
    // of invoking the model) means tests can assert the invariant without
    // touching the live tree.
    if (!projectOpen()) return {};

    const QString root = projectRoot();
    if (root.isEmpty() || !QDir(root).exists()) return {};

    auto shouldSkip = [](const QString &name) {
        if (name.isEmpty()) return true;
        if (name == QLatin1String(".") || name == QLatin1String("..")) return true;
        if (name.startsWith(QLatin1Char('.'))) return true;
        static const QStringList kExplicitSkips = {
            QStringLiteral("rpgforge.project"),
            QStringLiteral("node_modules"),
            QStringLiteral("build"),
            QStringLiteral("__pycache__"),
            QStringLiteral("Thumbs.db"),
        };
        return kExplicitSkips.contains(name);
    };

    QStringList out;
    std::function<void(const QDir &, const QString &)> walk;
    walk = [&](const QDir &dir, const QString &parentRel) {
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fi : entries) {
            const QString name = fi.fileName();
            if (shouldSkip(name)) continue;
            const QString rel = parentRel.isEmpty()
                ? name : parentRel + QLatin1Char('/') + name;
            out.append(rel);
            if (fi.isDir()) walk(QDir(fi.absoluteFilePath()), rel);
        }
    };
    walk(QDir(root), QString());
    return out;
}

bool RpgForgeDBus::waitForFsQuiescence(int timeoutMs)
{
    // Spin a local event loop until the debounce timer becomes inactive.
    // Use ProjectManager's public accessors where possible; we need to peek
    // into the private debounce timer, so we find it by objectName-less
    // QTimer introspection via children(). Simpler: poll via invokeMethod
    // on a short tick until the hard timeout elapses.
    QElapsedTimer elapsed;
    elapsed.start();

    while (elapsed.elapsed() < timeoutMs) {
        // Find a QTimer child of ProjectManager whose single-shot + 250ms
        // interval identifies it as the debounce timer. There's only one
        // such timer (m_fsDebounce) in the current design.
        bool debounceActive = false;
        const auto timers = ProjectManager::instance().findChildren<QTimer*>();
        for (QTimer *t : timers) {
            if (t->isSingleShot() && t->interval() == 250 && t->isActive()) {
                debounceActive = true;
                break;
            }
        }
        if (!debounceActive) return true;

        // Process events so queued slot invocations (e.g. the debounce
        // firing) can run; then yield briefly.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return false;
}
