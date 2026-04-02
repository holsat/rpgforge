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

#include "mainwindow.h"
#include "breadcrumbbar.h"
#include "compiledialog.h"
#include "explorersidebar.h"
#include "corkboardview.h"
#include "imagepreview.h"
#include "fileexplorer.h"
#include "gitpanel.h"
#include "outlinepanel.h"
#include "pdfexporter.h"
#include "previewpanel.h"
#include "projecttreepanel.h"
#include "variablespanel.h"
#include "variablemanager.h"
#include "variablecompletionmodel.h"
#include "projectmanager.h"
#include "projectsettingsdialog.h"
#include "metadatadialog.h"
#include "newprojectdialog.h"
#include "projecttreemodel.h"
#include "sidebar.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KStandardAction>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Attribute>
#include <KXMLGUIFactory>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <QApplication>
#include <QDropEvent>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setupEditor();
    setupSidebar();
    setupActions();

    setupGUI(Default, QStringLiteral(":/rpgforgeui.rc"));

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

    // Initial preview update
    onTextChanged();
}

MainWindow::~MainWindow()
{
    saveSession();
    qDeleteAll(m_errorRanges);
    m_errorRanges.clear();
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

    // Debounce text changes to prevent UI stutter and recursion crashes
    m_textChangeDebounce = new QTimer(this);
    m_textChangeDebounce->setSingleShot(true);
    m_textChangeDebounce->setInterval(500);
    connect(m_textChangeDebounce, &QTimer::timeout, this, &MainWindow::updateErrorHighlighting);

    // Connect signals
    connect(m_document, &KTextEditor::Document::textChanged,
            this, &MainWindow::onTextChanged);
    connect(m_document, &KTextEditor::Document::documentUrlChanged,
            this, &MainWindow::updateTitle);
    connect(m_document, &KTextEditor::Document::modifiedChanged,
            this, &MainWindow::updateTitle);
    connect(m_editorView, &KTextEditor::View::cursorPositionChanged,
            this, &MainWindow::onCursorPositionChanged);
    connect(m_editorView, &KTextEditor::View::verticalScrollPositionChanged,
            this, &MainWindow::syncScroll);

    // Register variable autocomplete safely after the view is fully initialized
    QTimer::singleShot(500, this, [this]() {
        m_editorView->setAutomaticInvocationEnabled(true);
        auto *completionModel = new VariableCompletionModel(this);
        m_editorView->registerCompletionModel(completionModel);

        // Install event filter on KateCompletionWidget to fix popup positioning/z-order
        for (QObject *child : this->children()) {
            QWidget *w = qobject_cast<QWidget*>(child);
            if (w && QString::fromLatin1(w->metaObject()->className()).contains(QLatin1String("Completion"))) {
                w->installEventFilter(this);
            }
        }

        // Install event filter on the editor view and all its child widgets so we
        // can intercept drops from the project tree and insert markdown links.
        m_editorView->installEventFilter(this);
        const auto editorChildren = m_editorView->findChildren<QWidget*>();
        for (QWidget *child : editorChildren) {
            child->installEventFilter(this);
        }
    });
}

