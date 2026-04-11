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
#include "synopsisservice.h"
#include <QMimeData>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>
#include <KLocalizedString>
#include <QMutexLocker>

ProjectTreeModel::ProjectTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_rootItem = new ProjectTreeItem();
    m_rootItem->type = ProjectTreeItem::Folder;
    m_rootItem->name = QStringLiteral("Root");
}

ProjectTreeModel::~ProjectTreeModel()
{
    QMutexLocker locker(&m_treeMutex);
    delete m_rootItem;
    m_rootItem = nullptr;
}

void ProjectTreeModel::setProjectData(const QJsonObject &treeData)
{
    beginResetModel();
    {
        QMutexLocker locker(&m_treeMutex);
        delete m_rootItem;
        m_rootItem = loadItem(treeData, nullptr);
        if (!m_rootItem) {
            m_rootItem = new ProjectTreeItem();
            m_rootItem->type = ProjectTreeItem::Folder;
            m_rootItem->name = QStringLiteral("Root");
        }
    }
    endResetModel();
}

QJsonObject ProjectTreeModel::projectData() const
{
    QMutexLocker locker(&m_treeMutex);
    return saveItem(m_rootItem);
}

static QString categoryToString(ProjectTreeItem::Category category) {
    switch (category) {
        case ProjectTreeItem::Manuscript: return QStringLiteral("manuscript");
        case ProjectTreeItem::Research: return QStringLiteral("research");
        case ProjectTreeItem::Chapter: return QStringLiteral("chapter");
        case ProjectTreeItem::Scene: return QStringLiteral("scene");
        case ProjectTreeItem::Characters: return QStringLiteral("characters");
        case ProjectTreeItem::Places: return QStringLiteral("places");
        case ProjectTreeItem::Cultures: return QStringLiteral("cultures");
        case ProjectTreeItem::Stylesheet: return QStringLiteral("stylesheet");
        case ProjectTreeItem::Notes: return QStringLiteral("notes");
        default: return QStringLiteral("none");
    }
}

static ProjectTreeItem::Category stringToCategory(const QString &str) {
    if (str == QStringLiteral("manuscript")) return ProjectTreeItem::Manuscript;
    if (str == QStringLiteral("research")) return ProjectTreeItem::Research;
    if (str == QStringLiteral("chapter")) return ProjectTreeItem::Chapter;
    if (str == QStringLiteral("scene")) return ProjectTreeItem::Scene;
    if (str == QStringLiteral("characters")) return ProjectTreeItem::Characters;
    if (str == QStringLiteral("places")) return ProjectTreeItem::Places;
    if (str == QStringLiteral("cultures")) return ProjectTreeItem::Cultures;
    if (str == QStringLiteral("stylesheet")) return ProjectTreeItem::Stylesheet;
    if (str == QStringLiteral("notes")) return ProjectTreeItem::Notes;
    return ProjectTreeItem::None;
}

ProjectTreeItem* ProjectTreeModel::loadItem(const QJsonObject &obj, ProjectTreeItem *parent)
{
    auto *item = new ProjectTreeItem();
    item->parent = parent;
    item->type = obj.value(QStringLiteral("type")).toString() == QStringLiteral("file") ? ProjectTreeItem::File : ProjectTreeItem::Folder;
    item->category = stringToCategory(obj.value(QStringLiteral("category")).toString());
    item->name = obj.value(QStringLiteral("name")).toString();
    item->path = obj.value(QStringLiteral("path")).toString();
    item->synopsis = obj.value(QStringLiteral("synopsis")).toString();
    item->status = obj.value(QStringLiteral("status")).toString();
    item->transient = obj.value(QStringLiteral("transient")).toBool(false);

    QJsonArray children = obj.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &v : children) {
        item->children.append(loadItem(v.toObject(), item));
    }

    return item;
}

