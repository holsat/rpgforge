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

    // Project Data interface
    void setProjectData(const QJsonObject &data);
    QJsonObject projectData() const;

    ProjectTreeItem* rootItem() const { return m_rootItem; }
    ProjectTreeItem* itemFromIndex(const QModelIndex &index) const;
    ProjectTreeItem* findItem(const QString &relativePath, ProjectTreeItem *root = nullptr) const;
    QModelIndex indexForItem(ProjectTreeItem *item) const;

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
