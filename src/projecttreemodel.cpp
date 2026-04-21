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
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>
#include <KLocalizedString>

// Shared on-disk resolver used by both ProjectManager::validateTree() and
// ProjectTreePanel's click handlers. See header for strategy documentation.
QString tryResolveOnDisk(const QDir &projectDir, const QString &relativePath)
{
    if (relativePath.isEmpty()) return QString();
    if (!projectDir.exists()) return QString();

    static const QStringList kExts = {
        QStringLiteral(""),        // path as-is, in case it's already a file
        QStringLiteral(".md"),
        QStringLiteral(".markdown"),
        QStringLiteral(".mkd"),
        QStringLiteral(".txt"),
        QStringLiteral(".rtf")
    };

    // Path variants: as-is, and with spaces translated to underscores
    // (Scrivener-imported projects often display underscored filenames as
    // spaces, while the on-disk file keeps the underscore).
    QStringList pathVariants{relativePath};
    if (relativePath.contains(QLatin1Char(' '))) {
        pathVariants.append(QString(relativePath).replace(QLatin1Char(' '), QLatin1Char('_')));
    }

    // Strategy 1: exact path + extension variants
    for (const QString &basePath : std::as_const(pathVariants)) {
        for (const QString &ext : kExts) {
            const QString candidate = basePath + ext;
            const QFileInfo fi(projectDir.absoluteFilePath(candidate));
            if (fi.exists() && fi.isFile()) return candidate;
        }
    }

    // Strategy 2: nested-leaf pattern, e.g. path "A/B" -> "A/B/B.md"
    for (const QString &basePath : std::as_const(pathVariants)) {
        const QString leaf = QFileInfo(basePath).fileName();
        if (leaf.isEmpty()) continue;
        for (const QString &ext : kExts) {
            if (ext.isEmpty()) continue;
            const QString candidate = basePath + QLatin1Char('/') + leaf + ext;
            const QFileInfo fi(projectDir.absoluteFilePath(candidate));
            if (fi.exists() && fi.isFile()) return candidate;
        }
    }

    // Strategy 3: parent-directory scan, space/underscore and case tolerant.
    auto normaliseForCompare = [](QString s) {
        s.replace(QLatin1Char('_'), QLatin1Char(' '));
        return s.toLower();
    };

    for (const QString &basePath : std::as_const(pathVariants)) {
        const QFileInfo expected(projectDir.absoluteFilePath(basePath));
        const QDir parent = expected.dir();
        if (!parent.exists()) continue;
        const QString leafNorm = normaliseForCompare(expected.fileName());
        const QFileInfoList entries = parent.entryInfoList(QDir::Files);
        for (const QFileInfo &entry : entries) {
            const QString stemNorm = normaliseForCompare(entry.completeBaseName());
            const QString fullNorm = normaliseForCompare(entry.fileName());
            if (stemNorm == leafNorm || fullNorm == leafNorm) {
                return projectDir.relativeFilePath(entry.absoluteFilePath());
            }
        }
    }

    return QString();
}

ProjectTreeItem::Category ProjectTreeModel::categoryForPath(const QString &relativePath)
{
    if (relativePath.isEmpty()) return ProjectTreeItem::None;

    // Split on both '/' and '\\' so Windows-style paths from older project files
    // are handled the same as Unix paths. Only the first segment matters.
    const QString firstSegment = relativePath.section(QRegularExpression(QStringLiteral("[/\\\\]")),
                                                     0, 0).toLower();
    if (firstSegment == QLatin1String("manuscript")) return ProjectTreeItem::Manuscript;
    if (firstSegment == QLatin1String("lorekeeper")) return ProjectTreeItem::LoreKeeper;
    if (firstSegment == QLatin1String("research"))   return ProjectTreeItem::Research;
    return ProjectTreeItem::None;
}

ProjectTreeItem::Category ProjectTreeModel::effectiveCategory(const ProjectTreeItem *item) const
{
    if (!item) return ProjectTreeItem::None;
    const ProjectTreeItem *cursor = item;
    while (cursor && cursor != m_rootItem) {
        if (cursor->category != ProjectTreeItem::None) return cursor->category;
        cursor = cursor->parent;
    }
    return ProjectTreeItem::None;
}