void MainWindow::setupSidebar()
{
    // Create the sidebar panels
    m_fileExplorer = new FileExplorer(this);
    m_projectTree = new ProjectTreePanel(this);
    auto *explorerStack = new ExplorerSidebar(m_projectTree, m_fileExplorer, this);
    
    m_outlinePanel = new OutlinePanel(this);
    m_gitPanel = new GitPanel(this);
    m_breadcrumbBar = new BreadcrumbBar(this);
    m_previewPanel = new PreviewPanel(this);
    m_variablesPanel = new VariablesPanel(this);

    // Create the sidebar and add panels
    m_sidebar = new Sidebar(this);
    m_fileExplorerId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("folder-explorer"), QIcon::fromTheme(QStringLiteral("folder"))),
        QStringLiteral("Explorer"),
        explorerStack);
    m_outlineId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("view-list-tree")),
        QStringLiteral("Document Outline"),
        m_outlinePanel);
    m_variablesId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("code-variable"), QIcon::fromTheme(QStringLiteral("variable"))),
        QStringLiteral("Variables"),
        m_variablesPanel);
    m_gitId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("vcs-branch"),
                         QIcon::fromTheme(QStringLiteral("git"))),
        QStringLiteral("Git / Versioning"),
        m_gitPanel);

    connect(m_projectTree->createButton(), &QPushButton::clicked, this, &MainWindow::newProject);

    // Show explorer by default
    m_sidebar->showPanel(m_fileExplorerId);

    // Build the central layout: [splitter [sidebar | editor | preview]]
    auto *centralWidget = new QWidget(this);
    auto *hbox = new QHBoxLayout(centralWidget);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);

    auto *editorContainer = new QWidget(m_mainSplitter);
    auto *vbox = new QVBoxLayout(editorContainer);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_breadcrumbBar);
    
    // Use a plain container with QVBoxLayout instead of QStackedWidget.
    // QStackedWidget interferes with KateCompletionWidget popup positioning
    // because it clips child widgets and affects coordinate mapping.
    auto *viewContainer = new QWidget(editorContainer);
    m_centralViewLayout = new QVBoxLayout(viewContainer);
    m_centralViewLayout->setContentsMargins(0, 0, 0, 0);
    m_centralViewLayout->setSpacing(0);

    m_corkboardView = new CorkboardView(this);
    m_imagePreview = new ImagePreview(this);
    m_pdfViewer = new QWebEngineView(this);

    m_centralViewLayout->addWidget(m_editorView);
    m_centralViewLayout->addWidget(m_corkboardView);
    m_centralViewLayout->addWidget(m_imagePreview);
    m_centralViewLayout->addWidget(m_pdfViewer);

    // Only show the editor view initially; hide the rest
    m_corkboardView->hide();
    m_imagePreview->hide();
    m_pdfViewer->hide();

    vbox->addWidget(viewContainer);

    m_mainSplitter->addWidget(m_sidebar);
    m_mainSplitter->addWidget(editorContainer);
    m_mainSplitter->addWidget(m_previewPanel);

    // Set initial sizes: sidebar (250px), editor (1), preview (1)
    m_mainSplitter->setSizes({250, 600, 550});
    m_mainSplitter->setStretchFactor(0, 0); 
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 1);

    hbox->addWidget(m_mainSplitter, 1); 

    setCentralWidget(centralWidget);

    // Wire up connections
    connect(m_fileExplorer, &FileExplorer::fileActivated,
            this, &MainWindow::openFileFromUrl);
    connect(m_projectTree, &ProjectTreePanel::fileActivated, this, [this](const QString &relativePath) {
        if (ProjectManager::instance().isProjectOpen()) {
            QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relativePath);
            openFileFromUrl(QUrl::fromLocalFile(fullPath));
            // openFileFromUrl already calls showCentralView for the appropriate view
            // (image preview, PDF viewer, or editor) — do not override it here.
        }
    });
    connect(m_projectTree, &ProjectTreePanel::folderActivated, this, [this](ProjectTreeItem *folder) {
        if (!folder) return;
        const QString nameLower = folder->name.toLower();
        if (nameLower == QStringLiteral("media") || nameLower == QStringLiteral("stylesheets")) {
            return;
        }
        m_corkboardView->setFolder(folder);
        showCentralView(m_corkboardView);
    });
    connect(m_corkboardView, &CorkboardView::fileActivated, this, [this](const QString &relativePath) {
        if (ProjectManager::instance().isProjectOpen()) {
            QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relativePath);
            openFileFromUrl(QUrl::fromLocalFile(fullPath));
            showCentralView(m_editorView);
        }
    });
    connect(m_corkboardView, &CorkboardView::itemsReordered, this, [this](ProjectTreeItem *folder, ProjectTreeItem *draggedItem, ProjectTreeItem *targetItem) {
        int toIdx = -1;
        if (targetItem) {
            toIdx = folder->children.indexOf(targetItem);
        } else {
            toIdx = folder->children.size();
        }

        if (m_projectTree->model()->moveItem(draggedItem, folder, toIdx)) {
            ProjectManager::instance().setTree(m_projectTree->model()->projectData());
            ProjectManager::instance().saveProject();
            // Debounced refresh will be triggered by model signals via ProjectTreePanel
        }
    });
    connect(m_outlinePanel, &OutlinePanel::headingsUpdated,
            m_breadcrumbBar, &BreadcrumbBar::setHeadings);
    connect(m_outlinePanel, &OutlinePanel::headingClicked,
            this, &MainWindow::navigateToLine);
    connect(m_breadcrumbBar, &BreadcrumbBar::headingClicked,
            this, &MainWindow::navigateToLine);
    connect(m_breadcrumbBar, &BreadcrumbBar::togglePreviewRequested,
            this, [this]() {
                m_togglePreviewAction->setChecked(!m_togglePreviewAction->isChecked());
                togglePreview();
            });

    connect(m_variablesPanel, &VariablesPanel::variablesChanged, this, [this]() {
        VariableManager::instance().setPanelVariables(m_variablesPanel->variables());
    });

    connect(&VariableManager::instance(), &VariableManager::variablesChanged, this, [this]() {
        if (m_previewPanel) {
            m_previewPanel->setMarkdown(m_document->text());
        }
    });

    // Clear the corkboard when a project opens or closes so it never holds
    // pointers into a tree that has been rebuilt/destroyed.
    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, [this]() {
        if (m_corkboardView) m_corkboardView->setFolder(nullptr);
    });
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, [this]() {
        if (m_corkboardView) {
            m_corkboardView->setFolder(nullptr);
            showCentralView(m_editorView);
        }
    });

    // Reload preview stylesheet when project settings change (e.g., stylesheet path)
    connect(&ProjectManager::instance(), &ProjectManager::projectSettingsChanged, this, [this]() {
        if (m_previewPanel) {
            m_previewPanel->reloadStylesheet();
        }
    });
}

