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
#include "gitservice.h"
#include "clonedialog.h"
#include "visualdiffview.h"
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
#include "settingsdialog.h"
#include "chatpanel.h"
#include "simulationpanel.h"
#include "charactergenerator.h"
#include "simulationcomparedialog.h"
#include "problemspanel.h"
#include "analyzerservice.h"
#include "llmservice.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "knowledgebase.h"
#include "metadatadialog.h"
#include "newprojectdialog.h"
#include "projecttreemodel.h"
#include "sidebar.h"
#include "synopsisservice.h"
#include "onboardingwizard.h"
#include "scrivenerimporter.h"
#include "documentconverter.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KStandardAction>
#include <KToolBar>
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
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QLineEdit>
#include <QWebEngineView>

#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setupEditor();
    setupSidebar();
    setupActions();

    setupGUI(Default, QStringLiteral(":/rpgforgeui.rc"));

    // Merge KTextEditor::View's GUI (Edit, View, Selection, Tools menus)
    guiFactory()->addClient(m_editorView);

    // Kompare part GUI is added/removed dynamically in showCentralView() so its
    // toolbar only appears when the diff view is actually active.

    // Hide duplicate save actions from the merged client to avoid toolbar clutter
    if (auto *editorAction = m_editorView->action(QStringLiteral("file_save"))) editorAction->setVisible(false);
    if (auto *editorAction = m_editorView->action(QStringLiteral("file_save_as"))) editorAction->setVisible(false);
    if (m_researchView) {
        if (auto *editorAction = m_researchView->action(QStringLiteral("file_save"))) editorAction->setVisible(false);
        if (auto *editorAction = m_researchView->action(QStringLiteral("file_save_as"))) editorAction->setVisible(false);
    }

    connect(&LLMService::instance(), &LLMService::modelNotFound,
            this, &MainWindow::onModelNotFound);

    updateTitle();
    resize(1400, 900);

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    if (!settings.value(QStringLiteral("firstRunComplete"), false).toBool()) {
        OnboardingWizard wizard(this);
        if (wizard.exec() == QDialog::Accepted) {
            // After successful onboarding, if a project was created, open the README
            if (ProjectManager::instance().isProjectOpen()) {
                QString readmePath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(QStringLiteral("research/README.md"));
                if (QFile::exists(readmePath)) {
                    openFileFromUrl(QUrl::fromLocalFile(readmePath));
                }
            }
        }
    }

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

KTextEditor::View* MainWindow::activeView() const
{
    if (m_researchView && m_researchView->isVisible() && m_researchView->hasFocus()) {
        return m_researchView;
    }
    return m_editorView;
}

KTextEditor::Document* MainWindow::activeDocument() const
{
    auto *view = activeView();
    if (view == m_researchView) return m_researchDocument;
    return m_document;
}

