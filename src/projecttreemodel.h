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
#include <QIcon>
#include <QMutex>

struct ProjectTreeItem {
    enum Type { Folder, File };
    enum Category { None, Manuscript, Research, Chapter, Scene, Characters, Places, Cultures, Stylesheet, Notes };
    Type type;
    Category category = None;
    QString name;
    QString path; // Relative to project root
    QString synopsis;
    QString status;
    bool transient = false;
    QList<ProjectTreeItem*> children;
    ProjectTreeItem *parent = nullptr;

    ~ProjectTreeItem() {
        qDeleteAll(children);
    }
};

class ProjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        TransientRole = Qt::UserRole + 100,
        CategoryRole = Qt::UserRole + 101,
        SynopsisRole = Qt::UserRole + 102,
        StatusRole = Qt::UserRole + 103
    };
    explicit ProjectTreeModel(QObject *parent = nullptr);
    ~ProjectTreeModel() override;

    void setProjectData(const QJsonObject &treeData);
    QJsonObject projectData() const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    // Manipulation
    QModelIndex addFolder(const QString &name, const QString &path = QString(), const QModelIndex &parent = QModelIndex());
    QModelIndex addFile(const QString &name, const QString &path, const QModelIndex &parent = QModelIndex());
    QModelIndex addTransientVersionLink(const QString &name, const QString &path, const QModelIndex &parent);
    void addFileWithSmartDiscovery(const QString &absolutePath, const QModelIndex &parent = QModelIndex());
    bool removeItem(const QModelIndex &index);
    bool moveItem(ProjectTreeItem *item, ProjectTreeItem *newParent, int newRow);

    // Suppress per-row signals during bulk operations (e.g. import).
    // Call beginBulkImport() before, endBulkImport() after; the view refreshes atomically.
    void beginBulkImport();
    void endBulkImport();

    QModelIndex indexForItem(ProjectTreeItem *item) const;

    // Drag and Drop
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

    ProjectTreeItem* itemFromIndex(const QModelIndex &index) const;
    ProjectTreeItem* findItem(const QString &relativePath, ProjectTreeItem *root = nullptr) const;
    ProjectTreeItem* rootItem() const { return m_rootItem; }

private:
    ProjectTreeItem* loadItem(const QJsonObject &obj, ProjectTreeItem *parent);
    QJsonObject saveItem(ProjectTreeItem *item) const;

    ProjectTreeItem *m_rootItem;
    bool m_bulkImporting = false;
    mutable QMutex m_treeMutex;
};

#endif // PROJECTTREEMODEL_H