void MainWindow::setupActions()
{
    actionCollection()->addAction(KStandardAction::New, QStringLiteral("file_new"), this, SLOT(newFile()));
    actionCollection()->addAction(KStandardAction::Open, QStringLiteral("file_open"), this, SLOT(openFile()));
    auto *saveAct = KStandardAction::save(this, &MainWindow::saveFile, actionCollection());
    actionCollection()->addAction(KStandardAction::SaveAs, QStringLiteral("file_save_as"), this, SLOT(saveFileAs()));
    
    auto *newProjectAct = new QAction(this);
    newProjectAct->setText(i18n("New Project..."));
    newProjectAct->setIcon(QIcon::fromTheme(QStringLiteral("project-development-new")));
    actionCollection()->addAction(QStringLiteral("project_new"), newProjectAct);
    connect(newProjectAct, &QAction::triggered, this, &MainWindow::newProject);

    auto *openProjectAct = new QAction(this);
    openProjectAct->setText(i18n("Open Project..."));
    openProjectAct->setIcon(QIcon::fromTheme(QStringLiteral("project-open")));
    actionCollection()->addAction(QStringLiteral("project_open"), openProjectAct);
    connect(openProjectAct, &QAction::triggered, this, &MainWindow::openProject);

    auto *closeProjectAct = new QAction(this);
    closeProjectAct->setText(i18n("Close Project"));
    closeProjectAct->setIcon(QIcon::fromTheme(QStringLiteral("project-development-close")));
    actionCollection()->addAction(QStringLiteral("project_close"), closeProjectAct);
    connect(closeProjectAct, &QAction::triggered, this, &MainWindow::closeProject);

    auto *projectSettingsAct = new QAction(this);
    projectSettingsAct->setText(i18n("Project Settings..."));
    projectSettingsAct->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    actionCollection()->addAction(QStringLiteral("project_settings"), projectSettingsAct);
    connect(projectSettingsAct, &QAction::triggered, this, &MainWindow::projectSettings);

    auto *compileAct = new QAction(this);
    compileAct->setText(i18n("Compile to PDF..."));
    compileAct->setIcon(QIcon::fromTheme(QStringLiteral("document-export-pdf")));
    actionCollection()->addAction(QStringLiteral("compile_project"), compileAct);
    connect(compileAct, &QAction::triggered, this, &MainWindow::compileToPdf);

    KStandardAction::quit(qApp, &QApplication::quit, actionCollection());

    actionCollection()->setDefaultShortcut(saveAct, Qt::CTRL | Qt::Key_S);

    m_togglePreviewAction = new QAction(this);
    m_togglePreviewAction->setText(i18n("Show Preview"));
    m_togglePreviewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    m_togglePreviewAction->setCheckable(true);
    m_togglePreviewAction->setChecked(true);
    actionCollection()->addAction(QStringLiteral("toggle_preview"), m_togglePreviewAction);
    actionCollection()->setDefaultShortcut(m_togglePreviewAction, Qt::CTRL | Qt::Key_P);
    connect(m_togglePreviewAction, &QAction::triggered, this, &MainWindow::togglePreview);
}

