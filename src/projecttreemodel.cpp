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

#include "projecttreemodel.h"
#include "projectmanager.h"
#include "markdownparser.h"
#include <QMimeData>
#include <QDataStream>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <KLocalizedString>

ProjectTreeModel::ProjectTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_rootItem = new ProjectTreeItem();
    m_rootItem->name = i18n("Root");
    m_rootItem->type = ProjectTreeItem::Folder;
}

ProjectTreeModel::~ProjectTreeModel()
{
    delete m_rootItem;
}

QModelIndex ProjectTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();

    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();

    QMutexLocker locker(&m_treeMutex);
    ProjectTreeItem *childItem = parentItem->children.value(row);
    if (childItem) return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex ProjectTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return QModelIndex();

    ProjectTreeItem *childItem = static_cast<ProjectTreeItem*>(index.internalPointer());
    ProjectTreeItem *parentItem = childItem->parent;

    if (parentItem == m_rootItem || !parentItem) return QModelIndex();

    ProjectTreeItem *grandParent = parentItem->parent;
    if (!grandParent) return QModelIndex();

    return createIndex(grandParent->children.indexOf(parentItem), 0, parentItem);
}

int ProjectTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;

    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return 0;

    QMutexLocker locker(&m_treeMutex);
    return parentItem->children.count();
}

int ProjectTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant ProjectTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    ProjectTreeItem *item = static_cast<ProjectTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return item->name;
    } else if (role == Qt::DecorationRole) {
        if (item->type == ProjectTreeItem::Folder) {
            switch(item->category) {
                case ProjectTreeItem::Manuscript: return QIcon::fromTheme(QStringLiteral("document-edit"));
                case ProjectTreeItem::Research: return QIcon::fromTheme(QStringLiteral("system-search"));
                case ProjectTreeItem::LoreKeeper: return QIcon(QStringLiteral(":/icons/lorekeeper.png"));
                case ProjectTreeItem::Media: return QIcon::fromTheme(QStringLiteral("folder-videos"));
                case ProjectTreeItem::Chapter: return QIcon::fromTheme(QStringLiteral("folder-text"));
                default: return QIcon::fromTheme(QStringLiteral("folder"));
            }
        } else {
            QString suffix = QFileInfo(item->path).suffix().toLower();
            if (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
                return QIcon::fromTheme(QStringLiteral("image-x-generic"));
            } else if (suffix == QStringLiteral("pdf")) {
                return QIcon::fromTheme(QStringLiteral("application-pdf"));
            } else if (item->category == ProjectTreeItem::LoreKeeper) {
                return QIcon::fromTheme(QStringLiteral("text-x-changelog"));
            } else if (item->category == ProjectTreeItem::Scene) {
                return QIcon::fromTheme(QStringLiteral("document-edit-sign-symbolic"));
            }
            return QIcon::fromTheme(QStringLiteral("text-markdown"));
        }
    } else if (role == PathRole) {
        return item->path;
    } else if (role == TypeRole) {
        return static_cast<int>(item->type);
    } else if (role == CategoryRole) {
        return static_cast<int>(item->category);
    } else if (role == SynopsisRole) {
        return item->synopsis;
    } else if (role == StatusRole) {
        return item->status;
    } else if (role == Qt::ToolTipRole) {
        return item->path;
    }

    return QVariant();
}

bool ProjectTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) return false;

    ProjectTreeItem *item = static_cast<ProjectTreeItem*>(index.internalPointer());

    if (role == Qt::EditRole) {
        item->name = value.toString();
        Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    } else if (role == CategoryRole) {
        item->category = static_cast<ProjectTreeItem::Category>(value.toInt());
        Q_EMIT dataChanged(index, index, {Qt::DecorationRole, CategoryRole});
        return true;
    } else if (role == SynopsisRole) {
        item->synopsis = value.toString();
        Q_EMIT dataChanged(index, index, {SynopsisRole});
        return true;
    } else if (role == StatusRole) {
        item->status = value.toString();
        Q_EMIT dataChanged(index, index, {StatusRole});
        return true;
    }

    return false;
}

