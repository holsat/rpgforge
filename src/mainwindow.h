#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>

namespace KTextEditor {
    class Document;
    class Editor;
    class View;
}

class BreadcrumbBar;
class FileExplorer;
class GitPanel;
class OutlinePanel;
class Sidebar;
class QTimer;
class QUrl;

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
    void onCursorPositionChanged();
    void updateCursorContext();
    void onTextChanged();
    void navigateToLine(int line);

private:
    void setupEditor();
    void setupSidebar();
    void setupActions();
    void updateTitle();
    void saveSession();
    void restoreSession();

    KTextEditor::Editor *m_editor = nullptr;
    KTextEditor::Document *m_document = nullptr;
    KTextEditor::View *m_editorView = nullptr;

    Sidebar *m_sidebar = nullptr;
    FileExplorer *m_fileExplorer = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    GitPanel *m_gitPanel = nullptr;
    BreadcrumbBar *m_breadcrumbBar = nullptr;

    QTimer *m_cursorDebounce = nullptr;

    int m_fileExplorerId = -1;
    int m_outlineId = -1;
    int m_gitId = -1;
};

#endif // MAINWINDOW_H