bool ProjectTreeModel::isAuthoritativeRoot(const ProjectTreeItem *item) const
{
    if (!item || item == m_rootItem) return false;
    if (item->type != ProjectTreeItem::Folder) return false;
    if (item->parent != m_rootItem) return false;
    return categoryForPath(item->path) != ProjectTreeItem::None;
}

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
        const QString newName = value.toString();
        if (newName.isEmpty() || newName == item->name) return false;

        // Re-entrant path: ProjectManager::renameItem() calls us after it
        // has already renamed on disk and is updating the in-memory name.
        // Just apply the name change and emit; PM handles the path cascade.
        if (m_inPmRename) {
            item->name = newName;
            Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
            return true;
        }

        // Top-level path (e.g. QTreeView inline F2 commit): route through
        // ProjectManager so the disk file is renamed, item->path cascades
        // to descendants, and the project file is persisted atomically.
        // PM::renameItem will call back into setData with m_inPmRename set.
        return ProjectManager::instance().renameItem(item->path, newName);
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

// Validate that `item` is still reachable from `root` WITHOUT dereferencing
// `item` until we've proven it's in the tree. Walking up from item->parent
// (the previous implementation) is UB when item is a dangling pointer — the
// very first dereference reads freed memory and can segfault, which is how
// we crashed during edit-commit repaints racing against background scanners.
// Walk down from root instead: if we reach item by traversing known-live
// children, the pointer is valid.
static bool isItemInTree(ProjectTreeItem *item, ProjectTreeItem *root) {
    if (!item || !root) return false;
    if (item == root) return true;
    for (ProjectTreeItem *child : root->children) {
        if (isItemInTree(item, child)) return true;
    }
    return false;
}

