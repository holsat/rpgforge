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
#include <QMimeData>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>

ProjectTreeModel::ProjectTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_rootItem = new ProjectTreeItem();
    m_rootItem->type = ProjectTreeItem::Folder;
    m_rootItem->name = QStringLiteral("Root");
}

ProjectTreeModel::~ProjectTreeModel()
{
    delete m_rootItem;
}

void ProjectTreeModel::setProjectData(const QJsonObject &treeData)
{
    beginResetModel();
    delete m_rootItem;
    m_rootItem = loadItem(treeData, nullptr);
    if (!m_rootItem) {
        m_rootItem = new ProjectTreeItem();
        m_rootItem->type = ProjectTreeItem::Folder;
        m_rootItem->name = QStringLiteral("Root");
    }
    endResetModel();
}

QJsonObject ProjectTreeModel::projectData() const
{
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
    for (const auto &childVal : children) {
        item->children.append(loadItem(childVal.toObject(), item));
    }
    
    return item;
}

QJsonObject ProjectTreeModel::saveItem(ProjectTreeItem *item) const
{
    QJsonObject obj;
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
    ProjectTreeItem *childItem = parentItem->children.value(row);
    if (childItem) return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex ProjectTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return QModelIndex();

    ProjectTreeItem *childItem = itemFromIndex(child);
    ProjectTreeItem *parentItem = childItem->parent;

    if (parentItem == m_rootItem || !parentItem) return QModelIndex();

    // Find the row of the parentItem in ITS parent
    ProjectTreeItem *grandParent = parentItem->parent;
    if (!grandParent) return QModelIndex();
    
    int row = grandParent->children.indexOf(parentItem);
    return createIndex(row, 0, parentItem);
}

int ProjectTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    ProjectTreeItem *parentItem = itemFromIndex(parent);
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
                case ProjectTreeItem::Stylesheet: return QIcon::fromTheme(QStringLiteral("applications-graphics-symbolic"));
                case ProjectTreeItem::Notes: return QIcon::fromTheme(QStringLiteral("note-sticky"));
                default: return QIcon::fromTheme(QStringLiteral("folder"));
            }
        } else {
            if (item->transient) return QIcon::fromTheme(QStringLiteral("document-history"));
            QString suffix = QFileInfo(item->path).suffix().toLower();
            if (suffix == QLatin1String("pdf")) return QIcon::fromTheme(QStringLiteral("application-pdf"));
            if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")) return QIcon::fromTheme(QStringLiteral("image-x-generic"));
            
            switch (item->category) {
                case ProjectTreeItem::Chapter: return QIcon::fromTheme(QStringLiteral("document-export"));
                case ProjectTreeItem::Scene: return QIcon::fromTheme(QStringLiteral("document-edit-symbolic"));
                case ProjectTreeItem::Characters: return QIcon::fromTheme(QStringLiteral("user-identity-symbolic"));
                case ProjectTreeItem::Places: return QIcon::fromTheme(QStringLiteral("applications-graphics-symbolic"));
                case ProjectTreeItem::Cultures: return QIcon::fromTheme(QStringLiteral("view-list-details-symbolic"));
                case ProjectTreeItem::Notes: return QIcon::fromTheme(QStringLiteral("note-sticky-symbolic"));
                default: return QIcon::fromTheme(QStringLiteral("text-x-markdown"));
            }
        }
    } else if (role == TransientRole) {
        return item->transient;
    }
    return QVariant();
}

Qt::ItemFlags ProjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (itemFromIndex(index)->type == ProjectTreeItem::Folder) {
        f |= Qt::ItemIsDropEnabled;
    }
    return f;
}

bool ProjectTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) return false;
    
    ProjectTreeItem *item = itemFromIndex(index);
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

ProjectTreeItem* ProjectTreeModel::itemFromIndex(const QModelIndex &index) const
{
    if (index.isValid()) {
        return static_cast<ProjectTreeItem*>(index.internalPointer());
    }
    return m_rootItem;
}