void MainWindow::newFile()
{
    m_document->closeUrl();
    m_document->setHighlightingMode(QStringLiteral("Markdown"));
    updateTitle();
    onTextChanged();
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
    if (!url.isEmpty() && url.isLocalFile()) {
        QString path = url.toLocalFile();
        QString suffix = QFileInfo(path).suffix().toLower();

        static const QStringList imgSuffixes = {
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
            QStringLiteral("bmp")
        };
        // Text formats we are willing to open in the editor.
        // Anything not on this list (e.g. .emf, .docx, .exe) is silently
        // ignored — handing a binary file to KTextEditor marks the document
        // read-only, which persists even after a subsequent markdown file is opened.
        static const QStringList textSuffixes = {
            QStringLiteral("md"),   QStringLiteral("markdown"), QStringLiteral("mkd"),
            QStringLiteral("txt"),  QStringLiteral("css"),      QStringLiteral("yaml"),
            QStringLiteral("yml"),  QStringLiteral("json"),     QStringLiteral("html"),
            QStringLiteral("htm"),  QStringLiteral("xml"),      QStringLiteral("rpgvars"),
            QString()               // no extension
        };

        if (imgSuffixes.contains(suffix)) {
            if (m_imagePreview->loadImage(path)) {
                showCentralView(m_imagePreview);
                if (m_previewPanel) m_previewPanel->hide();
                return;
            }
        } else if (suffix == QLatin1String("pdf")) {
            m_pdfViewer->setUrl(url);
            showCentralView(m_pdfViewer);
            if (m_previewPanel) m_previewPanel->hide();
            return;
        } else if (!textSuffixes.contains(suffix)) {
            // Unknown / likely binary — don't open in the editor
            return;
        }

        m_document->openUrl(url);
        showCentralView(m_editorView);
        if (m_previewPanel && m_togglePreviewAction->isChecked()) {
            m_previewPanel->show();
            m_previewPanel->setBaseUrl(url);
        }
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
        if (m_previewPanel) {
            m_previewPanel->setBaseUrl(url);
        }
        updateTitle();
    }
}

void MainWindow::newProject()
{
    QString defaultDir = m_fileExplorer ? m_fileExplorer->rootPath() : QDir::homePath();
    NewProjectDialog dialog(defaultDir, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString dir = dialog.projectDir();
        QString name = dialog.projectName();
        
        if (!dir.isEmpty() && !name.isEmpty()) {
            if (ProjectManager::instance().createProject(dir, name)) {
                m_fileExplorer->setRootPath(dir);
                
                // Add top level folder with project name
                QModelIndex rootFolder = m_projectTree->model()->addFolder(name);

                // Add Stylesheets folder with a link to the default style.css
                QModelIndex stylesheetsFolder = m_projectTree->model()->addFolder(QStringLiteral("Stylesheets"), rootFolder);
                m_projectTree->model()->addFile(QStringLiteral("style.css"), QStringLiteral("stylesheets/style.css"), stylesheetsFolder);

                // If a file is open, add it to the project
                if (m_document && !m_document->url().isEmpty()) {
                    QString openFilePath = m_document->url().toLocalFile();
                    m_projectTree->model()->addFileWithSmartDiscovery(openFilePath, rootFolder);
                }
                
                ProjectManager::instance().setTree(m_projectTree->model()->projectData());
                ProjectManager::instance().saveProject();
                
                updateTitle();
            }
        }
    }
}