Qt::ItemFlags ProjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;

    ProjectTreeItem *item = static_cast<ProjectTreeItem*>(index.internalPointer());

    // Authoritative top-level folders (manuscript/, lorekeeper/, research/)
    // are protected: the user cannot rename, drag, or remove them. They can
    // still receive drops (items dragged into them) and be selected.
    if (isAuthoritativeRoot(item)) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    }

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;

    // LoreKeeper content is read-only (the service owns its dossiers).
    // Inherited category is the right check: a file under
    // lorekeeper/Characters/ should be read-only even if its own category
    // field is None.
    if (effectiveCategory(item) != ProjectTreeItem::LoreKeeper) {
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

namespace {

// Names that must NEVER appear in the tree regardless of nesting level.
// These are either RPG Forge's own side-files (rpgforge.project,
// .rpgforge-vectors.db) or third-party scaffolding that is never part of
// project content (.git, node_modules, .DS_Store). The list is applied
// after the "dot-prefix is hidden" rule so it's mostly a defensive backup
// for non-hidden offenders (e.g. rpgforge.project) — plus it documents the
// full skip policy in one place.
bool shouldSkipEntry(const QString &name)
{
    if (name.isEmpty()) return true;
    if (name == QLatin1String(".") || name == QLatin1String("..")) return true;

    // Hide every dotfile / dotdir. Covers .git, .rpgforge, .rpgforge-vectors.db,
    // .rpgforge-lorekeeper.lock, .DS_Store, .vscode, .idea, etc. in one go.
    if (name.startsWith(QLatin1Char('.'))) return true;

    // Non-hidden but still never part of the tree.
    static const QStringList kExplicitSkips = {
        QStringLiteral("rpgforge.project"),
        QStringLiteral("node_modules"),
        QStringLiteral("build"),
        QStringLiteral("__pycache__"),
        QStringLiteral("Thumbs.db"),
    };
    return kExplicitSkips.contains(name);
}

// Apply an orderHints list to a freshly-read directory listing. Items whose
// name appears in `order` come first, in that sequence; unknown names
// preserve their incoming alphanumeric order after the hinted tail. Any
// `order` entries that don't match an actual disk entry are silently
// dropped (the hint was stale — disk is authoritative for what exists).
QFileInfoList reorderByHints(const QFileInfoList &entries,
                             const QJsonArray &order)
{
    if (order.isEmpty() || entries.size() < 2) return entries;

    QHash<QString, QFileInfo> byName;
    byName.reserve(entries.size());
    for (const QFileInfo &fi : entries) byName.insert(fi.fileName(), fi);

    QFileInfoList out;
    out.reserve(entries.size());
    QSet<QString> used;
    for (const QJsonValue &v : order) {
        const QString name = v.toString();
        auto it = byName.constFind(name);
        if (it != byName.constEnd() && !used.contains(name)) {
            out.append(*it);
            used.insert(name);
        }
    }
    for (const QFileInfo &fi : entries) {
        if (!used.contains(fi.fileName())) out.append(fi);
    }
    return out;
}

} // anonymous namespace

void ProjectTreeModel::buildFromDisk(const QString &projectPath,
                                      const QJsonObject &nodeMetadata,
                                      const QJsonObject &orderHints)
{
    beginResetModel();
    {
        QMutexLocker locker(&m_treeMutex);
        delete m_rootItem;
        m_rootItem = new ProjectTreeItem();
        m_rootItem->name = i18n("Root");
        m_rootItem->type = ProjectTreeItem::Folder;
    }

    if (projectPath.isEmpty() || !QDir(projectPath).exists()) {
        endResetModel();
        return;
    }

    // Stamp a node with any persisted per-path metadata. nodeMetadata keys
    // are project-relative paths; absent or empty entries leave defaults.
    auto applyMetadata = [&nodeMetadata](ProjectTreeItem *item) {
        if (!item || item->path.isEmpty()) return;
        const QJsonValue v = nodeMetadata.value(item->path);
        if (!v.isObject()) return;
        const QJsonObject meta = v.toObject();
        if (meta.contains(QStringLiteral("synopsis"))) {
            item->synopsis = meta.value(QStringLiteral("synopsis")).toString();
        }
        if (meta.contains(QStringLiteral("status"))) {
            item->status = meta.value(QStringLiteral("status")).toString();
        }
        if (meta.contains(QStringLiteral("displayName"))) {
            const QString dn = meta.value(QStringLiteral("displayName")).toString();
            if (!dn.isEmpty()) item->name = dn;
        }
        if (meta.contains(QStringLiteral("categoryOverride"))) {
            const int c = meta.value(QStringLiteral("categoryOverride")).toInt(-1);
            if (c >= 0 && c <= static_cast<int>(ProjectTreeItem::Notes)) {
                item->category = static_cast<ProjectTreeItem::Category>(c);
            }
        }
    };

    const QDir projectDir(projectPath);

    std::function<void(ProjectTreeItem*, const QDir&, const QString&)> populate;
    populate = [&](ProjectTreeItem *parent, const QDir &dir, const QString &parentRel) {
        if (!parent) return;

        const QFileInfoList rawEntries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::IgnoreCase);

        // Filter skip list first so ordering hints don't waste slots.
        QFileInfoList filtered;
        filtered.reserve(rawEntries.size());
        for (const QFileInfo &fi : rawEntries) {
            if (shouldSkipEntry(fi.fileName())) continue;
            filtered.append(fi);
        }

        const QJsonArray order = orderHints.value(parentRel).toArray();
        const QFileInfoList ordered = reorderByHints(filtered, order);

        for (const QFileInfo &fi : ordered) {
            const QString name = fi.fileName();
            const QString rel  = parentRel.isEmpty()
                ? name
                : parentRel + QLatin1Char('/') + name;

            auto *child = new ProjectTreeItem();
            child->parent = parent;
            child->path = rel;

            if (fi.isDir()) {
                child->type = ProjectTreeItem::Folder;
                child->name = name;
                // Category defaults to the path-derived umbrella. A
                // nodeMetadata categoryOverride (applied below) wins.
                child->category = categoryForPath(rel);
                applyMetadata(child);

                // If no override AND no path-derived umbrella, inherit
                // from parent so nested research subfolders still carry
                // the Research category for routing.
                if (child->category == ProjectTreeItem::None && parent->category != ProjectTreeItem::None) {
                    child->category = parent->category;
                }

                parent->children.append(child);
                populate(child, QDir(fi.absoluteFilePath()), rel);
            } else {
                child->type = ProjectTreeItem::File;
                // Use complete base name for display (matches addFile
                // convention in setupDefaultProject), but keep the full
                // filename on disk via the path field.
                child->name = fi.completeBaseName().isEmpty()
                    ? name
                    : fi.completeBaseName();

                // File-level umbrella: inherit from parent's category so a
                // file under manuscript/ routes as Manuscript content, etc.
                if (parent->category != ProjectTreeItem::None) {
                    child->category = parent->category;
                }
                applyMetadata(child);
                parent->children.append(child);
            }
        }
    };

    {
        QMutexLocker locker(&m_treeMutex);
        populate(m_rootItem, projectDir, QString());
    }

    endResetModel();
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

    // Path is the source of truth for the three authoritative categories
    // (Manuscript / LoreKeeper / Research). If the path places this item
    // under manuscript/, lorekeeper/, or research/, and the JSON didn't
    // assign a more specific subcategory (Chapter, Scene, Characters,
    // Places, Cultures, ...) that implies the same umbrella, stamp the
    // umbrella category. User-assigned subcategories are preserved so
    // Chapter/Scene survive a round-trip.
    //
    // If the item has no category of its own AND its path doesn't fall
    // under an authoritative root, inherit the parent's category.
    auto isSubcategoryOf = [](ProjectTreeItem::Category child,
                              ProjectTreeItem::Category umbrella) {
        if (umbrella == ProjectTreeItem::Manuscript) {
            return child == ProjectTreeItem::Chapter
                || child == ProjectTreeItem::Scene;
        }
        if (umbrella == ProjectTreeItem::LoreKeeper) {
            return child == ProjectTreeItem::Characters
                || child == ProjectTreeItem::Places
                || child == ProjectTreeItem::Cultures;
        }
        // Research has no known subcategories today.
        return false;
    };

    const auto pathDerived = categoryForPath(item->path);
    if (pathDerived != ProjectTreeItem::None) {
        const bool keepExistingSubcategory =
            item->category != ProjectTreeItem::None
            && (item->category == pathDerived
                || isSubcategoryOf(item->category, pathDerived));
        if (!keepExistingSubcategory) {
            item->category = pathDerived;
        }
    } else if (item->category == ProjectTreeItem::None && parent) {
        item->category = parent->category;
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
    // Category is still written to JSON for backward compatibility with
    // external tools, but it is informational only: loadItem() recomputes
    // the category from path for the three authoritative roots and from
    // parent inheritance otherwise. The on-disk value is not trusted.
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
    if (index.isValid()) {
        if (index.model() != this) return nullptr;
        auto *item = static_cast<ProjectTreeItem*>(index.internalPointer());
        // Mutex guards against background scanners mutating the tree mid-traversal.
        QMutexLocker locker(&m_treeMutex);
        if (isItemInTree(item, m_rootItem)) return item;
        return nullptr;
    }
    return m_rootItem;
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
    // Drop-auto-remove guard: QAbstractItemView::startDrag() calls
    // clearOrRemove() after a successful Move drop. Our custom dropMimeData()
    // has already performed an atomic move via beginMoveRows/endMoveRows
    // which updates Qt's persistent indexes to follow the move. By the time
    // clearOrRemove() runs, the source-row persistent index points at the
    // item's NEW location, so the auto-remove would destroy the moved item
    // at its destination. Short-circuit while a drop is in flight.
    if (m_dropInProgress) {
        qDebug() << "ProjectTreeModel::removeRows: blocked by drop-in-progress guard"
                 << "(row=" << row << "count=" << count << ")";
        return false;
    }

    ProjectTreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem || row < 0 || row + count > parentItem->children.count()) {
        return false;
    }

    QMutexLocker locker(&m_treeMutex);

    // Refuse to remove authoritative top-level folders. Checked before any
    // mutation so a mixed-selection remove is atomic: if any child in the
    // range is protected, the entire operation is rejected. This keeps the
    // three authoritative roots present for the life of the project.
    for (int i = 0; i < count; ++i) {
        if (isAuthoritativeRoot(parentItem->children.at(row + i))) {
            qDebug() << "ProjectTreeModel: Refusing to remove authoritative folder"
                     << parentItem->children.at(row + i)->path;
            return false;
        }
    }

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

    {
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
    }

    // Re-derive the umbrella category for the moved subtree from its new
    // position. JSON metadata for routing (compile, RAG, etc.) follows the
    // logical move so a Chapter dragged into Research is routed as Research.
    propagateCategoryFromParent(item);

    return true;
}

