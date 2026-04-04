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

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QDebug>

ProjectManager& ProjectManager::instance()
{
    static ProjectManager inst;
    return inst;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
    loadDefaults();
}

void ProjectManager::loadDefaults()
{
    m_data = QJsonObject();
    m_data[QStringLiteral("name")] = QStringLiteral("Untitled Project");
    m_data[QStringLiteral("author")] = QString();
    
    QJsonObject pdf;
    pdf[QStringLiteral("pageSize")] = QStringLiteral("A4");
    pdf[QStringLiteral("marginLeft")] = 20.0;
    pdf[QStringLiteral("marginRight")] = 20.0;
    pdf[QStringLiteral("marginTop")] = 20.0;
    pdf[QStringLiteral("marginBottom")] = 20.0;
    pdf[QStringLiteral("showPageNumbers")] = true;
    m_data[QStringLiteral("pdf")] = pdf;

    m_data[QStringLiteral("stylesheet")] = QStringLiteral("stylesheets/style.css");
    m_data[QStringLiteral("autoSync")] = true; // Enabled by default as per requirement
    m_data[QStringLiteral("tree")] = QJsonObject();
    m_data[QStringLiteral("version")] = 1;
}

bool ProjectManager::autoSync() const
{
    return m_data.value(QStringLiteral("autoSync")).toBool(true);
}

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
    // Don't emit projectSettingsChanged for every tree change to avoid spam
    // The panel will manage its own state and call saveProject()
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
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    m_projectFilePath = QFileInfo(filePath).absoluteFilePath();
    m_data = doc.object();
    
    Q_EMIT projectOpened();
    return true;
}

bool ProjectManager::createProject(const QString &dirPath, const QString &projectName)
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    m_projectFilePath = dir.absoluteFilePath(QStringLiteral("rpgforge.project"));
    loadDefaults();
    setProjectName(projectName);
    
    // Create stylesheets/ directory and default style.css
    dir.mkdir(QStringLiteral("stylesheets"));
    QString stylePath = dir.absoluteFilePath(QStringLiteral("stylesheets/style.css"));
    if (!QFile::exists(stylePath)) {
        QFile styleFile(stylePath);
        if (styleFile.open(QIODevice::WriteOnly)) {
            styleFile.write(
                "/* RPG Forge Project Stylesheet */\n"
                "/* Use this file to control the look of your Preview and PDF Export. */\n\n"
                "/* Example: Professional Book Font */\n"
                "body {\n"
                "    font-family: 'Crimson Text', 'Georgia', serif;\n"
                "    line-height: 1.6;\n"
                "}\n\n"
                "/* Example: PDF Pagination Settings */\n"
                "@page {\n"
                "    size: A4;\n"
                "    margin: 20mm;\n"
                "    /* Page numbers are handled automatically by RPG Forge, */\n"
                "    /* but you can add custom headers/footers here using CSS Paged Media. */\n"
                "}\n\n"
                "/* Example: Chapter Headers starting on new pages */\n"
                "h1 {\n"
                "    break-before: page;\n"
                "    color: #2c3e50;\n"
                "    text-align: center;\n"
                "    margin-top: 50px;\n"
                "}\n"
            );
            styleFile.close();
        }
    }

    bool saved = saveProject();
    if (saved) {
        Q_EMIT projectOpened();
    }
    return saved;
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

    QJsonDocument doc(m_data);
    file.write(doc.toJson());
    return true;
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

QString ProjectManager::pageSize() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("pageSize")).toString();
}

void ProjectManager::setPageSize(const QString &size)
{
    if (pageSize() != size) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("pageSize")] = size;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

double ProjectManager::marginLeft() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("marginLeft")).toDouble();
}

void ProjectManager::setMarginLeft(double margin)
{
    if (marginLeft() != margin) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("marginLeft")] = margin;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

double ProjectManager::marginRight() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("marginRight")).toDouble();
}

void ProjectManager::setMarginRight(double margin)
{
    if (marginRight() != margin) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("marginRight")] = margin;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

double ProjectManager::marginTop() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("marginTop")).toDouble();
}

void ProjectManager::setMarginTop(double margin)
{
    if (marginTop() != margin) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("marginTop")] = margin;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

double ProjectManager::marginBottom() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("marginBottom")).toDouble();
}

void ProjectManager::setMarginBottom(double margin)
{
    if (marginBottom() != margin) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("marginBottom")] = margin;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

bool ProjectManager::showPageNumbers() const
{
    return m_data.value(QStringLiteral("pdf")).toObject().value(QStringLiteral("showPageNumbers")).toBool();
}

void ProjectManager::setShowPageNumbers(bool show)
{
    if (showPageNumbers() != show) {
        QJsonObject pdf = m_data.value(QStringLiteral("pdf")).toObject();
        pdf[QStringLiteral("showPageNumbers")] = show;
        m_data[QStringLiteral("pdf")] = pdf;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::stylesheetPath() const
{
    return m_data.value(QStringLiteral("stylesheet")).toString();
}

void ProjectManager::setStylesheetPath(const QString &path)
{
    if (stylesheetPath() != path) {
        m_data[QStringLiteral("stylesheet")] = path;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::stylesheetFolderPath() const
{
    if (!isProjectOpen()) return {};
    return QDir(projectPath()).absoluteFilePath(QStringLiteral("stylesheets"));
}

QStringList ProjectManager::stylesheetPaths() const
{
    if (!isProjectOpen()) return {};

    QDir folder(stylesheetFolderPath());
    if (folder.exists()) {
        QStringList result;
        for (const QString &entry : folder.entryList({QStringLiteral("*.css")}, QDir::Files, QDir::Name)) {
            result << folder.absoluteFilePath(entry);
        }
        return result;
    }

    // Fallback: legacy single stylesheet path
    QString single = stylesheetPath();
    if (!single.isEmpty()) {
        QString full = QDir(projectPath()).absoluteFilePath(single);
        if (QFile::exists(full)) return { full };
    }
    return {};
}

#include <QRegularExpression>
#include "variablemanager.h"

static int countWordsInFile(const QString &fullPath) {
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    QString text = QString::fromUtf8(file.readAll());
    QString content = VariableManager::stripMetadata(text);
    return content.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).count();
}

static int countWordsInTree(const QJsonObject &item, const QString &projectPath) {
    int count = 0;
    if (item.value(QStringLiteral("type")).toString() == QStringLiteral("file")) {
        QString relPath = item.value(QStringLiteral("path")).toString();
        count += countWordsInFile(QDir(projectPath).absoluteFilePath(relPath));
    }
    
    QJsonArray children = item.value(QStringLiteral("children")).toArray();
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