QJsonObject ProjectTreeModel::saveItem(ProjectTreeItem *item) const
{
    QJsonObject obj;
    if (!item) return obj;
    obj[QStringLiteral("type")] = item->type == ProjectTreeItem::File ? QStringLiteral("file") : QStringLiteral("folder");
    obj[QStringLiteral("category")] = categoryToString(item->category);
    obj[QStringLiteral("name")] = item->name;
    obj[QStringLiteral("path")] = item->path;
    obj[QStringLiteral("synopsis")] = item->synopsis;
    obj[QStringLiteral("status")] = item->status;
    obj[QStringLiteral("transient")] = item->transient;

    QJsonArray children;
    for (auto *child : item->children) {
        children.append(saveItem(child));
    }
    obj[QStringLiteral("children")] = children;

    return obj;
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

QModelIndex ProjectTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return QModelIndex();

    ProjectTreeItem *childItem = itemFromIndex(child);
    if (!childItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    ProjectTreeItem *parentItem = childItem->parent;

    if (parentItem == m_rootItem || !parentItem) return QModelIndex();

    ProjectTreeItem *grandParent = parentItem->parent;
    if (!grandParent) return QModelIndex();

    int row = grandParent->children.indexOf(parentItem);
    return createIndex(row, 0, parentItem);
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

    ProjectTreeItem *item = itemFromIndex(index);
    if (!item) return QVariant();

    QMutexLocker locker(&m_treeMutex);
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return item->name;
    } else if (role == Qt::DecorationRole) {
        if (item->type == ProjectTreeItem::Folder) {
            switch (item->category) {
                case ProjectTreeItem::Manuscript: return QIcon::fromTheme(QStringLiteral("document-edit"));
                case ProjectTreeItem::Research: return QIcon::fromTheme(QStringLiteral("search"));
                case ProjectTreeItem::Chapter: return QIcon::fromTheme(QStringLiteral("book-contents"));
                case ProjectTreeItem::Characters: return QIcon::fromTheme(QStringLiteral("user-identity"));
                case ProjectTreeItem::Places: return QIcon::fromTheme(QStringLiteral("applications-graphics"));
                case ProjectTreeItem::Cultures: return QIcon::fromTheme(QStringLiteral("view-list-details"));
                default: return QIcon::fromTheme(QStringLiteral("folder"));
            }
        } else {
            if (item->transient) return QIcon::fromTheme(QStringLiteral("document-history"));
            switch (item->category) {
                case ProjectTreeItem::Scene: return QIcon::fromTheme(QStringLiteral("document-edit-symbolic"));
                case ProjectTreeItem::Notes: return QIcon::fromTheme(QStringLiteral("note-sticky"));
                case ProjectTreeItem::Stylesheet: return QIcon::fromTheme(QStringLiteral("applications-graphics-symbolic"));
                default: {
                    QString suffix = QFileInfo(item->path).suffix().toLower();
                    if (suffix == QStringLiteral("pdf")) return QIcon::fromTheme(QStringLiteral("application-pdf"));
                    if (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
                        return QIcon::fromTheme(QStringLiteral("image-x-generic"));
                    return QIcon::fromTheme(QStringLiteral("text-markdown"));
                }
            }
        }
    } else if (role == TransientRole) {
        return item->transient;
    } else if (role == CategoryRole) {
        return static_cast<int>(item->category);
    } else if (role == SynopsisRole) {
        return item->synopsis;
    } else if (role == StatusRole) {
        return item->status;
    }
    return QVariant();
}

Qt::ItemFlags ProjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;

    ProjectTreeItem *item = itemFromIndex(index);
    if (!item) return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (item->type == ProjectTreeItem::Folder) {
        f |= Qt::ItemIsDropEnabled;
    }
    return f;
}

bool ProjectTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) return false;

    ProjectTreeItem *item = itemFromIndex(index);
    if (!item) return false;

    QMutexLocker locker(&m_treeMutex);
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

static bool isItemInTree(ProjectTreeItem *item, ProjectTreeItem *root) {
    if (!item || !root) return false;
    if (item == root) return true;
    for (auto *child : root->children) {
        if (isItemInTree(item, child)) return true;
    }
    return false;
}

