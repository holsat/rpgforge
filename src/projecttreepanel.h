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

#ifndef PROJECTTREEPANEL_H
#define PROJECTTREEPANEL_H

#include <QWidget>
#include <QModelIndex>
#include <QPersistentModelIndex>

class QTreeView;
class ProjectTreeModel;
class QToolButton;
class QPushButton;
class QComboBox;
struct ProjectTreeItem;

class ProjectTreePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectTreePanel(QWidget *parent = nullptr);
    ~ProjectTreePanel() override;

    QPushButton* createButton() const { return m_createBtn; }
    ProjectTreeModel* model() const { return m_model; }
    ProjectTreeItem* activeFolder() const;

Q_SIGNALS:
    void fileActivated(const QString &relativePath);
    void folderActivated(ProjectTreeItem *folderItem);
    void diffRequested(const QString &filePath, const QString &oldHash, const QString &newHash = QString());

private Q_SLOTS:
    void onProjectOpened();
    void onProjectClosed();
    void onItemActivated(const QModelIndex &index);
    void onCustomContextMenu(const QPoint &pos);
    
    void addFolder();
    void addFile();
    void removeItem();
    void renameItem();
    void renameFile();
    void editMetadata();
    void syncProject();
    void onRepoCreated(const QString &cloneUrl);
    void createExploration();
    void switchExploration(const QString &name);
    void updateExplorationList();
    ProjectTreeItem* currentFolder() const;

private:
    void setupUi();
    void setupEmptyState();
    void saveTree();
    void requestRefresh();

    QTreeView *m_treeView = nullptr;
    QPersistentModelIndex m_activeFolderIndex;
    QTimer *m_refreshTimer = nullptr;
    QWidget *m_emptyWidget = nullptr;
    QPushButton *m_createBtn = nullptr;
    ProjectTreeModel *m_model = nullptr;
    QToolButton *m_addFolderBtn = nullptr;
    QToolButton *m_addFileBtn = nullptr;
    QToolButton *m_syncBtn = nullptr;
    QComboBox *m_explorationCombo = nullptr;
    QToolButton *m_newExplorationBtn = nullptr;
};

#endif // PROJECTTREEPANEL_H
