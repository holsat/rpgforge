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

#include "projectmanager.h"
#include "projectkeys.h"
#include "projecttreemodel.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>
#include <QMetaType>
#include <QDebug>
#include <QThread>
#include <KLocalizedString>
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

// ---------------------------------------------------------------------------
// SelfMutationScope: RAII guard bracketed around PM's own disk+tree mutations.
// Covers two cases:
//   (a) during the scope, the depth-gated watcher handler drops any events
//       that fire synchronously while we're still inside the mutation;
//   (b) after the scope ends, the m_selfQuietUntilMs cutoff tells the
//       debounce handler to ignore events whose ms-since-epoch arrival
//       falls within a short post-mutation window. fsnotify delivers
//       kernel events asynchronously, so (b) catches the late arrivals
//       that (a) would otherwise miss.
// The quiet window is generous relative to the 250 ms debounce so that
// events dispatched after the mutation still get swallowed.
// ---------------------------------------------------------------------------
SelfMutationScope::SelfMutationScope(ProjectManager *pm) : m_pm(pm)
{
    ++m_pm->m_selfMutationDepth;
}

SelfMutationScope::~SelfMutationScope()
{
    --m_pm->m_selfMutationDepth;
    // 1 s window — covers the 250 ms debounce plus plenty of margin for
    // fsnotify delivery jitter. Use QDateTime::currentMSecsSinceEpoch()
    // so the cutoff is monotonic wrt the processFsChanges check below.
    const qint64 kQuietWindowMs = 1000;
    const qint64 target = QDateTime::currentMSecsSinceEpoch() + kQuietWindowMs;
    if (target > m_pm->m_selfQuietUntilMs) {
        m_pm->m_selfQuietUntilMs = target;
    }
}

ProjectManager& ProjectManager::instance()
{
    static ProjectManager s_instance;
    return s_instance;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
    // Ensure queued connections and QVariant use across the
    // reconciliationRequired signal work out of the box.
    qRegisterMetaType<ReconciliationEntry>("ReconciliationEntry");
    qRegisterMetaType<QList<ReconciliationEntry>>("QList<ReconciliationEntry>");

    m_treeModel = new ProjectTreeModel(this);
    m_treeUpdateTimer = new QTimer(this);
    m_treeUpdateTimer->setSingleShot(true);
    m_treeUpdateTimer->setInterval(200); // Debounce updates
    connect(m_treeUpdateTimer, &QTimer::timeout, this, &ProjectManager::processTreeUpdateQueue);

    // Proxy structural signals from the model so external code can react
    // to tree changes without holding a raw model pointer.
    connect(m_treeModel, &ProjectTreeModel::rowsInserted,
            this, &ProjectManager::treeStructureChanged);
    connect(m_treeModel, &ProjectTreeModel::rowsRemoved,
            this, &ProjectManager::treeStructureChanged);
    connect(m_treeModel, &ProjectTreeModel::modelReset,
            this, &ProjectManager::treeStructureChanged);
    connect(m_treeModel, &ProjectTreeModel::dataChanged,
            this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        Q_EMIT treeItemDataChanged(roles);
    });

    // Filesystem watcher + debounce. The debounce coalesces bursts of
    // fsWatcher events (e.g. a git checkout writes dozens of files) into
    // a single reloadFromDisk(). The watcher is depth-gated: events are
    // dropped while an external-change window or a self-mutation is in
    // flight.
    m_fsWatcher = new QFileSystemWatcher(this);
    m_fsDebounce = new QTimer(this);
    m_fsDebounce->setSingleShot(true);
    m_fsDebounce->setInterval(250);
    connect(m_fsDebounce, &QTimer::timeout, this, &ProjectManager::processFsChanges);

    auto watcherTriggered = [this]() {
        if (m_externalChangeDepth > 0 || m_selfMutationDepth > 0) return;
        m_fsDebounce->start();
    };
    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, watcherTriggered);
    connect(m_fsWatcher, &QFileSystemWatcher::fileChanged, this, watcherTriggered);

    loadDefaults();
}

int ProjectManager::effectiveCategoryForPath(const QString &relativePath) const
{
    if (!m_treeModel) return ProjectTreeItem::None;
    ProjectTreeItem *item = m_treeModel->findItem(relativePath);
    if (!item) return ProjectTreeItem::None;
    return static_cast<int>(m_treeModel->effectiveCategory(item));
}

void ProjectManager::loadDefaults()
{
    m_meta = ProjectMetadata{};
    m_meta.name = i18n("Untitled Project");
    m_meta.author = i18n("Unknown Author");
    m_meta.version = ProjectKeys::CurrentVersion;

    // Default LoreKeeper Configuration
    QJsonObject lkConfig;
    QJsonArray categories;
    QJsonObject charCat;
    charCat[QStringLiteral("name")] = i18n("Characters");
    charCat[QStringLiteral("prompt")] = i18n(
        "Maintain a comprehensive Character Dossier. Synthesize new information into these sections:\n"
        "1. Basic Identity\n2. Physical Description\n3. Personality\n4. Habits/Mannerisms\n"
        "5. Background & Origin\n6. Goals\n7. Internal/External Conflicts\n8. Key Moments."
    );
    categories.append(charCat);
    lkConfig[QStringLiteral("categories")] = categories;
    m_meta.loreKeeperConfig = lkConfig;

    m_extraJson = QJsonObject();

    if (m_treeModel) {
        m_treeModel->setProjectData(QJsonObject());
    }
}

bool ProjectManager::isProjectOpen() const
{
    return !m_projectFilePath.isEmpty();
}

QString ProjectManager::projectPath() const
{
    if (m_projectFilePath.isEmpty()) return QString();
    return QFileInfo(m_projectFilePath).absolutePath();
}

ProjectManager::LoadedProjectJson ProjectManager::readProjectJson(const QString &filePath)
{
    LoadedProjectJson out;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ProjectManager: Failed to open file for reading:" << filePath;
        return out;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        qWarning() << "ProjectManager: Invalid project file format (not a JSON object):" << filePath;
        return out;
    }

    out.doc = doc.object();
    // Migrate legacy tree-node fields (string type/category values, etc.)
    // before parsing the metadata. The migrate() helper operates on the
    // full project object in place.
    out.migrated = migrate(out.doc);
    out.valid = true;
    return out;
}

bool ProjectManager::openProject(const QString &filePath)
{
    qDebug() << "ProjectManager: Authoritative request to open project:" << filePath;
    m_projectFilePath = filePath;

    if (!reloadFromDisk()) {
        m_projectFilePath.clear();
        return false;
    }

    qDebug() << "ProjectManager: Model synced. Root children:" << m_treeModel->rowCount(QModelIndex());
    Q_EMIT projectOpened();
    return true;
}

bool ProjectManager::createProject(const QString &dirPath, const QString &projectName)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) return false;
    }

    m_projectFilePath = dir.absoluteFilePath(QStringLiteral("rpgforge.project"));
    loadDefaults();
    m_meta.name = projectName;

    return saveProject();
}