ProjectTreeItem* ProjectTreeModel::itemFromIndex(const QModelIndex &index) const
{
    if (index.isValid()) {
        if (index.model() != this) return nullptr;
        auto *item = static_cast<ProjectTreeItem*>(index.internalPointer());
        // Mutex is needed to safely traverse/check tree
        QMutexLocker locker(&m_treeMutex);
        if (isItemInTree(item, m_rootItem)) return item;
        return nullptr;
    }
    return m_rootItem;
}

ProjectTreeItem* ProjectTreeModel::findItem(const QString &relativePath, ProjectTreeItem *root) const
{
    QMutexLocker locker(&m_treeMutex);
    if (!root) root = m_rootItem;
    if (!root) return nullptr;

    if (root->path == relativePath) return root;

    for (auto *child : root->children) {
        ProjectTreeItem *found = findItem(relativePath, child);
        if (found) return found;
    }
    return nullptr;
}

QModelIndex ProjectTreeModel::indexForItem(ProjectTreeItem *item) const
{
    if (!item || item == m_rootItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    ProjectTreeItem *parentItem = item->parent;
    if (!parentItem) return QModelIndex();
    int row = parentItem->children.indexOf(item);
    if (row == -1) return QModelIndex();
    return createIndex(row, 0, item);
}

void ProjectTreeModel::beginBulkImport()
{
    m_bulkImporting = true;
    beginResetModel();
}

void ProjectTreeModel::endBulkImport()
{
    m_bulkImporting = false;
    endResetModel();
}

QModelIndex ProjectTreeModel::addFolder(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();

    SynopsisService::instance().pause();
    if (!m_bulkImporting) beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::Folder;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    if (!m_bulkImporting) endInsertRows();
    SynopsisService::instance().resume();

    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addFile(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();

    SynopsisService::instance().pause();
    if (!m_bulkImporting) beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    if (!m_bulkImporting) endInsertRows();
    SynopsisService::instance().resume();

    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addTransientVersionLink(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) return QModelIndex();
    
    QMutexLocker locker(&m_treeMutex);
    int row = parentItem->children.count();

    SynopsisService::instance().pause();
    beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->transient = true;
    item->parent = parentItem;
    parentItem->children.append(item);
    endInsertRows();
    SynopsisService::instance().resume();

    return index(row, 0, parent);
}

void ProjectTreeModel::addFileWithSmartDiscovery(const QString &absolutePath, const QModelIndex &parent)
{
    QFileInfo fi(absolutePath);
    QString projectDir = ProjectManager::instance().projectPath();
    QString relativePath = QDir(projectDir).relativeFilePath(absolutePath);
    
    QString suffix = fi.suffix().toLower();
    
    static QStringList mediaSuffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), 
        QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
        QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"),
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov")
    };
    
    bool isMedia = mediaSuffixes.contains(suffix);
    QModelIndex targetParent = parent;
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    
    if (isMedia && (parentItem == m_rootItem || !targetParent.isValid())) {
        QMutexLocker locker(&m_treeMutex);
        ProjectTreeItem *mediaFolder = nullptr;
        for (auto *child : m_rootItem->children) {
            if (child->type == ProjectTreeItem::Folder && child->name.toLower() == QStringLiteral("media")) {
                mediaFolder = child;
                break;
            }
        }
        locker.unlock();
        
        if (!mediaFolder) {
            targetParent = addFolder(i18n("Media"), QStringLiteral("media"), QModelIndex());
        } else {
            targetParent = indexForItem(mediaFolder);
        }
    }
    
    ProjectTreeItem *pItem = itemFromIndex(targetParent);
    if (!pItem) return;
    
    {
        QMutexLocker locker(&m_treeMutex);
        for (auto *child : pItem->children) {
            if (child->type == ProjectTreeItem::File && child->path == relativePath) return;
        }
    }

    addFile(fi.completeBaseName(), relativePath, targetParent);
    
    static QStringList textSuffixes = {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("txt")};
    if (textSuffixes.contains(suffix)) {
        QFile file(absolutePath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            static QRegularExpression linkRegex(QStringLiteral("(?:\\[.*\\]|\\!.*\\])\\((.*)\\)"));
            auto it = linkRegex.globalMatch(content);
            while (it.hasNext()) {
                auto match = it.next();
                QString link = match.captured(1);
                if (link.contains(QLatin1Char('#'))) link = link.split(QLatin1Char('#')).first();
                if (link.contains(QLatin1Char('?'))) link = link.split(QLatin1Char('?')).first();
                
                QString absoluteLink = QDir(fi.absolutePath()).absoluteFilePath(link);
                if (QFile::exists(absoluteLink)) {
                    addFileWithSmartDiscovery(absoluteLink, parent);
                }
            }
        }
    }
}

