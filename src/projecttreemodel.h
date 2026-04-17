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

class QMimeData;

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

    QStringList allFiles() const;

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
    mutable QRecursiveMutex m_treeMutex;
};

#endif // PROJECTTREEMODEL_H