void MainWindow::setupEditor()
{
    m_editor = KTextEditor::Editor::instance();
    if (!m_editor) return;

    // Main Manuscript Document
    m_document = m_editor->createDocument(this);
    m_editorView = m_document->createView(this);
    m_document->setHighlightingMode(QStringLiteral("Markdown"));

    // Research Document (for split view)
    m_researchDocument = m_editor->createDocument(this);
    m_researchView = m_researchDocument->createView(this);
    m_researchDocument->setHighlightingMode(QStringLiteral("Markdown"));

    m_editorSplitter = new QSplitter(Qt::Horizontal, this);
    m_editorSplitter->addWidget(m_editorView);
    m_editorSplitter->addWidget(m_researchView);
    m_researchView->hide(); // hidden until a research file is opened

    // Shared signals for both editors
    auto setupConnections = [this](KTextEditor::Document *doc, KTextEditor::View *view) {
        connect(doc, &KTextEditor::Document::textChanged, this, &MainWindow::onTextChanged);
        connect(view, &KTextEditor::View::cursorPositionChanged, this, &MainWindow::onCursorPositionChanged);
        connect(view, &KTextEditor::View::contextMenuAboutToShow, this, &MainWindow::showEditorContextMenu);
    };

    setupConnections(m_document, m_editorView);
    setupConnections(m_researchDocument, m_researchView);

    connect(m_editorView, &KTextEditor::View::verticalScrollPositionChanged, this, &MainWindow::syncScroll);
    connect(m_researchView, &KTextEditor::View::verticalScrollPositionChanged, this, &MainWindow::syncScroll);

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

    m_analyzerDebounce = new QTimer(this);
    m_analyzerDebounce->setSingleShot(true);
    m_analyzerDebounce->setInterval(5000); // 5s debounce for LLM analysis
    connect(m_analyzerDebounce, &QTimer::timeout, this, [this]() {
        auto *doc = activeDocument();
        if (doc && doc->url().isLocalFile()) {
            AnalyzerService::instance().analyzeDocument(doc->url().toLocalFile(), doc->text());
        }
    });

    connect(&AnalyzerService::instance(), &AnalyzerService::diagnosticsUpdated, this, &MainWindow::onDiagnosticsUpdated);

    // Connect signals
    auto connectDocSignals = [this](KTextEditor::Document *doc) {
        connect(doc, &KTextEditor::Document::documentUrlChanged, this, &MainWindow::updateTitle);
        connect(doc, &KTextEditor::Document::modifiedChanged, this, &MainWindow::updateTitle);
    };
    connectDocSignals(m_document);
    connectDocSignals(m_researchDocument);

    // Step 1: Seamless Version Control (Auto-Sync)
    auto setupAutoSync = [this](KTextEditor::Document *doc) {
        connect(doc, &KTextEditor::Document::documentSavedOrUploaded, this, [this](KTextEditor::Document *d) {
            if (ProjectManager::instance().isProjectOpen() && ProjectManager::instance().autoSync()) {
                GitService::instance().autoCommit(d->url().toLocalFile());
            }
        });
    };
    setupAutoSync(m_document);
    setupAutoSync(m_researchDocument);

    // Register variable autocomplete safely after the view is fully initialized
    QTimer::singleShot(500, this, [this]() {
        auto setupEditorView = [this](KTextEditor::View *view) {
            if (!view) return;
            view->setAutomaticInvocationEnabled(true);
            auto *completionModel = new VariableCompletionModel(this);
            view->registerCompletionModel(completionModel);

            view->installEventFilter(this);
            const auto children = view->findChildren<QWidget*>();
            for (QWidget *child : children) {
                child->installEventFilter(this);
            }
        };

        setupEditorView(m_editorView);
        setupEditorView(m_researchView);

        // Install event filter on KateCompletionWidget to fix popup positioning/z-order
        for (QObject *child : this->children()) {
            QWidget *w = qobject_cast<QWidget*>(child);
            if (w && QString::fromLatin1(w->metaObject()->className()).contains(QLatin1String("Completion"))) {
                w->installEventFilter(this);
            }
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
    m_chatPanel = new ChatPanel(this);
    m_simulationPanel = new SimulationPanel(this);

    // Create the sidebar and add panels
    m_sidebar = new Sidebar(this);
    m_fileExplorerId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("folder-symbolic")),
        i18n("Project Explorer"),
        explorerStack);
    m_outlineId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("view-list-tree-symbolic")),
        QStringLiteral("Document Outline"),
        m_outlinePanel);
    m_variablesId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("code-variable-symbolic")),
        QStringLiteral("Variables"),
        m_variablesPanel);
    m_chatId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("chat-conversation-symbolic")),
        i18n("AI Writing Assistant"),
        m_chatPanel);
    m_simulationId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("media-playback-start-symbolic")),
        i18n("Rule Simulation"),
        m_simulationPanel);

    connect(m_chatPanel, &ChatPanel::insertTextAtCursor, this, [this](const QString &text) {
        if (m_document && m_editorView) {
            m_editorView->setFocus();
            
            // Typewriter effect
            int *index = new int(0);
            QTimer *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, [this, timer, index, text]() {
                if (*index < text.length()) {
                    // Insert a chunk of characters for speed/smoothness balance
                    int chunk = qMin(5, text.length() - *index);
                    m_document->insertText(m_editorView->cursorPosition(), text.mid(*index, chunk));
                    *index += chunk;
                } else {
                    timer->stop();
                    timer->deleteLater();
                    delete index;
                }
            });
            timer->start(20); // 20ms between chunks
        }
    });

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
    m_diffView = new VisualDiffView(this);
    connect(m_diffView, &VisualDiffView::saveRequested, this, [this](const QString &path) {
        if (m_document->url().toLocalFile() == path) {
            m_document->openUrl(QUrl::fromLocalFile(path));
        }
    });
    connect(m_diffView, &VisualDiffView::reloadRequested, this, [this](const QString &path) {
        if (m_document->url().toLocalFile() == path) {
            m_document->openUrl(QUrl::fromLocalFile(path));
        }
    });
    m_pdfViewer = new QWebEngineView(this);

    m_centralViewLayout->addWidget(m_editorSplitter);
    m_centralViewLayout->addWidget(m_corkboardView);
    m_centralViewLayout->addWidget(m_imagePreview);
    m_centralViewLayout->addWidget(m_diffView);
    m_centralViewLayout->addWidget(m_pdfViewer);

    // Only show the editor view initially; hide the rest
    m_corkboardView->hide();
    m_imagePreview->hide();
    m_diffView->hide();
    m_pdfViewer->hide();
    vbox->addWidget(viewContainer);

    m_mainSplitter->addWidget(m_sidebar);
    m_mainSplitter->addWidget(editorContainer);
    m_mainSplitter->addWidget(m_previewPanel);

    // Set initial sizes: sidebar (250px), editor (1), preview (1)
    m_mainSplitter->setSizes({250, 1150, 0});
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);
    m_previewPanel->hide();

    m_problemsPanel = new ProblemsPanel(this);

    SynopsisService::instance().setModel(m_projectTree->model());

    connect(m_projectTree->model(), &ProjectTreeModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        if (roles.contains(ProjectTreeModel::SynopsisRole) || roles.contains(ProjectTreeModel::StatusRole)) {
            // If the corkboard is showing a folder that contains one of these items, refresh it
            // Simple approach: if corkboard is visible, just refresh it.
            if (m_corkboardView->isVisible()) {
                m_corkboardView->setFolder(m_corkboardView->currentFolder());
            }
        }
    });

    m_diagnosticsStatus = new QLabel(i18n("0 Errors, 0 Warnings, 0 Info"), this);
    m_diagnosticsStatus->setContentsMargins(5, 0, 5, 0);
    statusBar()->addPermanentWidget(m_diagnosticsStatus);

    m_wordCountStatus = new QLabel(i18n("Words: 0"), this);
    m_wordCountStatus->setContentsMargins(5, 0, 5, 0);
    statusBar()->addPermanentWidget(m_wordCountStatus);

    m_projectStatsStatus = new QLabel(i18n("Project: 0 words"), this);
    m_projectStatsStatus->setContentsMargins(5, 0, 5, 0);
    statusBar()->addPermanentWidget(m_projectStatsStatus);
    m_projectStatsStatus->hide(); // only shown if project open

    m_syncStatusLabel = new QLabel(this);
    m_syncStatusLabel->setContentsMargins(5, 0, 5, 0);
    statusBar()->addWidget(m_syncStatusLabel);
    m_syncStatusLabel->hide();

    m_syncProgressBar = new QProgressBar(this);
    m_syncProgressBar->setMaximumWidth(150);
    m_syncProgressBar->setMaximumHeight(15);
    m_syncProgressBar->setTextVisible(false);
    statusBar()->addWidget(m_syncProgressBar);
    m_syncProgressBar->hide();

    connect(m_projectTree, &ProjectTreePanel::syncStarted, this, [this]() {
        m_syncStatusLabel->setText(i18n("Syncing..."));
        m_syncStatusLabel->show();
        m_syncProgressBar->setValue(0);
        m_syncProgressBar->show();
    });

    connect(m_projectTree, &ProjectTreePanel::syncProgress, this, [this](int value, const QString &message) {
        m_syncProgressBar->setValue(value);
        m_syncStatusLabel->setText(message);
    });

    connect(m_projectTree, &ProjectTreePanel::syncFinished, this, [this](bool success, const QString &message) {
        if (!success) {
            m_syncStatusLabel->setText(i18n("Sync Failed: %1", message));
            QTimer::singleShot(5000, m_syncStatusLabel, &QWidget::hide);
        } else {
            m_syncStatusLabel->setText(i18n("Sync Complete"));
            QTimer::singleShot(3000, m_syncStatusLabel, &QWidget::hide);
        }
        m_syncProgressBar->hide();
    });

    connect(m_problemsPanel, &ProblemsPanel::statsChanged, this, [this](int errors, int warnings, int infos) {
        m_diagnosticsStatus->setText(i18n("%1 Errors, %2 Warnings, %3 Info", errors, warnings, infos));
    });

    // Reveal the Problems panel with a minimum height when diagnostics arrive,
    // so users don't have to know to drag up the invisible splitter handle.
    connect(&AnalyzerService::instance(), &AnalyzerService::diagnosticsUpdated, this,
            [this](const QString &, const QList<Diagnostic> &diagnostics) {
        if (!diagnostics.isEmpty()) {
            QList<int> sizes = m_vSplitter->sizes();
            if (sizes.size() >= 2 && sizes[1] < 120) {
                sizes[1] = 120;
                sizes[0] = qMax(sizes[0] - 120, 0);
                m_vSplitter->setSizes(sizes);
            }
        }
    });

    m_vSplitter = new QSplitter(Qt::Vertical, centralWidget);
    m_vSplitter->addWidget(m_mainSplitter);
    m_vSplitter->addWidget(m_problemsPanel);
    m_vSplitter->setStretchFactor(0, 1);
    m_vSplitter->setStretchFactor(1, 0);

    hbox->addWidget(m_vSplitter, 1);

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
    connect(m_projectTree, &ProjectTreePanel::diffRequested, this, &MainWindow::showDiff);
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

    connect(m_problemsPanel, &ProblemsPanel::issueActivated, this, [this](const QString &filePath, int line) {
        openFileFromUrl(QUrl::fromLocalFile(filePath));
        navigateToLine(line);
    });

    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, [this]() {
        if (m_corkboardView) m_corkboardView->setFolder(nullptr);
        KnowledgeBase::instance().initForProject(ProjectManager::instance().projectPath());
        m_projectStatsStatus->show();
        updateProjectStats();
        SynopsisService::instance().scanProject();
    });
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, [this]() {
        if (m_corkboardView) {
            m_corkboardView->setFolder(nullptr);
            showCentralView(m_editorView);
        }
        m_projectStatsStatus->hide();
        KnowledgeBase::instance().close();
    });
    connect(m_document, &KTextEditor::Document::documentSavedOrUploaded, this, [this]() {
        updateProjectStats();
        if (ProjectManager::instance().isProjectOpen() && m_document->url().isLocalFile()) {
            QString filePath = m_document->url().toLocalFile();
            QString relPath = QDir(ProjectManager::instance().projectPath()).relativeFilePath(filePath);

            SynopsisService::instance().requestUpdate(relPath, true);
            // Note: autoCommit is handled by the setupAutoSync connection in setupEditor;
            // calling it here again would create a duplicate commit on every save.
            KnowledgeBase::instance().indexFile(filePath);
        }
    });
    connect(m_document, &KTextEditor::Document::documentUrlChanged, this, [this]() {
        updateTitle();
        if (!m_currentUrl.isEmpty() && m_currentUrl.isLocalFile() && ProjectManager::instance().isProjectOpen()) {
            QString relPath = QDir(ProjectManager::instance().projectPath()).relativeFilePath(m_currentUrl.toLocalFile());
            // User navigated away from m_currentUrl — request background update
            SynopsisService::instance().requestUpdate(relPath, true);
        }
        m_currentUrl = m_document->url();
    });
    connect(m_projectTree->model(), &ProjectTreeModel::modelReset, this, &MainWindow::updateProjectStats);
    connect(m_projectTree->model(), &ProjectTreeModel::rowsInserted, this, &MainWindow::updateProjectStats);
    connect(m_projectTree->model(), &ProjectTreeModel::rowsRemoved, this, &MainWindow::updateProjectStats);

    // Reload preview stylesheet when project settings change (e.g., stylesheet path)
    connect(&ProjectManager::instance(), &ProjectManager::projectSettingsChanged, this, [this]() {
        if (m_previewPanel) {
            m_previewPanel->reloadStylesheet();
        }
    });
}

