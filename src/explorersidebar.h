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

#ifndef EXPLORERSIDEBAR_H
#define EXPLORERSIDEBAR_H

#include <QWidget>

class QSplitter;
class QToolButton;
class ProjectTreePanel;
class FileExplorer;

class ExplorerSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit ExplorerSidebar(ProjectTreePanel *projectTree, FileExplorer *fileExplorer, QWidget *parent = nullptr);
    ~ExplorerSidebar() override;

private Q_SLOTS:
    /// Show or hide the FileExplorer pane and persist the choice to
    /// QSettings "ui/filesystem_browser_visible" so the state sticks
    /// across restarts.
    void setFileExplorerVisible(bool visible);

private:
    void setupUi();

    ProjectTreePanel *m_projectTree;
    FileExplorer *m_fileExplorer;
    QSplitter *m_splitter;
    QToolButton *m_toggleBtn = nullptr;
};

#endif // EXPLORERSIDEBAR_H
