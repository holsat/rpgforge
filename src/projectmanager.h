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

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <optional>

#include "projectmetadata.h"
#include "reconciliationtypes.h"
#include "treenodesnapshot.h"

class ProjectTreeModel;
struct ProjectTreeItem;

/**
 * @brief Manages the RPG Forge project structure and settings.
 * 
 * A project is a directory containing an 'rpgforge.project' JSON file.
 */
class ProjectManager : public QObject
{
    Q_OBJECT

public:
    static ProjectManager& instance();

    bool openProject(const QString &filePath);
    bool createProject(const QString &dirPath, const QString &projectName);
    bool saveProject();
    void closeProject();

    bool isProjectOpen() const;
    QString projectFilePath() const { return m_projectFilePath; }
    QString projectPath() const;

    // Project Metadata
    QString projectName() const;
    void setProjectName(const QString &name);

    QString author() const;
    void setAuthor(const QString &author);

    // Page Settings
    QString pageSize() const;
    void setPageSize(const QString &size);
    double marginLeft() const;
    void setMarginLeft(double val);
    double marginRight() const;
    void setMarginRight(double val);
    double marginTop() const;
    void setMarginTop(double val);
    double marginBottom() const;
    void setMarginBottom(double val);

    bool showPageNumbers() const;
    void setShowPageNumbers(bool show);

    // Stylesheet
    QString stylesheetPath() const;
    void setStylesheetPath(const QString &path);

    // AutoSync
    bool autoSync() const;
    void setAutoSync(bool enabled);

    // Returns the absolute path to the stylesheets/ directory
    QString stylesheetFolderPath() const;
    // Returns absolute paths of all .css files in the stylesheets/ directory
    QStringList stylesheetPaths() const;

    // LoreKeeper Configuration
    QJsonObject loreKeeperConfig() const;
    void setLoreKeeperConfig(const QJsonObject &config);

    // Logical Tree Management — model() access is intentionally restricted
    // to ProjectTreePanel. Qt's model/view framework requires a non-const
    // QAbstractItemModel* to render a QTreeView; no other consumer needs
    // the live model. All other code must use the snapshot API below,
    // plus the mutation wrappers (addFile / addFolder / moveItem /
    // removeItem / renameItem / setNodeSynopsis / requestTreeUpdate).
private:
    friend class ProjectTreePanel;
    ProjectTreeModel* model() const { return m_treeModel; }
public:

    // ---- Snapshot-based read API (safe for non-view consumers) ----
    //
    // All return deep-copy POD structs built under m_treeModel's internal
    // mutex. No raw pointers or QAbstractItemModel pointers escape.
    // Snapshot producers run in O(subtree-size); fine for UI-scale trees.

    /**
     * \brief Returns a deep-copy snapshot of the whole project tree.
     *
     * Returns an empty TreeNodeSnapshot when no project is open. The
     * returned value owns its data and is safe to carry across threads.
     */
    TreeNodeSnapshot treeSnapshot() const;

    /**
     * \brief Returns a snapshot of the node at \a relativePath, or nullopt
     * if no such node is in the tree. Exact-match, case-sensitive.
     */
    std::optional<TreeNodeSnapshot> nodeSnapshot(const QString &relativePath) const;

    /**
     * \brief Returns a snapshot of the folder at \a relativePath, or nullopt
     * if the path is not in the tree or resolves to a non-Folder node.
     */
    std::optional<TreeNodeSnapshot> folderSnapshot(const QString &relativePath) const;

    /**
     * \brief Flat list of every File-typed node's project-relative path.
     *
     * Unlike getActiveFiles() (which returns absolute paths for legacy
     * consumers), allFilePaths() returns project-relative paths and is
     * the preferred read for snapshot consumers.
     */
    QStringList allFilePaths() const;

    /**
     * \brief True if the tree contains a node at \a relativePath.
     */
    bool pathExists(const QString &relativePath) const;
    
    // Encapsulated Tree Mutations (The authoritative way to change project structure)
    bool addFile(const QString &name, const QString &relativePath, const QString &parentPath = QString());
    bool addFolder(const QString &name, const QString &relativePath, const QString &parentPath = QString());
    bool moveItem(const QString &sourcePath, const QString &targetParentPath);
    bool removeItem(const QString &path);
    bool renameItem(const QString &path, const QString &newName);