void MainWindow::openProject()
{
    const QString filePath = QFileDialog::getOpenFileName(this, i18n("Open Project"),
        QString(), i18n("RPG Forge Project (rpgforge.project)"));
    
    if (!filePath.isEmpty()) {
        if (ProjectManager::instance().openProject(filePath)) {
            m_fileExplorer->setRootPath(ProjectManager::instance().projectPath());
            updateTitle();
        }
    }
}

void MainWindow::closeProject()
{
    ProjectManager::instance().closeProject();

    // Clear document and views to prevent stale data from triggering updates
    if (m_document) {
        m_document->closeUrl();
    }
    if (m_outlinePanel) {
        m_outlinePanel->documentChanged(QString());
    }
    if (m_previewPanel) {
        m_previewPanel->setMarkdown(QString());
    }
    updateTitle();
}

void MainWindow::projectSettings()
{
    if (!ProjectManager::instance().isProjectOpen()) {
        QMessageBox::information(this, i18n("Project Settings"), 
            i18n("No project is currently open."));
        return;
    }
    
    ProjectSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        dialog.save();
        updateTitle();
    }
}

void MainWindow::compileToPdf()
{
    if (!ProjectManager::instance().isProjectOpen()) {
        QMessageBox::information(this, i18n("Compile Project"), 
            i18n("No project is currently open."));
        return;
    }

    CompileDialog settingsDialog(this);
    if (settingsDialog.exec() != QDialog::Accepted) return;
    
    CompileOptions options = settingsDialog.options();

    QString outputPath = QFileDialog::getSaveFileName(this, i18n("Export to PDF"),
        ProjectManager::instance().projectPath(), i18n("PDF Files (*.pdf)"));
    
    if (outputPath.isEmpty()) return;

    auto *exporter = new PdfExporter(this);
    connect(exporter, &PdfExporter::finished, this, [this, exporter](bool success, const QString &message) {
        if (success) {
            QMessageBox::information(this, i18n("Compile Success"), message);
        } else {
            QMessageBox::critical(this, i18n("Compile Error"), message);
        }
        exporter->deleteLater();
    });
    
    exporter->exportProject(outputPath, options);
}

void MainWindow::onTextChanged()
{
    // Minimal work during typing to prevent hangs/crashes
    if (m_document && m_textChangeDebounce) {
        m_textChangeDebounce->start();
    }
}

void MainWindow::updateErrorHighlighting()
{
    if (!m_document) return;

    QString text = m_document->text();

    // 1. Sync front-matter variables
    auto frontMatterVars = VariableManager::parseFrontMatter(text);
    VariableManager::instance().setDocumentVariables(frontMatterVars);

    // 2. Update auxiliary views
    QString contentOnly = VariableManager::stripMetadata(text);
    if (m_outlinePanel) m_outlinePanel->documentChanged(contentOnly);
    if (m_previewPanel) m_previewPanel->setMarkdown(contentOnly);

    // 3. Highlight undefined variable references with red squiggly underline
    qDeleteAll(m_errorRanges);
    m_errorRanges.clear();

    // Build set of known variable names (without CALC: prefix)
    QSet<QString> knownVars;
    const auto names = VariableManager::instance().variableNames();
    for (const QString &name : names) {
        if (name.startsWith(QLatin1String("CALC:"))) {
            knownVars.insert(name.mid(5));
        } else {
            knownVars.insert(name);
        }
    }

    // Scan document for {{varname}} patterns
    static const QRegularExpression varRefRegex(QStringLiteral("\\{\\{([A-Za-z0-9_.]+)\\}\\}"));

    // Create error attribute (red squiggly underline)
    KTextEditor::Attribute::Ptr errorAttr(new KTextEditor::Attribute());
    errorAttr->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    errorAttr->setUnderlineColor(Qt::red);

    for (int line = 0; line < m_document->lines(); ++line) {
        const QString lineText = m_document->line(line);
        QRegularExpressionMatchIterator it = varRefRegex.globalMatch(lineText);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString varName = match.captured(1);
            if (!knownVars.contains(varName)) {
                // Unknown variable — mark the entire {{varname}} with error underline
                int startCol = match.capturedStart(0);
                int endCol = match.capturedEnd(0);
                KTextEditor::Range range(line, startCol, line, endCol);
                KTextEditor::MovingRange *mr = m_document->newMovingRange(range);
                mr->setAttribute(errorAttr);
                mr->setZDepth(-100.0);
                m_errorRanges.append(mr);
            }
        }
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
        if (m_previewPanel && m_previewPanel->isVisible()) {
            m_previewPanel->scrollToLine(line);
        }
    }
}

