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

#ifndef FILEEXPLORER_H
#define FILEEXPLORER_H

#include <QModelIndex>
#include <QWidget>

class QTreeView;
class QFileSystemModel;
class QLineEdit;
class QToolButton;
class GitStatusModel;

class FileExplorer : public QWidget
{
    Q_OBJECT

public:
    explicit FileExplorer(QWidget *parent = nullptr);
    ~FileExplorer() override;

    void setRootPath(const QString &path);
    QString rootPath() const;

    bool showHiddenFiles() const { return m_showHidden; }
    void setShowHiddenFiles(bool show);

Q_SIGNALS:
    void fileActivated(const QUrl &url);

private Q_SLOTS:
    void onItemActivated(const QModelIndex &index);
    void onCustomContextMenu(const QPoint &pos);
    void onGitStatusChanged();
    void toggleHiddenFiles();

    void renameItem();
    void deleteItem();
    void newFile();
    void newFolder();
    void openFolder();

private:
    void setupUi();
    void applyFilter();

    QTreeView *m_treeView = nullptr;
    QFileSystemModel *m_model = nullptr;
    GitStatusModel *m_gitStatus = nullptr;
    QToolButton *m_hiddenBtn = nullptr;
    QModelIndex m_contextMenuIndex;
    bool m_showHidden = false;
};

#endif // FILEEXPLORER_H
