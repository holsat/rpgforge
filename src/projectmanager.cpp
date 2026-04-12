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
    m_data[QStringLiteral("name")] = i18n("Untitled Project");
    m_data[QStringLiteral("author")] = i18n("Unknown Author");
    m_data[QStringLiteral("pageSize")] = QStringLiteral("A4");
    m_data[QStringLiteral("marginLeft")] = 20.0;
    m_data[QStringLiteral("marginRight")] = 20.0;
    m_data[QStringLiteral("marginTop")] = 20.0;
    m_data[QStringLiteral("marginBottom")] = 20.0;
    m_data[QStringLiteral("showPageNumbers")] = true;
    m_data[QStringLiteral("autoSync")] = true;
}

QString ProjectManager::projectName() const
{
    return m_data.value(QStringLiteral("name")).toString();
}

void ProjectManager::setProjectName(const QString &name)
{
    if (projectName() != name) {
        m_data[QStringLiteral("name")] = name;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::author() const
{
    return m_data.value(QStringLiteral("author")).toString();
}

void ProjectManager::setAuthor(const QString &author)
{
    if (this->author() != author) {
        m_data[QStringLiteral("author")] = author;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::pageSize() const { return m_data.value(QStringLiteral("pageSize")).toString(); }
void ProjectManager::setPageSize(const QString &size) { m_data[QStringLiteral("pageSize")] = size; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginLeft() const { return m_data.value(QStringLiteral("marginLeft")).toDouble(); }
void ProjectManager::setMarginLeft(double margin) { m_data[QStringLiteral("marginLeft")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginRight() const { return m_data.value(QStringLiteral("marginRight")).toDouble(); }
void ProjectManager::setMarginRight(double margin) { m_data[QStringLiteral("marginRight")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginTop() const { return m_data.value(QStringLiteral("marginTop")).toDouble(); }
void ProjectManager::setMarginTop(double margin) { m_data[QStringLiteral("marginTop")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginBottom() const { return m_data.value(QStringLiteral("marginBottom")).toDouble(); }
void ProjectManager::setMarginBottom(double margin) { m_data[QStringLiteral("marginBottom")] = margin; Q_EMIT projectSettingsChanged(); }

bool ProjectManager::showPageNumbers() const { return m_data.value(QStringLiteral("showPageNumbers")).toBool(); }
void ProjectManager::setShowPageNumbers(bool show) { m_data[QStringLiteral("showPageNumbers")] = show; Q_EMIT projectSettingsChanged(); }

QString ProjectManager::stylesheetPath() const { return m_data.value(QStringLiteral("stylesheetPath")).toString(); }
void ProjectManager::setStylesheetPath(const QString &path) { m_data[QStringLiteral("stylesheetPath")] = path; Q_EMIT projectSettingsChanged(); }

bool ProjectManager::autoSync() const { return m_data.value(QStringLiteral("autoSync")).toBool(); }
void ProjectManager::setAutoSync(bool enabled)
{
    if (autoSync() != enabled) {
        m_data[QStringLiteral("autoSync")] = enabled;
        Q_EMIT projectSettingsChanged();
    }
}

QJsonObject ProjectManager::tree() const
{
    return m_data.value(QStringLiteral("tree")).toObject();
}

void ProjectManager::setTree(const QJsonObject &tree)
{
    m_data[QStringLiteral("tree")] = tree;
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

    const int fileVersion = m_data.value(QStringLiteral("version")).toInt(0);
    if (fileVersion > 1) {
        qWarning() << "rpgforge.project was written by a newer version of RPG Forge (schema version"
                   << fileVersion << "). Some data may not load correctly.";
    }

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

    m_data[QStringLiteral("version")] = 1;
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
    if (tree.value(QStringLiteral("type")).toInt() == 1) { // File
        QString relPath = tree.value(QStringLiteral("path")).toString();
        QString fullPath = QDir(projectPath).absoluteFilePath(relPath);
        QFile f(fullPath);
        if (f.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f.readAll());
            count = VariableManager::stripMetadata(content).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        }
    }
    
    QJsonArray children = tree.value(QStringLiteral("children")).toArray();
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