    /**
     * \brief Write a synopsis for the node at \a relativePath.
     *
     * Routes the write through ProjectTreeModel::setData so dataChanged
     * fires (and is proxied out via treeItemDataChanged), and persists
     * the project immediately. Returns false if the path is not in the
     * tree or the model rejects the setData call. Path-based wrapper
     * used by SynopsisService so it does not need access to the live
     * model.
     */
    bool setNodeSynopsis(const QString &relativePath, const QString &synopsis);
    
    // Authorization check for finding items
    ProjectTreeItem* findItem(const QString &path) const;

    int calculateTotalWordCount() const;
    void triggerWordCountUpdate();

    /**
     * \brief Persists exploration caches into the project file.
     *
     * Stores both the word-count cache (commit hash to word count) and the
     * branch-colour map (branch name to colour string) in the project's
     * JSON data under reserved keys.  The project file is saved to disk
     * immediately via saveProject().
     *
     * Must be called on the main thread.  Typically called on project close
     * or when ExplorationGraphView::colorMapChanged() fires.
     *
     * \param wordCountCache   Map of commit hash strings to word-count integers,
     *                         as returned by GitService::saveWordCountCache().
     * \param explorationColors Map of branch name strings to colour strings
     *                          (e.g. "#a0c4ff"), as returned by
     *                          ExplorationGraphView::saveColorMap().
     * \sa loadExplorationData()
     */
    void saveExplorationData(const QVariantMap &wordCountCache,
                              const QVariantMap &explorationColors);

    /**
     * \brief Reads exploration caches from the project file.
     *
     * Populates the output parameters from the project JSON data written by
     * saveExplorationData().  Both output parameters are cleared before
     * being populated; if no data was previously saved the maps are left
     * empty rather than returning an error.
     *
     * Must be called on the main thread.  Typically called immediately
     * after openProject() succeeds.
     *
     * \param[out] wordCountCache    Populated with a map of commit hash strings
     *                               to word-count integers.
     * \param[out] explorationColors Populated with a map of branch name strings
     *                               to colour strings.
     * \sa saveExplorationData(), GitService::loadWordCountCache(),
     *     ExplorationGraphView::loadColorMap()
     */
    void loadExplorationData(QVariantMap &wordCountCache,
                              QVariantMap &explorationColors) const;

    // Returns a flat list of absolute paths for all files registered in the project tree.
    QStringList getActiveFiles() const;

    // Setup project with default folders (Manuscript, Research, etc.)
    void setupDefaultProject(const QString &dir, const QString &name);

Q_SIGNALS:
    void projectOpened();
    void projectClosed();
    void projectSettingsChanged();
    void treeChanged();
    void totalWordCountUpdated(int count);

    /**
     * \brief A folder/file was inserted or removed in the tree.
     * Proxy of ProjectTreeModel::rowsInserted / rowsRemoved / modelReset
     * so external code can react to structural tree changes without
     * needing a raw model pointer.
     */
    void treeStructureChanged();

    /**
     * \brief A specific role on an existing tree item changed.
     * Proxy of ProjectTreeModel::dataChanged. The role list is forwarded
     * verbatim; consumers filter for SynopsisRole / StatusRole / etc.
     */
    void treeItemDataChanged(const QList<int> &roles);

    /**
     * \brief Fires when validateTree() discovers tree entries pointing at
     *        paths that can't be resolved on disk.
     *
     * Payload is one entry per missing node. Each entry may carry a
     * fuzzy-matched \c suggestedPath. MainWindow handles this by opening a
     * ReconciliationDialog; scripted / headless runs can consume it via
     * the RpgForgeDBus::pendingReconciliation() accessor.
     */
    void reconciliationRequired(const QList<ReconciliationEntry> &entries);

public Q_SLOTS:
    void requestTreeUpdate(const QString &category, const QString &entityName, const QString &relativePath);

    /**
     * \brief Effective umbrella category for the item at \a relativePath.
     *
     * Walks the parent chain to find the nearest categorised ancestor.
     * Wrapper around ProjectTreeModel::effectiveCategory so callers do
     * not need a raw model pointer just to look up a category. Returns
     * ProjectTreeItem::None if the path is not in the tree.
     */
    int effectiveCategoryForPath(const QString &relativePath) const;

    /**
     * \brief Snapshot the current tree as a JSON object.
     *
     * Read-only accessor that returns the same JSON the project file
     * persists. Use this from importers / exporters / DBus introspection /
     * PDF compile / DBus listings — anywhere code only needs to inspect
     * the tree, not mutate it. Mutations must go through setTreeData(),
     * moveItem(), or requestTreeUpdate().
     */
    QJsonObject treeData() const;