bool ProjectManager::saveProject()
{
    if (m_projectFilePath.isEmpty()) return false;

    QFile file(m_projectFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    // Phase 6: before serialising metadata, refresh nodeMetadata and
    // orderHints from the live tree so user edits (synopsis, status,
    // custom child order) get persisted. These are the authoritative
    // stores going forward; the legacy `tree` JSON is still emitted
    // below for back-compat with older builds.
    if (m_treeModel) {
        m_meta.nodeMetadata = extractNodeMetadata();
        m_meta.orderHints   = extractOrderHints();
    }

    // Compose the on-disk JSON:
    //  1. typed metadata fields via ProjectMetadata::toJson
    //  2. unknown top-level keys carried over from the previous load
    //  3. the live tree JSON from the model (back-compat only)
    QJsonObject doc = m_meta.toJson();

    for (auto it = m_extraJson.constBegin(); it != m_extraJson.constEnd(); ++it) {
        if (!doc.contains(it.key())) {
            doc.insert(it.key(), it.value());
        }
    }

    // Back-compat: keep writing the legacy tree field so older builds can
    // still open the project. Deprecated; removable once we're comfortable
    // dropping support for pre-Phase-6 builds.
    doc[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();

    QJsonDocument docWrapper(doc);
    file.write(docWrapper.toJson());
    return true;
}

void ProjectManager::closeProject()
{
    m_projectFilePath.clear();
    if (m_fsWatcher) {
        const QStringList dirs = m_fsWatcher->directories();
        const QStringList files = m_fsWatcher->files();
        if (!dirs.isEmpty()) m_fsWatcher->removePaths(dirs);
        if (!files.isEmpty()) m_fsWatcher->removePaths(files);
    }
    if (m_fsDebounce) m_fsDebounce->stop();
    loadDefaults();
    Q_EMIT projectClosed();
}

bool ProjectManager::reloadFromDisk()
{
    if (m_projectFilePath.isEmpty()) return false;

    LoadedProjectJson loaded = readProjectJson(m_projectFilePath);
    if (!loaded.valid) return false;

    // Suppress self-generated watcher events: reload may touch files if
    // validateTree() needs to heal a missing authoritative folder, and
    // may rewrite rpgforge.project if a legacy migration runs.
    bool needsSave = loaded.migrated;
    {
        SelfMutationScope scope(this);

        // Parse typed metadata fields. ProjectMetadata::fromJson also handles
        // the v1-flat-margin -> v3-nested-margins migration.
        m_meta = ProjectMetadata::fromJson(loaded.doc);

        // Preserve any top-level keys that ProjectMetadata does not recognise
        // so saveProject() can write them back verbatim — future-compat with
        // fields introduced by newer builds.
        m_extraJson = QJsonObject();
        const QStringList known = ProjectMetadata::knownKeys();
        for (auto it = loaded.doc.constBegin(); it != loaded.doc.constEnd(); ++it) {
            if (!known.contains(it.key())) {
                m_extraJson.insert(it.key(), it.value());
            }
        }

        // Phase 6 legacy migration: if the project has a tree JSON but no
        // nodeMetadata yet, extract synopsis/status/categoryOverride per
        // path from the legacy tree so they survive the switch to the
        // disk-authoritative model. Triggers only once — subsequent opens
        // see a populated nodeMetadata and skip the migration entirely.
        if (m_meta.nodeMetadata.isEmpty()
            && loaded.doc.contains(QLatin1String(ProjectKeys::Tree))) {
            const QJsonObject legacyTree =
                loaded.doc.value(QLatin1String(ProjectKeys::Tree)).toObject();
            if (!legacyTree.isEmpty()) {
                qDebug() << "ProjectManager: migrating legacy tree metadata -> nodeMetadata";
                migrateLegacyTreeToNodeMetadata(legacyTree);
                if (!m_meta.nodeMetadata.isEmpty()) needsSave = true;
            }
        }

        qDebug() << "ProjectManager: Rebuilding tree from disk (path-authoritative).";
        m_treeModel->buildFromDisk(projectPath(),
                                   m_meta.nodeMetadata,
                                   m_meta.orderHints);
        validateTree();

        if (needsSave) {
            saveProject();
        }
    }

    updateWatcherPaths();

    Q_EMIT treeStructureChanged();
    Q_EMIT treeChanged();
    return true;
}

void ProjectManager::updateWatcherPaths()
{
    if (!m_fsWatcher) return;

    // Clear existing entries before repopulating. This is cheap on UI-scale
    // trees and avoids the churn of computing a diff.
    const QStringList oldDirs = m_fsWatcher->directories();
    const QStringList oldFiles = m_fsWatcher->files();
    if (!oldDirs.isEmpty()) m_fsWatcher->removePaths(oldDirs);
    if (!oldFiles.isEmpty()) m_fsWatcher->removePaths(oldFiles);

    if (m_projectFilePath.isEmpty() || !m_treeModel) return;

    QStringList folderPaths;
    const QString projectRoot = projectPath();
    // Always watch the project root so new top-level entries surface.
    if (!projectRoot.isEmpty() && QFileInfo(projectRoot).isDir()) {
        folderPaths.append(projectRoot);
    }

    m_treeModel->executeUnderLock([&] {
        std::function<void(ProjectTreeItem*)> walk = [&](ProjectTreeItem *item) {
            if (!item) return;
            if (item->type == ProjectTreeItem::Folder && !item->path.isEmpty()) {
                const QString absPath = QDir(projectRoot).absoluteFilePath(item->path);
                if (QFileInfo(absPath).isDir()) {
                    folderPaths.append(absPath);
                }
            }
            for (auto *child : item->children) walk(child);
        };
        walk(m_treeModel->rootItem());
    });

    if (!folderPaths.isEmpty()) {
        m_fsWatcher->addPaths(folderPaths);
    }
}

void ProjectManager::beginExternalChange()
{
    // Pre-increment to keep the counter monotonic when called cross-thread.
    const int newDepth = ++m_externalChangeDepth;
    if (newDepth == 1) {
        // Stop any pending debounce so we don't reload mid-window.
        if (QThread::currentThread() == this->thread()) {
            if (m_fsDebounce) m_fsDebounce->stop();
        } else {
            QMetaObject::invokeMethod(this, [this] {
                if (m_fsDebounce) m_fsDebounce->stop();
            }, Qt::QueuedConnection);
        }
    }
}

void ProjectManager::endExternalChange()
{
    if (m_externalChangeDepth == 0) {
        qWarning() << "ProjectManager::endExternalChange: called with no open window — ignoring";
        return;
    }
    const int newDepth = --m_externalChangeDepth;
    if (newDepth != 0) return;

    // Marshal the reload onto the main thread. Callers from QtConcurrent
    // workers (e.g. GitService::switchExploration) must not touch QObject
    // timers or QFileSystemWatcher from a worker thread.
    if (QThread::currentThread() == this->thread()) {
        reloadFromDisk();
    } else {
        QMetaObject::invokeMethod(this, [this] {
            reloadFromDisk();
        }, Qt::QueuedConnection);
    }
}

void ProjectManager::processFsChanges()
{
    if (!isProjectOpen()) return;
    if (m_externalChangeDepth > 0 || m_selfMutationDepth > 0) return;

    // Post-mutation quiet window: swallow events that are the kernel's
    // delayed notification of our own writes.
    if (QDateTime::currentMSecsSinceEpoch() < m_selfQuietUntilMs) {
        return;
    }

    // Minimal first cut: full reload. Phase 6 can refine this into a
    // targeted subtree diff once the tree sources directly from disk.
    qDebug() << "ProjectManager: External filesystem change detected, reloading.";
    reloadFromDisk();
}

QString ProjectManager::projectName() const { return m_meta.name; }
void ProjectManager::setProjectName(const QString &name)
{
    m_meta.name = name;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::author() const { return m_meta.author; }
void ProjectManager::setAuthor(const QString &author)
{
    m_meta.author = author;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::pageSize() const { return m_meta.pageSize; }
void ProjectManager::setPageSize(const QString &size)
{
    m_meta.pageSize = size;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginLeft() const { return m_meta.margins.left; }
void ProjectManager::setMarginLeft(double val)
{
    m_meta.margins.left = val;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginRight() const { return m_meta.margins.right; }
void ProjectManager::setMarginRight(double val)
{
    m_meta.margins.right = val;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginTop() const { return m_meta.margins.top; }
void ProjectManager::setMarginTop(double val)
{
    m_meta.margins.top = val;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginBottom() const { return m_meta.margins.bottom; }
void ProjectManager::setMarginBottom(double val)
{
    m_meta.margins.bottom = val;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::showPageNumbers() const { return m_meta.showPageNumbers; }
void ProjectManager::setShowPageNumbers(bool show)
{
    m_meta.showPageNumbers = show;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::stylesheetPath() const { return m_meta.stylesheetPath; }
void ProjectManager::setStylesheetPath(const QString &path)
{
    m_meta.stylesheetPath = path;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::autoSync() const { return m_meta.autoSync; }
void ProjectManager::setAutoSync(bool enabled)
{
    m_meta.autoSync = enabled;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::stylesheetFolderPath() const { return QDir(projectPath()).absoluteFilePath(QStringLiteral("stylesheets")); }
QStringList ProjectManager::stylesheetPaths() const
{
    QDir dir(stylesheetFolderPath());
    QStringList paths;
    for (const auto &fi : dir.entryInfoList({QStringLiteral("*.css")}, QDir::Files)) {
        paths << fi.absoluteFilePath();
    }
    return paths;
}

QJsonObject ProjectManager::loreKeeperConfig() const { return m_meta.loreKeeperConfig; }
void ProjectManager::setLoreKeeperConfig(const QJsonObject &config)
{
    m_meta.loreKeeperConfig = config;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::addFile(const QString &name, const QString &relativePath, const QString &parentPath)
{
    if (!isProjectOpen()) return false;
    if (relativePath.isEmpty()) {
        qWarning() << "ProjectManager::addFile: empty relative path rejected";
        return false;
    }

    SelfMutationScope scope(this);

    const QString absPath = QDir(projectPath()).absoluteFilePath(relativePath);
    QFileInfo fi(absPath);

    // Disk op first. If the file doesn't exist yet, create an empty one
    // (so the tree and disk are consistent). If it already exists we leave
    // its contents untouched — this path is also used to register files
    // that were produced by something else (e.g. LoreKeeper AI output).
    const bool fileAlreadyExisted = fi.exists();
    if (!fileAlreadyExisted) {
        // mkpath is a no-op if the parent directory already exists.
        if (!QDir().mkpath(fi.absolutePath())) {
            qWarning() << "ProjectManager::addFile: failed to create parent directory for" << absPath;
            return false;
        }
        QFile f(absPath);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning() << "ProjectManager::addFile: failed to create" << absPath;
            return false;
        }
        f.close();
    }

    QModelIndex parentIdx;
    if (!parentPath.isEmpty()) {
        ProjectTreeItem *parentItem = m_treeModel->findItem(parentPath);
        if (parentItem) {
            parentIdx = m_treeModel->indexForItem(parentItem);
        }
    }

    if (!m_treeModel->addFile(name, relativePath, parentIdx).isValid()) {
        // Tree op failed — rollback the disk op only if we just created the file.
        if (!fileAlreadyExisted) {
            QFile::remove(absPath);
        }
        qWarning() << "ProjectManager::addFile: tree insertion failed for" << relativePath;
        return false;
    }

    return maybeSaveAfterMutation();
}

bool ProjectManager::addFolder(const QString &name, const QString &relativePath, const QString &parentPath)
{
    if (!isProjectOpen()) return false;

    SelfMutationScope scope(this);

    // The UI currently passes an empty relativePath when creating a plain
    // logical folder via the "Add Folder" button. Derive a sensible disk
    // path from parent + name so the folder actually materialises on disk.
    QString effectivePath = relativePath;
    if (effectivePath.isEmpty()) {
        if (name.isEmpty()) {
            qWarning() << "ProjectManager::addFolder: empty name and path rejected";
            return false;
        }
        effectivePath = parentPath.isEmpty() ? name : (parentPath + QLatin1Char('/') + name);
    }

    const QString absPath = QDir(projectPath()).absoluteFilePath(effectivePath);
    const bool dirAlreadyExisted = QFileInfo(absPath).exists();

    if (!dirAlreadyExisted) {
        if (!QDir().mkpath(absPath)) {
            qWarning() << "ProjectManager::addFolder: failed to create directory" << absPath;
            return false;
        }
    }

    QModelIndex parentIdx;
    if (!parentPath.isEmpty()) {
        ProjectTreeItem *parentItem = m_treeModel->findItem(parentPath);
        if (parentItem) {
            parentIdx = m_treeModel->indexForItem(parentItem);
        }
    }

    if (!m_treeModel->addFolder(name, effectivePath, parentIdx).isValid()) {
        // Tree op failed — rollback only if we just created the directory.
        if (!dirAlreadyExisted) {
            QDir(absPath).removeRecursively();
        }
        qWarning() << "ProjectManager::addFolder: tree insertion failed for" << effectivePath;
        return false;
    }

    // New folder: add it to the watcher so external changes inside it surface.
    if (m_fsWatcher && !absPath.isEmpty() && QFileInfo(absPath).isDir()) {
        m_fsWatcher->addPath(absPath);
    }

    return maybeSaveAfterMutation();
}

bool ProjectManager::moveItem(const QString &sourcePath, const QString &targetParentPath)
{
    // Append-to-end overload: delegate to the three-argument form.
    if (!isProjectOpen()) return false;
    ProjectTreeItem *newParent = targetParentPath.isEmpty()
        ? m_treeModel->rootItem()
        : m_treeModel->findItem(targetParentPath);
    if (!newParent) return false;
    return moveItem(sourcePath, targetParentPath, newParent->children.count());
}

bool ProjectManager::removeItem(const QString &path)
{
    if (!isProjectOpen()) return false;
    if (path.isEmpty()) return false;

    SelfMutationScope scope(this);

    ProjectTreeItem *item = m_treeModel->findItem(path);
    if (!item) return false;

    // Belt-and-suspenders: the model also rejects authoritative roots in
    // removeRows, but checking here produces a clear log line and avoids
    // ever issuing a disk delete against a canonical folder. We guard on
    // both the model's structural check AND the canonical path strings,
    // since setupDefaultProject wraps the three canonical folders in an
    // outer project-name folder — so their parent is not m_rootItem and
    // the structural check alone would not fire.
    const QString canonical = path.toLower();
    const bool isCanonicalPath =
        canonical == QLatin1String("manuscript")
        || canonical == QLatin1String("lorekeeper")
        || canonical == QLatin1String("research");
    if (m_treeModel->isAuthoritativeRoot(item) || isCanonicalPath) {
        qWarning() << "ProjectManager::removeItem: refusing to remove authoritative root" << path;
        return false;
    }

    const QString absPath = QDir(projectPath()).absoluteFilePath(path);
    const QFileInfo fi(absPath);
    const bool isFolder = (item->type == ProjectTreeItem::Folder);

    // Disk op first. If the on-disk entry is missing we still prune the tree
    // (it was already out-of-sync); only a genuine failure to delete an
    // existing entry is an error.
    bool diskOk = true;
    if (fi.exists()) {
        if (isFolder) {
            diskOk = QDir(absPath).removeRecursively();
        } else {
            diskOk = QFile::remove(absPath);
        }
    }
    if (!diskOk) {
        qWarning() << "ProjectManager::removeItem: failed to delete on disk:" << absPath;
        return false;
    }

    const QModelIndex idx = m_treeModel->indexForItem(item);
    if (!m_treeModel->removeRow(idx.row(), idx.parent())) {
        // Can't un-delete — leave the tree dirty and log. The next validate/
        // reconciliation pass will resync.
        qWarning() << "ProjectManager::removeItem: disk delete succeeded but tree removal failed for" << path;
        return false;
    }

    return maybeSaveAfterMutation();
}

bool ProjectManager::renameItem(const QString &path, const QString &newName)
{
    if (!isProjectOpen()) return false;
    if (path.isEmpty() || newName.isEmpty()) return false;

    SelfMutationScope scope(this);

    ProjectTreeItem *item = m_treeModel->findItem(path);
    if (!item) return false;

    if (m_treeModel->isAuthoritativeRoot(item)) {
        qWarning() << "ProjectManager::renameItem: refusing to rename authoritative root" << path;
        return false;
    }

    const QFileInfo oldFi(QDir(projectPath()).absoluteFilePath(path));
    const QString parentAbsDir = oldFi.absolutePath();
    const QString oldBasename = oldFi.fileName();

    // The new on-disk basename: for files with an extension we keep the
    // extension intact if the caller did not supply one, mirroring what
    // setData(EditRole) expects.
    QString newBasename = newName;
    if (item->type == ProjectTreeItem::File) {
        const QString ext = oldFi.suffix();
        if (!ext.isEmpty() && !newBasename.contains(QLatin1Char('.'))) {
            newBasename = newBasename + QLatin1Char('.') + ext;
        }
    }

    const QString newAbsPath = parentAbsDir + QLatin1Char('/') + newBasename;
    const QString newRelPath = QDir(projectPath()).relativeFilePath(newAbsPath);

    // If the rename is a no-op (same basename) short-circuit.
    const bool onDiskRenameNeeded = (oldBasename != newBasename);
    if (onDiskRenameNeeded && QFileInfo(newAbsPath).exists()) {
        qWarning() << "ProjectManager::renameItem: target already exists on disk:" << newAbsPath;
        return false;
    }

    if (onDiskRenameNeeded && oldFi.exists()) {
        QDir parentDir(parentAbsDir);
        if (!parentDir.rename(oldBasename, newBasename)) {
            qWarning() << "ProjectManager::renameItem: disk rename failed:" << oldBasename << "->" << newBasename;
            return false;
        }
    }

    const QString oldPathSaved = item->path;
    const QModelIndex idx = m_treeModel->indexForItem(item);
    // Arm the re-entry guard so setData(EditRole) just updates the name
    // instead of routing back through this function.
    m_treeModel->m_inPmRename = true;
    const bool setOk = m_treeModel->setData(idx, newName, Qt::EditRole);
    m_treeModel->m_inPmRename = false;
    if (!setOk) {
        // Rollback: undo the disk rename if we made one.
        if (onDiskRenameNeeded && oldFi.exists()) {
            QDir(parentAbsDir).rename(newBasename, oldBasename);
        }
        qWarning() << "ProjectManager::renameItem: tree setData failed for" << path;
        return false;
    }

    // Cascade the path change to the item and all descendants so the tree
    // path mirrors the new on-disk location.
    if (onDiskRenameNeeded) {
        m_treeModel->updatePathsAfterMoveOrRename(item, oldPathSaved, newRelPath);
    }

    return maybeSaveAfterMutation();
}

ProjectTreeItem* ProjectManager::findItem(const QString &path) const
{
    if (!isProjectOpen()) return nullptr;
    return m_treeModel->findItem(path);
}

bool ProjectManager::setNodeSynopsis(const QString &relativePath, const QString &synopsis)
{
    if (!m_treeModel) return false;
    SelfMutationScope scope(this);
    ProjectTreeItem *item = m_treeModel->findItem(relativePath);
    if (!item) return false;
    const QModelIndex idx = m_treeModel->indexForItem(item);
    if (!idx.isValid()) return false;
    if (!m_treeModel->setData(idx, synopsis, ProjectTreeModel::SynopsisRole)) return false;
    saveProject();
    return true;
}

void ProjectManager::beginBatch()
{
    ++m_batchDepth;
}

void ProjectManager::endBatch()
{
    if (m_batchDepth <= 0) {
        qWarning() << "ProjectManager::endBatch: called with no open batch — ignoring";
        return;
    }
    --m_batchDepth;
    if (m_batchDepth == 0 && m_batchPendingSave) {
        m_batchPendingSave = false;
        saveProject();
        Q_EMIT treeStructureChanged();
    }
}

bool ProjectManager::maybeSaveAfterMutation()
{
    if (m_batchDepth > 0) {
        m_batchPendingSave = true;
        return true;
    }
    return saveProject();
}

TreeNodeSnapshot ProjectManager::treeSnapshot() const
{
    TreeNodeSnapshot out;
    if (!m_treeModel) return out;
    m_treeModel->executeUnderLock([&] {
        out = m_treeModel->snapshotFrom(m_treeModel->rootItem());
    });
    return out;
}

std::optional<TreeNodeSnapshot> ProjectManager::nodeSnapshot(const QString &relativePath) const
{
    if (!m_treeModel) return std::nullopt;
    std::optional<TreeNodeSnapshot> out;
    m_treeModel->executeUnderLock([&] {
        ProjectTreeItem *item = m_treeModel->findItem(relativePath);
        if (item) out = m_treeModel->snapshotFrom(item);
    });
    return out;
}

std::optional<TreeNodeSnapshot> ProjectManager::folderSnapshot(const QString &relativePath) const
{
    if (!m_treeModel) return std::nullopt;
    std::optional<TreeNodeSnapshot> out;
    m_treeModel->executeUnderLock([&] {
        ProjectTreeItem *item = m_treeModel->findItem(relativePath);
        if (item && item->type == ProjectTreeItem::Folder) {
            out = m_treeModel->snapshotFrom(item);
        }
    });
    return out;
}

QStringList ProjectManager::allFilePaths() const
{
    QStringList paths;
    if (!m_treeModel) return paths;
    m_treeModel->executeUnderLock([&] {
        std::function<void(ProjectTreeItem*)> walk = [&](ProjectTreeItem *item) {
            if (!item) return;
            if (item->type == ProjectTreeItem::File) {
                paths.append(item->path);
            }
            for (auto *child : item->children) walk(child);
        };
        walk(m_treeModel->rootItem());
    });
    return paths;
}

bool ProjectManager::pathExists(const QString &relativePath) const
{
    if (!m_treeModel) return false;
    bool exists = false;
    m_treeModel->executeUnderLock([&] {
        exists = (m_treeModel->findItem(relativePath) != nullptr);
    });
    return exists;
}

void ProjectManager::migrateLegacyTreeToNodeMetadata(const QJsonObject &legacyTree)
{
    // Walk the legacy tree recursively. For every node that has a
    // non-default synopsis / status / categoryOverride / displayName,
    // write one entry into m_meta.nodeMetadata keyed by the node's path.
    //
    // Definitions:
    //  - synopsis / status: any non-empty string (matches live-tree semantics)
    //  - categoryOverride: a category that can't be re-derived from the
    //    node's path (categoryForPath returns None, or differs from the
    //    stored value). Path-derivable categories never need to persist —
    //    buildFromDisk will re-derive them.
    //  - displayName: the legacy tree stored `name` alongside `path`. Most
    //    files have a name that matches completeBaseName(path), in which
    //    case buildFromDisk will reconstruct the same name from disk.
    //    Legacy entries where `name` diverges from completeBaseName are
    //    rare (usually Scrivener imports where the display label was
    //    hand-edited); preserve those explicitly.
    std::function<void(const QJsonObject&)> walk = [&](const QJsonObject &node) {
        const QString path = node.value(QLatin1String(ProjectKeys::Path)).toString();
        const QString synopsis = node.value(QStringLiteral("synopsis")).toString();
        const QString status   = node.value(QStringLiteral("status")).toString();
        const QString name     = node.value(QStringLiteral("name")).toString();

        int category = -1;
        const QJsonValue catVal = node.value(QLatin1String(ProjectKeys::Category));
        if (catVal.isDouble()) category = catVal.toInt();

        QJsonObject entry;
        if (!synopsis.isEmpty()) entry[QStringLiteral("synopsis")] = synopsis;
        if (!status.isEmpty())   entry[QStringLiteral("status")]   = status;

        // Only persist category when it can't be inferred from the path.
        // Comparison is against path-derived umbrella — a Chapter tag under
        // manuscript/ is user-meaningful and can't be derived, so it
        // qualifies as an override.
        if (category > 0) {
            const int pathDerived = static_cast<int>(
                ProjectTreeModel::categoryForPath(path));
            if (pathDerived != category) {
                entry[QStringLiteral("categoryOverride")] = category;
            }
        }

        if (!path.isEmpty() && !name.isEmpty()) {
            const QString baseName = QFileInfo(path).completeBaseName();
            // For folders the name commonly matches the directory name;
            // for files it matches completeBaseName. Anything else is a
            // user-overridden label — preserve it.
            const QString expectedFromPath =
                baseName.isEmpty() ? QFileInfo(path).fileName() : baseName;
            if (name != expectedFromPath) {
                entry[QStringLiteral("displayName")] = name;
            }
        }

        if (!path.isEmpty() && !entry.isEmpty()) {
            m_meta.nodeMetadata.insert(path, entry);
        }

        const QJsonArray children =
            node.value(QLatin1String(ProjectKeys::Children)).toArray();
        for (const QJsonValue &c : children) {
            walk(c.toObject());
        }
    };

    walk(legacyTree);
}

QJsonObject ProjectManager::extractNodeMetadata() const
{
    QJsonObject out;
    if (!m_treeModel) return out;

    m_treeModel->executeUnderLock([&] {
        std::function<void(const ProjectTreeItem*)> walk = [&](const ProjectTreeItem *item) {
            if (!item) return;

            // Root has no path; skip. Transient nodes (git history links)
            // must never land in the persistence layer.
            const bool isRoot = (item == m_treeModel->rootItem());
            if (!isRoot && !item->path.isEmpty()) {
                QJsonObject entry;
                if (!item->synopsis.isEmpty()) {
                    entry[QStringLiteral("synopsis")] = item->synopsis;
                }
                if (!item->status.isEmpty()) {
                    entry[QStringLiteral("status")] = item->status;
                }

                // categoryOverride is emitted only when the category can't
                // be reconstructed from the path on the next load. For the
                // three authoritative umbrellas (Manuscript / LoreKeeper /
                // Research), buildFromDisk derives them from the first
                // path segment, so we never persist those. Sub-categories
                // (Chapter, Scene, etc.) and explicit overrides for items
                // outside the authoritative roots must be persisted.
                if (item->category != ProjectTreeItem::None) {
                    const int pathDerived = static_cast<int>(
                        ProjectTreeModel::categoryForPath(item->path));
                    if (pathDerived != static_cast<int>(item->category)) {
                        entry[QStringLiteral("categoryOverride")] =
                            static_cast<int>(item->category);
                    }
                }

                // displayName survives only when it diverges from what
                // buildFromDisk would assign from the filename.
                const QString expected = (item->type == ProjectTreeItem::File)
                    ? QFileInfo(item->path).completeBaseName()
                    : QFileInfo(item->path).fileName();
                if (!item->name.isEmpty()
                    && !expected.isEmpty()
                    && item->name != expected) {
                    entry[QStringLiteral("displayName")] = item->name;
                }

                if (!entry.isEmpty()) {
                    out.insert(item->path, entry);
                }
            }

            for (const ProjectTreeItem *child : item->children) {
                walk(child);
            }
        };
        walk(m_treeModel->rootItem());
    });

    return out;
}

QJsonObject ProjectManager::extractOrderHints() const
{
    QJsonObject out;
    if (!m_treeModel) return out;

    m_treeModel->executeUnderLock([&] {
        std::function<void(const ProjectTreeItem*)> walk = [&](const ProjectTreeItem *item) {
            if (!item) return;

            if (item->type == ProjectTreeItem::Folder
                && item->children.size() > 1) {

                // Build the current order of this folder's child filenames
                // (derived from path so it matches what buildFromDisk sees
                // on the next load).
                QStringList currentOrder;
                currentOrder.reserve(item->children.size());
                for (const ProjectTreeItem *child : item->children) {
                    currentOrder.append(QFileInfo(child->path).fileName());
                }

                // Sort a copy using the same collation buildFromDisk uses
                // (QDir::Name + QDir::IgnoreCase). If the current order
                // matches, the hint is implicit — don't persist.
                QStringList sorted = currentOrder;
                std::sort(sorted.begin(), sorted.end(),
                          [](const QString &a, const QString &b) {
                              return a.compare(b, Qt::CaseInsensitive) < 0;
                          });

                if (sorted != currentOrder) {
                    QJsonArray arr;
                    for (const QString &n : currentOrder) arr.append(n);
                    // Key is the parent's project-relative path; root's is
                    // empty, which the JSON representation carries as "".
                    const QString parentKey = (item == m_treeModel->rootItem())
                        ? QString() : item->path;
                    out.insert(parentKey, arr);
                }
            }

            for (const ProjectTreeItem *child : item->children) {
                walk(child);
            }
        };
        walk(m_treeModel->rootItem());
    });

    return out;
}

bool ProjectManager::migrate(QJsonObject &data)
{
    int version = data.value(QLatin1String(ProjectKeys::Version)).toInt(1);
    bool changed = false;
    qDebug() << "ProjectManager: Checking migration for project version" << version;

    if (version < 3) {
        qDebug() << "ProjectManager: Upgrading project schema to v3.";
        version = 3;
        changed = true;
    }

    QJsonObject tree = data.value(QLatin1String(ProjectKeys::Tree)).toObject();
    
    // Recursive helper to ensure categories are set for standard folders
    std::function<bool(QJsonObject&)> migrateNode = [&](QJsonObject &node) -> bool {
        bool nodeChanged = false;
        QString name = node.value(QLatin1String(ProjectKeys::Name)).toString();
        int cat = node.value(QLatin1String(ProjectKeys::Category)).toInt();

        // If category is missing or None, try to identify by name
        if (cat == static_cast<int>(ProjectTreeItem::None)) {
            if (name.compare(QLatin1String(ProjectKeys::FolderManuscript), Qt::CaseInsensitive) == 0 || 
                name == i18n(ProjectKeys::FolderManuscript)) {
                node[QLatin1String(ProjectKeys::Category)] = static_cast<int>(ProjectTreeItem::Manuscript);
                nodeChanged = true;
            } else if (name.compare(QLatin1String(ProjectKeys::FolderResearch), Qt::CaseInsensitive) == 0 || 
                       name == i18n(ProjectKeys::FolderResearch)) {
                node[QLatin1String(ProjectKeys::Category)] = static_cast<int>(ProjectTreeItem::Research);
                nodeChanged = true;
            } else if (name.compare(QLatin1String(ProjectKeys::FolderLoreKeeper), Qt::CaseInsensitive) == 0 || 
                       name == i18n(ProjectKeys::FolderLoreKeeper)) {
                node[QLatin1String(ProjectKeys::Category)] = static_cast<int>(ProjectTreeItem::LoreKeeper);
                nodeChanged = true;
            } else if (name.compare(QLatin1String(ProjectKeys::FolderMedia), Qt::CaseInsensitive) == 0 || 
                       name == i18n(ProjectKeys::FolderMedia)) {
                node[QLatin1String(ProjectKeys::Category)] = static_cast<int>(ProjectTreeItem::Media);
                nodeChanged = true;
            }
        }

        // Fix type field: handle string values and misclassified files
        QJsonValue typeVal = node.value(QLatin1String(ProjectKeys::Type));
        if (typeVal.isString()) {
            QString typeStr = typeVal.toString().toLower();
            node[QLatin1String(ProjectKeys::Type)] = (typeStr == QLatin1String("file")) ? 1 : 0;
            nodeChanged = true;
        } else {
            int nodeType = typeVal.toInt();
            QString nodePath = node.value(QLatin1String(ProjectKeys::Path)).toString();
            if (nodeType == 0 && !nodePath.isEmpty()) {
                QString suffix = QFileInfo(nodePath).suffix().toLower();
                static const QStringList fileSuffixes = {
                    QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mkd"),
                    QStringLiteral("txt"), QStringLiteral("css"), QStringLiteral("yaml"),
                    QStringLiteral("yml"), QStringLiteral("json"), QStringLiteral("html"),
                    QStringLiteral("htm"), QStringLiteral("xml"), QStringLiteral("rpgvars"),
                    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                    QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
                    QStringLiteral("bmp"), QStringLiteral("pdf")
                };
                if (fileSuffixes.contains(suffix)) {
                    node[QLatin1String(ProjectKeys::Type)] = 1;
                    nodeChanged = true;
                }
            }
        }

        // Handle string-valued category (for projects with string enum values)
        QJsonValue catJsonVal = node.value(QLatin1String(ProjectKeys::Category));
        if (catJsonVal.isString()) {
            static const QHash<QString, int> catStrMap = {
                {QStringLiteral("none"), 0}, {QStringLiteral("manuscript"), 1},
                {QStringLiteral("research"), 2}, {QStringLiteral("lorekeeper"), 3},
                {QStringLiteral("media"), 4}, {QStringLiteral("chapter"), 5},
                {QStringLiteral("scene"), 6}, {QStringLiteral("characters"), 7},
                {QStringLiteral("places"), 8}, {QStringLiteral("cultures"), 9},
                {QStringLiteral("stylesheet"), 10}, {QStringLiteral("notes"), 11}
            };
            node[QLatin1String(ProjectKeys::Category)] = catStrMap.value(catJsonVal.toString().toLower(), 0);
            nodeChanged = true;
        }

        // Recurse into children
        QJsonArray children = node.value(QLatin1String(ProjectKeys::Children)).toArray();
        bool childrenChanged = false;
        for (int i = 0; i < children.size(); ++i) {
            QJsonObject child = children[i].toObject();
            if (migrateNode(child)) {
                children[i] = child;
                childrenChanged = true;
            }
        }

        if (childrenChanged) {
            node[QLatin1String(ProjectKeys::Children)] = children;
            nodeChanged = true;
        }

        return nodeChanged;
    };

    if (migrateNode(tree)) {
        qDebug() << "ProjectManager: Successfully categorized standard folders during migration.";
        data[QLatin1String(ProjectKeys::Tree)] = tree;
        changed = true;
    }

    if (changed) {
        data[QLatin1String(ProjectKeys::Version)] = version;
    }
    return changed;
}

void ProjectManager::setupDefaultProject(const QString &dir, const QString &name)
{
    qDebug() << "ProjectManager: Populating default project structure...";
    QDir projectDir(dir);
    
    // Root logical folder
    QModelIndex rootIdx = m_treeModel->addFolder(name, QString());

    // 1. Manuscript (Chapters -> Scenes)
    projectDir.mkpath(QStringLiteral("manuscript"));
    QModelIndex manuscriptIdx = m_treeModel->addFolder(i18n(ProjectKeys::FolderManuscript), QStringLiteral("manuscript"), rootIdx);
    m_treeModel->setData(manuscriptIdx, static_cast<int>(ProjectTreeItem::Manuscript), ProjectTreeModel::CategoryRole);
    
    // 2. Research (Open in split-screen)
    projectDir.mkpath(QStringLiteral("research"));
    QModelIndex researchIdx = m_treeModel->addFolder(i18n(ProjectKeys::FolderResearch), QStringLiteral("research"), rootIdx);
    m_treeModel->setData(researchIdx, static_cast<int>(ProjectTreeItem::Research), ProjectTreeModel::CategoryRole);

    // 3. LoreKeeper (Auto-generated content)
    projectDir.mkpath(QStringLiteral("lorekeeper"));
    QModelIndex loreIdx = m_treeModel->addFolder(i18n(ProjectKeys::FolderLoreKeeper), QStringLiteral("lorekeeper"), rootIdx);
    m_treeModel->setData(loreIdx, static_cast<int>(ProjectTreeItem::LoreKeeper), ProjectTreeModel::CategoryRole);
    
    // 4. Media (Linked assets)
    projectDir.mkpath(QStringLiteral("media"));
    QModelIndex mediaIdx = m_treeModel->addFolder(i18n(ProjectKeys::FolderMedia), QStringLiteral("media"), rootIdx);
    m_treeModel->setData(mediaIdx, static_cast<int>(ProjectTreeItem::Media), ProjectTreeModel::CategoryRole);

    // Initial guide file
    QString readmePath = QStringLiteral("research/README.md");
    QFile readmeFile(projectDir.absoluteFilePath(readmePath));
    if (readmeFile.open(QIODevice::WriteOnly)) {
        readmeFile.write("# Welcome to your RPG Forge Project\n\nHappy worldbuilding!");
        readmeFile.close();
    }
    m_treeModel->addFile(i18n("Project Guide"), readmePath, researchIdx);

    saveProject();
    
    qDebug() << "ProjectManager: Default content ready. Emitting projectOpened.";
    Q_EMIT projectOpened();
}

QStringList ProjectManager::getActiveFiles() const
{
    if (!isProjectOpen()) return {};
    QStringList relativePaths = m_treeModel->allFiles();
    QStringList absolutePaths;
    QDir projectDir(projectPath());
    for (const QString &rel : relativePaths) {
        absolutePaths.append(projectDir.absoluteFilePath(rel));
    }
    return absolutePaths;
}

void ProjectManager::triggerWordCountUpdate()
{
    if (!isProjectOpen()) return;

    // Fire-and-forget: result reaches listeners via totalWordCountUpdated
    // signal; no future to hold on to.
    QThreadPool::globalInstance()->start([this]() {
        int count = countWordsInTree(m_treeModel->projectData(), projectPath());
        Q_EMIT totalWordCountUpdated(count);
    });
}

void ProjectManager::saveExplorationData(const QVariantMap &wordCountCache,
                                          const QVariantMap &explorationColors)
{
    if (m_projectFilePath.isEmpty()) return;

    m_meta.wordCountCache = wordCountCache;
    m_meta.explorationColors = explorationColors;
    saveProject();
}

void ProjectManager::loadExplorationData(QVariantMap &wordCountCache,
                                          QVariantMap &explorationColors) const
{
    wordCountCache = m_meta.wordCountCache;
    explorationColors = m_meta.explorationColors;
}

void ProjectManager::requestTreeUpdate(const QString &category, const QString &entityName, const QString &relativePath)
{
    qDebug() << "ProjectManager: Received tree update request for" << entityName << "in" << category;
    QMutexLocker locker(&m_queueMutex);
    m_treeUpdateQueue.enqueue({category, entityName, relativePath});
    m_treeUpdateTimer->start();
}

void ProjectManager::processTreeUpdateQueue()
{
    QMutexLocker locker(&m_queueMutex);
    if (m_treeUpdateQueue.isEmpty()) return;

    qDebug() << "ProjectManager: Processing" << m_treeUpdateQueue.size() << "tree update requests.";

    bool changed = false;
    while (!m_treeUpdateQueue.isEmpty()) {
        TreeUpdateRequest req = m_treeUpdateQueue.dequeue();
        qDebug() << "ProjectManager: Processing request for" << req.entityName << "in category" << req.category;

        // Find LoreKeeper root using a robust breadth-first search
        ProjectTreeItem *loreRoot = nullptr;
        QQueue<ProjectTreeItem*> searchQueue;
        searchQueue.enqueue(m_treeModel->rootItem());

        while (!searchQueue.isEmpty()) {
            ProjectTreeItem *current = searchQueue.dequeue();
            if (!current) continue;
            if (current->category == ProjectTreeItem::LoreKeeper) {
                loreRoot = current;
                break;
            }
            for (auto *c : current->children) searchQueue.enqueue(c);
        }

        if (!loreRoot) {
            qWarning() << "ProjectManager: CRITICAL - Could not find LoreKeeper root node in authoritative tree!";
            continue;
        }

        // Ensure the category subfolder exists — route through addFolder so
        // it materialises on disk under lorekeeper/<Category>/ (and not just
        // in the tree).
        const QString lkRootPath = loreRoot->path.isEmpty()
            ? QStringLiteral("lorekeeper")
            : loreRoot->path;
        const QString catFolderPath = lkRootPath + QLatin1Char('/') + req.category;

        ProjectTreeItem *catFolder = nullptr;
        for (auto *c : loreRoot->children) {
            if (c->name == req.category) {
                catFolder = c;
                break;
            }
        }

        if (!catFolder) {
            qDebug() << "ProjectManager: Category folder" << req.category
                     << "not found, creating on disk + tree at" << catFolderPath;
            if (!addFolder(req.category, catFolderPath, lkRootPath)) {
                qWarning() << "ProjectManager: Failed to create LoreKeeper category folder" << catFolderPath;
                continue;
            }
            catFolder = m_treeModel->findItem(catFolderPath);
            if (!catFolder) {
                qWarning() << "ProjectManager: addFolder succeeded but findItem failed for" << catFolderPath;
                continue;
            }
        }

        bool found = false;
        for (auto *c : catFolder->children) {
            if (c->path == req.relativePath) {
                found = true;
                break;
            }
        }

        if (!found) {
            qDebug() << "ProjectManager: Adding new file to model:" << req.entityName << "at" << req.relativePath;
            // LoreKeeper wrote the file to disk before calling us, so addFile
            // will register the existing file rather than creating a new one.
            if (addFile(req.entityName, req.relativePath, catFolder->path)) {
                changed = true;
            }
        } else {
            qDebug() << "ProjectManager: Entity" << req.entityName << "already exists in tree.";
        }
    }

    if (changed) {
        qDebug() << "ProjectManager: Tree structure changed, emitting signal.";
        Q_EMIT treeChanged();
    }
}

QJsonObject ProjectManager::treeData() const
{
    if (!m_treeModel) return {};
    return m_treeModel->projectData();
}

void ProjectManager::notifyTreeChanged()
{
    if (!m_treeModel) return;
    // Re-applying the same data triggers beginResetModel/endResetModel so
    // attached views refresh. Cheaper than scanning for what changed.
    m_treeModel->setProjectData(m_treeModel->projectData());
    Q_EMIT treeChanged();
}

bool ProjectManager::setTreeData(const QJsonObject &treeJson)
{
    if (!m_treeModel) return false;
    if (treeJson.isEmpty()) return false;

    SelfMutationScope scope(this);
    m_treeModel->setProjectData(treeJson);
    validateTree();          // self-heal authoritative folders + leaf-as-file
    saveProject();           // persist the corrected tree
    updateWatcherPaths();    // tree shape may have changed; re-arm watcher
    Q_EMIT treeChanged();
    return true;
}

bool ProjectManager::moveItem(const QString &draggedPath,
                               const QString &newParentPath,
                               int row)
{
    if (!m_treeModel) return false;
    if (draggedPath.isEmpty()) return false;

    SelfMutationScope scope(this);

    ProjectTreeItem *dragged = m_treeModel->findItem(draggedPath);
    if (!dragged) {
        qWarning() << "ProjectManager::moveItem: dragged path not found:" << draggedPath;
        return false;
    }

    // Reject moving authoritative top-level folders (defensive — the model
    // also rejects via removeRows / dropMimeData, but the explicit check
    // here gives a clear log line).
    if (m_treeModel->isAuthoritativeRoot(dragged)) {
        qWarning() << "ProjectManager::moveItem: refusing to move authoritative root" << draggedPath;
        return false;
    }

    ProjectTreeItem *newParent = newParentPath.isEmpty()
        ? m_treeModel->rootItem()
        : m_treeModel->findItem(newParentPath);
    if (!newParent) {
        qWarning() << "ProjectManager::moveItem: target parent not found:" << newParentPath;
        return false;
    }

    // No-op guard: dropping into the same parent at the end is a benign
    // reorder; let the tree op handle it but skip the disk rename entirely.
    const QString projectDir = projectPath();
    const QString oldAbs = QDir(projectDir).absoluteFilePath(draggedPath);
    const QFileInfo oldFi(oldAbs);
    const QString baseName = oldFi.fileName();

    const QString newParentAbs = newParentPath.isEmpty()
        ? projectDir
        : QDir(projectDir).absoluteFilePath(newParentPath);
    const QString newAbs = newParentAbs + QLatin1Char('/') + baseName;
    const QString newRelPath = QDir(projectDir).relativeFilePath(newAbs);

    const bool sameParent = (QFileInfo(oldFi.absolutePath()).absoluteFilePath()
                              == QFileInfo(newParentAbs).absoluteFilePath());

    // Disk op first — rename into the target parent. Only done when the
    // parent is actually changing AND something exists on disk.
    const bool onDiskMoveNeeded = !sameParent && oldFi.exists();
    if (onDiskMoveNeeded) {
        if (QFileInfo(newAbs).exists()) {
            qWarning() << "ProjectManager::moveItem: target already exists on disk:" << newAbs;
            return false;
        }
        // Ensure target parent dir exists (mkpath is a no-op if present).
        QDir().mkpath(newParentAbs);
        if (!QDir().rename(oldAbs, newAbs)) {
            qWarning() << "ProjectManager::moveItem: disk rename failed:" << oldAbs << "->" << newAbs;
            return false;
        }
        qDebug() << "ProjectManager::moveItem: disk" << oldAbs << "->" << newAbs;
    }

    const QString oldPathSaved = dragged->path;
    if (!m_treeModel->moveItem(dragged, newParent, row)) {
        // Rollback: undo the disk rename if we performed one.
        if (onDiskMoveNeeded) {
            QDir().rename(newAbs, oldAbs);
        }
        return false;
    }

    // Cascade the new path prefix to the moved subtree so tree paths track
    // the on-disk location. Skipped when the parent didn't change.
    if (!sameParent) {
        m_treeModel->updatePathsAfterMoveOrRename(dragged, oldPathSaved, newRelPath);

        // Rewrite relative markdown links inside the moved file (or
        // every .md/.markdown file under the moved folder) so links
        // to media/, other chapters, etc. keep resolving against the
        // new location. Without this, moving a chapter into a
        // subdirectory breaks every ![](image.png) inside it.
        rewriteRelativeLinksAfterMove(oldFi.absoluteFilePath(), newAbs);
    }

    maybeSaveAfterMutation();
    return true;
}

void ProjectManager::rewriteRelativeLinksAfterMove(const QString &oldAbsPath,
                                                     const QString &newAbsPath)
{
    // Collect every markdown file in the moved subtree (just the single
    // file when moving a .md file; all nested .md files when moving a
    // folder). The QDir::absolutePath() values we compare are "the
    // directory the file lived in BEFORE the move" vs "the directory
    // the file lives in NOW" — only those paths need updating.
    struct FileMove { QString oldDir; QString newAbs; };
    QList<FileMove> targets;

    const QFileInfo newInfo(newAbsPath);
    if (newInfo.isFile()) {
        const QString suffix = newInfo.suffix().toLower();
        if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
            targets.append({QFileInfo(oldAbsPath).absolutePath(), newAbsPath});
        }
    } else if (newInfo.isDir()) {
        QDirIterator it(newAbsPath, {QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString newFileAbs = it.next();
            const QString relUnderMove = QDir(newAbsPath).relativeFilePath(newFileAbs);
            const QString oldFileDir = QFileInfo(QDir(oldAbsPath).absoluteFilePath(relUnderMove))
                                        .absolutePath();
            targets.append({oldFileDir, newFileAbs});
        }
    }

    // Link-rewrite regex. Catches standard CommonMark image/link
    // syntax: ![alt](path) and [text](path). URLs with a scheme
    // (http://, https://, file://) or absolute paths pass through
    // unchanged — only relative paths need repointing.
    static const QRegularExpression linkRe(
        QStringLiteral("(!?\\[[^\\]]*\\])\\(([^)\\s]+)(?:\\s+\"[^\"]*\")?\\)"));

    int totalFiles = 0;
    int totalRewrites = 0;
    for (const FileMove &fm : targets) {
        QFile f(fm.newAbs);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QByteArray originalBytes = f.readAll();
        f.close();
        const QString originalText = QString::fromUtf8(originalBytes);

        QString rewritten = originalText;
        int offset = 0;
        int rewritesInFile = 0;
        auto it = linkRe.globalMatch(originalText);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString linkPrefix = m.captured(1);
            const QString target = m.captured(2);

            // Skip absolute paths, URLs, anchors, and empty targets.
            if (target.isEmpty()) continue;
            if (target.startsWith(QLatin1Char('#'))) continue;
            if (target.startsWith(QLatin1Char('/'))) continue;
            static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
            if (scheme.match(target).hasMatch()) continue;

            // Resolve the link against the file's OLD directory to get
            // the absolute target, then recompute the path relative to
            // the file's NEW directory.
            const QString targetAbs = QDir(fm.oldDir).absoluteFilePath(target);
            const QString newDir = QFileInfo(fm.newAbs).absolutePath();
            QString newRel = QDir(newDir).relativeFilePath(targetAbs);
            // Prefer forward slashes in the markdown source even on
            // systems where QDir::separator() is backslash.
            newRel.replace(QLatin1Char('\\'), QLatin1Char('/'));

            if (newRel == target) continue;   // already good

            const QString replacement = linkPrefix + QLatin1Char('(') + newRel + QLatin1Char(')');
            const int matchStart = m.capturedStart() + offset;
            const int matchLen = m.capturedLength();
            rewritten.replace(matchStart, matchLen, replacement);
            offset += replacement.length() - matchLen;
            ++rewritesInFile;
        }

        if (rewritesInFile > 0) {
            QFile out(fm.newAbs);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(rewritten.toUtf8());
                out.close();
                totalRewrites += rewritesInFile;
                ++totalFiles;
            }
        }
    }

    if (totalRewrites > 0) {
        qInfo().noquote() << "ProjectManager: rewrote" << totalRewrites
                           << "relative link(s) across" << totalFiles
                           << "markdown file(s) after move" << oldAbsPath
                           << "->" << newAbsPath;
    }
}

void ProjectManager::validateTree()
{
    if (!m_treeModel || !m_treeModel->rootItem()) return;

    // Phase 6: the tree now comes from disk, so most of the legacy heals
    // (leaf-folder-as-file, extension auto-repair, fuzzy resolution) can
    // never fire — buildFromDisk does not produce those shapes. The only
    // invariants still worth enforcing here are:
    //
    //   1. The three authoritative top-level folders (manuscript/,
    //      lorekeeper/, research/) must exist on disk AND in the tree,
    //      stamped with the correct category.
    //   2. Belt-and-suspenders: any Folder node that somehow has children
    //      but is typed as File gets corrected to Folder.
    //
    // Reconciliation entries are no longer produced here: a disk walk
    // can't surface missing-on-disk file nodes because the tree only
    // contains what disk has. If anyone ever calls validateTree after a
    // non-disk-derived shape (e.g. a scripted setProjectData for import),
    // missing entries simply won't be caught — and that's fine, importers
    // are expected to produce a valid tree.

    bool changed = false;

    // Pass 1: normalise Folder-vs-File shape for any legacy / imported nodes.
    std::function<void(ProjectTreeItem*)> validate = [&](ProjectTreeItem *item) {
        if (!item) return;
        if (item->type == ProjectTreeItem::File && !item->children.isEmpty()) {
            qDebug() << "ProjectManager: validateTree: Correcting type File->Folder for"
                     << item->name << "(has children)";
            item->type = ProjectTreeItem::Folder;
            changed = true;
        }
        for (auto *child : item->children) validate(child);
    };
    validate(m_treeModel->rootItem());

    // Pass 2: ensure the three authoritative top-level folders exist in
    // both tree and disk. Permissive match (path / name / category) so a
    // legacy tree with an empty-path Manuscript folder is healed in place
    // rather than duplicated.
    struct AuthoritativeFolder {
        const char *path;
        const char *defaultName;
        ProjectTreeItem::Category category;
    };
    static const AuthoritativeFolder kAuthoritativeFolders[] = {
        {"manuscript", "Manuscript", ProjectTreeItem::Manuscript},
        {"lorekeeper", "LoreKeeper", ProjectTreeItem::LoreKeeper},
        {"research",   "Research",   ProjectTreeItem::Research},
    };

    ProjectTreeItem *root = m_treeModel->rootItem();
    const QString projectDirPath = projectPath();

    for (const auto &spec : kAuthoritativeFolders) {
        const QString canonicalPath = QString::fromLatin1(spec.path);
        const QString canonicalName = QString::fromLatin1(spec.defaultName);

        ProjectTreeItem *existing = nullptr;
        for (auto *child : root->children) {
            if (child->type != ProjectTreeItem::Folder) continue;

            const bool pathMatches = !child->path.isEmpty()
                && child->path.compare(canonicalPath, Qt::CaseInsensitive) == 0;
            const bool nameMatches = !child->name.isEmpty()
                && child->name.compare(canonicalName, Qt::CaseInsensitive) == 0;
            const bool categoryMatches = child->category == spec.category;

            if (pathMatches || nameMatches || categoryMatches) {
                existing = child;
                break;
            }
        }

        if (existing) {
            if (existing->path.compare(canonicalPath, Qt::CaseInsensitive) != 0) {
                qDebug() << "ProjectManager: validateTree: normalizing authoritative folder path"
                         << existing->path << "->" << canonicalPath;
                existing->path = canonicalPath;
                changed = true;
            }
            if (existing->category != spec.category) {
                existing->category = spec.category;
                changed = true;
            }
            if (existing->name.isEmpty()) {
                existing->name = canonicalName;
                changed = true;
            }
        } else {
            qDebug() << "ProjectManager: validateTree: creating missing authoritative folder"
                     << canonicalPath;
            QModelIndex newIdx = m_treeModel->addFolder(i18n(spec.defaultName),
                                                         canonicalPath, QModelIndex());
            if (ProjectTreeItem *newItem = m_treeModel->itemFromIndex(newIdx)) {
                newItem->category = spec.category;
            }
            changed = true;
        }

        // mkpath is a no-op when the directory exists, so this is safe to
        // run every time.
        if (!projectDirPath.isEmpty()) {
            QDir(projectDirPath).mkpath(canonicalPath);
        }
    }

    if (changed) {
        qDebug() << "ProjectManager: validateTree: Tree corrections applied, saving project.";
        saveProject();
    }
}

int ProjectManager::countWordsInTree(const QJsonObject &tree, const QString &projectPath) const
{
    int total = 0;
    if (tree.value(QStringLiteral("type")).toInt() == static_cast<int>(ProjectTreeItem::File)) {
        QString path = tree.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && (path.endsWith(QStringLiteral(".md")) || path.endsWith(QStringLiteral(".markdown")))) {
            QFile file(QDir(projectPath).absoluteFilePath(path));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(file.readAll());
                total = content.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).count();
            }
        }
    }

    QJsonArray children = tree.value(QStringLiteral("children")).toArray();
    for (const auto &child : children) {
        total += countWordsInTree(child.toObject(), projectPath);
    }
    return total;
}
