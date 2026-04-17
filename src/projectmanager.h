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
#include <QTimer>
#include <QQueue>
#include <QMutex>

class ProjectTreeModel;
struct ProjectTreeItem;

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

    bool openProject(const QString &filePath);
    bool createProject(const QString &dirPath, const QString &projectName);
    bool saveProject();
    void closeProject();

    bool isProjectOpen() const;
    QString projectFilePath() const { return m_projectFilePath; }
    QString projectPath() const;

    // Project Metadata
    QString projectName() const;
    void setProjectName(const QString &name);

    QString author() const;
    void setAuthor(const QString &author);

    // Page Settings
    QString pageSize() const;
    void setPageSize(const QString &size);
    double marginLeft() const;
    void setMarginLeft(double val);
    double marginRight() const;
    void setMarginRight(double val);
    double marginTop() const;
    void setMarginTop(double val);
    double marginBottom() const;
    void setMarginBottom(double val);

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

    // LoreKeeper Configuration
    QJsonObject loreKeeperConfig() const;
    void setLoreKeeperConfig(const QJsonObject &config);

    // Logical Tree Management
    ProjectTreeModel* model() const { return m_treeModel; }
    
    // Encapsulated Tree Mutations (The authoritative way to change project structure)
    bool addFile(const QString &name, const QString &relativePath, const QString &parentPath = QString());
    bool addFolder(const QString &name, const QString &relativePath, const QString &parentPath = QString());
    bool moveItem(const QString &sourcePath, const QString &targetParentPath);
    bool removeItem(const QString &path);
    bool renameItem(const QString &path, const QString &newName);
    
    // Authorization check for finding items
    ProjectTreeItem* findItem(const QString &path) const;

    int calculateTotalWordCount() const;
    void triggerWordCountUpdate();

    /**
     * \brief Persists exploration caches into the project file.
     *
     * Stores both the word-count cache (commit hash to word count) and the
     * branch-colour map (branch name to colour string) in the project's
     * JSON data under reserved keys.  The project file is saved to disk
     * immediately via saveProject().
     *
     * Must be called on the main thread.  Typically called on project close
     * or when ExplorationGraphView::colorMapChanged() fires.
     *
     * \param wordCountCache   Map of commit hash strings to word-count integers,
     *                         as returned by GitService::saveWordCountCache().
     * \param explorationColors Map of branch name strings to colour strings
     *                          (e.g. "#a0c4ff"), as returned by
     *                          ExplorationGraphView::saveColorMap().
     * \sa loadExplorationData()
     */
    void saveExplorationData(const QVariantMap &wordCountCache,
                              const QVariantMap &explorationColors);

    /**
     * \brief Reads exploration caches from the project file.
     *
     * Populates the output parameters from the project JSON data written by
     * saveExplorationData().  Both output parameters are cleared before
     * being populated; if no data was previously saved the maps are left
     * empty rather than returning an error.
     *
     * Must be called on the main thread.  Typically called immediately
     * after openProject() succeeds.
     *
     * \param[out] wordCountCache    Populated with a map of commit hash strings
     *                               to word-count integers.
     * \param[out] explorationColors Populated with a map of branch name strings
     *                               to colour strings.
     * \sa saveExplorationData(), GitService::loadWordCountCache(),
     *     ExplorationGraphView::loadColorMap()
     */
    void loadExplorationData(QVariantMap &wordCountCache,
                              QVariantMap &explorationColors) const;

    // Returns a flat list of absolute paths for all files registered in the project tree.
    QStringList getActiveFiles() const;

    // Setup project with default folders (Manuscript, Research, etc.)
    void setupDefaultProject(const QString &dir, const QString &name);

Q_SIGNALS:
    void projectOpened();
    void projectClosed();
    void projectSettingsChanged();
    void treeChanged();
    void totalWordCountUpdated(int count);

public Q_SLOTS:
    void requestTreeUpdate(const QString &category, const QString &entityName, const QString &relativePath);

private Q_SLOTS:
    void processTreeUpdateQueue();

private:
    explicit ProjectManager(QObject *parent = nullptr);
    
    void loadDefaults();
    bool migrate(QJsonObject &data);
    void validateTree();
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);
    int countWordsInTree(const QJsonObject &tree, const QString &projectPath) const;

    struct TreeUpdateRequest {
        QString category;
        QString entityName;
        QString relativePath;
    };

    QString m_projectFilePath;
    QJsonObject m_data;
    ProjectTreeModel *m_treeModel;
    QTimer *m_treeUpdateTimer;
    QQueue<TreeUpdateRequest> m_treeUpdateQueue;
    QMutex m_queueMutex;
};

#endif // PROJECTMANAGER_H
