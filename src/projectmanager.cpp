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
#include "projecttreemodel.h"
#include "variablemanager.h"
#include "projectkeys.h"

#include <KLocalizedString>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>
#include <QtConcurrent>
#include <QRegularExpression>

ProjectManager& ProjectManager::instance()
{
    static ProjectManager s_instance;
    return s_instance;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
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
}

void ProjectManager::migrate(QJsonObject &data)
{
    int version = data.value(QLatin1String(ProjectKeys::Version)).toInt(1);
    bool changed = false;

    // v1 -> v2: Nest flat margins
    if (version < 2) {
        QJsonObject margins;
        margins[QStringLiteral("left")] = data.value(QLatin1String(ProjectKeys::MarginLeft)).toDouble(20.0);
        margins[QStringLiteral("right")] = data.value(QLatin1String(ProjectKeys::MarginRight)).toDouble(20.0);
        margins[QStringLiteral("top")] = data.value(QLatin1String(ProjectKeys::MarginTop)).toDouble(20.0);
        margins[QStringLiteral("bottom")] = data.value(QLatin1String(ProjectKeys::MarginBottom)).toDouble(20.0);

        data[QLatin1String(ProjectKeys::Margins)] = margins;

        data.remove(QLatin1String(ProjectKeys::MarginLeft));
        data.remove(QLatin1String(ProjectKeys::MarginRight));
        data.remove(QLatin1String(ProjectKeys::MarginTop));
        data.remove(QLatin1String(ProjectKeys::MarginBottom));

        version = 2;
        changed = true;
    }

    if (changed) {
        data[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        qDebug() << "ProjectManager: Migrated project schema to version" << ProjectKeys::CurrentVersion;
    }
}

QString ProjectManager::projectName() const
{
    return m_data.value(QLatin1String(ProjectKeys::Name)).toString();
}

void ProjectManager::setProjectName(const QString &name)
{
    if (projectName() != name) {
        m_data[QLatin1String(ProjectKeys::Name)] = name;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::author() const
{
    return m_data.value(QLatin1String(ProjectKeys::Author)).toString();
}

void ProjectManager::setAuthor(const QString &author)
{
    if (this->author() != author) {
        m_data[QLatin1String(ProjectKeys::Author)] = author;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::pageSize() const { return m_data.value(QLatin1String(ProjectKeys::PageSize)).toString(); }
void ProjectManager::setPageSize(const QString &size) { m_data[QLatin1String(ProjectKeys::PageSize)] = size; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginLeft() const { 
    return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("left")).toDouble(20.0); 
}
void ProjectManager::setMarginLeft(double margin) { 
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("left")] = margin;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged(); 
}

double ProjectManager::marginRight() const { 
    return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("right")).toDouble(20.0); 
}
void ProjectManager::setMarginRight(double margin) { 
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("right")] = margin;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged(); 
}

double ProjectManager::marginTop() const { 
    return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("top")).toDouble(20.0); 
}
void ProjectManager::setMarginTop(double margin) { 
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("top")] = margin;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged(); 
}

double ProjectManager::marginBottom() const { 
    return m_data.value(QLatin1String(ProjectKeys::Margins)).toObject().value(QStringLiteral("bottom")).toDouble(20.0); 
}
void ProjectManager::setMarginBottom(double margin) { 
    QJsonObject margins = m_data.value(QLatin1String(ProjectKeys::Margins)).toObject();
    margins[QStringLiteral("bottom")] = margin;
    m_data[QLatin1String(ProjectKeys::Margins)] = margins;
    Q_EMIT projectSettingsChanged(); 
}

bool ProjectManager::showPageNumbers() const { return m_data.value(QLatin1String(ProjectKeys::ShowPageNumbers)).toBool(); }
void ProjectManager::setShowPageNumbers(bool show) { m_data[QLatin1String(ProjectKeys::ShowPageNumbers)] = show; Q_EMIT projectSettingsChanged(); }

QString ProjectManager::stylesheetPath() const { return m_data.value(QLatin1String(ProjectKeys::StylesheetPath)).toString(); }
void ProjectManager::setStylesheetPath(const QString &path) { m_data[QLatin1String(ProjectKeys::StylesheetPath)] = path; Q_EMIT projectSettingsChanged(); }

bool ProjectManager::autoSync() const { return m_data.value(QLatin1String(ProjectKeys::AutoSync)).toBool(); }
void ProjectManager::setAutoSync(bool enabled)
{
    if (autoSync() != enabled) {
        m_data[QLatin1String(ProjectKeys::AutoSync)] = enabled;
        Q_EMIT projectSettingsChanged();
    }
}

QJsonObject ProjectManager::tree() const
{
    return m_data.value(QLatin1String(ProjectKeys::Tree)).toObject();
}

void ProjectManager::setTree(const QJsonObject &tree)
{
    m_data[QLatin1String(ProjectKeys::Tree)] = tree;
    Q_EMIT treeChanged();
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

    Q_EMIT projectOpened();
    return true;
}