namespace {
// Categorise the Category enum values into "umbrella" (top-level routing
// category that propagates down a subtree) vs "sub-category" (user-set
// tag that is independent of where the item lives in the tree).
bool isUmbrellaCategory(ProjectTreeItem::Category cat)
{
    return cat == ProjectTreeItem::Manuscript
        || cat == ProjectTreeItem::LoreKeeper
        || cat == ProjectTreeItem::Research;
}
}

void ProjectTreeModel::propagateCategoryFromParent(ProjectTreeItem *item)
{
    if (!item) return;

    // Special case: the item IS an authoritative top-level folder (would
    // only happen via a non-user code path; user moves are blocked). Its
    // own category is the umbrella; cascade to children but do not touch
    // the item itself.
    const bool itemIsAuthoritativeUmbrella =
        isUmbrellaCategory(item->category) && isAuthoritativeRoot(item);

    // Determine the umbrella to propagate. Start from the PARENT's chain
    // (not the item itself) so a moved item with a stale umbrella tag
    // (e.g. a file that was Manuscript, dragged under Research) gets its
    // own tag rewritten to match the new branch.
    ProjectTreeItem::Category umbrella = ProjectTreeItem::None;
    {
        ProjectTreeItem *anc = itemIsAuthoritativeUmbrella ? item : item->parent;
        while (anc) {
            if (isUmbrellaCategory(anc->category)) {
                umbrella = anc->category;
                break;
            }
            anc = anc->parent;
        }
    }

    // Moved out from under any umbrella (e.g. dragged to root outside the
    // three authoritative folders): leave categories alone so user intent
    // is not erased.
    if (umbrella == ProjectTreeItem::None) return;

    QList<ProjectTreeItem*> changedItems;

    std::function<void(ProjectTreeItem*)> walk = [&](ProjectTreeItem *node) {
        if (!node) return;

        // Skip the umbrella ancestor itself when it is the moved item — its
        // own category is the source of truth for the cascade.
        const bool skipSelf = (node == item) && itemIsAuthoritativeUmbrella;

        if (!skipSelf) {
            const bool wasUmbrella = isUmbrellaCategory(node->category);
            const bool wasNone     = node->category == ProjectTreeItem::None;
            // Overwrite stale umbrellas and unset categories. Sub-category
            // tags like Chapter / Scene / Characters survive.
            if ((wasUmbrella || wasNone) && node->category != umbrella) {
                node->category = umbrella;
                changedItems.append(node);
            }
        }

        for (auto *child : node->children) walk(child);
    };

    {
        QMutexLocker locker(&m_treeMutex);
        walk(item);
    }

    // Emit dataChanged so icons, status indicators, etc. refresh.
    for (auto *node : std::as_const(changedItems)) {
        const QModelIndex idx = indexForItem(node);
        if (idx.isValid()) Q_EMIT dataChanged(idx, idx, {CategoryRole, Qt::DecorationRole});
    }
}