Qt::ItemFlags ProjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;

    ProjectTreeItem *item = static_cast<ProjectTreeItem*>(index.internalPointer());
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;

    // LoreKeeper root node is protected: no editing, no removing
    if (item->category == ProjectTreeItem::LoreKeeper && item->type == ProjectTreeItem::Folder && item->parent == m_rootItem) {
        return flags;
    }

    // LoreKeeper content is read-only
    if (item->category != ProjectTreeItem::LoreKeeper) {
        flags |= Qt::ItemIsEditable;
    }

    if (item->type == ProjectTreeItem::Folder) {
        flags |= Qt::ItemIsDropEnabled;
    }

    return flags;
}

void ProjectTreeModel::setProjectData(const QJsonObject &data)
{
    beginResetModel();
    delete m_rootItem;
    m_rootItem = loadItem(data);
    if (!m_rootItem) {
        m_rootItem = new ProjectTreeItem();
        m_rootItem->name = i18n("Root");
        m_rootItem->type = ProjectTreeItem::Folder;
    }
    endResetModel();
}

QJsonObject ProjectTreeModel::projectData() const
{
    return saveItem(m_rootItem);
}

ProjectTreeItem* ProjectTreeModel::loadItem(const QJsonObject &obj, ProjectTreeItem *parent)
{
    auto *item = new ProjectTreeItem();
    item->name = obj.value(QStringLiteral("name")).toString();
    item->path = obj.value(QStringLiteral("path")).toString();
    item->synopsis = obj.value(QStringLiteral("synopsis")).toString();
    item->status = obj.value(QStringLiteral("status")).toString();
    // Type: handle both int and string formats
    QJsonValue typeVal = obj.value(QStringLiteral("type"));
    if (typeVal.isString()) {
        item->type = (typeVal.toString().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0)
            ? ProjectTreeItem::File : ProjectTreeItem::Folder;
    } else {
        int t = typeVal.toInt();
        item->type = (t == static_cast<int>(ProjectTreeItem::File))
            ? ProjectTreeItem::File : ProjectTreeItem::Folder;
    }

    // Category: handle both int and string formats
    QJsonValue catVal = obj.value(QStringLiteral("category"));
    if (catVal.isString()) {
        static const QHash<QString, ProjectTreeItem::Category> catMap = {
            {QStringLiteral("none"), ProjectTreeItem::None},
            {QStringLiteral("manuscript"), ProjectTreeItem::Manuscript},
            {QStringLiteral("research"), ProjectTreeItem::Research},
            {QStringLiteral("lorekeeper"), ProjectTreeItem::LoreKeeper},
            {QStringLiteral("media"), ProjectTreeItem::Media},
            {QStringLiteral("chapter"), ProjectTreeItem::Chapter},
            {QStringLiteral("scene"), ProjectTreeItem::Scene},
            {QStringLiteral("characters"), ProjectTreeItem::Characters},
            {QStringLiteral("places"), ProjectTreeItem::Places},
            {QStringLiteral("cultures"), ProjectTreeItem::Cultures},
            {QStringLiteral("stylesheet"), ProjectTreeItem::Stylesheet},
            {QStringLiteral("notes"), ProjectTreeItem::Notes}
        };
        item->category = catMap.value(catVal.toString().toLower(), ProjectTreeItem::None);
    } else {
        int c = catVal.toInt();
        item->category = (c >= 0 && c <= static_cast<int>(ProjectTreeItem::Notes))
            ? static_cast<ProjectTreeItem::Category>(c) : ProjectTreeItem::None;
    }
    item->parent = parent;

    QJsonArray children = obj.value(QStringLiteral("children")).toArray();
    for (const auto &child : children) {
        item->children.append(loadItem(child.toObject(), item));
    }

    return item;
}

QJsonObject ProjectTreeModel::saveItem(ProjectTreeItem *item) const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = item->name;
    obj[QStringLiteral("path")] = item->path;
    obj[QStringLiteral("synopsis")] = item->synopsis;
    obj[QStringLiteral("status")] = item->status;
    obj[QStringLiteral("type")] = static_cast<int>(item->type);
    obj[QStringLiteral("category")] = static_cast<int>(item->category);

    QJsonArray children;
    for (auto *child : item->children) {
        children.append(saveItem(child));
    }
    obj[QStringLiteral("children")] = children;

    return obj;
}

ProjectTreeItem* ProjectTreeModel::itemFromIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return m_rootItem;
    return static_cast<ProjectTreeItem*>(index.internalPointer());
}