void MainWindow::togglePreview()
{
    if (m_previewPanel) {
        m_previewPanel->setVisible(m_togglePreviewAction->isChecked());
        if (m_previewPanel->isVisible()) {
            syncScroll();
        }
    }
}

void MainWindow::syncScroll()
{
    if (!m_editorView || !m_previewPanel || !m_previewPanel->isVisible()) return;

    // Use scrollToLine with smooth=false for real-time synchronization
    int currentLine = m_editorView->firstDisplayedLine();
    m_previewPanel->scrollToLine(currentLine, false);
}

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("RPG Forge");
    if (ProjectManager::instance().isProjectOpen()) {
        title = ProjectManager::instance().projectName() + QStringLiteral(" — ") + title;
    }
    
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

void MainWindow::showCentralView(QWidget *widget)
{
    m_editorView->setVisible(widget == m_editorView);
    m_corkboardView->setVisible(widget == m_corkboardView);
    m_imagePreview->setVisible(widget == m_imagePreview);
    m_pdfViewer->setVisible(widget == m_pdfViewer);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Fix KateCompletionWidget positioning.
    // Kate's updatePosition() uses parentWidget()->geometry() for bounds checking,
    // but on Wayland the geometry position includes window decoration offsets (e.g. y=25
    // for title bar) while mapFromGlobal treats the widget's (0,0) as global (0,0).
    // This causes the bounds check to use wrong values, letting the popup extend
    // below the visible window area. We fix this by deferring a reposition after show.
    if (event->type() == QEvent::Show
        && QString::fromLatin1(watched->metaObject()->className()).contains(QLatin1String("Completion"))
        && m_editorView) {
        QWidget *popup = qobject_cast<QWidget*>(watched);
        if (popup && popup->parentWidget()) {
            // Defer repositioning to after the Show event is fully processed
            QTimer::singleShot(0, this, [this, popup]() {
                if (!popup->isVisible() || !m_editorView) return;

                KTextEditor::Cursor cursor = m_editorView->cursorPosition();
                QPoint cursorLocal = m_editorView->cursorToCoordinate(cursor);

                if (cursorLocal == QPoint(-1, -1)) return;

                // Map cursor position to popup's parent coordinate space
                QPoint cursorInParent = m_editorView->mapTo(popup->parentWidget(), cursorLocal);

                // Estimate line height from the editor view's font
                int lineHeight = m_editorView->fontMetrics().height() + 2;

                int x = popup->x(); // Keep Kate's x positioning
                int y = cursorInParent.y() + lineHeight; // Below cursor
                int parentHeight = popup->parentWidget()->height();

                // If popup would extend below the window, move it above the cursor
                if (y + popup->height() > parentHeight) {
                    y = cursorInParent.y() - popup->height();
                }
                // Clamp to top of window
                if (y < 0) {
                    y = 0;
                }

                popup->move(x, y);
                popup->raise(); // Ensure popup is above all sibling widgets (centralWidget etc)
            });
        }
    }

    // Intercept project-tree drops onto the editor view to insert markdown links.
    // We only handle drops that carry our custom mime type (from ProjectTreeModel::mimeData).
    const auto evType = event->type();
    if (evType == QEvent::DragEnter || evType == QEvent::DragMove || evType == QEvent::Drop) {
        QWidget *w = qobject_cast<QWidget*>(watched);
        if (w && m_editorView && (w == m_editorView || m_editorView->isAncestorOf(w))) {
            if (evType == QEvent::DragEnter) {
                auto *e = static_cast<QDragEnterEvent*>(event);
                if (e->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))
                        && e->mimeData()->hasUrls()) {
                    e->acceptProposedAction();
                    return true;
                }
            } else if (evType == QEvent::DragMove) {
                auto *e = static_cast<QDragMoveEvent*>(event);
                if (e->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))
                        && e->mimeData()->hasUrls()) {
                    e->acceptProposedAction();
                    return true;
                }
            } else { // Drop
                auto *e = static_cast<QDropEvent*>(event);
                if (e->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-treeitem"))
                        && e->mimeData()->hasUrls()) {
                    // Map drop position into editor-view coordinates and place the cursor there
                    QPoint localPt = w->mapTo(m_editorView, e->position().toPoint());
                    KTextEditor::Cursor cursor = m_editorView->coordinatesToCursor(localPt);
                    if (cursor.isValid()) {
                        m_editorView->setCursorPosition(cursor);
                    }

                    // Decode item pointers to get project names; pair each with its URL
                    QList<QPair<QString, QUrl>> items;
                    QByteArray encoded = e->mimeData()->data(QStringLiteral("application/x-rpgforge-treeitem"));
                    QList<QUrl> urls = e->mimeData()->urls();
                    const int count = encoded.size() / static_cast<int>(sizeof(ProjectTreeItem*));
                    for (int i = 0; i < count && i < urls.size(); ++i) {
                        ProjectTreeItem *dragItem = *reinterpret_cast<ProjectTreeItem**>(
                            encoded.data() + i * static_cast<int>(sizeof(ProjectTreeItem*)));
                        QString name = (dragItem && !dragItem->name.isEmpty())
                            ? dragItem->name
                            : QFileInfo(urls[i].toLocalFile()).completeBaseName();
                        items.append({name, urls[i]});
                    }
                    // Fallback: if pointer decoding produced fewer entries than URLs
                    for (int i = items.size(); i < urls.size(); ++i) {
                        items.append({QFileInfo(urls[i].toLocalFile()).completeBaseName(), urls[i]});
                    }

                    insertProjectLinksAtCursor(items);
                    e->acceptProposedAction();
                    return true;
                }
            }
        }
    }

    return KXmlGuiWindow::eventFilter(watched, event);
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

    if (ProjectManager::instance().isProjectOpen()) {
        settings.setValue(QStringLiteral("lastProject"), ProjectManager::instance().projectFilePath());
    } else {
        settings.remove(QStringLiteral("lastProject"));
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

    if (settings.contains(QStringLiteral("lastProject"))) {
        QString projectPath = settings.value(QStringLiteral("lastProject")).toString();
        if (QFile::exists(projectPath)) {
            ProjectManager::instance().openProject(projectPath);
            if (m_fileExplorer) {
                m_fileExplorer->setRootPath(ProjectManager::instance().projectPath());
            }
        }
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

void MainWindow::insertProjectLinksAtCursor(const QList<QPair<QString, QUrl>> &items)
{
    if (!m_document || !m_editorView || items.isEmpty()) return;

    // Base directory: directory of the open document, or the project root
    QString baseDir;
    if (!m_document->url().isEmpty() && m_document->url().isLocalFile()) {
        baseDir = QFileInfo(m_document->url().toLocalFile()).absolutePath();
    } else if (ProjectManager::instance().isProjectOpen()) {
        baseDir = ProjectManager::instance().projectPath();
    }

    static const QStringList imgSuffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
        QStringLiteral("bmp")
    };

    QStringList parts;
    for (const auto &[name, url] : items) {
        if (!url.isLocalFile()) continue;
        const QString absPath = url.toLocalFile();
        const QString relPath = baseDir.isEmpty()
            ? absPath
            : QDir(baseDir).relativeFilePath(absPath);
        const QString suffix = QFileInfo(absPath).suffix().toLower();

        if (imgSuffixes.contains(suffix)) {
            parts << QStringLiteral("![%1](%2)").arg(name, relPath);
        } else {
            parts << QStringLiteral("[%1](%2)").arg(name, relPath);
        }
    }

    if (parts.isEmpty()) return;

    const QString insertText = parts.join(QLatin1Char('\n'));
    m_document->insertText(m_editorView->cursorPosition(), insertText);
}
