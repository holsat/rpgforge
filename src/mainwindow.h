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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>

namespace KTextEditor {
    class Document;
    class Editor;
    class View;
    class MovingRange;
}

class BreadcrumbBar;
class CorkboardView;
class ImagePreview;
class FileExplorer;
class GitPanel;
class OutlinePanel;
class PreviewPanel;
class ProjectTreePanel;
class VariablesPanel;
class Sidebar;
class QWebEngineView;
class QPushButton;
class QTimer;
class QUrl;
class QSplitter;
class QAction;
class QVBoxLayout;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private Q_SLOTS:
    void newFile();
    void openFile();
    void openFileFromUrl(const QUrl &url);
    void saveFile();
    void saveFileAs();
    void newProject();
    void openProject();
    void closeProject();
    void projectSettings();
    void compileToPdf();
    void onCursorPositionChanged();
    void updateCursorContext();
    void onTextChanged();
    void navigateToLine(int line);
    void togglePreview();
    void syncScroll();
    void updateErrorHighlighting();

private:
    void setupEditor();
    void setupSidebar();
    void setupActions();
    void updateTitle();
    void saveSession();
    void restoreSession();
    void showCentralView(QWidget *widget);

    KTextEditor::Editor *m_editor = nullptr;
    KTextEditor::Document *m_document = nullptr;
    KTextEditor::View *m_editorView = nullptr;

    QVBoxLayout *m_centralViewLayout = nullptr;
    CorkboardView *m_corkboardView = nullptr;
    ImagePreview *m_imagePreview = nullptr;
    QWebEngineView *m_pdfViewer = nullptr;

    Sidebar *m_sidebar = nullptr;
    FileExplorer *m_fileExplorer = nullptr;
    ProjectTreePanel *m_projectTree = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    GitPanel *m_gitPanel = nullptr;
    BreadcrumbBar *m_breadcrumbBar = nullptr;
    PreviewPanel *m_previewPanel = nullptr;
    VariablesPanel *m_variablesPanel = nullptr;

    QSplitter *m_mainSplitter = nullptr;
    QAction *m_togglePreviewAction = nullptr;

    QTimer *m_cursorDebounce = nullptr;
    QTimer *m_textChangeDebounce = nullptr;

    int m_fileExplorerId = -1;
    int m_projectTreeId = -1;
    int m_outlineId = -1;
    int m_gitId = -1;
    int m_variablesId = -1;

    QList<KTextEditor::MovingRange*> m_errorRanges;
};

#endif // MAINWINDOW_H
