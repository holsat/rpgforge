#include "mainwindow.h"
#include "breadcrumbbar.h"
#include "fileexplorer.h"
#include "gitpanel.h"
#include "outlinepanel.h"
#include "sidebar.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KStandardAction>
#include <KXMLGUIFactory>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setupEditor();
    setupSidebar();
    setupActions();
    setupGUI(Default, QStringLiteral("rpgforgeui.rc"));

    // Merge KTextEditor::View's GUI (Edit, View, Selection, Tools menus)
    guiFactory()->addClient(m_editorView);

    updateTitle();
    resize(1400, 900);

    // Restore previous session after everything is set up
    restoreSession();

    // Auto-save session every 5 seconds so state survives crashes
    auto *autoSaveTimer = new QTimer(this);
    autoSaveTimer->setInterval(5000);
    connect(autoSaveTimer, &QTimer::timeout, this, &MainWindow::saveSession);
    autoSaveTimer->start();
}

MainWindow::~MainWindow()
{
    saveSession();
}

void MainWindow::setupEditor()
{
    m_editor = KTextEditor::Editor::instance();
    m_document = m_editor->createDocument(this);
    m_editorView = m_document->createView(this);

    // Enable markdown syntax highlighting
    m_document->setHighlightingMode(QStringLiteral("Markdown"));

    // Debounce cursor position changes
    m_cursorDebounce = new QTimer(this);
    m_cursorDebounce->setSingleShot(true);
    m_cursorDebounce->setInterval(100);
    connect(m_cursorDebounce, &QTimer::timeout, this, &MainWindow::updateCursorContext);

    // Connect signals
    connect(m_document, &KTextEditor::Document::textChanged,
            this, &MainWindow::onTextChanged);
    connect(m_document, &KTextEditor::Document::documentUrlChanged,
            this, &MainWindow::updateTitle);
    connect(m_document, &KTextEditor::Document::modifiedChanged,
            this, &MainWindow::updateTitle);
    connect(m_editorView, &KTextEditor::View::cursorPositionChanged,
            this, &MainWindow::onCursorPositionChanged);
}

void MainWindow::setupSidebar()
{
    // Create the sidebar panels
    m_fileExplorer = new FileExplorer(this);
    m_outlinePanel = new OutlinePanel(this);
    m_gitPanel = new GitPanel(this);
    m_breadcrumbBar = new BreadcrumbBar(this);

    // Create the sidebar and add panels
    m_sidebar = new Sidebar(this);
    m_fileExplorerId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("folder")),
        QStringLiteral("File Explorer"),
        m_fileExplorer);
    m_outlineId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("view-list-tree")),
        QStringLiteral("Document Outline"),
        m_outlinePanel);
    m_gitId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("vcs-branch"),
                         QIcon::fromTheme(QStringLiteral("git"))),
        QStringLiteral("Git / Versioning"),
        m_gitPanel);

    // Show file explorer by default
    m_sidebar->showPanel(m_fileExplorerId);

    // Build the central layout: [sidebar | breadcrumb+editor]
    auto *centralWidget = new QWidget(this);
    auto *hbox = new QHBoxLayout(centralWidget);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);

    hbox->addWidget(m_sidebar);

    auto *editorContainer = new QWidget(centralWidget);
    auto *vbox = new QVBoxLayout(editorContainer);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_breadcrumbBar);
    vbox->addWidget(m_editorView);

    hbox->addWidget(editorContainer, 1); // editor gets all remaining space

    setCentralWidget(centralWidget);

    // Wire up connections
    connect(m_fileExplorer, &FileExplorer::fileActivated,
            this, &MainWindow::openFileFromUrl);
    connect(m_outlinePanel, &OutlinePanel::headingsUpdated,
            m_breadcrumbBar, &BreadcrumbBar::setHeadings);
    connect(m_outlinePanel, &OutlinePanel::headingClicked,
            this, &MainWindow::navigateToLine);
    connect(m_breadcrumbBar, &BreadcrumbBar::headingClicked,
            this, &MainWindow::navigateToLine);
}

void MainWindow::setupActions()
{
    KStandardAction::openNew(this, &MainWindow::newFile, actionCollection());
    KStandardAction::open(this, &MainWindow::openFile, actionCollection());
    KStandardAction::save(this, &MainWindow::saveFile, actionCollection());
    KStandardAction::saveAs(this, &MainWindow::saveFileAs, actionCollection());
    KStandardAction::quit(qApp, &QApplication::quit, actionCollection());
}

void MainWindow::newFile()
{
    m_document->closeUrl();
    m_document->setHighlightingMode(QStringLiteral("Markdown"));
    updateTitle();
}

