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
class VisualDiffView;
class FileExplorer;
class GitPanel;
class OutlinePanel;
class PreviewPanel;
class ProjectTreePanel;
class VariablesPanel;
class ChatPanel;
class ProblemsPanel;
class Sidebar;
class QWebEngineView;
class QPushButton;
class QTimer;
class QUrl;
class QSplitter;
class QAction;
class QVBoxLayout;
class QLabel;
#include <QPair>
#include <QList>
#include <QUrl>
#include "analyzerservice.h"

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void newFile();
    void openFile();
    void openFileFromUrl(const QUrl &url);
    void saveFile();
    void saveFileAs();
    void newProject();
    void openProject();
    void closeProject();
    void cloneProject();
    void projectSettings();
    void globalSettings();
    void compileToPdf();
    void onCursorPositionChanged();
    void updateCursorContext();
    void onTextChanged();
    void navigateToLine(int line);
    void togglePreview();
    void syncScroll();
    void updateErrorHighlighting();
    /**
     * @brief Shows a diff between two files or two Git versions.
     * @param path1 Either an absolute file path OR the first path for git diff.
     * @param oldOrNewPath2 If hash1 is a git hash, this is the other hash (or empty). 
     *                      If path1 is a file path, this is the second file path.
     */
    void showDiff(const QString &path1, const QString &path2OrHash1, const QString &hash2 = QString());

    // AI Actions
    void aiExpand();
    void aiRewrite();
    void aiSummarize();
    void onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);

private:
    void setupEditor();
    void setupSidebar();
    void setupActions();
    void updateTitle();
    void saveSession();
    void restoreSession();
    void showCentralView(QWidget *widget);
    void insertProjectLinksAtCursor(const QList<QPair<QString, QUrl>> &items);

    KTextEditor::Editor *m_editor = nullptr;
    KTextEditor::Document *m_document = nullptr;
    KTextEditor::View *m_editorView = nullptr;

    QVBoxLayout *m_centralViewLayout = nullptr;
    CorkboardView *m_corkboardView = nullptr;
    ImagePreview *m_imagePreview = nullptr;
    VisualDiffView *m_diffView = nullptr;
    QWebEngineView *m_pdfViewer = nullptr;

    Sidebar *m_sidebar = nullptr;
    FileExplorer *m_fileExplorer = nullptr;
    ProjectTreePanel *m_projectTree = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    GitPanel *m_gitPanel = nullptr;
    BreadcrumbBar *m_breadcrumbBar = nullptr;
    PreviewPanel *m_previewPanel = nullptr;
    VariablesPanel *m_variablesPanel = nullptr;
    ChatPanel *m_chatPanel = nullptr;
    ProblemsPanel *m_problemsPanel = nullptr;
    QLabel *m_diagnosticsStatus = nullptr;

    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_vSplitter = nullptr;
    QAction *m_togglePreviewAction = nullptr;

    QTimer *m_cursorDebounce = nullptr;
    QTimer *m_textChangeDebounce = nullptr;
    QTimer *m_analyzerDebounce = nullptr;

    int m_fileExplorerId = -1;
    int m_projectTreeId = -1;
    int m_outlineId = -1;
    int m_gitId = -1;
    int m_variablesId = -1;
    int m_chatId = -1;

    QList<KTextEditor::MovingRange*> m_errorRanges;
    QList<KTextEditor::MovingRange*> m_diagnosticRanges;
};

#endif // MAINWINDOW_H