void MainWindow::setupActions()
{
    KStandardAction::openNew(this, &MainWindow::newFile, actionCollection());
    KStandardAction::open(this, &MainWindow::openFile, actionCollection());
    KStandardAction::save(this, &MainWindow::saveFile, actionCollection());
    KStandardAction::saveAs(this, &MainWindow::saveFileAs, actionCollection());
    
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

    auto *cloneProjectAct = new QAction(this);
    cloneProjectAct->setText(i18n("Clone Project from GitHub..."));
    cloneProjectAct->setIcon(QIcon::fromTheme(QStringLiteral("project-development-pull")));
    actionCollection()->addAction(QStringLiteral("project_clone"), cloneProjectAct);
    connect(cloneProjectAct, &QAction::triggered, this, &MainWindow::cloneProject);

    auto *closeProjectAct = new QAction(this);
    closeProjectAct->setText(i18n("Close Project"));
    closeProjectAct->setIcon(QIcon::fromTheme(QStringLiteral("project-development-close")));
    actionCollection()->addAction(QStringLiteral("project_close"), closeProjectAct);
    connect(closeProjectAct, &QAction::triggered, this, &MainWindow::closeProject);

    auto *importScrivenerAct = new QAction(this);
    importScrivenerAct->setText(i18n("Import Scrivener Project..."));
    importScrivenerAct->setIcon(QIcon::fromTheme(QStringLiteral("document-import")));
    actionCollection()->addAction(QStringLiteral("project_import_scrivener"), importScrivenerAct);
    connect(importScrivenerAct, &QAction::triggered, this, &MainWindow::importScrivener);

    auto *importWordAct = new QAction(this);
    importWordAct->setText(i18n("Import Word Documents..."));
    importWordAct->setIcon(QIcon::fromTheme(QStringLiteral("document-import")));
    actionCollection()->addAction(QStringLiteral("project_import_word"), importWordAct);
    connect(importWordAct, &QAction::triggered, this, &MainWindow::importWord);

    auto *charGenAct = new QAction(this);
    charGenAct->setText(i18n("AI Character Generator..."));
    charGenAct->setIcon(QIcon::fromTheme(QStringLiteral("user-identity")));
    actionCollection()->addAction(QStringLiteral("project_character_generator"), charGenAct);
    actionCollection()->setDefaultShortcut(charGenAct, Qt::CTRL | Qt::SHIFT | Qt::Key_C);
    connect(charGenAct, &QAction::triggered, this, &MainWindow::characterGenerator);

    auto *startSimAct = new QAction(this);
    startSimAct->setText(i18n("Start Simulation"));
    startSimAct->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    actionCollection()->addAction(QStringLiteral("simulation_start"), startSimAct);
    actionCollection()->setDefaultShortcut(startSimAct, Qt::Key_F5);
    connect(startSimAct, &QAction::triggered, this, &MainWindow::startSimulation);

    auto *compareSimAct = new QAction(this);
    compareSimAct->setText(i18n("Compare Simulation Results..."));
    compareSimAct->setIcon(QIcon::fromTheme(QStringLiteral("insert-link")));
    actionCollection()->addAction(QStringLiteral("simulation_compare"), compareSimAct);
    actionCollection()->setDefaultShortcut(compareSimAct, Qt::CTRL | Qt::SHIFT | Qt::Key_D);
    connect(compareSimAct, &QAction::triggered, this, &MainWindow::compareSimulations);

    auto *projectSettingsAct = new QAction(this);
    projectSettingsAct->setText(i18n("Project Settings..."));
    projectSettingsAct->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    actionCollection()->addAction(QStringLiteral("project_settings"), projectSettingsAct);
    connect(projectSettingsAct, &QAction::triggered, this, &MainWindow::projectSettings);

    auto *globalSettingsAct = KStandardAction::preferences(this, &MainWindow::globalSettings, actionCollection());
    globalSettingsAct->setText(i18n("Configure RPG Forge..."));
    actionCollection()->addAction(QStringLiteral("global_settings"), globalSettingsAct);

    auto *expandAct = new QAction(this);
    expandAct->setText(i18n("AI: Expand Selection"));
    expandAct->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    actionCollection()->addAction(QStringLiteral("ai_expand"), expandAct);
    actionCollection()->setDefaultShortcut(expandAct, Qt::CTRL | Qt::ALT | Qt::Key_E);
    connect(expandAct, &QAction::triggered, this, &MainWindow::aiExpand);

    auto *rewriteAct = new QAction(this);
    rewriteAct->setText(i18n("AI: Rewrite Selection"));
    rewriteAct->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    actionCollection()->addAction(QStringLiteral("ai_rewrite"), rewriteAct);
    actionCollection()->setDefaultShortcut(rewriteAct, Qt::CTRL | Qt::ALT | Qt::Key_R);
    connect(rewriteAct, &QAction::triggered, this, &MainWindow::aiRewrite);

    auto *summarizeAct = new QAction(this);
    summarizeAct->setText(i18n("AI: Summarize Selection"));
    summarizeAct->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    actionCollection()->addAction(QStringLiteral("ai_summarize"), summarizeAct);
    actionCollection()->setDefaultShortcut(summarizeAct, Qt::CTRL | Qt::ALT | Qt::Key_S);
    connect(summarizeAct, &QAction::triggered, this, &MainWindow::aiSummarize);

    auto *compileAct = new QAction(this);
    compileAct->setText(i18n("Compile to PDF..."));
    compileAct->setIcon(QIcon::fromTheme(QStringLiteral("document-export-pdf")));
    actionCollection()->addAction(QStringLiteral("compile_project"), compileAct);
    actionCollection()->setDefaultShortcut(compileAct, Qt::CTRL | Qt::SHIFT | Qt::Key_P);
    connect(compileAct, &QAction::triggered, this, &MainWindow::compileToPdf);

    KStandardAction::quit(qApp, &QApplication::quit, actionCollection());

    m_togglePreviewAction = new QAction(this);
    m_togglePreviewAction->setText(i18n("Show Preview"));
    m_togglePreviewAction->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    m_togglePreviewAction->setCheckable(true);
    m_togglePreviewAction->setChecked(false);
    actionCollection()->addAction(QStringLiteral("toggle_preview"), m_togglePreviewAction);
    actionCollection()->setDefaultShortcut(m_togglePreviewAction, Qt::CTRL | Qt::Key_P);
    connect(m_togglePreviewAction, &QAction::triggered, this, &MainWindow::togglePreview);

    auto *searchBoxAction = new QWidgetAction(this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Search document or project..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(250);
    searchBoxAction->setDefaultWidget(m_searchEdit);
    actionCollection()->addAction(QStringLiteral("toolbar_search"), searchBoxAction);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchEdit) {
            performSearch(m_searchEdit->text());
        }
    });

    auto *projectPreviewAct = new QAction(this);
    projectPreviewAct->setText(i18n("Project Preview Mode"));
    projectPreviewAct->setIcon(QIcon::fromTheme(QStringLiteral("view-list-tree")));
    projectPreviewAct->setCheckable(true);
    actionCollection()->addAction(QStringLiteral("project_preview_mode"), projectPreviewAct);
    connect(projectPreviewAct, &QAction::triggered, this, [this](bool enabled) {
        if (m_previewPanel) {
            m_previewPanel->setProjectMode(enabled);
            if (enabled) {
                updateProjectPreview();
            } else if (m_document) {
                m_previewPanel->setMarkdown(m_document->text());
            }
        }
    });

    auto *focusModeAct = new QAction(this);
    focusModeAct->setText(i18n("Focus Mode"));
    focusModeAct->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
    focusModeAct->setCheckable(true);
    actionCollection()->addAction(QStringLiteral("focus_mode"), focusModeAct);
    actionCollection()->setDefaultShortcut(focusModeAct, Qt::CTRL | Qt::SHIFT | Qt::Key_F);
    connect(focusModeAct, &QAction::triggered, this, &MainWindow::toggleFocusMode);
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

        // Check if this is a research file
        bool isResearch = false;
        if (ProjectManager::instance().isProjectOpen()) {
            QString relPath = QDir(ProjectManager::instance().projectPath()).relativeFilePath(path);
            ProjectTreeItem *item = m_projectTree->model()->findItem(relPath);
            if (item) {
                // Check ancestors for Research category
                ProjectTreeItem *p = item;
                while (p) {
                    if (p->category == ProjectTreeItem::Research || 
                        p->category == ProjectTreeItem::Characters ||
                        p->category == ProjectTreeItem::Places ||
                        p->category == ProjectTreeItem::Cultures) {
                        isResearch = true;
                        break;
                    }
                    p = p->parent;
                }
            }
        }

        if (isResearch) {
            m_researchDocument->openUrl(url);
            m_researchView->show();
            // Ensure first view stays visible
            m_editorView->show();
        } else {
            m_document->openUrl(url);
            m_researchView->hide();
        }

        showCentralView(m_editorSplitter);
        if (m_previewPanel && m_togglePreviewAction->isChecked()) {
            m_previewPanel->show();
            m_previewPanel->setBaseUrl(url);
        }
        const QString fileName = url.fileName();
        if (fileName.endsWith(QLatin1String(".md")) ||
            fileName.endsWith(QLatin1String(".markdown")) ||
            fileName.endsWith(QLatin1String(".mkd"))) {
            if (isResearch) m_researchDocument->setHighlightingMode(QStringLiteral("Markdown"));
            else m_document->setHighlightingMode(QStringLiteral("Markdown"));
        }
        updateTitle();
        onTextChanged();
        saveSession();
    }
}