void MainWindow::openFile()
{
    const QUrl url = QFileDialog::getOpenFileUrl(this, i18n("Open File"), QUrl(),
        i18n("Markdown Files (*.md *.markdown *.mkd *.txt);;All Files (*)"));
    if (!url.isEmpty()) {
        openFileFromUrl(url);
    }
}

void MainWindow::openFileFromUrl(const QUrl &url)
{
    if (!url.isEmpty()) {
        m_document->openUrl(url);
        const QString fileName = url.fileName();
        if (fileName.endsWith(QLatin1String(".md")) ||
            fileName.endsWith(QLatin1String(".markdown")) ||
            fileName.endsWith(QLatin1String(".mkd"))) {
            m_document->setHighlightingMode(QStringLiteral("Markdown"));
        }
        updateTitle();
        onTextChanged();
        saveSession();
    }
}

void MainWindow::saveFile()
{
    if (m_document->url().isEmpty()) {
        saveFileAs();
    } else {
        m_document->save();
    }
}

void MainWindow::saveFileAs()
{
    const QUrl url = QFileDialog::getSaveFileUrl(this, i18n("Save File As"), QUrl(),
        i18n("Markdown Files (*.md *.markdown);;All Files (*)"));
    if (!url.isEmpty()) {
        m_document->saveAs(url);
        updateTitle();
    }
}

void MainWindow::onTextChanged()
{
    if (m_outlinePanel && m_document) {
        m_outlinePanel->documentChanged(m_document->text());
    }
}

void MainWindow::onCursorPositionChanged()
{
    m_cursorDebounce->start();
}

void MainWindow::updateCursorContext()
{
    if (!m_editorView) return;
    int line = m_editorView->cursorPosition().line();

    if (m_outlinePanel) {
        m_outlinePanel->highlightForLine(line);
    }
    if (m_breadcrumbBar) {
        m_breadcrumbBar->updateForLine(line);
    }
}

void MainWindow::navigateToLine(int line)
{
    if (m_editorView) {
        m_editorView->setCursorPosition(KTextEditor::Cursor(line, 0));
        m_editorView->setFocus();
    }
}

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("RPG Forge");
    if (!m_document->url().isEmpty()) {
        title = m_document->url().fileName() + QStringLiteral(" — ") + title;
    } else {
        title = i18n("Untitled") + QStringLiteral(" — ") + title;
    }
    if (m_document->isModified()) {
        title.prepend(QStringLiteral("* "));
    }
    setWindowTitle(title);
}

void MainWindow::saveSession()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());

    if (m_fileExplorer) {
        settings.setValue(QStringLiteral("explorerRoot"), m_fileExplorer->rootPath());
        settings.setValue(QStringLiteral("showHiddenFiles"), m_fileExplorer->showHiddenFiles());
    }

    if (m_document && !m_document->url().isEmpty()) {
        settings.setValue(QStringLiteral("lastFile"), m_document->url().toString());
    } else {
        settings.remove(QStringLiteral("lastFile"));
    }

    if (m_editorView) {
        auto cursor = m_editorView->cursorPosition();
        settings.setValue(QStringLiteral("cursorLine"), cursor.line());
        settings.setValue(QStringLiteral("cursorColumn"), cursor.column());
    }

    // Save which sidebar panel is active
    if (m_sidebar) {
        settings.setValue(QStringLiteral("sidebarPanel"), m_sidebar->currentPanel());
    }
}

void MainWindow::restoreSession()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    }
    if (settings.contains(QStringLiteral("windowState"))) {
        restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    }

    if (m_fileExplorer && settings.contains(QStringLiteral("showHiddenFiles"))) {
        m_fileExplorer->setShowHiddenFiles(settings.value(QStringLiteral("showHiddenFiles")).toBool());
    }

    if (m_fileExplorer && settings.contains(QStringLiteral("explorerRoot"))) {
        m_fileExplorer->setRootPath(settings.value(QStringLiteral("explorerRoot")).toString());
    }

    if (settings.contains(QStringLiteral("lastFile"))) {
        QUrl url(settings.value(QStringLiteral("lastFile")).toString());
        if (url.isValid() && url.isLocalFile() && QFile::exists(url.toLocalFile())) {
            openFileFromUrl(url);
            int line = settings.value(QStringLiteral("cursorLine"), 0).toInt();
            int col = settings.value(QStringLiteral("cursorColumn"), 0).toInt();
            if (m_editorView) {
                m_editorView->setCursorPosition(KTextEditor::Cursor(line, col));
            }
        }
    }

    // Restore active sidebar panel
    if (m_sidebar && settings.contains(QStringLiteral("sidebarPanel"))) {
        int panelId = settings.value(QStringLiteral("sidebarPanel")).toInt();
        if (panelId >= 0) {
            m_sidebar->showPanel(panelId);
        }
    }
}