void ProjectTreeModel::updatePathsAfterMoveOrRename(ProjectTreeItem *item,
                                                     const QString &oldPathPrefix,
                                                     const QString &newPathPrefix)
{
    if (!item) return;
    if (oldPathPrefix == newPathPrefix) return;

    QList<ProjectTreeItem*> changed;
    {
        QMutexLocker locker(&m_treeMutex);
        std::function<void(ProjectTreeItem*)> walk = [&](ProjectTreeItem *node) {
            if (!node) return;
            if (!node->path.isEmpty()) {
                if (node->path == oldPathPrefix) {
                    node->path = newPathPrefix;
                    changed.append(node);
                } else if (node->path.startsWith(oldPathPrefix + QLatin1Char('/'))) {
                    node->path = newPathPrefix + node->path.mid(oldPathPrefix.length());
                    changed.append(node);
                }
            }
            for (auto *child : node->children) walk(child);
        };
        walk(item);
    }

    for (auto *node : std::as_const(changed)) {
        const QModelIndex idx = indexForItem(node);
        if (idx.isValid()) Q_EMIT dataChanged(idx, idx, {PathRole, Qt::ToolTipRole});
    }
}

TreeNodeSnapshot ProjectTreeModel::snapshotFrom(const ProjectTreeItem *item) const
{
    TreeNodeSnapshot snap;
    if (!item) return snap;

    snap.name = item->name;
    snap.path = item->path;
    snap.synopsis = item->synopsis;
    snap.status = item->status;
    snap.type = static_cast<int>(item->type);
    snap.category = static_cast<int>(item->category);
    // diskPresent: populated from disk in Phase 6; keep permissive default.
    snap.diskPresent = true;
    // isTransient: the ProjectTreeItem `isTransient` flag is introduced in a
    // later phase; keep false until then.
    snap.isTransient = false;

    snap.children.reserve(item->children.size());
    for (const ProjectTreeItem *child : item->children) {
        snap.children.append(snapshotFrom(child));
    }
    return snap;
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

    // Accumulators for the Kate-friendly MIME types. text/plain gets a
    // markdown-link rendering of each dragged file so dropping into the
    // editor inserts something immediately useful; text/uri-list gives
    // Kate + any other app the absolute file path so it can open the
    // target if it wants to.
    QStringList plainLines;
    QList<QUrl> urls;

    const QString projectRoot = ProjectManager::instance().projectPath();

    for (const QModelIndex &index : indexes) {
        if (!index.isValid()) continue;
        ProjectTreeItem *item = itemFromIndex(index);
        if (!item) continue;

        // Serialize for our custom same-model DnD (move within tree).
        stream << item->name << item->path;

        // Build external-facing representations only for File items —
        // dragging a folder into the editor shouldn't paste the folder
        // path as a link.
        if (item->type != ProjectTreeItem::File) continue;
        if (projectRoot.isEmpty() || item->path.isEmpty()) continue;

        const QString abs = QDir(projectRoot).absoluteFilePath(item->path);
        urls.append(QUrl::fromLocalFile(abs));

        // Images render as embed-syntax image links; everything else
        // renders as a plain link. The relative path is project-root-
        // relative so the editor's markdown preview resolves it
        // correctly regardless of which file the author drops into.
        static const QStringList imageSuffixes = {
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("svg")
        };
        const QString suffix = QFileInfo(item->path).suffix().toLower();
        const QString label = item->name;
        if (imageSuffixes.contains(suffix)) {
            plainLines << QStringLiteral("![%1](%2)").arg(label, item->path);
        } else {
            plainLines << QStringLiteral("[%1](%2)").arg(label, item->path);
        }
    }

    mimeData->setData(QStringLiteral("application/x-rpgforge-treeitem"), encoded);
    if (!plainLines.isEmpty()) {
        mimeData->setText(plainLines.join(QLatin1Char('\n')));
    }
    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }
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

        // Authoritative top-level folders are not draggable at all.
        if (isAuthoritativeRoot(draggedItem)) return false;

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

    // Arm the drop-in-progress guard. QAbstractItemView will call
    // removeRows() on us synchronously after this function returns as part
    // of its post-Move clearOrRemove() pass; removeRows() checks this flag
    // and short-circuits while it's set. The flag is cleared on the next
    // event-loop tick (QueuedConnection) so legitimate post-drop removals
    // triggered by the user still work.
    m_dropInProgress = true;
    QMetaObject::invokeMethod(this, [this] {
        m_dropInProgress = false;
    }, Qt::QueuedConnection);

    ProjectTreeItem *targetItem = itemFromIndex(parent);
    if (!targetItem) return false;
    if (targetItem != m_rootItem && targetItem->type != ProjectTreeItem::Folder) return false;

    // Collect all valid items first, then move them through PM::moveItem
    // so the on-disk structure follows the drop atomically.
    QByteArray encoded = data->data(QStringLiteral("application/x-rpgforge-treeitem"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);

    struct PendingMove {
        QString path;
        ProjectTreeItem *item;
    };
    QList<PendingMove> itemsToMove;
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

        // Authoritative top-level folders are not moveable. Skip any such
        // item silently — the drop still proceeds for remaining items.
        if (isAuthoritativeRoot(draggedItem)) continue;

        // Verify no circular move
        bool circular = false;
        ProjectTreeItem *p = targetItem;
        while (p) {
            if (p == draggedItem) { circular = true; break; }
            p = p->parent;
        }
        if (!circular) {
            itemsToMove.append({draggedItem->path, draggedItem});
        }
    }

    if (itemsToMove.isEmpty()) return false;

    const QString targetParentPath = (targetItem == m_rootItem) ? QString() : targetItem->path;
    int insertRow = (row >= 0) ? row : targetItem->children.count();

    bool anyMoved = false;
    for (const auto &pm : itemsToMove) {
        if (!pm.item || !pm.item->parent) continue;

        ProjectTreeItem *oldParent = pm.item->parent;
        int oldRow = oldParent->children.indexOf(pm.item);
        if (oldRow < 0) continue;

        if (oldParent == targetItem && oldRow < insertRow) {
            insertRow--;
        }

        // Route through ProjectManager so the disk rename happens first and
        // rolls back on partial failure. Using the path captured before the
        // loop (the item pointer is still valid, but its path may be
        // rewritten by each successful move).
        if (ProjectManager::instance().moveItem(pm.path, targetParentPath, insertRow)) {
            insertRow++;
            anyMoved = true;
        }
    }

    return anyMoved;
}
