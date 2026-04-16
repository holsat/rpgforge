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

#include "projectmanager.h"
#include "projectkeys.h"
#include "projecttreemodel.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <KLocalizedString>
#include <QtConcurrent/QtConcurrent>

ProjectManager& ProjectManager::instance()
{
    static ProjectManager s_instance;
    return s_instance;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
    m_treeModel = new ProjectTreeModel(this);
    m_treeUpdateTimer = new QTimer(this);
    m_treeUpdateTimer->setSingleShot(true);
    m_treeUpdateTimer->setInterval(200); // Debounce updates
    connect(m_treeUpdateTimer, &QTimer::timeout, this, &ProjectManager::processTreeUpdateQueue);
    
    loadDefaults();
}

void ProjectManager::loadDefaults()
{
    m_data = QJsonObject();
    m_data[QLatin1String(ProjectKeys::Name)] = i18n("Untitled Project");
    m_data[QLatin1String(ProjectKeys::Author)] = i18n("Unknown Author");
    m_data[QLatin1String(ProjectKeys::PageSize)] = QStringLiteral("A4");
    
    QJsonObject margins;
    margins[QStringLiteral("left")] = 20.0;
    margins[QStringLiteral("right")] = 20.0;
    margins[QStringLiteral("top")] = 20.0;
    margins[QStringLiteral("bottom")] = 20.0;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;

    m_data[QLatin1String(ProjectKeys::ShowPageNumbers)] = true;
    m_data[QLatin1String(ProjectKeys::AutoSync)] = true;
    m_data[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;

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
    m_data[QLatin1String(ProjectKeys::LoreKeeperConfig)] = lkConfig;

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

bool ProjectManager::openProject(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    m_projectFilePath = filePath;
    m_data = doc.object();

    // Migrate old project files to current schema version
    migrate(m_data);

    // Sync authoritative model
    m_treeModel->setProjectData(m_data.value(QLatin1String(ProjectKeys::Tree)).toObject());

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
    m_data[QLatin1String(ProjectKeys::Name)] = projectName;
    
    return saveProject();
}

bool ProjectManager::saveProject()
{
    if (m_projectFilePath.isEmpty()) return false;

    QFile file(m_projectFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    // Always sync the model data back to m_data before saving
    m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
    m_data[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;

    QJsonDocument doc(m_data);
    file.write(doc.toJson());
    return true;
}

void ProjectManager::closeProject()
{
    m_projectFilePath.clear();
    loadDefaults();
    Q_EMIT projectClosed();
}

QString ProjectManager::projectName() const { return m_data.value(QLatin1String(ProjectKeys::Name)).toString(); }
void ProjectManager::setProjectName(const QString &name)
{
    m_data[QLatin1String(ProjectKeys::Name)] = name;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::author() const { return m_data.value(QLatin1String(ProjectKeys::Author)).toString(); }
void ProjectManager::setAuthor(const QString &author)
{
    m_data[QLatin1String(ProjectKeys::Author)] = author;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::pageSize() const { return m_data.value(QLatin1String(ProjectKeys::PageSize)).toString(); }
void ProjectManager::setPageSize(const QString &size)
{
    m_data[QLatin1String(ProjectKeys::PageSize)] = size;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginLeft() const { return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("left")).toDouble(20.0); }
void ProjectManager::setMarginLeft(double val)
{
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("left")] = val;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginRight() const { return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("right")).toDouble(20.0); }
void ProjectManager::setMarginRight(double val)
{
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("right")] = val;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginTop() const { return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("top")).toDouble(20.0); }
void ProjectManager::setMarginTop(double val)
{
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("top")] = val;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged();
}

double ProjectManager::marginBottom() const { return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("bottom")).toDouble(20.0); }
void ProjectManager::setMarginBottom(double val)
{
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("bottom")] = val;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::showPageNumbers() const { return m_data.value(QLatin1String(ProjectKeys::ShowPageNumbers)).toBool(true); }
void ProjectManager::setShowPageNumbers(bool show)
{
    m_data[QLatin1String(ProjectKeys::ShowPageNumbers)] = show;
    Q_EMIT projectSettingsChanged();
}

QString ProjectManager::stylesheetPath() const { return m_data.value(QLatin1String(ProjectKeys::StylesheetPath)).toString(); }
void ProjectManager::setStylesheetPath(const QString &path)
{
    m_data[QLatin1String(ProjectKeys::StylesheetPath)] = path;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::autoSync() const { return m_data.value(QLatin1String(ProjectKeys::AutoSync)).toBool(true); }
void ProjectManager::setAutoSync(bool enabled)
{
    m_data[QLatin1String(ProjectKeys::AutoSync)] = enabled;
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

QJsonObject ProjectManager::loreKeeperConfig() const { return m_data.value(QLatin1String(ProjectKeys::LoreKeeperConfig)).toObject(); }
void ProjectManager::setLoreKeeperConfig(const QJsonObject &config)
{
    m_data[QLatin1String(ProjectKeys::LoreKeeperConfig)] = config;
    Q_EMIT projectSettingsChanged();
}

bool ProjectManager::addFile(const QString &name, const QString &relativePath, const QString &parentPath)
{
    if (!isProjectOpen()) return false;

    // Validation: Ensure physical file exists
    QString absPath = QDir(projectPath()).absoluteFilePath(relativePath);
    if (!QFile::exists(absPath)) {
        qWarning() << "ProjectManager: Rejected addFile — Physical file does not exist:" << absPath;
        return false;
    }

    QModelIndex parentIdx;
    if (!parentPath.isEmpty()) {
        ProjectTreeItem *parentItem = m_treeModel->findItem(parentPath);
        if (parentItem) {
            parentIdx = m_treeModel->indexForItem(parentItem);
        }
    }

    if (m_treeModel->addFile(name, relativePath, parentIdx).isValid()) {
        m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
        return saveProject();
    }
    return false;
}

bool ProjectManager::addFolder(const QString &name, const QString &relativePath, const QString &parentPath)
{
    if (!isProjectOpen()) return false;

    QModelIndex parentIdx;
    if (!parentPath.isEmpty()) {
        ProjectTreeItem *parentItem = m_treeModel->findItem(parentPath);
        if (parentItem) {
            parentIdx = m_treeModel->indexForItem(parentItem);
        }
    }

    if (m_treeModel->addFolder(name, relativePath, parentIdx).isValid()) {
        m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
        return saveProject();
    }
    return false;
}

bool ProjectManager::moveItem(const QString &sourcePath, const QString &targetParentPath)
{
    if (!isProjectOpen()) return false;
    
    ProjectTreeItem *item = m_treeModel->findItem(sourcePath);
    ProjectTreeItem *newParent = m_treeModel->findItem(targetParentPath);
    
    if (item && newParent) {
        if (m_treeModel->moveItem(item, newParent, newParent->children.count())) {
            m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
            return saveProject();
        }
    }
    return false;
}

bool ProjectManager::removeItem(const QString &path)
{
    if (!isProjectOpen()) return false;
    
    ProjectTreeItem *item = m_treeModel->findItem(path);
    if (item) {
        QModelIndex idx = m_treeModel->indexForItem(item);
        if (m_treeModel->removeRow(idx.row(), idx.parent())) {
            m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
            return saveProject();
        }
    }
    return false;
}

bool ProjectManager::renameItem(const QString &path, const QString &newName)
{
    if (!isProjectOpen()) return false;
    
    ProjectTreeItem *item = m_treeModel->findItem(path);
    if (item) {
        QModelIndex idx = m_treeModel->indexForItem(item);
        if (m_treeModel->setData(idx, newName, Qt::EditRole)) {
            m_data[QLatin1String(ProjectKeys::Tree)] = m_treeModel->projectData();
            return saveProject();
        }
    }
    return false;
}

ProjectTreeItem* ProjectManager::findItem(const QString &path) const
{
    if (!isProjectOpen()) return nullptr;
    return m_treeModel->findItem(path);
}

void ProjectManager::migrate(QJsonObject &data)
{
    int version = data.value(QLatin1String(ProjectKeys::Version)).toInt(1);
    bool changed = false;

    // Standard migration logic
    if (version < 3) {
        version = 3;
        changed = true;
    }

    if (changed) {
        data[QLatin1String(ProjectKeys::Version)] = version;
        saveProject();
    }
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

    QtConcurrent::run([this]() {
        int count = countWordsInTree(m_treeModel->projectData(), projectPath());
        Q_EMIT totalWordCountUpdated(count);
    });
}

void ProjectManager::requestTreeUpdate(const QString &category, const QString &entityName, const QString &relativePath)
{
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
        
        ProjectTreeItem *loreRoot = nullptr;
        std::function<void(ProjectTreeItem*)> findLore = [&](ProjectTreeItem *item) {
            if (!item) return;
            if (item->category == ProjectTreeItem::LoreKeeper) {
                loreRoot = item;
                return;
            }
            for (auto *c : item->children) {
                if (loreRoot) break;
                findLore(c);
            }
        };
        findLore(m_treeModel->rootItem());
        
        if (loreRoot) {
            ProjectTreeItem *catFolder = nullptr;
            for (auto *c : loreRoot->children) {
                if (c->name == req.category) {
                    catFolder = c;
                    break;
                }
            }
            
            if (!catFolder) {
                m_treeModel->addFolder(req.category, QStringLiteral("lorekeeper/") + req.category, m_treeModel->indexForItem(loreRoot));
                catFolder = loreRoot->children.last();
            }
            
            bool found = false;
            for (auto *c : catFolder->children) {
                if (c->path == req.relativePath) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                m_treeModel->addFile(req.entityName, req.relativePath, m_treeModel->indexForItem(catFolder));
                changed = true;
            }
        }
    }
    
    if (changed) {
        saveProject();
        Q_EMIT treeChanged();
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