void MainWindow::saveFile()
{
    auto *doc = activeDocument();
    if (doc->url().isEmpty()) {
        saveFileAs();
    } else {
        doc->save();
    }
    ProjectManager::instance().saveProject();
}

void MainWindow::saveFileAs()
{
    const QUrl url = QFileDialog::getSaveFileUrl(this, i18n("Save File As"), QUrl(),
        i18n("Markdown Files (*.md *.markdown);;All Files (*)"));
    if (!url.isEmpty()) {
        auto *doc = activeDocument();
        doc->saveAs(url);
        if (m_previewPanel && !m_researchView->hasFocus()) {
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
                ProjectManager::instance().setupDefaultProject(dir, name);
                updateTitle();
                
                // Automatically open the README
                QString readmePath = QDir(dir).absoluteFilePath(QStringLiteral("research/README.md"));
                if (QFile::exists(readmePath)) {
                    openFileFromUrl(QUrl::fromLocalFile(readmePath));
                }
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

void MainWindow::cloneProject()
{
    CloneDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString url = dialog.url();
        QString path = dialog.localPath();

        if (url.isEmpty() || path.isEmpty()) return;

        QProgressDialog progress(i18n("Cloning Project..."), i18n("Cancel"), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.show();

        auto future = GitService::instance().cloneRepo(url, path);
        
        // Wait for clone to finish (non-blocking for UI events due to processEvents)
        while (!future.isFinished()) {
            QApplication::processEvents();
            if (progress.wasCanceled()) break;
        }

        if (future.result()) {
            // Check for rpgforge.project in the cloned directory
            QString projectFile = QDir(path).absoluteFilePath(QStringLiteral("rpgforge.project"));
            if (QFile::exists(projectFile)) {
                if (ProjectManager::instance().openProject(projectFile)) {
                    m_fileExplorer->setRootPath(ProjectManager::instance().projectPath());
                    updateTitle();
                    QMessageBox::information(this, i18n("Clone Success"), i18n("Project cloned and opened successfully."));
                }
            } else {
                QMessageBox::warning(this, i18n("Clone Complete"), 
                    i18n("Project cloned, but no 'rpgforge.project' file was found in the repository."));
                m_fileExplorer->setRootPath(path);
            }
        } else if (!progress.wasCanceled()) {
            QMessageBox::critical(this, i18n("Clone Error"), i18n("Failed to clone the repository. Check your URL and internet connection."));
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
    if (activeDocument()) {
        if (m_textChangeDebounce) m_textChangeDebounce->start();
        if (m_analyzerDebounce) m_analyzerDebounce->start();
    }
}

void MainWindow::updateErrorHighlighting()
{
    auto *doc = activeDocument();
    if (!doc) return;

    QString text = doc->text();

    // 1. Sync front-matter variables
    auto frontMatterVars = VariableManager::parseFrontMatter(text);
    VariableManager::instance().setDocumentVariables(frontMatterVars);

    // 2. Update auxiliary views
    QString contentOnly = VariableManager::stripMetadata(text);
    if (m_outlinePanel) m_outlinePanel->documentChanged(contentOnly);
    
    auto *projectPreviewAct = actionCollection()->action(QStringLiteral("project_preview_mode"));
    if (projectPreviewAct && projectPreviewAct->isChecked()) {
        updateProjectPreview();
    } else if (m_previewPanel) {
        m_previewPanel->setMarkdown(contentOnly);
    }

    // 3. Update word count
    // Simple word count: split by whitespace and filter out empty strings
    int wordCount = contentOnly.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).count();
    m_wordCountStatus->setText(i18n("Words: %1", wordCount));

    // 4. Highlight undefined variable references with red squiggly underline
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

    for (int line = 0; line < doc->lines(); ++line) {
        const QString lineText = doc->line(line);
        QRegularExpressionMatchIterator it = varRefRegex.globalMatch(lineText);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString varName = match.captured(1);
            if (!knownVars.contains(varName)) {
                // Unknown variable — mark the entire {{varname}} with error underline
                int startCol = match.capturedStart(0);
                int endCol = match.capturedEnd(0);
                KTextEditor::Range range(line, startCol, line, endCol);
                KTextEditor::MovingRange *mr = doc->newMovingRange(range);
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

    // Typewriter scrolling (keep cursor centered)
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    if (settings.value(QStringLiteral("editor/typewriterScrolling"), false).toBool()) {
        auto *view = activeView();
        if (view) {
            // TODO: Centering cursor in KF6 KTextEditor::View
            view->setCursorPosition(view->cursorPosition());
        }
    }
}

void MainWindow::updateCursorContext()
{
    auto *view = activeView();
    if (!view) return;
    int line = view->cursorPosition().line();

    if (m_outlinePanel) {
        m_outlinePanel->highlightForLine(line);
    }
    if (m_breadcrumbBar) {
        m_breadcrumbBar->updateForLine(line);
    }
}

void MainWindow::navigateToLine(int line)
{
    auto *view = activeView();
    if (view) {
        view->setCursorPosition(KTextEditor::Cursor(line, 0));
        view->setFocus();
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
    auto *view = activeView();
    if (!view || !m_previewPanel || !m_previewPanel->isVisible()) return;

    // Use scrollToLine with smooth=false for real-time synchronization
    int currentLine = view->firstDisplayedLine();
    m_previewPanel->scrollToLine(currentLine, false);
}

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("RPG Forge");
    if (ProjectManager::instance().isProjectOpen()) {
        title = ProjectManager::instance().projectName() + QStringLiteral(" — ") + title;
    }

    auto *doc = activeDocument();
    if (doc && !doc->url().isEmpty()) {
        title = doc->url().fileName() + QStringLiteral(" — ") + title;
    } else {
        title = i18n("Untitled") + QStringLiteral(" — ") + title;
    }
    if (doc && doc->isModified()) {
        title.prepend(QStringLiteral("* "));
    }
    setWindowTitle(title);
}
void MainWindow::showCentralView(QWidget *widget)
{
    m_editorSplitter->setVisible(widget == m_editorSplitter || widget == m_editorView || widget == m_researchView);
    m_corkboardView->setVisible(widget == m_corkboardView);
    m_imagePreview->setVisible(widget == m_imagePreview);
    m_diffView->setVisible(widget == m_diffView);
    m_pdfViewer->setVisible(widget == m_pdfViewer);

    // Add the Kompare part's toolbar only while the diff view is visible,
    // so its actions never pollute the primary toolbar.
    if (m_diffView && m_diffView->part()) {
        const bool showingDiff = (widget == m_diffView);
        if (showingDiff && !m_diffClientAdded) {
            guiFactory()->addClient(m_diffView->part());
            m_diffClientAdded = true;
        } else if (!showingDiff && m_diffClientAdded) {
            guiFactory()->removeClient(m_diffView->part());
            m_diffClientAdded = false;
        }
    }
}

void MainWindow::showDiff(const QString &path1, const QString &path2OrHash1, const QString &hash2)
{
    // If path2OrHash1 looks like a file path (starts with /), compare files.
    // Otherwise, treat as git hashes.
    if (path2OrHash1.startsWith(QLatin1Char('/'))) {
        m_diffView->setFiles(path1, path2OrHash1);
    } else {
        m_diffView->setDiff(path1, path2OrHash1, hash2);
    }
    showCentralView(m_diffView);
    if (m_previewPanel) m_previewPanel->hide();
}

void MainWindow::performSearch(const QString &text)
{
    if (text.isEmpty()) return;

    auto *view = activeView();
    auto *doc = activeDocument();

    if (view && doc && !doc->url().isEmpty()) {
        // Document Search: Use KTextEditor's search functionality
        KTextEditor::SearchOptions options = KTextEditor::Default;
        KTextEditor::Cursor start = view->cursorPosition();
        
        // Find next occurrence
        QList<KTextEditor::Range> results = doc->searchText(KTextEditor::Range(start, doc->documentEnd()), text, options);
        KTextEditor::Range range = results.isEmpty() ? KTextEditor::Range::invalid() : results.first();
        
        if (!range.isValid()) {
            // Wrap around
            results = doc->searchText(KTextEditor::Range(doc->documentRange().start(), start), text, options);
            range = results.isEmpty() ? KTextEditor::Range::invalid() : results.first();
        }

        if (range.isValid()) {
            view->setSelection(range);
            view->setCursorPosition(range.start());
        } else {
            // Not found in current document, fall back to global search if project is open
            if (ProjectManager::instance().isProjectOpen()) {
                m_sidebar->showPanel(m_chatId);
                m_chatPanel->askAI(i18n("Search project for: %1", text));
            }
        }
    } else if (ProjectManager::instance().isProjectOpen()) {
        // Global Search: Ask AI / KnowledgeBase via Chat Panel
        m_sidebar->showPanel(m_chatId);
        m_chatPanel->askAI(i18n("Search project for: %1", text));
    }
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
        && QString::fromLatin1(watched->metaObject()->className()).contains(QLatin1String("Completion"))) {
        
        auto *view = activeView();
        if (!view) return false;

        QWidget *popup = qobject_cast<QWidget*>(watched);
        if (popup && popup->parentWidget()) {
            // Defer repositioning to after the Show event is fully processed
            QTimer::singleShot(0, this, [this, popup, view]() {
                if (!popup->isVisible() || !view) return;

                KTextEditor::Cursor cursor = view->cursorPosition();
                QPoint cursorLocal = view->cursorToCoordinate(cursor);

                if (cursorLocal == QPoint(-1, -1)) return;

                // Map cursor position to popup's parent coordinate space
                QPoint cursorInParent = view->mapTo(popup->parentWidget(), cursorLocal);

                // Estimate line height from the editor view's font
                int lineHeight = view->fontMetrics().height() + 2;

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
        auto *view = activeView();
        if (w && view && (w == view || view->isAncestorOf(w))) {
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
                    // Map drop position into active view coordinates and place the cursor there
                    QPoint localPt = w->mapTo(view, e->position().toPoint());
                    KTextEditor::Cursor cursor = view->coordinatesToCursor(localPt);
                    if (cursor.isValid()) {
                        view->setCursorPosition(cursor);
                    }

                    // Decode item paths (encoded as newline-separated relative paths)
                    // and pair each with its URL. Resolving by path is safe even if
                    // the tree reloaded between dragStart and drop.
                    QList<QPair<QString, QUrl>> items;
                    const QStringList paths = QString::fromUtf8(
                        e->mimeData()->data(QStringLiteral("application/x-rpgforge-treeitem"))
                    ).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    QList<QUrl> urls = e->mimeData()->urls();
                    for (int i = 0; i < paths.size() && i < urls.size(); ++i) {
                        QString name = QFileInfo(paths[i]).completeBaseName();
                        if (name.isEmpty()) name = QFileInfo(urls[i].toLocalFile()).completeBaseName();
                        items.append({name, urls[i]});
                    }
                    // Fallback: if paths had fewer entries than URLs
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

    // Middle click scrolling fix
    if (event->type() == QEvent::MouseButtonPress) {
        auto *e = static_cast<QMouseEvent*>(event);
        auto *w = qobject_cast<QWidget*>(watched);
        auto *view = activeView();
        if (w && view && (w == view || view->isAncestorOf(w))) {
            if (e->button() == Qt::MiddleButton) {
                QPoint localPt = w->mapTo(view, e->position().toPoint());
                KTextEditor::Cursor cursor = view->coordinatesToCursor(localPt);
                if (cursor.isValid()) {
                    view->setCursorPosition(cursor);
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
        auto cursor = m_editorView->cursorPosition();
        settings.setValue(QStringLiteral("cursorLine"), cursor.line());
        settings.setValue(QStringLiteral("cursorColumn"), cursor.column());
    } else {
        settings.remove(QStringLiteral("lastFile"));
    }

    if (m_researchDocument && !m_researchDocument->url().isEmpty()) {
        settings.setValue(QStringLiteral("lastResearchFile"), m_researchDocument->url().toString());
        auto cursor = m_researchView->cursorPosition();
        settings.setValue(QStringLiteral("researchCursorLine"), cursor.line());
        settings.setValue(QStringLiteral("researchCursorColumn"), cursor.column());
        settings.setValue(QStringLiteral("researchVisible"), m_researchView->isVisible());
    } else {
        settings.remove(QStringLiteral("lastResearchFile"));
    }

    if (m_editorSplitter) {
        settings.setValue(QStringLiteral("editorSplitter"), m_editorSplitter->saveState());
    }

    if (m_mainSplitter) {
        settings.setValue(QStringLiteral("mainSplitter"), m_mainSplitter->saveState());
    }

    if (m_vSplitter) {
        settings.setValue(QStringLiteral("vSplitter"), m_vSplitter->saveState());
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

    if (settings.contains(QStringLiteral("lastResearchFile"))) {
        QUrl url(settings.value(QStringLiteral("lastResearchFile")).toString());
        if (url.isValid() && url.isLocalFile() && QFile::exists(url.toLocalFile())) {
            m_researchDocument->openUrl(url);
            if (settings.value(QStringLiteral("researchVisible"), false).toBool()) {
                m_researchView->show();
            }
            int line = settings.value(QStringLiteral("researchCursorLine"), 0).toInt();
            int col = settings.value(QStringLiteral("researchCursorColumn"), 0).toInt();
            if (m_researchView) {
                m_researchView->setCursorPosition(KTextEditor::Cursor(line, col));
            }
        }
    }

    if (m_editorSplitter && settings.contains(QStringLiteral("editorSplitter"))) {
        m_editorSplitter->restoreState(settings.value(QStringLiteral("editorSplitter")).toByteArray());
    }

    if (m_mainSplitter && settings.contains(QStringLiteral("mainSplitter"))) {
        m_mainSplitter->restoreState(settings.value(QStringLiteral("mainSplitter")).toByteArray());
    }

    if (m_vSplitter && settings.contains(QStringLiteral("vSplitter"))) {
        m_vSplitter->restoreState(settings.value(QStringLiteral("vSplitter")).toByteArray());
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
    auto *doc = activeDocument();
    auto *view = activeView();
    if (!doc || !view || items.isEmpty()) return;

    // Base directory: directory of the open document, or the project root
    QString baseDir;
    if (!doc->url().isEmpty() && doc->url().isLocalFile()) {
        baseDir = QFileInfo(doc->url().toLocalFile()).absolutePath();
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
    doc->insertText(view->cursorPosition(), insertText);
}

void MainWindow::globalSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // Models might have changed, refresh the list in chat panel
    }
}

void MainWindow::aiExpand()
{
    auto *view = activeView();
    if (!view || !m_chatPanel) return;
    QString selection = view->selectionText();
    if (selection.isEmpty()) return;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString promptsJson = settings.value(QStringLiteral("llm/prompts")).toString();
    QString prompt = i18n("Please expand on the following worldbuilding text, adding more detail and lore.");
    if (!promptsJson.isEmpty()) {
        QJsonObject obj = QJsonDocument::fromJson(promptsJson.toUtf8()).object();
        prompt = obj.value(i18n("Expand")).toString();
    }

    m_sidebar->showPanel(m_chatId);
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection);
}

void MainWindow::aiRewrite()
{
    auto *view = activeView();
    if (!view || !m_chatPanel) return;
    QString selection = view->selectionText();
    if (selection.isEmpty()) return;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString promptsJson = settings.value(QStringLiteral("llm/prompts")).toString();
    QString prompt = i18n("Please rewrite the following text for better flow and impact.");
    if (!promptsJson.isEmpty()) {
        QJsonObject obj = QJsonDocument::fromJson(promptsJson.toUtf8()).object();
        prompt = obj.value(i18n("Rewrite")).toString();
    }

    m_sidebar->showPanel(m_chatId);
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection);
}

void MainWindow::aiSummarize()
{
    auto *view = activeView();
    if (!view || !m_chatPanel) return;
    QString selection = view->selectionText();
    if (selection.isEmpty()) return;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString promptsJson = settings.value(QStringLiteral("llm/prompts")).toString();
    QString prompt = i18n("Please summarize the following rules or worldbuilding text.");
    if (!promptsJson.isEmpty()) {
        QJsonObject obj = QJsonDocument::fromJson(promptsJson.toUtf8()).object();
        prompt = obj.value(i18n("Summarize")).toString();
    }

    m_sidebar->showPanel(m_chatId);
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection);
}

void MainWindow::onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics)
{
    KTextEditor::Document *doc = nullptr;
    if (m_document && m_document->url().toLocalFile() == filePath) doc = m_document;
    else if (m_researchDocument && m_researchDocument->url().toLocalFile() == filePath) doc = m_researchDocument;

    if (!doc) return;

    // TODO: This currently clears ALL diagnostic ranges, which might affect the other editor.
    // Ideally we'd store ranges per-document.
    for (auto *r : m_diagnosticRanges) {
        delete r;
    }
    m_diagnosticRanges.clear();

    for (const Diagnostic &d : diagnostics) {
        if (d.line < 0 || d.line >= doc->lines()) continue;

        QString lineText = doc->line(d.line);
        int startCol = lineText.length() - lineText.trimmed().length(); // Skip leading whitespace
        int endCol = lineText.length();

        KTextEditor::Range range(d.line, startCol, d.line, endCol);
        auto *mr = doc->newMovingRange(range);
        
        KTextEditor::Attribute::Ptr attr(new KTextEditor::Attribute());
        if (d.severity == DiagnosticSeverity::Error) {
            attr->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            attr->setUnderlineColor(Qt::red);
        } else if (d.severity == DiagnosticSeverity::Warning) {
            attr->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            attr->setUnderlineColor(Qt::yellow);
        } else {
            attr->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            attr->setUnderlineColor(Qt::blue);
        }
        
        mr->setAttribute(attr);
        mr->setAttributeOnlyForViews(true);
        
        m_diagnosticRanges.append(mr);
    }
}

void MainWindow::showEditorContextMenu(KTextEditor::View *view, QMenu *menu)
{
    if (!view || !menu) return;

    // Check if we've already added the AI menu to this specific QMenu object.
    // KTextEditor sometimes reuses the menu or different plugins add to it.
    for (auto *action : menu->actions()) {
        if (action->text() == i18n("AI Assistant") || action->menu() && action->menu()->title() == i18n("AI Assistant")) {
            return;
        }
    }

    menu->addSeparator();
    auto *aiMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("chat-conversation")), i18n("AI Assistant"));
    
    auto *expand = aiMenu->addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Expand Selection"));
    connect(expand, &QAction::triggered, this, &MainWindow::aiExpand);
    
    auto *rewrite = aiMenu->addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Rewrite Selection"));
    connect(rewrite, &QAction::triggered, this, &MainWindow::aiRewrite);
    
    auto *summarize = aiMenu->addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Summarize Selection"));
    connect(summarize, &QAction::triggered, this, &MainWindow::aiSummarize);

    if (view->selectionText().isEmpty()) {
        aiMenu->setEnabled(false);
        aiMenu->setToolTip(i18n("Please select text to use AI actions."));
    }
}

void MainWindow::importScrivener()
{
    QString scrivPath = QFileDialog::getExistingDirectory(this, i18n("Select Scrivener Project (.scriv)"), QDir::homePath());
    if (scrivPath.isEmpty()) return;

    if (!ProjectManager::instance().isProjectOpen()) {
        QMessageBox::warning(this, i18n("No Project Open"), i18n("Please open or create an RPG Forge project before importing."));
        return;
    }

    ScrivenerImporter importer;
    auto *progressDialog = new QProgressDialog(i18n("Importing Scrivener Project..."), i18n("Cancel"), 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    
    connect(&importer, &ScrivenerImporter::progress, progressDialog, &QProgressDialog::setValue);
    connect(&importer, &ScrivenerImporter::progress, progressDialog, [progressDialog](int, const QString &message) {
        progressDialog->setLabelText(message);
    });

    importer.import(scrivPath, ProjectManager::instance().projectPath(), m_projectTree->model());
    
    ProjectManager::instance().setTree(m_projectTree->model()->projectData());
    ProjectManager::instance().saveProject();
    
    progressDialog->close();
    delete progressDialog;
    
    QMessageBox::information(this, i18n("Import Complete"), i18n("Scrivener project imported successfully."));
}

void MainWindow::importWord()
{
    QStringList files = QFileDialog::getOpenFileNames(this, i18n("Select Word/RTF Documents"), QDir::homePath(), 
                                                    i18n("Documents (*.docx *.rtf *.pdf);;All Files (*)"));
    if (files.isEmpty()) return;

    if (!ProjectManager::instance().isProjectOpen()) {
        QMessageBox::warning(this, i18n("No Project Open"), i18n("Please open or create an RPG Forge project before importing."));
        return;
    }

    DocumentConverter converter;
    QString projectDir = ProjectManager::instance().projectPath();
    QString mediaDir = projectDir + QStringLiteral("/media");
    
    ProjectTreeModel *model = m_projectTree->model();
    
    // 1. Find Manuscript folder
    QModelIndex manuscriptIdx;
    ProjectTreeItem *rootItem = model->itemFromIndex(QModelIndex());
    for (int i = 0; i < rootItem->children.count(); ++i) {
        if (rootItem->children[i]->category == ProjectTreeItem::Manuscript) {
            manuscriptIdx = model->index(i, 0, QModelIndex());
            break;
        }
    }
    
    // Fallback to current selection or root if Manuscript not found
    if (!manuscriptIdx.isValid()) {
        manuscriptIdx = m_projectTree->currentIndex();
    }
    
    for (const QString &file : files) {
        QFileInfo info(file);
        QString baseName = info.baseName();
        QString safeName = DocumentConverter::sanitizePrefix(baseName);
        
        // Create a subfolder for this specific document
        QString folderRelPath = QStringLiteral("manuscript/") + safeName;
        if (!manuscriptIdx.isValid()) folderRelPath = safeName; // fallback
        
        QModelIndex targetFolderIdx = model->addFolder(baseName, folderRelPath, manuscriptIdx);
        
        auto result = converter.convertToMarkdown(file, safeName, mediaDir);
        
        if (result.success) {
            QString relPath = folderRelPath + QDir::separator() + safeName + QStringLiteral(".md");
            QString absPath = QDir(projectDir).absoluteFilePath(relPath);
            
            QDir().mkpath(QFileInfo(absPath).absolutePath());
            
            QFile outFile(absPath);
            if (outFile.open(QIODevice::WriteOnly)) {
                outFile.write(result.markdown.toUtf8());
                outFile.close();
                model->addFile(baseName, relPath, targetFolderIdx);
            }
        } else {
            QMessageBox::warning(this, i18n("Import Error"), i18n("Failed to convert %1: %2", baseName, result.error));
        }
    }
    
    ProjectManager::instance().setTree(model->projectData());
    ProjectManager::instance().saveProject();
    
    QMessageBox::information(this, i18n("Import Complete"), i18n("%1 documents imported successfully.", files.count()));
}

void MainWindow::updateProjectStats()
{
    if (!ProjectManager::instance().isProjectOpen()) {
        m_projectStatsStatus->hide();
        return;
    }

    int totalWords = ProjectManager::instance().calculateTotalWordCount();
    m_projectStatsStatus->setText(i18n("Project: %1 words", totalWords));
    m_projectStatsStatus->show();
}

static void collectProjectMarkdown(ProjectTreeItem *folder, QString &markdown, int &chapterCounter) {
    if (!folder) return;
    
    bool isChapter = (folder->category == ProjectTreeItem::Chapter);

    if (isChapter) {
        chapterCounter++;
        if (!markdown.isEmpty()) markdown += QStringLiteral("\n\n<div class=\"page-break\"></div>\n\n");
        markdown += QStringLiteral("# Chapter %1: %2\n\n").arg(QString::number(chapterCounter), folder->name);
    }

    for (auto *child : folder->children) {
        if (child->type == ProjectTreeItem::File) {
            QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(child->path);
            QFile file(fullPath);
            if (file.open(QIODevice::ReadOnly)) {
                if (!markdown.isEmpty() && child->category != ProjectTreeItem::Scene && !isChapter) {
                     markdown += QStringLiteral("\n\n<div class=\"page-break\"></div>\n\n");
                }
                markdown += VariableManager::stripMetadata(QString::fromUtf8(file.readAll())) + QStringLiteral("\n\n");
            }
        } else {
            collectProjectMarkdown(child, markdown, chapterCounter);
        }
    }
}

void MainWindow::updateProjectPreview()
{
    if (!m_previewPanel || !ProjectManager::instance().isProjectOpen()) return;

    QString markdown;
    auto treeData = ProjectManager::instance().tree();
    ProjectTreeModel model;
    model.setProjectData(treeData);
    
    // Find Manuscript folder - STRICTLY limit to this
    ProjectTreeItem *manuscript = nullptr;
    ProjectTreeItem *root = model.itemFromIndex(QModelIndex());
    for (auto *child : root->children) {
        if (child->category == ProjectTreeItem::Manuscript) {
            manuscript = child;
            break;
        }
    }

    if (manuscript) {
        int chapterCounter = 0;
        collectProjectMarkdown(manuscript, markdown, chapterCounter);
    } else {
        // If no Manuscript folder exists, show a helpful message instead of the whole project
        markdown = i18n("> **Note:** To see a project preview, please designate a folder as your **Manuscript** in the Project Explorer.");
    }
    
    m_previewPanel->setMarkdown(markdown);
}

void MainWindow::toggleFocusMode()
{
    auto *focusAct = actionCollection()->action(QStringLiteral("focus_mode"));
    bool active = focusAct->isChecked();

    m_sidebar->setVisible(!active);
    m_previewPanel->setVisible(!active && m_togglePreviewAction->isChecked());
    m_breadcrumbBar->setVisible(!active);
    m_problemsPanel->setVisible(!active);
    statusBar()->setVisible(!active);
    
    // Hide toolbars
    for (auto *toolbar : findChildren<KToolBar*>()) {
        toolbar->setVisible(!active);
    }

    if (active) {
        showFullScreen();
    } else {
        showNormal();
    }
}

void MainWindow::characterGenerator()
{
    CharacterGenerator gen(this);
    gen.exec();
}

void MainWindow::startSimulation()
{
    if (m_simulationPanel) {
        m_sidebar->showPanel(m_simulationId);
        m_simulationPanel->startSimulation();
    }
}

void MainWindow::compareSimulations()
{
    QString projectPath = ProjectManager::instance().projectPath();
    if (projectPath.isEmpty()) return;

    QString simDir = QDir(projectPath).absoluteFilePath(QStringLiteral("simulations"));
    
    QString pathA = QFileDialog::getOpenFileName(this, i18n("Select First Result"), simDir, i18n("JSON Files (*.json)"));
    if (pathA.isEmpty()) return;

    QString pathB = QFileDialog::getOpenFileName(this, i18n("Select Second Result"), simDir, i18n("JSON Files (*.json)"));
    if (pathB.isEmpty()) return;

    QFile fileA(pathA);
    QFile fileB(pathB);

    if (fileA.open(QIODevice::ReadOnly) && fileB.open(QIODevice::ReadOnly)) {
        QJsonObject objA = QJsonDocument::fromJson(fileA.readAll()).object();
        QJsonObject objB = QJsonDocument::fromJson(fileB.readAll()).object();
        
        SimulationCompareDialog dialog(objA, objB, this);
        dialog.exec();
    }
}

void MainWindow::onModelNotFound(LLMProvider provider, const QString &invalidModel, const QStringList &available)
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(i18n("Model Unavailable"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(dlg);
    auto *label = new QLabel(i18n(
        "The model <b>%1</b> is no longer available from this provider.<br/>"
        "Select a replacement model to use instead:", invalidModel), dlg);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *combo = new QComboBox(dlg);
    combo->addItems(available);
    layout->addWidget(combo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    connect(dlg, &QDialog::accepted, this, [combo]() {
        const QString selected = combo->currentText();
        if (!selected.isEmpty())
            LLMService::instance().retryWithModel(selected);
    });

    dlg->open();
    Q_UNUSED(provider)
}





