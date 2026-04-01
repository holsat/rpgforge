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
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
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

    // Register variable autocomplete
    auto *completionModel = new VariableCompletionModel(this);
    m_editorView->registerCompletionModel(completionModel);
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
    
    m_centralStack = new QStackedWidget(editorContainer);
    m_corkboardView = new CorkboardView(this);
    m_imagePreview = new ImagePreview(this);
    m_pdfViewer = new QWebEngineView(this);
    m_centralStack->addWidget(m_editorView);
    m_centralStack->addWidget(m_corkboardView);
    m_centralStack->addWidget(m_imagePreview);
    m_centralStack->addWidget(m_pdfViewer);
    
    vbox->addWidget(m_centralStack);

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
            m_centralStack->setCurrentWidget(m_editorView);
        }
    });
    connect(m_projectTree, &ProjectTreePanel::folderActivated, this, [this](ProjectTreeItem *folder) {
        if (folder->name.toLower() == QStringLiteral("media")) {
            // Switch to editor or something neutral, or just stay on last view
            return;
        }
        m_corkboardView->setFolder(folder);
        m_centralStack->setCurrentWidget(m_corkboardView);
    });
    connect(m_corkboardView, &CorkboardView::fileActivated, this, [this](const QString &relativePath) {
        if (ProjectManager::instance().isProjectOpen()) {
            QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relativePath);
            openFileFromUrl(QUrl::fromLocalFile(fullPath));
            m_centralStack->setCurrentWidget(m_editorView);
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
        
        static QStringList imgSuffixes = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), 
                                        QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"), 
                                        QStringLiteral("bmp")};
        
        if (imgSuffixes.contains(suffix)) {
            if (m_imagePreview->loadImage(path)) {
                m_centralStack->setCurrentWidget(m_imagePreview);
                if (m_previewPanel) m_previewPanel->hide();
                return;
            }
        } else if (suffix == QLatin1String("pdf")) {
            m_pdfViewer->setUrl(url);
            m_centralStack->setCurrentWidget(m_pdfViewer);
            if (m_previewPanel) m_previewPanel->hide();
            return;
        }
        
        m_document->openUrl(url);
        m_centralStack->setCurrentWidget(m_editorView);
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
    if (m_document) {
        QString text = m_document->text();

        // Parse YAML front-matter
        auto frontMatterVars = VariableManager::parseFrontMatter(text);
        VariableManager::instance().setDocumentVariables(frontMatterVars);

        QString contentOnly = VariableManager::stripMetadata(text);

        if (m_outlinePanel) {
            m_outlinePanel->documentChanged(contentOnly);
        }
        if (m_previewPanel) {
            m_previewPanel->setMarkdown(contentOnly);
        }
        
        updateErrorHighlighting();
    }
}

void MainWindow::updateErrorHighlighting()
{
    if (!m_document) return;

    // Clear old ranges
    qDeleteAll(m_errorRanges);
    m_errorRanges.clear();

    static KTextEditor::Attribute::Ptr errorAttr;
    if (!errorAttr) {
        errorAttr = new KTextEditor::Attribute();
        errorAttr->setUnderlineStyle(QTextCharFormat::WaveUnderline);
        errorAttr->setUnderlineColor(Qt::red);
    }

    QString text = m_document->text();
    auto vars = VariableManager::instance().variableNames();

    auto offsetToCursor = [&text](int offset) -> KTextEditor::Cursor {
        int line = 0;
        int col = 0;
        for (int i = 0; i < offset && i < text.length(); ++i) {
            if (text.at(i) == QLatin1Char('\n')) {
                line++;
                col = 0;
            } else {
                col++;
            }
        }
        return KTextEditor::Cursor(line, col);
    };

    // Pattern 1: Unresolved variables {{var}}
    static QRegularExpression unresolvedRegex(QStringLiteral("\\{\\{([A-Za-z0-9_\\.]+)\\}\\}"));
    auto it = unresolvedRegex.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        QString name = match.captured(1);
        if (!vars.contains(name)) {
            KTextEditor::Range range(offsetToCursor(match.capturedStart()), offsetToCursor(match.capturedEnd()));
            auto *mRange = m_document->newMovingRange(range);
            mRange->setAttribute(errorAttr);
            m_errorRanges.append(mRange);
        }
    }

    // Pattern 2: Potential syntax errors {var} (single braces)
    static QRegularExpression singleBraceRegex(QStringLiteral("(?<!\\{)\\{([A-Za-z0-9_\\.]+)\\}(?!\\})"));
    auto it2 = singleBraceRegex.globalMatch(text);
    while (it2.hasNext()) {
        auto match = it2.next();
        KTextEditor::Range range(offsetToCursor(match.capturedStart()), offsetToCursor(match.capturedEnd()));
        auto *mRange = m_document->newMovingRange(range);
        mRange->setAttribute(errorAttr);
        m_errorRanges.append(mRange);
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
