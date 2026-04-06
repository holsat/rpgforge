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

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>

/**
 * @brief Manages the RPG Forge project structure and settings.
 * 
 * A project is a directory containing an 'rpgforge.project' JSON file.
 */
class ProjectManager : public QObject
{
    Q_OBJECT

public:
    static ProjectManager& instance();

    bool isProjectOpen() const { return !m_projectFilePath.isEmpty(); }
    QString projectPath() const;
    QString projectFilePath() const { return m_projectFilePath; }

    bool openProject(const QString &filePath);
    bool createProject(const QString &dirPath, const QString &projectName);
    void setupDefaultProject(const QString &dirPath, const QString &projectName);
    void closeProject();
    bool saveProject();

    // Project Metadata
    QString projectName() const;
    void setProjectName(const QString &name);
    
    QString author() const;
    void setAuthor(const QString &author);

    // PDF Settings
    QString pageSize() const;
    void setPageSize(const QString &size);

    double marginLeft() const;
    void setMarginLeft(double margin);

    double marginRight() const;
    void setMarginRight(double margin);

    double marginTop() const;
    void setMarginTop(double margin);

    double marginBottom() const;
    void setMarginBottom(double margin);

    bool showPageNumbers() const;
    void setShowPageNumbers(bool show);

    // Stylesheet
    QString stylesheetPath() const;
    void setStylesheetPath(const QString &path);

    // AutoSync
    bool autoSync() const;
    void setAutoSync(bool enabled);

    // Returns the absolute path to the stylesheets/ directory
    QString stylesheetFolderPath() const;
    // Returns absolute paths of all .css files in the stylesheets/ directory
    QStringList stylesheetPaths() const;

    // Logical Tree Management
    QJsonObject tree() const;
    void setTree(const QJsonObject &tree);

    int calculateTotalWordCount() const;

Q_SIGNALS:
    void projectOpened();
    void projectClosed();
    void projectSettingsChanged();
    void treeChanged();

private:
    explicit ProjectManager(QObject *parent = nullptr);
    
    void loadDefaults();
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);
    int countWordsInTree(const QJsonObject &tree, const QString &projectPath) const;

    QString m_projectFilePath;
    QJsonObject m_data;
};

#endif // PROJECTMANAGER_H