bool ProjectTreeModel::removeItem(const QModelIndex &index)
{
    if (!index.isValid()) return false;
    ProjectTreeItem *item = itemFromIndex(index);
    if (!item || !item->parent) return false;
    ProjectTreeItem *parentItem = item->parent;
    int row = index.row();

    SynopsisService::instance().cancelRequest(item->path);
    SynopsisService::instance().pause();
    beginRemoveRows(index.parent(), row, row);
    {
        QMutexLocker locker(&m_treeMutex);
        parentItem->children.removeAt(row);
        delete item;
    }
    endRemoveRows();
    SynopsisService::instance().resume();
    return true;
}

bool ProjectTreeModel::moveItem(ProjectTreeItem *item, ProjectTreeItem *newParent, int newRow)
{
    if (!item || !newParent) return false;
    if (newParent->type == ProjectTreeItem::File) return false;

    ProjectTreeItem *oldParent = item->parent;
    if (!oldParent) return false;

    int oldRow = oldParent->children.indexOf(item);
    if (oldRow == -1) return false;

    beginMoveRows(indexForItem(oldParent), oldRow, oldRow, indexForItem(newParent), newRow);
    {
        QMutexLocker locker(&m_treeMutex);
        oldParent->children.removeAt(oldRow);
        item->parent = newParent;
        if (newRow > newParent->children.size()) newRow = newParent->children.size();
        newParent->children.insert(newRow, item);
    }
    endMoveRows();
    return true;
}

Qt::DropActions ProjectTreeModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QStringList ProjectTreeModel::mimeTypes() const
{
    return {QStringLiteral("application/x-rpgforge-treeitem")};
}

QMimeData* ProjectTreeModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mimeData = new QMimeData();
    QStringList paths;
    QList<QUrl> fileUrls;

    for (const QModelIndex &idx : indexes) {
        if (!idx.isValid()) continue;
        ProjectTreeItem *item = itemFromIndex(idx);
        if (!item) continue;
        
        QMutexLocker locker(&m_treeMutex);
        paths << item->path;

        if (item->type == ProjectTreeItem::File && ProjectManager::instance().isProjectOpen()) {
            QString absPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(item->path);
            if (QFile::exists(absPath))
                fileUrls << QUrl::fromLocalFile(absPath);
        }
    }

    mimeData->setData(QStringLiteral("application/x-rpgforge-treeitem"), paths.join(QLatin1Char('\n')).toUtf8());
    if (!fileUrls.isEmpty())
        mimeData->setUrls(fileUrls);

    return mimeData;
}

bool ProjectTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
    Q_UNUSED(column);
    if (action == Qt::IgnoreAction) return true;

    ProjectTreeItem *newParentItem = itemFromIndex(parent);
    if (!newParentItem) {
        QMutexLocker locker(&m_treeMutex);
        newParentItem = m_rootItem;
    }

    if (newParentItem->type == ProjectTreeItem::File) {
        newParentItem = newParentItem->parent;
    }

    if (!newParentItem) {
        QMutexLocker locker(&m_treeMutex);
        newParentItem = m_rootItem;
    }

    if (data->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))) {
        const QStringList paths = QString::fromUtf8(data->data(QStringLiteral("application/x-rpgforge-treeitem"))).split(QLatin1Char('\n'));
        for (const QString &path : paths) {
            ProjectTreeItem *item = findItem(path);
            if (item && item->parent != newParentItem) {
                int targetRow = (row == -1) ? newParentItem->children.count() : row;
                moveItem(item, newParentItem, targetRow);
            }
        }
        return true;
    }
    return false;
}
