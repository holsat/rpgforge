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

#ifndef PROJECTTREEMODEL_H
#define PROJECTTREEMODEL_H

#include <QAbstractItemModel>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QRecursiveMutex>

#include "treenodesnapshot.h"

class QDir;
class QMimeData;

/**
 * \brief Try to resolve a tree-stored project-relative path to an actual file
 *        on disk within \a projectDir.
 *
 * Returns the on-disk project-relative path if found, or an empty string if
 * no resolution could be made. Shared by ProjectManager::validateTree() and
 * ProjectTreePanel's click handlers so the healing heuristics remain DRY.
 *
 * Strategy (tried in order, with as-is and space->underscore path variants):
 *   1) exact path with each of the known text-file extensions appended
 *      ("" / .md / .markdown / .mkd / .txt / .rtf)
 *   2) nested-leaf pattern (path "X" -> file "X/X.ext", common from Scrivener)
 *   3) parent-directory scan with normalised (space/underscore, case-insensitive)
 *      stem comparison
 */
QString tryResolveOnDisk(const QDir &projectDir, const QString &relativePath);

struct ProjectTreeItem {
    enum Type {
        Folder,
        File
    };

    enum Category {
        None,
        Manuscript,
        Research,
        LoreKeeper,
        Media,
        Chapter,
        Scene,
        Characters,
        Places,
        Cultures,
        Stylesheet,
        Notes
    };

    QString name;
    QString path;
    QString synopsis;
    QString status;
    Type type = File;
    Category category = None;
    ProjectTreeItem *parent = nullptr;
    QList<ProjectTreeItem*> children;

    ~ProjectTreeItem() {
        qDeleteAll(children);
    }
};

class ProjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        TypeRole,
        CategoryRole,
        SynopsisRole,
        StatusRole
    };

    explicit ProjectTreeModel(QObject *parent = nullptr);
    ~ProjectTreeModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

    // Project Data interface
    void setProjectData(const QJsonObject &data);
    QJsonObject projectData() const;

    /**
     * \brief Rebuild the tree by walking the project directory on disk.
     *
     * Phase 6 of the tree-refactor: disk is the source of truth for
     * structure. Per-node metadata (synopsis, status, categoryOverride,
     * displayName) is looked up in \a nodeMetadata by project-relative
     * path and stamped onto the created nodes. Sibling ordering is
     * driven by \a orderHints (parentPath -> QJsonArray of child
     * filenames); absent or empty entries fall back to alphanumeric.
     *
     * Skips hidden / dot-prefixed files and directories, RPG Forge's own
     * on-disk side-files (rpgforge.project, .rpgforge-vectors.db) and
     * common OS / build artefacts that are never part of a project (.git,
     * .rpgforge, node_modules, .DS_Store, etc.).
     *
     * Runs under beginResetModel / endResetModel; attached views receive
     * a single modelReset at the end. Safe to call with an empty project
     * path (returns an empty tree).
     */
    void buildFromDisk(const QString &projectPath,
                       const QJsonObject &nodeMetadata,
                       const QJsonObject &orderHints);

    ProjectTreeItem* rootItem() const { return m_rootItem; }
    ProjectTreeItem* itemFromIndex(const QModelIndex &index) const;
    ProjectTreeItem* findItem(const QString &relativePath, ProjectTreeItem *root = nullptr) const;
    QModelIndex indexForItem(ProjectTreeItem *item) const;

    /**
     * @brief Derives the authoritative category from a project-relative path.
     *
     * The three authoritative top-level folders are:
     *   - "manuscript" -> Manuscript
     *   - "lorekeeper" -> LoreKeeper
     *   - "research"   -> Research
     *
     * Items that live under one of these roots (e.g. "manuscript/ch1/scene1.md")
     * inherit the root's category. Comparison is case-insensitive on the
     * first path segment. Returns ProjectTreeItem::None if the path is empty
     * or does not begin with one of the authoritative roots.
     */
    static ProjectTreeItem::Category categoryForPath(const QString &relativePath);

    /**
     * @brief Returns `item`'s own category, or the nearest ancestor's category
     * if the item's own category is None. Stops at the root (returns None if
     * no categorized ancestor is found).
     *
     * Used by routing consumers (compile, PDF export, file-open read-only
     * check, LoreKeeper RAG triggers, etc.) that need to treat unset
     * categories as inherited from the parent folder.
     */
    ProjectTreeItem::Category effectiveCategory(const ProjectTreeItem *item) const;

    /**
     * @brief Recompute and propagate the umbrella category of a subtree.
     *
     * Called after an item is moved to a new parent, so JSON category
     * metadata follows the move. Walks the subtree rooted at `item`. For
     * each node:
     *   - the *umbrella* category (Manuscript / LoreKeeper / Research) is
     *     overwritten to match the new parent's effective umbrella;
     *   - explicit *sub-category* tags (Chapter, Scene, Characters, Places,
     *     Cultures, Stylesheet, Notes) are preserved — those are user
     *     intent independent of where the item lives;
     *   - items previously categorised None get the new umbrella too.
     *
     * Emits dataChanged for each updated index.
     */
    void propagateCategoryFromParent(ProjectTreeItem *item);

    /**
     * @brief Returns true if the item is an authoritative top-level folder —
     * i.e. a Folder directly under the root whose path resolves to one of
     * the three authoritative categories (Manuscript / LoreKeeper / Research).
     *
     * Authoritative folders cannot be renamed, moved, or removed by the user.
     */
    bool isAuthoritativeRoot(const ProjectTreeItem *item) const;

    QModelIndex addFolder(const QString &name, const QString &path, const QModelIndex &parent = QModelIndex());
    QModelIndex addFile(const QString &name, const QString &path, const QModelIndex &parent = QModelIndex());
    QModelIndex addFileWithSmartDiscovery(const QString &absolutePath, const QModelIndex &parent = QModelIndex());
    QModelIndex addTransientVersionLink(const QString &name, const QString &path, const QModelIndex &parent = QModelIndex());
    
    bool removeItem(const QModelIndex &index);
    bool moveItem(ProjectTreeItem *item, ProjectTreeItem *newParent, int newRow);

    /**
     * @brief Rewrite the `path` field of \a item and every descendant when a
     * rename or move changes the path prefix.
     *
     * Replaces occurrences of \a oldPathPrefix at the head of each node's
     * path with \a newPathPrefix. Emits dataChanged(PathRole) for each
     * touched node so views pick up the new tooltip/path. Must be called
     * under m_treeMutex (the method acquires it internally).
     *
     * Used by atomic rename and move flows in ProjectManager to keep tree
     * paths in sync with the on-disk location after a successful disk op.
     */
    void updatePathsAfterMoveOrRename(ProjectTreeItem *item,
                                       const QString &oldPathPrefix,
                                       const QString &newPathPrefix);

    QStringList allFiles() const;

    /**
     * @brief Build a deep-copy TreeNodeSnapshot of the subtree rooted at `item`.
     *
     * Safe to call under external lock; does NOT take m_treeMutex itself —
     * caller is expected to wrap this in executeUnderLock() if concurrent
     * mutations are possible. Returns an empty snapshot if \a item is null.
     * The returned snapshot contains no references to model-owned memory
     * and is safe to carry across threads.
     */
    TreeNodeSnapshot snapshotFrom(const ProjectTreeItem *item) const;

    void beginBulkImport();
    void endBulkImport();

    // Utility for thread-safe operations on the tree
    template<typename F>
    void executeUnderLock(F func) {
        QMutexLocker locker(&m_treeMutex);
        func();
    }

private:
    ProjectTreeModel(const ProjectTreeModel&) = delete;
    ProjectTreeModel& operator=(const ProjectTreeModel&) = delete;

    ProjectTreeItem* loadItem(const QJsonObject &obj, ProjectTreeItem *parent = nullptr);
    QJsonObject saveItem(ProjectTreeItem *item) const;
    ProjectTreeItem* findItemRecursive(const QString &relativePath, ProjectTreeItem *root) const;

    ProjectTreeItem *m_rootItem;
    bool m_bulkImporting = false;
    // Set to true for the duration of our custom dropMimeData() and cleared
    // on the next event-loop tick. While set, removeRows() short-circuits
    // to block QAbstractItemView::startDrag()'s post-drop clearOrRemove()
    // from deleting the moved item at its new (destination) location. The
    // atomic move inside dropMimeData has already updated Qt's persistent
    // indexes to follow the move, so the view's auto-remove would otherwise
    // target the destination parent + destination row — destroying the
    // moved row at its new home.
    bool m_dropInProgress = false;

    // Guard against recursion when ProjectManager::renameItem() calls our
    // setData(EditRole) internally. When set, setData(EditRole) only
    // updates the in-memory name (PM already handled disk + path). When
    // clear, setData(EditRole) routes through PM::renameItem() so inline
    // F2 edits get the full disk+tree+path atomic treatment.
    bool m_inPmRename = false;
    mutable QRecursiveMutex m_treeMutex;

    // ProjectManager needs access to m_inPmRename to arm the re-entry guard
    // around its internal setData(EditRole) call during renameItem().
    friend class ProjectManager;
};

#endif // PROJECTTREEMODEL_H