ProjectTreeItem* ProjectTreeModel::findItem(const QString &relativePath, ProjectTreeItem *root) const
{
    QMutexLocker locker(&m_treeMutex);
    return findItemRecursive(relativePath, root ? root : m_rootItem);
}

ProjectTreeItem* ProjectTreeModel::findItemRecursive(const QString &relativePath, ProjectTreeItem *root) const
{
    if (!root) return nullptr;

    if (root->path == relativePath) return root;

    for (auto *child : root->children) {
        ProjectTreeItem *found = findItemRecursive(relativePath, child);
        if (found) return found;
    }
    return nullptr;
}

QModelIndex ProjectTreeModel::indexForItem(ProjectTreeItem *item) const
{
    if (!item || item == m_rootItem) return QModelIndex();
    ProjectTreeItem *parentItem = item->parent;
    if (!parentItem) return QModelIndex();
    int row = parentItem->children.indexOf(item);
    if (row == -1) return QModelIndex();
    return createIndex(row, 0, item);
}

QModelIndex ProjectTreeModel::addFolder(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();

    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();

    if (!m_bulkImporting) beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::Folder;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    if (!m_bulkImporting) endInsertRows();

    qDebug() << "ProjectTreeModel: [INSERT] Folder" << name << "at" << path << "under" << parentItem->name;
    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addFile(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();

    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();

    if (!m_bulkImporting) beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    if (!m_bulkImporting) endInsertRows();

    qDebug() << "ProjectTreeModel: [INSERT] File" << name << "at" << path << "under" << parentItem->name;
    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addFileWithSmartDiscovery(const QString &absolutePath, const QModelIndex &parent)
{
    QFileInfo fi(absolutePath);
    QString projectDir = ProjectManager::instance().projectPath();
    QString relativePath = QDir(projectDir).relativeFilePath(absolutePath);

    // If already in tree, skip
    if (findItem(relativePath)) return QModelIndex();

    QModelIndex newIdx = addFile(fi.completeBaseName(), relativePath, parent);

    // Smart discovery for markdown files: look for linked media
    QString suffix = fi.suffix().toLower();
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
        QFile file(absolutePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            MarkdownParser parser;
            auto links = parser.extractLinks(content);
            for (const auto &link : links) {
                QString absoluteLink = QDir(fi.absolutePath()).absoluteFilePath(link.url);
                if (QFile::exists(absoluteLink)) {
                    addFileWithSmartDiscovery(absoluteLink, parent);
                }
            }
        }
    }
    return newIdx;
}

QModelIndex ProjectTreeModel::addTransientVersionLink(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();
    
    beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->category = parentItem->category;
    item->parent = parentItem;
    parentItem->children.append(item);
    endInsertRows();
    
    return index(row, 0, parent);
}

bool ProjectTreeModel::removeItem(const QModelIndex &index)
{
    if (!index.isValid()) return false;
    return removeRows(index.row(), 1, index.parent());
}

bool ProjectTreeModel::removeRows(int row, int count, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem || row < 0 || row + count > parentItem->children.count()) return false;

    QMutexLocker locker(&m_treeMutex);
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        delete parentItem->children.takeAt(row);
    }
    endRemoveRows();
    return true;
}

bool ProjectTreeModel::moveItem(ProjectTreeItem *item, ProjectTreeItem *newParent, int newRow)
{
    if (!item || !newParent || item == newParent || !item->parent) return false;

    // Check for circular move (can't move into own descendant)
    ProjectTreeItem *p = newParent;
    while (p) {
        if (p == item) return false;
        p = p->parent;
    }

    QMutexLocker locker(&m_treeMutex);

    ProjectTreeItem *oldParent = item->parent;
    int oldRow = oldParent->children.indexOf(item);

    QModelIndex sourceParent = indexForItem(oldParent);
    QModelIndex destParent = indexForItem(newParent);

    if (!beginMoveRows(sourceParent, oldRow, oldRow, destParent, newRow)) return false;

    oldParent->children.removeAt(oldRow);
    if (newRow > newParent->children.count()) newRow = newParent->children.count();
    newParent->children.insert(newRow, item);
    item->parent = newParent;

    endMoveRows();
    return true;
}

QStringList ProjectTreeModel::allFiles() const
{
    QStringList files;
    std::function<void(ProjectTreeItem*)> collect = [&](ProjectTreeItem *item) {
        if (item->type == ProjectTreeItem::File) {
            files.append(item->path);
        }
        for (auto *child : item->children) {
            collect(child);
        }
    };
    collect(m_rootItem);
    return files;
}