bool ProjectManager::createProject(const QString &dirPath, const QString &projectName)
{
    qDebug() << "ProjectManager: Creating project shell at" << dirPath;
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    m_projectFilePath = dir.absoluteFilePath(QStringLiteral("rpgforge.project"));
    loadDefaults();
    setProjectName(projectName);
    
    dir.mkdir(QStringLiteral("stylesheets"));
    QString stylePath = dir.absoluteFilePath(QStringLiteral("stylesheets/style.css"));
    if (!QFile::exists(stylePath)) {
        QFile styleFile(stylePath);
        if (styleFile.open(QIODevice::WriteOnly)) {
            styleFile.write(
                "/* RPG Forge Project Stylesheet */\n"
                "body { font-family: serif; line-height: 1.6; }\n"
            );
            styleFile.close();
        }
    }

    return saveProject();
    // Signal NOT emitted here. It will be emitted by openProject or after setupDefaultProject.
}

void ProjectManager::setupDefaultProject(const QString &dir, const QString &name)
{
    qDebug() << "ProjectManager: Populating default project structure...";
    ProjectTreeModel model;
    QDir projectDir(dir);
    
    QModelIndex rootIdx = model.addFolder(name, QString());

    projectDir.mkpath(QStringLiteral("manuscript"));
    QModelIndex manuscriptIdx = model.addFolder(i18n("Manuscript"), QStringLiteral("manuscript"), rootIdx);
    model.setData(manuscriptIdx, static_cast<int>(ProjectTreeItem::Manuscript), ProjectTreeModel::CategoryRole);
    
    projectDir.mkpath(QStringLiteral("research"));
    QModelIndex researchIdx = model.addFolder(i18n("Research"), QStringLiteral("research"), rootIdx);
    model.setData(researchIdx, static_cast<int>(ProjectTreeItem::Research), ProjectTreeModel::CategoryRole);

    // 3. Library (Auto-generated dossiers)
    projectDir.mkpath(QStringLiteral("library/Character Sketches"));
    QModelIndex libraryIdx = model.addFolder(i18n("Library"), QStringLiteral("library"), rootIdx);
    model.setData(libraryIdx, static_cast<int>(ProjectTreeItem::Library), ProjectTreeModel::CategoryRole);
    
    QModelIndex libCharsIdx = model.addFolder(i18n("Character Sketches"), QStringLiteral("library/Character Sketches"), libraryIdx);
    model.setData(libCharsIdx, static_cast<int>(ProjectTreeItem::Characters), ProjectTreeModel::CategoryRole);
    
    QString readmePath = QStringLiteral("research/README.md");
    QFile readmeFile(projectDir.absoluteFilePath(readmePath));
    if (readmeFile.open(QIODevice::WriteOnly)) {
        readmeFile.write("# Welcome to your RPG Forge Project\n\nHappy worldbuilding!");
        readmeFile.close();
    }
    model.addFile(i18n("Project Guide"), readmePath, researchIdx);

    setTree(model.projectData());
    saveProject();
    
    qDebug() << "ProjectManager: Default content ready. Emitting projectOpened.";
    Q_EMIT projectOpened();
}

void ProjectManager::closeProject()
{
    m_projectFilePath.clear();
    loadDefaults();
    Q_EMIT projectClosed();
}

bool ProjectManager::saveProject()
{
    if (m_projectFilePath.isEmpty()) return false;

    QFile file(m_projectFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    m_data[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
    QJsonDocument doc(m_data);
    file.write(doc.toJson());
    return true;
}

QString ProjectManager::stylesheetFolderPath() const
{
    if (!isProjectOpen()) return QString();
    return QDir(projectPath()).absoluteFilePath(QStringLiteral("stylesheets"));
}

QStringList ProjectManager::stylesheetPaths() const
{
    QStringList paths;
    QDir dir(stylesheetFolderPath());
    if (dir.exists()) {
        const auto files = dir.entryList({QStringLiteral("*.css")}, QDir::Files);
        for (const auto &file : files) {
            paths.append(dir.absoluteFilePath(file));
        }
    }
    return paths;
}

int ProjectManager::countWordsInTree(const QJsonObject &tree, const QString &projectPath) const
{
    int count = 0;
    if (tree.value(QLatin1String(ProjectKeys::Type)).toInt() == 1) { // File
        QString relPath = tree.value(QLatin1String(ProjectKeys::Path)).toString();
        QString fullPath = QDir(projectPath).absoluteFilePath(relPath);
        QFile f(fullPath);
        if (f.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f.readAll());
            count = VariableManager::stripMetadata(content).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        }
    }
    
    QJsonArray children = tree.value(QLatin1String(ProjectKeys::Children)).toArray();
    for (const auto &child : children) {
      count += countWordsInTree(child.toObject(), projectPath);
    }
    return count;
}

int ProjectManager::calculateTotalWordCount() const
{
    if (!isProjectOpen()) return 0;
    return countWordsInTree(tree(), projectPath());
}

void ProjectManager::triggerWordCountUpdate()
{
    if (!isProjectOpen()) return;

    QtConcurrent::run([this]() {
        int count = countWordsInTree(tree(), projectPath());
        Q_EMIT totalWordCountUpdated(count);
    });
}
