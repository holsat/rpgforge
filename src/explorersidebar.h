/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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
class OutlinePanel;
class FileExplorer;

/// Stacks the Project Tree (always visible), Document Outline, and Filesystem
/// browser as three vertically-split, individually collapsible panes. Each of
/// the lower two panes has a header with a toggle button that persists its
/// visibility via QSettings. Collapsed panes yield their share of the
/// splitter to the Project Tree so the top pane fills the available space.
class ExplorerSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit ExplorerSidebar(ProjectTreePanel *projectTree,
                             OutlinePanel *outlinePanel,
                             FileExplorer *fileExplorer,
                             QWidget *parent = nullptr);
    ~ExplorerSidebar() override;

private Q_SLOTS:
    /// Show or hide the FileExplorer pane + persist to QSettings
    /// "ui/filesystem_browser_visible".
    void setFileExplorerVisible(bool visible);
    /// Show or hide the OutlinePanel + persist to QSettings
    /// "ui/outline_panel_visible".
    void setOutlinePanelVisible(bool visible);

private:
    void setupUi();
    /// Reflow splitter sizes so collapsed panes shrink to their header
    /// height and the remaining vertical space goes to the Project Tree
    /// (and whichever lower panes are still expanded).
    void reflowSplitter();

    ProjectTreePanel *m_projectTree;
    OutlinePanel *m_outlinePanel;
    FileExplorer *m_fileExplorer;
    QSplitter *m_splitter = nullptr;
    QWidget *m_outlinePane = nullptr;
    QWidget *m_filesystemPane = nullptr;
    QToolButton *m_outlineToggleBtn = nullptr;
    QToolButton *m_filesystemToggleBtn = nullptr;
    /// Last-known expanded height for each lower pane. Saved to QSettings
    /// when the user drags the splitter handle while the pane is expanded;
    /// restored when the pane is re-expanded after a collapse. 0 = "use
    /// the default" (first run).
    int m_outlineExpandedHeight = 0;
    int m_filesystemExpandedHeight = 0;

protected:
    void showEvent(QShowEvent *event) override;
};

#endif // EXPLORERSIDEBAR_H