ProjectTreeItem* ProjectTreeModel::findItem(const QString &relativePath, ProjectTreeItem *root) const
{
    if (!root) root = m_rootItem;

    // Check root and children
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
    ProjectTreeItem *parentItem = item->parent;
    if (!parentItem) return QModelIndex();
    int row = parentItem->children.indexOf(item);
    if (row == -1) return QModelIndex();
    return createIndex(row, 0, item);
}

QModelIndex ProjectTreeModel::addFolder(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    int row = parentItem->children.count();
    
    beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::Folder;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    endInsertRows();
    
    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addFile(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    int row = parentItem->children.count();
    
    beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->parent = parentItem;
    parentItem->children.append(item);
    endInsertRows();
    
    return index(row, 0, parent);
}

QModelIndex ProjectTreeModel::addTransientVersionLink(const QString &name, const QString &path, const QModelIndex &parent)
{
    ProjectTreeItem *parentItem = itemFromIndex(parent);
    int row = parentItem->children.count();
    
    beginInsertRows(parent, row, row);
    auto *item = new ProjectTreeItem();
    item->type = ProjectTreeItem::File;
    item->name = name;
    item->path = path;
    item->transient = true;
    item->parent = parentItem;
    parentItem->children.append(item);
    endInsertRows();
    
    return index(row, 0, parent);
}

void ProjectTreeModel::addFileWithSmartDiscovery(const QString &absolutePath, const QModelIndex &parent)
{
    QFileInfo fi(absolutePath);
    QString projectDir = ProjectManager::instance().projectPath();
    QString relativePath = QDir(projectDir).relativeFilePath(absolutePath);
    
    static QStringList textSuffixes = {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("txt")};
    bool isText = textSuffixes.contains(fi.suffix().toLower());
    
    QModelIndex targetParent = parent;
    
    if (!isText) {
        // Look for a "Media" folder
        ProjectTreeItem *parentItem = itemFromIndex(parent);
        ProjectTreeItem *mediaFolder = nullptr;
        for (auto *child : parentItem->children) {
            if (child->type == ProjectTreeItem::Folder && child->name.toLower() == QStringLiteral("media")) {
                mediaFolder = child;
                break;
            }
        }
        
        if (!mediaFolder) {
            targetParent = addFolder(QStringLiteral("Media"), QStringLiteral("media"), parent);
        } else {
            targetParent = index(parentItem->children.indexOf(mediaFolder), 0, parent);
        }
    }
    
    // Check if already in project to avoid duplicates
    ProjectTreeItem *pItem = itemFromIndex(targetParent);
    for (auto *child : pItem->children) {
        if (child->type == ProjectTreeItem::File && child->path == relativePath) return;
    }

    addFile(fi.completeBaseName(), relativePath, targetParent);
    
    // If it's a markdown file, scan for links
    if (isText) {
        QFile file(absolutePath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            // Scan for [text](link) and ![](link)
            static QRegularExpression linkRegex(QStringLiteral("(?:\\[.*\\]|\\!.*\\])\\((.*)\\)"));
            auto it = linkRegex.globalMatch(content);
            while (it.hasNext()) {
                auto match = it.next();
                QString link = match.captured(1);
                // Simple cleanup if link has queries or fragments
                if (link.contains(QLatin1Char('#'))) link = link.split(QLatin1Char('#')).first();
                if (link.contains(QLatin1Char('?'))) link = link.split(QLatin1Char('?')).first();
                
                // Resolve relative to the file being added
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
    ProjectTreeItem *parentItem = item->parent;
    int row = index.row();

    beginRemoveRows(index.parent(), row, row);
    parentItem->children.removeAt(row);
    delete item;
    endRemoveRows();
    return true;
}

bool ProjectTreeModel::moveItem(ProjectTreeItem *item, ProjectTreeItem *newParent, int newRow)
{
    if (!item || !newParent) return false;
    if (newParent->type == ProjectTreeItem::File) return false;

    ProjectTreeItem *oldParent = item->parent;
    if (!oldParent) return false; // Root cannot be moved

    int oldRow = oldParent->children.indexOf(item);
    if (oldRow == -1) return false;

    // Check circularity
    ProjectTreeItem *p = newParent;
    while (p) {
        if (p == item) return false;
        p = p->parent;
    }

    if (newRow == -1) newRow = newParent->children.count();

    if (oldParent == newParent) {
        if (newRow == oldRow || newRow == oldRow + 1) return true;
    }

    QModelIndex oldParentIndex = indexForItem(oldParent);
    QModelIndex newParentIndex = indexForItem(newParent);

    if (beginMoveRows(oldParentIndex, oldRow, oldRow, newParentIndex, newRow)) {
        oldParent->children.removeAt(oldRow);
        if (oldParent == newParent && newRow > oldRow) newRow--;
        newParent->children.insert(newRow, item);
        item->parent = newParent;
        endMoveRows();
        return true;
    }

    return false;
}

Qt::DropActions ProjectTreeModel::supportedDropActions() const { return Qt::MoveAction | Qt::CopyAction | Qt::LinkAction; }
QStringList ProjectTreeModel::mimeTypes() const { return {QStringLiteral("application/x-rpgforge-treeitem"), QStringLiteral("text/uri-list")}; }

QMimeData* ProjectTreeModel::mimeData(const QModelIndexList &indexes) const {
    if (indexes.isEmpty()) return nullptr;
    auto *mimeData = new QMimeData();

    QByteArray encodedData;
    QList<QUrl> fileUrls;

    for (const QModelIndex &idx : indexes) {
        if (!idx.isValid()) continue;
        ProjectTreeItem *item = itemFromIndex(idx);
        encodedData.append(reinterpret_cast<const char*>(&item), sizeof(item));

        // For file items, also include an absolute URI so the editor can receive drops
        if (item->type == ProjectTreeItem::File && ProjectManager::instance().isProjectOpen()) {
            QString absPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(item->path);
            if (QFile::exists(absPath))
                fileUrls << QUrl::fromLocalFile(absPath);
        }
    }

    mimeData->setData(QStringLiteral("application/x-rpgforge-treeitem"), encodedData);
    if (!fileUrls.isEmpty())
        mimeData->setUrls(fileUrls);

    return mimeData;
}

bool ProjectTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
    Q_UNUSED(column);
    if (action == Qt::IgnoreAction) return true;

    ProjectTreeItem *newParentItem = itemFromIndex(parent);
    
    // If dropped ON a file, try to put it in the file's parent folder
    if (newParentItem->type == ProjectTreeItem::File) {
        newParentItem = newParentItem->parent;
    }

    if (!newParentItem) newParentItem = m_rootItem;

    if (data->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))) {
        QByteArray encodedData = data->data(QStringLiteral("application/x-rpgforge-treeitem"));
        const int count = encodedData.size() / static_cast<int>(sizeof(ProjectTreeItem*));
        if (count == 0) return false;

        int insertRow = row;
        if (insertRow == -1) insertRow = newParentItem->children.count();

        bool anyMoved = false;
        for (int i = 0; i < count; i++) {
            ProjectTreeItem *dragItem = *reinterpret_cast<ProjectTreeItem**>(
                encodedData.data() + i * static_cast<int>(sizeof(ProjectTreeItem*)));

            if (!dragItem) continue;

            if (moveItem(dragItem, newParentItem, insertRow)) {
                anyMoved = true;
                // Since moveItem handles internal state, and we want to keep them together:
                insertRow = newParentItem->children.indexOf(dragItem) + 1;
            }
        }
        return anyMoved;
    } else if (data->hasUrls()) {
        QModelIndex targetIndex = indexForItem(newParentItem);
        for (const QUrl &url : data->urls()) {
            if (url.isLocalFile()) {
                addFileWithSmartDiscovery(url.toLocalFile(), targetIndex);
            }
        }
        return true;
    }

    return false;
}