void ProjectTreeModel::beginBulkImport()
{
    beginResetModel();
    m_bulkImporting = true;
}

void ProjectTreeModel::endBulkImport()
{
    m_bulkImporting = false;
    endResetModel();
}

Qt::DropActions ProjectTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList ProjectTreeModel::mimeTypes() const
{
    return {QStringLiteral("application/x-rpgforge-treeitem")};
}

QMimeData *ProjectTreeModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty()) return nullptr;

    auto *mimeData = new QMimeData();
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);

    for (const QModelIndex &index : indexes) {
        if (!index.isValid()) continue;
        ProjectTreeItem *item = itemFromIndex(index);
        if (item) {
            // Serialize name + path as a unique identifier (safe across model resets)
            stream << item->name << item->path;
        }
    }

    mimeData->setData(QStringLiteral("application/x-rpgforge-treeitem"), encoded);
    return mimeData;
}

bool ProjectTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(row);
    Q_UNUSED(column);

    if (!data || action != Qt::MoveAction) return false;
    if (!data->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))) return false;

    ProjectTreeItem *targetItem = itemFromIndex(parent);
    if (!targetItem) return false;
    if (targetItem != m_rootItem && targetItem->type != ProjectTreeItem::Folder) return false;

    QByteArray encoded = data->data(QStringLiteral("application/x-rpgforge-treeitem"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        QString name, path;
        stream >> name >> path;

        ProjectTreeItem *draggedItem = findItemRecursive(path, m_rootItem);
        // For folders with empty paths, match by name too
        if (!draggedItem) {
            std::function<ProjectTreeItem*(ProjectTreeItem*)> findByName;
            findByName = [&](ProjectTreeItem *node) -> ProjectTreeItem* {
                if (node->name == name && node->path == path) return node;
                for (auto *child : node->children) {
                    if (auto *found = findByName(child)) return found;
                }
                return nullptr;
            };
            draggedItem = findByName(m_rootItem);
        }
        if (!draggedItem) return false;
        if (draggedItem == targetItem) return false;

        ProjectTreeItem *p = targetItem;
        while (p) {
            if (p == draggedItem) return false;
            p = p->parent;
        }
    }

    return true;
}

bool ProjectTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    Q_UNUSED(column);

    if (!data || action != Qt::MoveAction) return false;
    if (!data->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))) return false;

    ProjectTreeItem *targetItem = itemFromIndex(parent);
    if (!targetItem) return false;
    if (targetItem != m_rootItem && targetItem->type != ProjectTreeItem::Folder) return false;

    // Collect all valid items first, then move them
    QByteArray encoded = data->data(QStringLiteral("application/x-rpgforge-treeitem"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);

    QList<ProjectTreeItem*> itemsToMove;
    while (!stream.atEnd()) {
        QString name, path;
        stream >> name >> path;

        ProjectTreeItem *draggedItem = findItemRecursive(path, m_rootItem);
        if (!draggedItem) {
            std::function<ProjectTreeItem*(ProjectTreeItem*)> findByName;
            findByName = [&](ProjectTreeItem *node) -> ProjectTreeItem* {
                if (node->name == name && node->path == path) return node;
                for (auto *child : node->children) {
                    if (auto *found = findByName(child)) return found;
                }
                return nullptr;
            };
            draggedItem = findByName(m_rootItem);
        }
        if (!draggedItem || !draggedItem->parent) continue;

        // Verify no circular move
        bool circular = false;
        ProjectTreeItem *p = targetItem;
        while (p) {
            if (p == draggedItem) { circular = true; break; }
            p = p->parent;
        }
        if (!circular) {
            itemsToMove.append(draggedItem);
        }
    }

    if (itemsToMove.isEmpty()) return false;

    int insertRow = (row >= 0) ? row : targetItem->children.count();

    for (auto *draggedItem : itemsToMove) {
        if (!draggedItem->parent) continue;

        ProjectTreeItem *oldParent = draggedItem->parent;
        int oldRow = oldParent->children.indexOf(draggedItem);
        if (oldRow < 0) continue;

        if (oldParent == targetItem && oldRow < insertRow) {
            insertRow--;
        }

        if (moveItem(draggedItem, targetItem, insertRow)) {
            insertRow++;
        }
    }

    return true;
}