    /**
     * \brief Force every attached view to refresh its rendering of the
     * current tree without mutating any data.
     *
     * Used after operations that touched the underlying file system or
     * the on-disk project state in ways that could affect how items
     * render (e.g. an exploration branch switch). Implemented via a
     * model reset; safe to call without any prior mutation.
     */
    void notifyTreeChanged();

    /**
     * \brief Replace the entire tree from a JSON object.
     *
     * Validating wrapper around ProjectTreeModel::setProjectData. Used by
     * importers (Scrivener, Word, etc.) after building a fresh tree
     * representation. Internally runs validateTree() so the persisted JSON
     * is healed (Folder→File for leaf-as-disk-file, missing authoritative
     * top-level folders created, etc.) before any save.
     *
     * Returns true if the tree was applied successfully. Refuses an empty
     * QJsonObject (no-op).
     */
    bool setTreeData(const QJsonObject &treeJson);

    /**
     * \brief Move a tree item to a new parent at the specified row.
     *
     * Validating wrapper around ProjectTreeModel::moveItem. Inputs are
     * project-relative paths (resolved internally via findItem) — callers
     * never need a raw ProjectTreeItem pointer. The move is rejected if
     * either item cannot be found, if the move would be circular, or if
     * the source is an authoritative root.
     *
     * Returns true on successful move; the project is saved automatically.
     */
    bool moveItem(const QString &draggedPath,
                  const QString &newParentPath,
                  int row);

    /**
     * \brief Validate and self-heal the current tree.
     *
     * Public so importers can call it on a freshly-populated tree before
     * the first save, ensuring the persisted JSON is correct from the
     * very first write. Also called automatically by openProject().
     *
     * Performs:
     *  - Fix item type (Folder vs File) based on extension and disk presence
     *  - Convert leaf Folder items that resolve to files on disk to File
     *    type, with the correct path/extension (handles Scrivener-style
     *    space/underscore translation)
     *  - Set canonical paths for the three authoritative top-level folders
     *  - Create the three authoritative folders (Manuscript / LoreKeeper /
     *    Research) on tree AND disk if they are not already present
     *  - Scan all File / leaf-Folder nodes for missing on-disk paths. Any
     *    that cannot be auto-healed are collected into a
     *    ReconciliationEntry list and emitted via reconciliationRequired().
     */
    void validateTree();

    /**
     * \brief Begin a batch of mutations.
     *
     * While a batch is open, saveProject() is deferred (mutations still
     * update tree + disk) and the batched save is flushed once by endBatch().
     * Nestable via a counter; the outermost endBatch() triggers the save +
     * a single treeStructureChanged emission.
     */
    void beginBatch();

    /**
     * \brief End a batch of mutations.
     *
     * If this is the outermost endBatch() call and at least one save was
     * deferred, saveProject() runs and treeStructureChanged is emitted once.
     */
    void endBatch();

private Q_SLOTS:
    void processTreeUpdateQueue();

private:
    explicit ProjectManager(QObject *parent = nullptr);

    void loadDefaults();
    bool migrate(QJsonObject &data);
    int countWordsInTree(const QJsonObject &tree, const QString &projectPath) const;

    struct TreeUpdateRequest {
        QString category;
        QString entityName;
        QString relativePath;
    };

    QString m_projectFilePath;
    ProjectMetadata m_meta;
    QJsonObject m_extraJson;          // unknown top-level keys preserved for round-trip fidelity
    ProjectTreeModel *m_treeModel;
    QTimer *m_treeUpdateTimer;
    QQueue<TreeUpdateRequest> m_treeUpdateQueue;
    QMutex m_queueMutex;

    // Batch state: beginBatch()/endBatch() defer saveProject() across many
    // mutations. A non-zero m_batchDepth counts nested opens; mutation paths
    // call maybeSaveAfterMutation() which becomes a no-op while a batch is
    // open and sets m_batchPendingSave = true for the eventual flush.
    int m_batchDepth = 0;
    bool m_batchPendingSave = false;

    // Internal helper: save now if no batch is open, otherwise remember to
    // save when the outermost endBatch() runs.
    bool maybeSaveAfterMutation();
};

#endif // PROJECTMANAGER_H
