#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <numeric>
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

#include "mainwindow.h"
#include "gitservice.h"
#include "clonedialog.h"
#include "visualdiffview.h"
#include "breadcrumbbar.h"
#include "compiledialog.h"
#include "explorersidebar.h"
#include "corkboardview.h"
#include "imagepreview.h"
#include "imagediffview.h"
#include "fileexplorer.h"
#include "gitpanel.h"
#include "outlinepanel.h"
#include "pdfexporter.h"
#include "previewpanel.h"
#include "projecttreepanel.h"
#include "variablespanel.h"
#include "variablemanager.h"
#include "variablecompletionmodel.h"
#include "inlineaiinvoker.h"
#include "inlineaicompletionmodel.h"
#include "debuglog.h"
#include "projectmanager.h"
#include "projectsettingsdialog.h"
#include "reconciliationdialog.h"
#include "settingsdialog.h"
#include "chatpanel.h"
#include "simulationpanel.h"
#include "charactergenerator.h"
#include "simulationcomparedialog.h"
#include "problemspanel.h"
#include "analyzerservice.h"
#include "llmservice.h"
#include "librarianservice.h"
#include "librariandatabase.h"
#include "entitygraphpanel.h"
#include "entitycommunitydetector.h"
#include "lorekeeperservice.h"
#include "agentgatekeeper.h"
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
#include "explorationspanel.h"
#include "explorationsgraphview.h"
#include "versionrecallbrowser.h"
#include "unsavedchangesdialog.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardAction>
#include <KToolBar>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Attribute>
#include <KXMLGUIFactory>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <QApplication>
#include <QCloseEvent>
#include <QToolTip>
#include <QHelpEvent>
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
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QWidgetAction>
#include <QLineEdit>
#include <QWebEngineView>

#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setupEditor();

    m_librarianService = new LibrarianService(&LLMService::instance(), this);
    AgentGatekeeper::instance().setLibrarianService(m_librarianService);
    LoreKeeperService::instance().init(&LLMService::instance(), m_librarianService);

    connect(m_librarianService, &LibrarianService::entityUpdated, this, &MainWindow::updateLibrarianHighlights);
    connect(m_librarianService, &LibrarianService::libraryVariablesChanged, this, [](const QMap<QString, QString> &vars) {
        RPGFORGE_DLOG("VARS") << "MainWindow: libraryVariablesChanged received"
                              << vars.size() << "entries; pushing into VariableManager";
        VariableManager::instance().setLibraryVariables(vars);
    });

    setupSidebar();
    if (m_variablesPanel) {
        m_variablesPanel->setLibrarianService(m_librarianService);
        connect(m_variablesPanel, &VariablesPanel::forceLoreKeeperScan, this, &MainWindow::onForceLoreScan);
        connect(&LoreKeeperService::instance(), &LoreKeeperService::loreUpdateStarted, m_variablesPanel, &VariablesPanel::onLoreScanStarted);
        connect(&LoreKeeperService::instance(), &LoreKeeperService::loreUpdateFinished, m_variablesPanel, &VariablesPanel::onLoreScanFinished);
    }
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
                AgentGatekeeper::instance().resumeAll();
                QString readmePath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(QStringLiteral("research/README.md"));
                if (QFile::exists(readmePath)) {
                    openFileFromUrl(QUrl::fromLocalFile(readmePath));
                }
            }
        }
        settings.setValue(QStringLiteral("firstRunComplete"), true);
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
    m_editorView->focusProxy()->installEventFilter(this);
    m_document->setHighlightingMode(QStringLiteral("Markdown"));

    // Research Document (for split view)
    m_researchDocument = m_editor->createDocument(this);
    m_researchView = m_researchDocument->createView(this);
    m_researchView->focusProxy()->installEventFilter(this);
    m_researchDocument->setHighlightingMode(QStringLiteral("Markdown"));

    m_editorSplitter = new QSplitter(Qt::Horizontal, this);
    m_editorSplitter->addWidget(m_editorView);

    // Wrap the research view in a small container that includes a close
    // button at the top, so the user has an obvious way to dismiss the
    // split when they are done with it. Hidden by default; only shown when
    // the user opens a Research file (which switches the split mode on).
    m_researchPane = new QWidget(this);
    {
        auto *paneLayout = new QVBoxLayout(m_researchPane);
        paneLayout->setContentsMargins(0, 0, 0, 0);
        paneLayout->setSpacing(0);

        auto *paneToolbar = new QHBoxLayout();
        paneToolbar->setContentsMargins(4, 2, 4, 2);
        auto *paneTitle = new QLabel(i18n("Research"), m_researchPane);
        QFont titleFont = paneTitle->font();
        titleFont.setBold(true);
        paneTitle->setFont(titleFont);
        paneToolbar->addWidget(paneTitle);
        paneToolbar->addStretch();
        auto *closeBtn = new QToolButton(m_researchPane);
        closeBtn->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
        closeBtn->setToolTip(i18n("Close Research View (returns to single-pane editor)"));
        closeBtn->setAutoRaise(true);
        connect(closeBtn, &QToolButton::clicked, this, [this]() {
            m_researchPane->hide();
            if (m_researchDocument) m_researchDocument->closeUrl();
        });
        paneToolbar->addWidget(closeBtn);
        paneLayout->addLayout(paneToolbar);
        paneLayout->addWidget(m_researchView, 1);
    }
    m_editorSplitter->addWidget(m_researchPane);

    m_editorView->show();
    m_researchPane->hide(); // hidden until a research file is opened

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

    // Load per-keystroke settings cache.
    {
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        m_typewriterScrolling = s.value(
            QStringLiteral("editor/typewriterScrolling"), false).toBool();
    }

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

    // Trigger the post-extraction maintenance chain after EITHER the
    // KnowledgeBase finishes indexing OR the LibrarianService finishes
    // a scan. Either event can leave chunk_entities, mention_count, or
    // community membership stale. The helper itself is idempotent and
    // cheap when nothing has changed.
    //
    // Wired ONCE at startup — Qt::UniqueConnection cannot be used here
    // because lambdas have no comparable identity for dedupe, so an
    // attempt with that flag triggers a fatal assert. The lambdas
    // re-read services each invocation so they remain correct across
    // project switches.
    connect(&KnowledgeBase::instance(), &KnowledgeBase::indexingFinished,
            this, [this]() { runEntityIndexMaintenance(); });
    if (m_librarianService) {
        connect(m_librarianService, &LibrarianService::scanningFinished,
                this, [this]() { runEntityIndexMaintenance(); });
    }

    // Connect signals
    auto connectDocSignals = [this](KTextEditor::Document *doc) {
        connect(doc, &KTextEditor::Document::documentUrlChanged, this, &MainWindow::updateTitle);
        connect(doc, &KTextEditor::Document::modifiedChanged, this, &MainWindow::updateTitle);
    };
    connectDocSignals(m_document);
    connectDocSignals(m_researchDocument);

    // Notify the Analyzer panel which document is currently open so it
    // can sort diagnostics for that document to the top of the list
    // (and optionally filter to it). Only the main editor's URL counts
    // — the research pane is auxiliary.
    connect(m_document, &KTextEditor::Document::documentUrlChanged, this, [this]() {
        if (m_problemsPanel) {
            m_problemsPanel->setCurrentDocument(m_document->url().toLocalFile());
        }
    });

    // Step 1: Seamless Version Control (Auto-Sync)
    auto setupAutoSync = [this](KTextEditor::Document *doc) {
        connect(doc, &KTextEditor::Document::documentSavedOrUploaded, this, [](KTextEditor::Document *d) {
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
            RPGFORGE_DLOG("VARS") << "Registered VariableCompletionModel on view"
                                   << view << "automaticInvocation="
                                   << view->isAutomaticInvocationEnabled();

            // Inline AI invocation (@lore, @forge, @chat, etc.) — Ctrl+Enter
            // on a command line fires the RagAssistService pipeline and
            // streams the response into the document at the original line.
            auto *invoker = new InlineAIInvoker(view, this);

            // Autocomplete popup for @-commands: types `@` → list of
            // registered commands drops in, typing more filters it,
            // Enter on a selection inserts "@<name> ".
            auto *aiCompletion = new InlineAICompletionModel(invoker, this);
            view->registerCompletionModel(aiCompletion);

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
    // Create the sidebar panels. OutlinePanel is hosted inside the Project
    // Explorer tab (alongside the project tree and filesystem browser) so
    // users can see document structure and project structure simultaneously.
    m_fileExplorer = new FileExplorer(this);
    m_projectTree = new ProjectTreePanel(this);
    m_outlinePanel = new OutlinePanel(this);
    auto *explorerStack = new ExplorerSidebar(m_projectTree, m_outlinePanel, m_fileExplorer, this);

    m_gitPanel = new GitPanel(this);
    m_explorationsPanel = new ExplorationsPanel(this);
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
    // Outline now lives inside the Project Explorer tab (ExplorerSidebar);
    // no longer a standalone tab.
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
    m_explorationsId = m_sidebar->addPanel(
        QIcon::fromTheme(QStringLiteral("vcs-branch")),
        i18n("Explorations"),
        m_explorationsPanel);

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

    // Explorations panel connections
    connect(m_explorationsPanel, &ExplorationsPanel::switchRequested,
            this, &MainWindow::onSwitchExplorationRequested);
    connect(m_explorationsPanel, &ExplorationsPanel::integrateRequested,
            this, &MainWindow::onIntegrateExplorationRequested);
    connect(m_explorationsPanel, &ExplorationsPanel::createLandmarkRequested,
            this, &MainWindow::onCreateLandmarkRequested);

    connect(&GitService::instance(), &GitService::explorationSwitchFailed,
            this, [this](const QString &reason) {
                KMessageBox::error(this, reason, i18n("Switch Failed"));
            });
    connect(&GitService::instance(), &GitService::stashApplyBlockedByDirtyTree,
            this, [this] {
                KMessageBox::information(this,
                    i18n("Please save or park your current changes before restoring parked changes."),
                    i18n("Cannot Restore"));
            });

    // ProjectTreePanel recall version
    connect(m_projectTree, &ProjectTreePanel::recallVersionRequested,
            this, &MainWindow::onRecallVersionRequested);

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

    // Conflict resolution banner (hidden by default). Uses palette tokens so it
    // adapts to the active theme and meets WCAG 2.1 AA contrast requirements.
    m_conflictBanner = new QFrame(this);
    m_conflictBanner->setFrameShape(QFrame::StyledPanel);
    m_conflictBanner->setStyleSheet(
        QStringLiteral("QFrame { background: palette(alternate-base); "
                       "border-left: 3px solid #FF8F00; padding: 4px; }"));
    m_conflictBanner->setVisible(false);
    {
        auto *bl = new QHBoxLayout(m_conflictBanner);
        bl->setContentsMargins(8, 4, 8, 4);
        m_conflictBannerLabel = new QLabel(m_conflictBanner);
        m_conflictBannerLabel->setStyleSheet(
            QStringLiteral("color: palette(text); font-weight: bold;"));
        const QString buttonStyle = QStringLiteral(
            "QPushButton { color: palette(button-text); background: palette(button); "
            "border: 1px solid palette(mid); border-radius: 3px; padding: 4px 12px; "
            "font-weight: bold; }"
            "QPushButton:hover { background: palette(light); }");
        m_conflictNextBtn = new QPushButton(i18n("Next Conflict →"), m_conflictBanner);
        m_conflictNextBtn->setStyleSheet(buttonStyle);
        auto *resolveAllBtn = new QPushButton(i18n("Complete Integration"), m_conflictBanner);
        resolveAllBtn->setStyleSheet(buttonStyle);
        bl->addWidget(m_conflictBannerLabel, 1);
        bl->addWidget(m_conflictNextBtn);
        bl->addWidget(resolveAllBtn);

        connect(m_conflictNextBtn, &QPushButton::clicked, this, [this] {
            ++m_conflictIndex;
            showNextConflict();
        });
        connect(resolveAllBtn, &QPushButton::clicked, this, [this] {
            QString repoPath = ProjectManager::instance().projectPath();
            GitService::instance().getConflictingFiles(repoPath)
                .then(this, [this, repoPath](QList<ConflictFile> remaining) {
                    if (!remaining.isEmpty()) {
                        KMessageBox::information(this,
                            i18n("There are still %1 unresolved conflicts. "
                                 "Please resolve all conflicts before completing the integration.",
                                 remaining.size()),
                            i18n("Conflicts Remaining"));
                        return;
                    }
                    GitService::instance().commitAll(repoPath, i18n("Integrated exploration"))
                        .then(this, [this](bool) {
                            m_conflictBanner->setVisible(false);
                            m_conflictFiles.clear();
                            m_conflictIndex = 0;
                            m_explorationsPanel->refresh();
                        });
                });
        });
    }
    vbox->addWidget(m_conflictBanner);

    // Use a plain container with QVBoxLayout instead of QStackedWidget.
    // QStackedLayout (NOT QStackedWidget) gives us "only one child visible at a time"
    // semantics without the reparenting that breaks KateCompletionWidget popup
    // positioning. Previously this was a QVBoxLayout with manual setVisible() calls,
    // but if any view's visibility was toggled out of band the editor could end up
    // sharing vertical space with another view (or get hidden behind it). With
    // QStackedLayout::StackOne (default), setCurrentWidget() guarantees exclusivity.
    auto *viewContainer = new QWidget(editorContainer);
    m_centralViewLayout = new QStackedLayout(viewContainer);
    m_centralViewLayout->setContentsMargins(0, 0, 0, 0);

    m_corkboardView = new CorkboardView(this);
    m_imagePreview = new ImagePreview(this);
    m_imageDiffView = new ImageDiffView(this);
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
    connect(m_diffView, &VisualDiffView::closeRequested, this, [this]() {
        showCentralView(m_editorSplitter);
    });
    m_pdfViewer = new QWebEngineView(this);

    // Entity graph view — Phase 2 of the relationship-graph work. The
    // panel is created with a null DB pointer here; setProjectPath
    // wires it up when a project opens (see projectOpened handler).
    m_entityGraphPanel = new EntityGraphPanel(/*db=*/nullptr, this);
    connect(m_entityGraphPanel, &EntityGraphPanel::openDossierRequested,
            this, [this](const QString &entityName) {
        if (entityName.isEmpty()) return;
        // Best-effort dossier resolution: look in lorekeeper/Characters/,
        // lorekeeper/Settings/, etc. Falls back to the project search
        // if no canonical dossier exists yet. The detailed search is
        // delegated to ProjectManager — same path the LoreKeeper UI uses.
        const QString projectDir = ProjectManager::instance().projectPath();
        if (projectDir.isEmpty()) return;
        const QStringList searchRoots = {
            QStringLiteral("lorekeeper/Characters"),
            QStringLiteral("lorekeeper/Settings"),
            QStringLiteral("lorekeeper"),
            QStringLiteral("research/Characters"),
            QStringLiteral("research"),
        };
        for (const QString &root : searchRoots) {
            const QString candidate = QDir(projectDir).absoluteFilePath(
                root + QStringLiteral("/") + entityName + QStringLiteral(".md"));
            if (QFileInfo::exists(candidate)) {
                m_document->openUrl(QUrl::fromLocalFile(candidate));
                showCentralView(m_editorSplitter);
                return;
            }
        }
        qInfo() << "Entity graph: no dossier file found for" << entityName;
    });

    m_centralViewLayout->addWidget(m_editorSplitter);
    m_centralViewLayout->addWidget(m_corkboardView);
    m_centralViewLayout->addWidget(m_imagePreview);
    m_centralViewLayout->addWidget(m_imageDiffView);
    m_centralViewLayout->addWidget(m_diffView);
    m_centralViewLayout->addWidget(m_pdfViewer);
    m_centralViewLayout->addWidget(m_entityGraphPanel);

    // Only show the editor view initially; hide the rest
    showCentralView(m_editorSplitter);
    m_editorView->show();

    vbox->addWidget(viewContainer);

    m_mainSplitter->addWidget(m_sidebar);
    m_mainSplitter->addWidget(editorContainer);
    m_mainSplitter->addWidget(m_previewPanel);

    // Set initial sizes: sidebar (250px), editor (1), preview (1)
    m_mainSplitter->setSizes({250, 1150, 0});
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);
    // 5s autosave timer + close-time save handle splitter persistence.
    // Earlier we hooked splitterMoved → debounced save thinking it
    // would reduce the close-after-drag race, but splitterMoved also
    // fires on programmatic resizes (e.g. when the ProblemsPanel's
    // table-widget sizeHint grows during project open) — that meant
    // the saved state was the auto-grown layout, not the user's
    // dragged choice, and the analyzer always opened maximized.
    // Initial size of 0 hides the preview; do NOT call hide() — togglePreview()
    // intentionally never calls setVisible() to avoid a QtWebEngine render crash.

    // Remember user-chosen preview pane width across show/hide cycles and
    // app restarts. Stored when the user drags the splitter handle; reused
    // by togglePreview() next time preview is shown.
    {
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        m_preferredPreviewWidth = s.value(QStringLiteral("preview/paneWidth"), 0).toInt();
    }
    connect(m_mainSplitter, &QSplitter::splitterMoved, this, [this]() {
        const QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() < 3 || sizes[2] <= 0) return;  // Ignore collapse events.
        m_preferredPreviewWidth = sizes[2];
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        s.setValue(QStringLiteral("preview/paneWidth"), m_preferredPreviewWidth);
    });

    m_problemsPanel = new ProblemsPanel(this);

    // SynopsisService consumes ProjectManager snapshots and writes back via
    // the public setNodeSynopsis() wrapper; no setup wiring needed here.

    connect(&ProjectManager::instance(), &ProjectManager::treeItemDataChanged, this, [this](const QList<int> &roles) {
        if (roles.contains(ProjectTreeModel::SynopsisRole) || roles.contains(ProjectTreeModel::StatusRole)) {
            // If the corkboard is showing a folder that contains one of these items, refresh it
            // Simple approach: if corkboard is visible, just refresh it.
            if (m_corkboardView->isVisible()) {
                m_corkboardView->setFolder(m_corkboardView->currentFolderPath());
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
    // Cap the analyzer pane so it can never overwhelm the editor area
    // even when the ProblemsPanel's QTableWidget has a huge sizeHint
    // (which happens during project-open when emitAllCached dumps
    // hundreds of cached diagnostics into the table). Without the cap,
    // QSplitter respects the child's preferred size and grows the
    // bottom pane, masking the user's saved size on the next layout
    // pass. 600px is more than the user could need for diagnostics
    // at typical viewing distance.
    m_problemsPanel->setMaximumHeight(600);

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
    m_corkboardView->subscribe();  // Consumes snapshots + PM signals — no friended model access.
    connect(m_projectTree, &ProjectTreePanel::folderActivated, this, [this](const QString &folderPath) {
        if (folderPath.isEmpty()) return;
        const QString nameLower = folderPath.section(QDir::separator(), -1).toLower();
        if (nameLower == QStringLiteral("media") || nameLower == QStringLiteral("stylesheets")) {
            return;
        }
        m_corkboardView->setFolder(folderPath);
        showCentralView(m_corkboardView);
    });
    connect(m_projectTree, &ProjectTreePanel::diffRequested, this, &MainWindow::showDiff);
    connect(m_corkboardView, &CorkboardView::fileActivated, this, [this](const QString &relativePath) {
        if (ProjectManager::instance().isProjectOpen()) {
            QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relativePath);
            openFileFromUrl(QUrl::fromLocalFile(fullPath));
        }
    });
    connect(m_corkboardView, &CorkboardView::itemsReordered, this, [](const QString &folderPath, const QString &draggedPath, const QString &targetPath) {
        // Compute target row via ProjectManager::findItem — no model leak.
        ProjectTreeItem *folder = ProjectManager::instance().findItem(folderPath);
        if (!folder) return;
        ProjectTreeItem *target = targetPath.isEmpty() ? nullptr : ProjectManager::instance().findItem(targetPath);
        const int toIdx = target ? folder->children.indexOf(target) : folder->children.size();
        ProjectManager::instance().moveItem(draggedPath, folderPath, toIdx);
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
        if (m_previewPanel && m_document
            && m_centralViewLayout && m_centralViewLayout->currentWidget() == m_editorSplitter) {
            m_previewPanel->setMarkdown(m_document->text());
        }
    });

    connect(m_problemsPanel, &ProblemsPanel::issueActivated, this, [this](const QString &filePath, int line) {
        openFileFromUrl(QUrl::fromLocalFile(filePath));
        navigateToLine(line);
    });

    // Debounce saveExplorationData() to coalesce bursts of colorMapChanged
    // signals (e.g. multiple branches inserted during a single layoutNodes()).
    m_saveExplorationDataTimer = new QTimer(this);
    m_saveExplorationDataTimer->setSingleShot(true);
    m_saveExplorationDataTimer->setInterval(1000);
    connect(m_saveExplorationDataTimer, &QTimer::timeout, this, [this] {
        if (!m_explorationsPanel) return;
        ProjectManager::instance().saveExplorationData(
            GitService::instance().saveWordCountCache(),
            m_explorationsPanel->graphView()->saveColorMap());
    });

    // When exploration color map changes, schedule a debounced persistence.
    connect(m_explorationsPanel->graphView(), &ExplorationGraphView::colorMapChanged,
            this, [this] {
                m_saveExplorationDataTimer->start();
            });

    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, [this]() {
        if (m_corkboardView) m_corkboardView->setFolder(QString());
        QString projectDir = ProjectManager::instance().projectPath();
        KnowledgeBase::instance().initForProject(projectDir);
        // Open the analyzer's per-project cache and replay any stored
        // diagnostics into the UI before the KB reindex below kicks
        // off — that way a fresh launch shows what was already
        // analyzed without waiting for the LLM. The background scan
        // queues anything whose on-disk hash differs from the cache
        // (or that has no cache row yet).
        AnalyzerService::instance().initForProject(projectDir);
        AnalyzerService::instance().kickBackgroundScan();
        LoreKeeperService::instance().setProjectPath(projectDir);
        // Also wire the librarian (variable extractor). Previously this was
        // only called from the openProject() menu handler — so session
        // restore never set it up, leaving dbOpen=false for the entire
        // session and silently dropping every extraction.
        if (m_librarianService) m_librarianService->setProjectPath(projectDir);

        // Bind the entity graph panel to this project's librarian DB so
        // its model can reload from a real connection. Phase 2 of the
        // relationship-graph work — refresh() pulls entities + edges
        // when the librarian has populated something.
        if (m_entityGraphPanel && m_librarianService) {
            m_entityGraphPanel->setDatabase(m_librarianService->database());
        }

        // RESUME ALL AGENTS
        AgentGatekeeper::instance().resumeAll();

        m_projectStatsStatus->show();
        updateProjectStats();
        SynopsisService::instance().scanProject();
        KnowledgeBase::instance().reindexProject();


        // Explorations panel
        m_explorationsPanel->setRootPath(projectDir);

        // Load cached exploration data
        QVariantMap wordCountCache, explorationColors;
        ProjectManager::instance().loadExplorationData(wordCountCache, explorationColors);
        GitService::instance().loadWordCountCache(wordCountCache);
        m_explorationsPanel->graphView()->loadColorMap(explorationColors);
    });

    connect(&ProjectManager::instance(), &ProjectManager::treeChanged, this, [this]() {
        updateProjectStats();
        KnowledgeBase::instance().reindexProject();
    });

    // Reconciliation: ProjectManager emits this when validateTree() finds tree
    // entries whose backing files are missing on disk. Show the batch dialog
    // so the user can Locate / Remove / Recreate each missing entry.
    connect(&ProjectManager::instance(), &ProjectManager::reconciliationRequired,
            this, &MainWindow::showReconciliationDialog);

    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, [this]() {
        // Save exploration data before closing
        ProjectManager::instance().saveExplorationData(
            GitService::instance().saveWordCountCache(),
            m_explorationsPanel->graphView()->saveColorMap());

        // PAUSE ALL AGENTS
        AgentGatekeeper::instance().pauseAll();

        if (m_corkboardView) {
            m_corkboardView->setFolder(QString());
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
            // Poke both git-status models so the M badge shows up immediately
            // instead of waiting for the 3-second polling cycle.
            if (m_projectTree) m_projectTree->refreshGitStatus();
            if (m_fileExplorer) m_fileExplorer->refreshGitStatus();
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
    connect(&ProjectManager::instance(), &ProjectManager::treeStructureChanged, this, &MainWindow::updateProjectStats);

    connect(&ProjectManager::instance(), &ProjectManager::totalWordCountUpdated, this, [this](int count) {
        m_projectStatsStatus->setText(i18n("Project: %1 words", count));
        m_projectStatsStatus->show();
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

    // Route File→Quit through MainWindow::close() so it fires closeEvent()
    // (the single shutdown-ceremony chokepoint). Going to QApplication::quit
    // here bypasses doc save + librarian WAL checkpoint. See bugfix-registry
    // entry "2026-04-28 — Unified graceful-shutdown path (UI + signal)".
    KStandardAction::quit(this, &MainWindow::close, actionCollection());

    // Entity graph view — Phase 2 of the relationship-graph work.
    auto *showEntityGraphAct = new QAction(this);
    showEntityGraphAct->setText(i18n("Show Entity Graph"));
    showEntityGraphAct->setIcon(QIcon::fromTheme(QStringLiteral("view-pim-tasks")));
    actionCollection()->addAction(QStringLiteral("view_entity_graph"), showEntityGraphAct);
    connect(showEntityGraphAct, &QAction::triggered, this, &MainWindow::showEntityGraph);

    // Phase 4 — "Go to entity dossier". Ctrl+E opens a small input
    // dialog; the user types an entity name (or alias) and it opens
    // the matching LoreKeeper dossier file.
    auto *gotoEntityAct = new QAction(this);
    gotoEntityAct->setText(i18n("Go to Entity Dossier..."));
    gotoEntityAct->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));
    actionCollection()->addAction(QStringLiteral("nav_goto_entity"), gotoEntityAct);
    actionCollection()->setDefaultShortcut(gotoEntityAct, Qt::CTRL | Qt::Key_E);
    connect(gotoEntityAct, &QAction::triggered, this, [this]() {
        if (!ProjectManager::instance().isProjectOpen()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, i18n("Go to Entity Dossier"),
            i18n("Entity name (or alias):"),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        if (!navigateToEntityDossier(name)) {
            QMessageBox::information(this,
                i18n("Entity Not Found"),
                i18n("No dossier found for \"%1\". Run the Librarian "
                     "to extract entities from the project first.", name));
        }
    });

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
        qDebug() << "MainWindow: openFileFromUrl request for:" << path;
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
                collapsePreviewPane();
                return;
            }
        } else if (suffix == QLatin1String("pdf")) {
            m_pdfViewer->setUrl(url);
            showCentralView(m_pdfViewer);
            collapsePreviewPane();
            return;
        } else if (!textSuffixes.contains(suffix)) {
            // Unknown / likely binary — don't open in the editor
            return;
        }

        // Check project context via the model's path-aware inheritance.
        // effectiveCategory walks up the tree and honours the authoritative
        // path-based categories. Currently used only to route Research
        // files into the split research pane; LoreKeeper files open with
        // full read/write access like any other document. The LoreKeeper
        // tree-node protection (no rename/delete via the tree panel) lives
        // in ProjectTreeModel::flags() and is independent of editor
        // writability.
        bool isResearch = false;
        if (ProjectManager::instance().isProjectOpen()) {
            QString relPath = QDir(ProjectManager::instance().projectPath()).relativeFilePath(path);
            ProjectTreeItem *item = ProjectManager::instance().findItem(relPath);
            if (item) {
                const auto cat = static_cast<ProjectTreeItem::Category>(
                    ProjectManager::instance().effectiveCategoryForPath(relPath));
                qDebug() << "MainWindow: Found item in tree for" << relPath
                         << "effectiveCategory:" << cat;
                if (cat == ProjectTreeItem::Research) {
                    isResearch = true;
                }
            }
        }

        if (isResearch) {
            qDebug() << "MainWindow: Opening as Research file.";
            m_researchPane->show();
            m_editorView->show();
            if (m_researchDocument->url() == url) {
                qDebug() << "MainWindow: Research file already open, bringing to front.";
                showCentralView(m_editorSplitter);
                return;
            }
            m_researchDocument->openUrl(url);
            m_researchDocument->setReadWrite(true);
        } else {
            qDebug() << "MainWindow: Opening as Manuscript/General file.";
            m_editorView->show();
            // Deliberately DO NOT hide m_researchView here. The split is a
            // user-controlled mode: it's enabled by opening a research file,
            // and stays on for subsequent navigation. Hiding it on every
            // manuscript click forced single-pane mode and surprised users.
            // The research pane is closed via the toolbar button on the
            // research view itself.
            if (m_document->url() == url) {
                qDebug() << "MainWindow: File already open, bringing to front.";
                showCentralView(m_editorSplitter);
                return;
            }
            m_document->openUrl(url);
            m_document->setReadWrite(true);
        }

        qDebug() << "MainWindow: Showing editor splitter.";
        showCentralView(m_editorSplitter);
        if (m_previewPanel && m_togglePreviewAction->isChecked()) {
            togglePreview();
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
        if (m_librarianService) m_librarianService->scanFile(path);
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
            AgentGatekeeper::instance().pauseAll();
            if (ProjectManager::instance().createProject(dir, name)) {
                if (m_librarianService) m_librarianService->setProjectPath(dir);
                LoreKeeperService::instance().setProjectPath(dir);
                m_fileExplorer->setRootPath(dir);
                ProjectManager::instance().setupDefaultProject(dir, name);
                updateTitle();
                
                // Automatically open the README
                QString readmePath = QDir(dir).absoluteFilePath(QStringLiteral("research/README.md"));
                if (QFile::exists(readmePath)) {
                    openFileFromUrl(QUrl::fromLocalFile(readmePath));
                }
            }
            AgentGatekeeper::instance().resumeAll();
        }
    }
}

void MainWindow::openProject()
{
    const QString filePath = QFileDialog::getOpenFileName(this, i18n("Open Project"),
        QString(), i18n("RPG Forge Project (rpgforge.project)"));
    
    if (!filePath.isEmpty()) {
        if (ProjectManager::instance().openProject(filePath)) {
            QString projectDir = ProjectManager::instance().projectPath();
            if (m_librarianService) m_librarianService->setProjectPath(projectDir);
            LoreKeeperService::instance().setProjectPath(projectDir);
            m_fileExplorer->setRootPath(projectDir);
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
    if (m_outlinePanel) m_outlinePanel->documentChanged(text); // Use FULL text for outline/line numbers
    
    auto *projectPreviewAct = actionCollection()->action(QStringLiteral("project_preview_mode"));
    if (projectPreviewAct && projectPreviewAct->isChecked()) {
        updateProjectPreview();
    } else if (m_previewPanel) {
        m_previewPanel->setMarkdown(contentOnly);
    }

    // 3. Update word count. Compiled-once regex + single-pass matcher avoids
    // the per-tick QRegularExpression construction and the intermediate
    // QStringList allocation that `.split().count()` creates.
    static const QRegularExpression wordRe(QStringLiteral("\\S+"));
    int wordCount = 0;
    for (auto it = wordRe.globalMatch(contentOnly); it.hasNext(); it.next()) ++wordCount;
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

    if (m_librarianService) {
        // scanFile is async; entityUpdated fires when it finishes and drives
        // updateLibrarianHighlights (see ctor wiring). Calling that path
        // synchronously here runs full-document regex + O(N) SQLite queries
        // on every keystroke's debounce tick, which is the main source of
        // typing stutter on large documents.
        m_librarianService->scanFile(doc->url().toLocalFile());
    }
}

void MainWindow::onCursorPositionChanged()
{
    m_cursorDebounce->start();

    // Typewriter scrolling (keep cursor centered). Cached via
    // m_typewriterScrolling — hitting QSettings on every cursor move
    // is expensive at keyboard-repeat speed.
    if (m_typewriterScrolling) {
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
        // Preview is always "visible" in Qt terms (we never hide the widget
        // to avoid a QSG crash); check the splitter pane width instead.
        if (m_previewPanel && m_mainSplitter) {
            const QList<int> sizes = m_mainSplitter->sizes();
            if (sizes.size() >= 3 && sizes[2] > 0) {
                m_previewPanel->scrollToLine(line);
            }
        }
    }
}

void MainWindow::togglePreview()
{
    // Keep the QWebEngineView mounted at all times; setVisible(false)/true
    // triggers a QSGBatchRenderer crash in libgallium on hide/show cycles.
    // Collapse/expand the splitter pane instead.
    if (!m_previewPanel || !m_mainSplitter) return;

    const bool shouldShow = m_togglePreviewAction->isChecked();

    QList<int> sizes = m_mainSplitter->sizes();
    if (sizes.size() < 3) return;

    if (shouldShow) {
        int total = 0;
        for (int s : sizes) total += s;
        // Restore the user's last chosen width; fall back to ~30% on first
        // show or if the saved width would leave no room for the editor.
        int previewSize = (m_preferredPreviewWidth > 0)
            ? m_preferredPreviewWidth
            : static_cast<int>(total * 0.3);
        const int minEditor = 200;
        if (previewSize > total - sizes[0] - minEditor) {
            previewSize = qMax(0, total - sizes[0] - minEditor);
        }
        sizes[2] = previewSize;
        sizes[1] = qMax(0, total - sizes[0] - previewSize);
        m_mainSplitter->setSizes(sizes);
        syncScroll();
    } else {
        sizes[1] += sizes[2];
        sizes[2] = 0;
        m_mainSplitter->setSizes(sizes);
    }
}

void MainWindow::collapsePreviewPane()
{
    // Shrink the preview column to 0 without calling hide()/setVisible() on the
    // QWebEngineView — those flip QtWebEngine's internal QQuickWidget and
    // trigger the QSGBatchRenderer crash on this GPU family.
    if (!m_mainSplitter) return;
    QList<int> sizes = m_mainSplitter->sizes();
    if (sizes.size() < 3 || sizes[2] == 0) return;
    sizes[1] += sizes[2];
    sizes[2] = 0;
    m_mainSplitter->setSizes(sizes);
}

void MainWindow::syncScroll()
{
    auto *view = activeView();
    if (!view || !m_previewPanel || !m_previewPanel->window()) return;
    // Skip when the editor isn't the foreground central widget (e.g. corkboard is up).
    if (m_centralViewLayout && m_centralViewLayout->currentWidget() != m_editorSplitter) return;
    // Skip when the preview pane is collapsed to width 0.
    if (m_mainSplitter) {
        const QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() >= 3 && sizes[2] <= 0) return;
    }

    // Use scrollToLine with smooth=false for real-time synchronization
    // Ensure the view is valid and has a Kate view interface
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
void MainWindow::runEntityIndexMaintenance()
{
    if (!ProjectManager::instance().isProjectOpen()) return;
    if (!m_librarianService) return;
    LibrarianDatabase *db = m_librarianService->database();
    if (!db) return;

    // 1. Link every chunk to the entities the librarian currently knows
    //    about. Idempotent — INSERT OR IGNORE on (chunk_id, entity_id).
    const int links = KnowledgeBase::instance().rebuildChunkEntityLinks();
    if (links <= 0) {
        // Nothing to do — either no chunks indexed yet or no entities
        // recognized in any chunk. Skip aggregates + community detection;
        // they'd produce empty results.
        return;
    }

    // 2. Refresh mention_count + first/last_appearance from
    //    chunk_entities. Cached for cheap graph-render lookups.
    db->refreshAggregatesFromVectorDb(
        QDir(ProjectManager::instance().projectPath())
            .absoluteFilePath(QStringLiteral(".rpgforge-vectors.db")));

    // 3. Phase 5: recompute communities. This is an INTERNAL index — no
    //    UI surfaces it directly. Retrieval (graph-augmented hybrid
    //    search, future hierarchical summaries) uses community_id as a
    //    grouping key. Running on every librarian re-extraction keeps
    //    it fresh without the user ever knowing it exists.
    EntityCommunityDetector(db).detectAndPersist();

    // 4. Reload the entity graph panel so any visual state (counts in
    //    tooltips, neighborhood expansion) reflects the new data.
    if (m_entityGraphPanel) m_entityGraphPanel->refresh();
}

void MainWindow::showEntityGraph()
{
    if (!m_entityGraphPanel) return;
    if (!ProjectManager::instance().isProjectOpen()) {
        qInfo() << "MainWindow::showEntityGraph: no project open — ignoring";
        return;
    }
    m_entityGraphPanel->refresh();
    showCentralView(m_entityGraphPanel);
}

bool MainWindow::navigateToEntityDossier(const QString &entityName)
{
    if (entityName.trimmed().isEmpty()) return false;
    const QString projectDir = ProjectManager::instance().projectPath();
    if (projectDir.isEmpty()) return false;

    // Resolve aliases through the librarian so "Ryz" finds the
    // Ryzen.md dossier — keeps the user's mental model consistent
    // with how the entity graph and graph-augmented retrieval already
    // resolve names.
    QString canonicalName = entityName.trimmed();
    if (m_librarianService && m_librarianService->database()) {
        const qint64 id = m_librarianService->database()->resolveEntityByName(canonicalName);
        if (id > 0) {
            const QString resolved = m_librarianService->database()->getEntityName(id);
            if (!resolved.isEmpty()) canonicalName = resolved;
        }
    }

    const QStringList searchRoots = {
        QStringLiteral("lorekeeper/Characters"),
        QStringLiteral("lorekeeper/Settings"),
        QStringLiteral("lorekeeper"),
        QStringLiteral("research/Characters"),
        QStringLiteral("research"),
    };
    for (const QString &root : searchRoots) {
        const QString candidate = QDir(projectDir).absoluteFilePath(
            root + QStringLiteral("/") + canonicalName + QStringLiteral(".md"));
        if (QFileInfo::exists(candidate)) {
            m_document->openUrl(QUrl::fromLocalFile(candidate));
            showCentralView(m_editorSplitter);
            return true;
        }
    }
    qInfo() << "MainWindow::navigateToEntityDossier: no dossier found for"
             << canonicalName;
    return false;
}

void MainWindow::showCentralView(QWidget *widget)
{
    // The editor splitter is what's actually parented in the stacked layout — if
    // a caller asks for the inner editor or research views, surface the splitter.
    QWidget *target = widget;
    if (widget == m_editorView || widget == m_researchView) {
        target = m_editorSplitter;
    }

    if (m_centralViewLayout->indexOf(target) >= 0) {
        m_centralViewLayout->setCurrentWidget(target);
    }

    if (target == m_editorSplitter) {
        // Ensure the centre column of the main splitter has size to actually
        // render the editor (defensive: a previous resize may have starved it).
        QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() >= 2 && sizes[1] < 100) {
            int total = sizes[0] + sizes[1] + (sizes.size() > 2 ? sizes[2] : 0);
            sizes[1] = total - sizes[0] - (sizes.size() > 2 ? sizes[2] : 0);
            m_mainSplitter->setSizes(sizes);
        }
    }

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

QString MainWindow::currentViewId() const
{
    if (m_diffView && m_diffView->isVisible()) {
        return QStringLiteral("diff");
    }
    if (m_corkboardView && m_corkboardView->isVisible()) {
        return QStringLiteral("corkboard");
    }
    if (m_imagePreview && m_imagePreview->isVisible()) {
        return QStringLiteral("imagepreview");
    }
    if (m_pdfViewer && m_pdfViewer->isVisible()) {
        return QStringLiteral("pdf");
    }
    if (m_entityGraphPanel && m_entityGraphPanel->isVisible()) {
        return QStringLiteral("entity_graph");
    }
    if (m_editorSplitter && m_editorSplitter->isVisible()) {
        return QStringLiteral("editor");
    }
    return QString();
}

void MainWindow::showDiff(const QString &path1, const QString &path2OrHash1, const QString &hash2)
{
    // Two-file compare: if either side is an image, route to ImageDiffView
    // (side-by-side visual compare). Kompare renders a blank part for
    // binary files, which isn't useful for images.
    if (path2OrHash1.startsWith(QLatin1Char('/'))
        && (ImageDiffView::isImagePath(path1) || ImageDiffView::isImagePath(path2OrHash1))) {
        if (m_imageDiffView->setImages(path1, path2OrHash1)) {
            showCentralView(m_imageDiffView);
            collapsePreviewPane();
            return;
        }
        // Fall through to Kompare if image load failed — at least something
        // shows up instead of a silent empty pane.
    }

    // If path2OrHash1 looks like a file path (starts with /), compare files.
    // Otherwise, treat as git hashes.
    if (path2OrHash1.startsWith(QLatin1Char('/'))) {
        m_diffView->setFiles(path1, path2OrHash1);
    } else {
        m_diffView->setDiff(path1, path2OrHash1, hash2);
    }
    showCentralView(m_diffView);
    collapsePreviewPane();
}

void MainWindow::performSearch(const QString &text)
{
    if (text.isEmpty()) return;

    auto *view = activeView();
    auto *doc = activeDocument();

    if (view && doc && !doc->url().isEmpty()) {
        KTextEditor::SearchOptions options = KTextEditor::Default;
        KTextEditor::Cursor start = view->cursorPosition();

        // Find NEXT if text is same
        if (text == m_lastSearchText && view->selection()) {
            start = view->selectionRange().end();
        }
        m_lastSearchText = text;

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
            // Not found in current document, ask if user wants global AI search
            if (ProjectManager::instance().isProjectOpen()) {
                auto res = QMessageBox::question(this, i18n("Not Found"), 
                    i18n("'%1' was not found in this document. Search entire project using AI?", text),
                    QMessageBox::Yes | QMessageBox::No);
                
                if (res == QMessageBox::Yes) {
                    m_sidebar->showPanel(m_chatId);
                    m_chatPanel->askAI(i18n("Search project for: %1", text), i18n("AI Project Search"));
                }
            } else {
                QMessageBox::information(this, i18n("Not Found"), i18n("'%1' was not found in this document.", text));
            }
        }
    } else if (ProjectManager::instance().isProjectOpen()) {
        // Global Search: Ask AI / KnowledgeBase via Chat Panel
        m_sidebar->showPanel(m_chatId);
        m_chatPanel->askAI(i18n("Search project for: %1", text), i18n("AI Project Search"));
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editorView->focusProxy() && event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent*>(event);
        KTextEditor::Cursor cursor = m_editorView->coordinatesToCursor(helpEvent->pos());
        
        // 1. Check Librarian Ranges
        for (auto *range : m_librarianRanges) {
            if (range->contains(cursor)) {
                QToolTip::showText(helpEvent->globalPos(), range->attribute()->toolTip());
                return true;
            }
        }
        
        // 2. Check Diagnostic Ranges
        for (auto *range : m_diagnosticRanges) {
            if (range->contains(cursor)) {
                QToolTip::showText(helpEvent->globalPos(), range->attribute()->toolTip());
                return true;
            }
        }
    }

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
            QTimer::singleShot(0, this, [popup, view]() {
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

    // Restore active sidebar panel
    if (m_sidebar && settings.contains(QStringLiteral("sidebarPanel"))) {
        int panelId = settings.value(QStringLiteral("sidebarPanel")).toInt();
        if (panelId >= 0) {
            m_sidebar->showPanel(panelId);
        }
    }

    // Splitter restoration MUST run AFTER the window has a real geometry —
    // restoreSession() is called from the constructor (before window->show())
    // so QSplitter::restoreState would otherwise apply proportions against
    // an unsized splitter, starving the editor pane. Defer to the next
    // event-loop iteration; by then show() has produced a real layout.
    QTimer::singleShot(0, this, &MainWindow::restoreSplittersDeferred);
}

void MainWindow::restoreSplittersDeferred()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    if (m_editorSplitter && settings.contains(QStringLiteral("editorSplitter"))) {
        m_editorSplitter->restoreState(settings.value(QStringLiteral("editorSplitter")).toByteArray());
    }

    if (m_mainSplitter && settings.contains(QStringLiteral("mainSplitter"))) {
        m_mainSplitter->restoreState(settings.value(QStringLiteral("mainSplitter")).toByteArray());
    }

    // vSplitter — top is editor cluster, bottom is analyzer.
    if (m_vSplitter && settings.contains(QStringLiteral("vSplitter"))) {
        m_vSplitter->restoreState(settings.value(QStringLiteral("vSplitter")).toByteArray());
        QList<int> sizes = m_vSplitter->sizes();
        if (sizes.size() >= 2) {
            const int total = sizes[0] + sizes[1];
            if (total > 0) {
                // Cap analyzer to its widget maxHeight (600) AND force editor
                // cluster ≥ 200 px so the user can never start with the upper
                // UI invisible. Apply analyzer cap first, then give the rest
                // to the editor cluster.
                const int analyzerCap = qBound(0, qMin(sizes[1], 600), total - 200);
                const int editorCluster = total - analyzerCap;
                if (sizes[0] < 200 || sizes[1] > 600) {
                    m_vSplitter->setSizes({editorCluster, analyzerCap});
                }
            }
        }
    }

    // mainSplitter — sidebar / editor / preview.
    if (m_mainSplitter && settings.contains(QStringLiteral("mainSplitter"))) {
        QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() >= 2 && sizes[1] < 200) {  // was < 80; bump to 200
            const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
            const int sidebar = qBound(180, sizes.value(0, 250), 320);
            const int preview = sizes.value(2, 0);
            m_mainSplitter->setSizes({sidebar, qMax(400, total - sidebar - preview), preview});
        }
    }
}

void MainWindow::insertProjectLinksAtCursor(const QList<QPair<QString, QUrl>> &items)
{
    auto *doc = activeDocument();
    auto *view = activeView();
    if (!doc || !view || items.isEmpty()) return;

    // Resolve links from the project root when the target is inside the
    // project — produces "./media/X.png" regardless of how deep the current
    // document is. Falls back to doc-dir-relative or absolute for anything
    // outside the project.
    const QString projectRoot = ProjectManager::instance().isProjectOpen()
        ? ProjectManager::instance().projectPath()
        : QString();
    QString docDir;
    if (!doc->url().isEmpty() && doc->url().isLocalFile()) {
        docDir = QFileInfo(doc->url().toLocalFile()).absolutePath();
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

        QString relPath;
        if (!projectRoot.isEmpty()
            && (absPath == projectRoot || absPath.startsWith(projectRoot + QDir::separator()))) {
            relPath = QStringLiteral("./") + QDir(projectRoot).relativeFilePath(absPath);
        } else if (!docDir.isEmpty()) {
            relPath = QDir(docDir).relativeFilePath(absPath);
        } else {
            relPath = absPath;
        }
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
        // Refresh cached settings that hot-paths read.
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        m_typewriterScrolling = s.value(
            QStringLiteral("editor/typewriterScrolling"), false).toBool();
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
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection, i18n("AI Expand"));
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
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection, i18n("AI Rewrite"));
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
    m_chatPanel->askAI(prompt + QStringLiteral("\n\n") + selection, i18n("AI Summarize"));
}

void MainWindow::onForceLoreScan()
{
    KTextEditor::Document *doc = activeDocument();
    if (doc && !doc->url().isEmpty() && doc->url().isLocalFile()) {
        LoreKeeperService::instance().indexDocument(doc->url().toLocalFile());
    }
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
        // AI returns 1-based indexing, convert to 0-based
        int line = qMax(0, d.line - 1);
        if (line >= doc->lines()) continue;

        QString lineText = doc->line(line);
        int startCol = lineText.length() - lineText.trimmed().length(); // Skip leading whitespace
        int endCol = lineText.length();

        KTextEditor::Range range(line, startCol, line, endCol);
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
        
        attr->setToolTip(d.message);
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
        if (action->text() == i18n("AI Assistant") || (action->menu() && action->menu()->title() == i18n("AI Assistant"))) {
            return;
        }
    }

    menu->addSeparator();
    auto *aiMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("chat-conversation")), i18n("AI Assistant"));

    // Check for Librarian inconsistencies at cursor
    KTextEditor::Cursor cursor = view->cursorPosition();
    KTextEditor::MovingRange *librarianRange = nullptr;
    for (auto *range : m_librarianRanges) {
        if (range && range->contains(cursor) && range->attribute() && range->attribute()->underlineColor() == Qt::red) {
            librarianRange = range;
            break;
        }
    }

    if (librarianRange) {
        auto *guardMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("security-high")), i18n("Consistency Guardian"));

        QString key;
        // Basic heuristic to get key from text (e.g. "Strength: 15")
        QString lineText = view->document()->line(librarianRange->start().line());
        QRegularExpression kvRegex(QStringLiteral("^\\s*[-*+]?\\s*([\\w\\s]+):"));
        auto match = kvRegex.match(lineText);
        if (match.hasMatch()) key = match.captured(1).trimmed().toLower();

        if (!key.isEmpty()) {
            QString rangeText = view->document()->text(librarianRange->toRange());
            auto *updateLib = guardMenu->addAction(i18n("Update Library to match '%1'", rangeText));
            connect(updateLib, &QAction::triggered, this, [this, key, rangeText]() {
                QList<qint64> entities = m_librarianService->database()->findEntitiesByAttribute(key, QVariant());
                for (qint64 id : entities) {
                    m_librarianService->database()->setAttribute(id, key, rangeText);
                }
                m_librarianService->scanAll(); // Refresh
            });

            auto *updateText = guardMenu->addAction(i18n("Revert text to Library value"));
            connect(updateText, &QAction::triggered, this, [this, key, librarianRange]() {
                QList<qint64> entities = m_librarianService->database()->findEntitiesByAttribute(key, QVariant());
                if (!entities.isEmpty()) {
                    QString masterVal = m_librarianService->database()->getAttribute(entities.first(), key).toString();
                    librarianRange->document()->replaceText(librarianRange->toRange(), masterVal);
                }
            });
        }
    }

    auto *expand = aiMenu->addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Expand Selection"));    connect(expand, &QAction::triggered, this, &MainWindow::aiExpand);
    
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

    // Ensure everything is paused before we even touch the project manager
    AgentGatekeeper::instance().pauseAll();

    // 1. Auto-create project if none open
    if (!ProjectManager::instance().isProjectOpen()) {
        QFileInfo fi(scrivPath);
        QString projectName = fi.baseName();
        QString targetDir = fi.absolutePath() + QDir::separator() + projectName + QStringLiteral("_forge");
        
        if (QDir(targetDir).exists()) {
            auto result = QMessageBox::question(this, i18n("Project Directory Exists"),
                i18n("The directory '%1' already exists. Use it for the new project?", targetDir));
            if (result != QMessageBox::Yes) {
                AgentGatekeeper::instance().resumeAll();
                return;
            }
        }

        // We temporarily block the resumeAll() that is normally triggered by projectOpened
        if (!ProjectManager::instance().createProject(targetDir, projectName)) {
            QMessageBox::critical(this, i18n("Error"), i18n("Failed to create project at %1", targetDir));
            AgentGatekeeper::instance().resumeAll();
            return;
        }
        
        if (m_librarianService) m_librarianService->setProjectPath(targetDir);
        m_fileExplorer->setRootPath(targetDir);
        ProjectManager::instance().setupDefaultProject(targetDir, projectName);
        updateTitle();
    }

    QString projectPath = ProjectManager::instance().projectPath();
    auto importer = std::make_shared<ScrivenerImporter>();
    
    auto *progressDialog = new QProgressDialog(i18n("Importing Scrivener Project..."), i18n("Cancel"), 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->show();
    
    connect(importer.get(), &ScrivenerImporter::progress, this, [progressDialog](int val, const QString &msg) {
        progressDialog->setValue(val);
        progressDialog->setLabelText(msg);
    }, Qt::QueuedConnection);

    // Explicitly keep them paused
    AgentGatekeeper::instance().pauseAll();

    QThreadPool::globalInstance()->start([importer, scrivPath, projectPath, this]() {
        ProjectTreeModel backgroundModel;
        bool success = importer->import(scrivPath, projectPath, &backgroundModel);
        auto resultData = backgroundModel.projectData();
        
        QMetaObject::invokeMethod(this, [this, success, resultData]() {
            if (success) {
                // Single validating wrapper: setTreeData internally runs
                // validateTree (heals leaf-Folder-as-File, ensures the three
                // authoritative top-level folders, etc.) and then saves the
                // already-corrected JSON. Importers no longer touch the
                // model directly.
                ProjectManager::instance().setTreeData(resultData);
                QMessageBox::information(this, i18n("Import Complete"), i18n("Scrivener project imported successfully."));
            } else {
                QMessageBox::warning(this, i18n("Import Failed"), i18n("Failed to import Scrivener project."));
            }

            // ONLY resume agents now that EVERYTHING is finished
            AgentGatekeeper::instance().resumeAll();
        }, Qt::QueuedConnection);
    });
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

    SynopsisService::instance().pause();

    // 1. Find the Manuscript folder via path lookup. The authoritative
    // root is at "manuscript" — validateTree creates it on project open.
    const bool hasManuscript = ProjectManager::instance().findItem(QStringLiteral("manuscript")) != nullptr;

    // Collision detector: the same safeName is used as both the manuscript
    // subfolder name AND the prefix on extracted media images. Refusing
    // overlapping names stops the second import from overwriting the first
    // import's media files.
    auto hasCollision = [&](const QString &sn) -> bool {
        if (sn.isEmpty()) return true;
        const QString folderRel = hasManuscript
            ? (QStringLiteral("manuscript/") + sn)
            : sn;
        const QString folderAbs = QDir(projectDir).absoluteFilePath(folderRel);
        if (QFileInfo::exists(folderAbs)) return true;
        QDir mediaD(mediaDir);
        if (mediaD.exists()
            && !mediaD.entryList({sn + QStringLiteral("_*")}, QDir::Files).isEmpty()) {
            return true;
        }
        return false;
    };

    for (const QString &file : files) {
        QFileInfo info(file);
        // completeBaseName() returns everything before the LAST dot, so
        // filenames like "Kabal v0.18.2 - Part 1 - Intro to Flaws.docx"
        // survive as-is instead of getting truncated to "Kabal v0".
        QString baseName = info.completeBaseName();
        QString safeName = DocumentConverter::sanitizePrefix(baseName);

        // If either the target manuscript subfolder OR the image-prefix slot
        // is already taken by a prior import, ask the user for a unique name
        // before we proceed. Empty-string / cancel skips this file entirely.
        if (hasCollision(safeName)) {
            QString suggested = baseName + QStringLiteral(" (2)");
            while (true) {
                bool ok = false;
                const QString newName = QInputDialog::getText(
                    this, i18n("Import Name Collision"),
                    i18n("\"%1\" conflicts with an existing folder or media prefix "
                         "in this project.\n\nChoose a different name for the "
                         "imported document:", baseName),
                    QLineEdit::Normal, suggested, &ok);
                if (!ok || newName.trimmed().isEmpty()) {
                    baseName.clear();
                    break;
                }
                const QString sanitized = DocumentConverter::sanitizePrefix(newName);
                if (!hasCollision(sanitized)) {
                    baseName = newName;
                    safeName = sanitized;
                    break;
                }
                suggested = newName + QStringLiteral(" (2)");
            }
            if (baseName.isEmpty()) continue;  // user cancelled; skip this file
        }

        // Create a per-document subfolder under Manuscript (or at root as
        // a fallback if Manuscript is somehow missing).
        QString folderRelPath = hasManuscript
            ? (QStringLiteral("manuscript/") + safeName)
            : safeName;
        const QString parentPath = hasManuscript ? QStringLiteral("manuscript") : QString();
        ProjectManager::instance().addFolder(baseName, folderRelPath, parentPath);

        auto result = converter.convertToMarkdown(file, safeName, mediaDir);

        if (result.success) {
            QString relPath = folderRelPath + QDir::separator() + safeName + QStringLiteral(".md");
            QString absPath = QDir(projectDir).absoluteFilePath(relPath);

            QDir().mkpath(QFileInfo(absPath).absolutePath());

            QFile outFile(absPath);
            if (outFile.open(QIODevice::WriteOnly)) {
                outFile.write(result.markdown.toUtf8());
                outFile.close();
                ProjectManager::instance().addFile(baseName, relPath, folderRelPath);
            }
        } else {
            QMessageBox::warning(this, i18n("Import Error"), i18n("Failed to convert %1: %2", baseName, result.error));
        }
    }

    ProjectManager::instance().saveProject();
    SynopsisService::instance().resume();
    ProjectManager::instance().notifyTreeChanged();

    // Auto-commit the newly-imported files if the project is a git repo.
    // Without this, imported manuscripts + media sit uncommitted until the
    // user manually runs Sync — and an overwrite before that sync wipes the
    // prior version from git history (which it never captured).
    if (GitService::instance().isRepo(projectDir)
        && GitService::instance().hasUncommittedChanges(projectDir)) {
        const QString label = files.size() == 1
            ? QFileInfo(files.first()).fileName()
            : i18n("%1 documents", files.size());
        GitService::instance().commitAll(projectDir,
            i18n("Import %1", label));
    }

    QMessageBox::information(this, i18n("Import Complete"), i18n("Documents imported successfully."));
    }


void MainWindow::updateProjectStats()
{
    if (!ProjectManager::instance().isProjectOpen()) {
        m_projectStatsStatus->hide();
        return;
    }

    ProjectManager::instance().triggerWordCountUpdate();
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
    auto treeData = ProjectManager::instance().treeData();
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

void MainWindow::onModelNotFound(LLMProvider provider, const QString &invalidModel, const QStringList &available, const QString &serviceName)
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(i18n("Model Unavailable"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(dlg);
    
    QString details;
    if (!serviceName.isEmpty()) {
        details = i18n("The service <b>%1</b> is attempting to use model <b>%2</b> via <b>%3</b>, but it is no longer available.",
                       serviceName, invalidModel, LLMService::providerName(provider));
    } else {
        details = i18n("The model <b>%1</b> is no longer available from provider <b>%2</b>.",
                       invalidModel, LLMService::providerName(provider));
    }

    auto *label = new QLabel(details + QStringLiteral("<br/><br/>") + i18n("Select a replacement model to use instead:"), dlg);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *combo = new QComboBox(dlg);
    combo->addItems(available);
    layout->addWidget(combo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    connect(dlg, &QDialog::finished, this, []() {
        LLMService::instance().setShowingModelDialog(false);
    });

    connect(dlg, &QDialog::accepted, this, [combo]() {
        const QString selected = combo->currentText();
        if (!selected.isEmpty())
            LLMService::instance().retryWithModel(selected);
    });

    dlg->open();
}






static KTextEditor::Cursor offsetToCursor(KTextEditor::Document *doc, int offset)
{
    int currentOffset = 0;
    for (int line = 0; line < doc->lines(); ++line) {
        int lineLength = doc->lineLength(line);
        if (offset <= currentOffset + lineLength) {
            return KTextEditor::Cursor(line, offset - currentOffset);
        }
        currentOffset += lineLength + 1; // +1 for newline
    }
    return KTextEditor::Cursor::invalid();
}

void MainWindow::updateLibrarianHighlights()
{
    auto *doc = activeDocument();
    if (!doc || !m_librarianService || !m_librarianService->database()->database().isOpen()) return;

    // Clear old highlights
    qDeleteAll(m_librarianRanges);
    m_librarianRanges.clear();

    QString text = doc->text();

    // 1. Check for inconsistencies (Red Squiggles)
    // We scan for key:value patterns and check them against the DB.
    // Static so the regex is compiled once per process, not per call.
    static const QRegularExpression kvRegex(
        QStringLiteral("^\\s*[-*+]?\\s*([\\w\\s]+):\\s*(.+)$"),
        QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = kvRegex.globalMatch(text);

    KTextEditor::Attribute::Ptr errorAttr(new KTextEditor::Attribute());
    errorAttr->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    errorAttr->setUnderlineColor(Qt::red);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1).trimmed().toLower();
        QString value = match.captured(2).trimmed();

        // Query DB for this key
        QList<qint64> entities = m_librarianService->database()->findEntitiesByAttribute(key, QVariant());
        for (qint64 id : entities) {
            QVariant masterVal = m_librarianService->database()->getAttribute(id, key);
            if (masterVal.isValid() && masterVal.toString() != value) {
                // Inconsistency found!
                KTextEditor::Cursor start = offsetToCursor(doc, match.capturedStart(2));
                KTextEditor::Cursor end = offsetToCursor(doc, match.capturedEnd(2));
                
                if (start.isValid() && end.isValid()) {
                    KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(start, end));
                    range->setAttribute(errorAttr);
                    range->setZDepth(100.0);
                    
                    // ADD TOOLTIP
                    QString source = m_librarianService->database()->getReferences(id).join(QStringLiteral(", "));
                    range->setAttribute(errorAttr);
                    errorAttr->setToolTip(i18n("Consistency Error: Library value for '%1' is '%2'\nSource: %3", 
                                               key, masterVal.toString(), source));
                    
                    m_librarianRanges.append(range);
                }
            }
        }
    }

    // 2. Highlight auto-bound variables (Green Underline)
    KTextEditor::Attribute::Ptr boundAttr(new KTextEditor::Attribute());
    boundAttr->setUnderlineStyle(QTextCharFormat::DashUnderline);
    boundAttr->setUnderlineColor(Qt::green);

    static const QRegularExpression varRegex(QStringLiteral("\\{\\{(.+?)\\}\\}"));
    it = varRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        KTextEditor::Cursor start = offsetToCursor(doc, match.capturedStart());
        KTextEditor::Cursor end = offsetToCursor(doc, match.capturedEnd());
        
        if (start.isValid() && end.isValid()) {
            KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(start, end));
            range->setAttribute(boundAttr);
            
            // ADD TOOLTIP
            QString varName = match.captured(1);
            KTextEditor::Attribute::Ptr specificAttr(new KTextEditor::Attribute(*boundAttr));
            specificAttr->setToolTip(i18n("Auto-bound Variable: Linked to Librarian '%1'", varName));
            range->setAttribute(specificAttr);
            
            m_librarianRanges.append(range);
        }
    }
}

// --- Explorations slots ---

void MainWindow::onSwitchExplorationRequested(const QString &branchName)
{
    const QString repoPath = ProjectManager::instance().projectPath();
    if (repoPath.isEmpty()) return;

    // Run the blocking libgit2 queries off the main thread; on large repos
    // hasUncommittedChanges() and currentBranch() can stall the UI.
    struct DirtyCheckResult {
        bool dirty;
        QString currentBranch;
    };
    auto fut = QtConcurrent::run([repoPath]() {
        DirtyCheckResult r;
        r.dirty = GitService::instance().hasUncommittedChanges(repoPath);
        r.currentBranch = GitService::instance().currentBranch(repoPath);
        return r;
    });

    fut.then(this, [this, repoPath, branchName](DirtyCheckResult r) {
        if (!r.dirty) {
            GitService::instance().switchExploration(repoPath, branchName)
                .then(this, [this](bool ok) {
                    if (ok) m_explorationsPanel->refresh();
                });
            return;
        }

        auto *dlg = new UnsavedChangesDialog(r.currentBranch, branchName, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        connect(dlg, &UnsavedChangesDialog::saveRequested,
                this, [this, repoPath, branchName](const QString &msg) {
                    GitService::instance().commitAll(repoPath, msg)
                        .then(this, [this, repoPath, branchName](bool ok) {
                            if (!ok) {
                                KMessageBox::error(this,
                                    i18n("Could not save your changes. Switch cancelled."),
                                    i18n("Save Failed"));
                                return;
                            }
                            GitService::instance().switchExploration(repoPath, branchName)
                                .then(this, [this](bool) {
                                    m_explorationsPanel->refresh();
                                });
                        });
                });

        connect(dlg, &UnsavedChangesDialog::parkRequested,
                this, [this, repoPath, branchName](const QString &stashMsg) {
                    GitService::instance().stashChanges(repoPath, stashMsg)
                        .then(this, [this, repoPath, branchName](bool ok) {
                            if (!ok) {
                                KMessageBox::error(this,
                                    i18n("Could not park your changes. Switch cancelled."),
                                    i18n("Park Failed"));
                                return;
                            }
                            GitService::instance().switchExploration(repoPath, branchName)
                                .then(this, [this](bool) {
                                    m_explorationsPanel->refresh();
                                });
                        });
                });

        dlg->open();
    });
}

void MainWindow::onIntegrateExplorationRequested(const QString &sourceBranch)
{
    QString repoPath = ProjectManager::instance().projectPath();
    if (repoPath.isEmpty()) return;

    GitService::instance().integrateExploration(repoPath, sourceBranch)
        .then(this, [this](bool ok) {
            if (ok) {
                m_explorationsPanel->refresh();
            } else {
                onIntegrateFailed();
            }
        });
}

void MainWindow::onIntegrateFailed()
{
    QString repoPath = ProjectManager::instance().projectPath();
    GitService::instance().getConflictingFiles(repoPath)
        .then(this, [this](QList<ConflictFile> conflicts) {
            if (conflicts.isEmpty()) return;
            m_conflictFiles = conflicts;
            m_conflictIndex = 0;
            m_conflictBanner->setVisible(true);
            showNextConflict();
        });
}

void MainWindow::showNextConflict()
{
    if (m_conflictIndex >= m_conflictFiles.size()) {
        m_conflictBanner->setVisible(false);
        return;
    }
    const ConflictFile &cf = m_conflictFiles.at(m_conflictIndex);
    m_conflictBannerLabel->setText(
        i18n("Resolving conflict %1 of %2 — %3",
             m_conflictIndex + 1,
             m_conflictFiles.size(),
             QFileInfo(cf.path).fileName()));
    m_conflictNextBtn->setEnabled(m_conflictIndex + 1 < m_conflictFiles.size());
    const QString repoPath = ProjectManager::instance().projectPath();
    m_diffView->setConflict(repoPath, cf.path, cf.ancestorHash, cf.oursHash, cf.theirsHash);
    showCentralView(m_diffView);
}

void MainWindow::onCreateLandmarkRequested(const QString &hash)
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, i18n("Create Landmark"),
        i18n("Landmark name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString repoPath = ProjectManager::instance().projectPath();
    GitService::instance().createTag(repoPath, name.trimmed(), hash);
    m_explorationsPanel->refresh();
}

void MainWindow::onRecallVersionRequested(const QString &hashOrPath)
{
    QString filePath = hashOrPath;
    QString repoPath = ProjectManager::instance().projectPath();
    QVariantMap colors = m_explorationsPanel->graphView()->saveColorMap();

    auto *dlg = new VersionRecallBrowser(filePath, repoPath, colors, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &VersionRecallBrowser::versionSelected,
            this, &MainWindow::onVersionSelected);
    dlg->open();
}

void MainWindow::onVersionSelected(const QString &filePath, const QString &commitHash)
{
    QString repoPath = ProjectManager::instance().projectPath();
    if (repoPath.isEmpty()) return;

    // Step 1: commit any uncommitted changes first so the current draft is preserved
    // as a Milestone before overwriting with the historical version.
    QFuture<bool> commitFuture;
    if (GitService::instance().hasUncommittedChanges(repoPath)) {
        commitFuture = GitService::instance().commitAll(repoPath,
            i18n("Auto-saved before recalling version"));
    } else {
        commitFuture = QtFuture::makeReadyValueFuture(true);
    }

    commitFuture.then(this, [this, filePath, commitHash](bool commitOk) {
        if (!commitOk) {
            KMessageBox::error(this,
                i18n("Could not save the current draft. Version recall aborted."),
                i18n("Recall Failed"));
            return;
        }

        // Step 2: extract the historical version into a unique temp file.
        auto *temp = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/rpgforge_recall_XXXXXX.md"));
        temp->setAutoRemove(false);
        if (!temp->open()) {
            KMessageBox::error(this, i18n("Could not create temp file for recall."),
                               i18n("Recall Failed"));
            delete temp;
            return;
        }
        const QString tempPath = temp->fileName();
        temp->close();
        delete temp;

        GitService::instance().extractVersion(filePath, commitHash, tempPath)
            .then(this, [this, filePath, tempPath](bool ok) {
                if (!ok) {
                    KMessageBox::error(this,
                        i18n("Failed to extract the selected version."),
                        i18n("Recall Failed"));
                    QFile::remove(tempPath);
                    return;
                }

                // Step 3: install atomically. On Linux rename(2) atomically replaces
                // an existing destination; no prior remove needed (removing first would
                // open a data-loss window if rename/copy subsequently fail).
                if (!QFile::rename(tempPath, filePath)) {
                    // Fall back to copy-over-existing if tmp is on a different filesystem.
                    QFile::remove(filePath);
                    if (!QFile::copy(tempPath, filePath)) {
                        KMessageBox::error(this,
                            i18n("Failed to install the recalled version."),
                            i18n("Recall Failed"));
                    }
                    QFile::remove(tempPath);
                }

                if (m_document && m_document->url().toLocalFile() == filePath) {
                    m_document->openUrl(QUrl::fromLocalFile(filePath));
                }
                m_explorationsPanel->refresh();
            });
    });
}

bool MainWindow::saveAllDocuments()
{
    bool ok = true;
    auto saveOne = [&ok](KTextEditor::Document *doc) {
        if (!doc) return;
        if (doc->url().isEmpty()) return; // untitled — skip, caller needs saveAs
        if (!doc->isModified()) return;
        if (!doc->save()) ok = false;
    };
    saveOne(m_document);
    saveOne(m_researchDocument);
    if (ProjectManager::instance().isProjectOpen()) {
        ProjectManager::instance().saveProject();
    }
    return ok;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Idempotency guard: closeEvent can be re-entered (e.g. SIGTERM routed
    // through closeAllWindows() while the user is also clicking the X). The
    // ceremony below is destructive (drains workers, closes the DB) so it
    // must run exactly once per process lifetime.
    static bool s_shutdownRunning = false;
    if (s_shutdownRunning) {
        event->accept();
        return;
    }
    s_shutdownRunning = true;

    // 1. Best-effort silent save of all documents + project. saveAllDocuments
    //    already encapsulates editor docs (m_document, m_researchDocument)
    //    and ProjectManager::saveProject(); reuse it rather than diverging.
    if (!saveAllDocuments()) {
        qWarning() << "MainWindow::closeEvent: one or more documents failed to "
                      "save during shutdown; continuing close.";
    }

    // 2. Pause the librarian so no new background writes start while we
    //    drain. Other long-running services don't expose pause()/resume()
    //    today; if they do later, mirror this call.
    if (m_librarianService) {
        m_librarianService->pause();
    }

    // 3. Drain the global thread pool. Workers writing to the librarian DB
    //    MUST complete before we close the connections, or SQLite races
    //    teardown and corrupts the WAL. 5s cap keeps a stuck task from
    //    holding shutdown forever.
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(5000);

    // 4. Close the librarian DB explicitly. LibrarianDatabase::close() runs
    //    PRAGMA wal_checkpoint(TRUNCATE) on every owned connection (see
    //    bugfix-registry entry 2026-04-28 "Extraction-pipeline runtime
    //    defects"), collapsing the WAL cleanly before SQLite tears down.
    if (m_librarianService && m_librarianService->database()) {
        m_librarianService->database()->close();
    }

    event->accept();
    QMainWindow::closeEvent(event);
}

QString MainWindow::activeConflictFile() const
{
    if (m_conflictFiles.isEmpty() || m_conflictIndex < 0
        || m_conflictIndex >= m_conflictFiles.size()) {
        return QString();
    }
    return m_conflictFiles.at(m_conflictIndex).path;
}

QList<int> MainWindow::conflictProgress() const
{
    return QList<int>{m_conflictIndex, static_cast<int>(m_conflictFiles.size())};
}

void MainWindow::invokeVersionRecall(const QString &filePath, const QString &commitHash)
{
    onVersionSelected(filePath, commitHash);
}

void MainWindow::showReconciliationDialog(const QList<ReconciliationEntry> &entries)
{
    if (entries.isEmpty()) return;

    ReconciliationDialog dlg(entries, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<ReconciliationEntry> applied = dlg.entries();
    ProjectManager &pm = ProjectManager::instance();
    pm.beginBatch();
    for (const ReconciliationEntry &entry : applied) {
        applyReconciliationEntry(entry);
    }
    pm.endBatch();
}

void MainWindow::applyReconciliationEntry(const ReconciliationEntry &entry)
{
    ProjectManager &pm = ProjectManager::instance();
    if (!pm.isProjectOpen()) return;

    switch (entry.action) {
    case ReconciliationEntry::None:
        // User left this row unresolved — nothing to do.
        break;

    case ReconciliationEntry::Locate: {
        if (entry.resolvedPath.isEmpty() || entry.resolvedPath == entry.path) {
            qDebug() << "MainWindow::applyReconciliationEntry: Locate with no change for"
                     << entry.path;
            break;
        }
        // Compute whether the parent directory changed. If so we need a
        // move + (possibly) a rename. Otherwise a pure rename suffices.
        const QString oldParent = QFileInfo(entry.path).path();
        const QString newParent = QFileInfo(entry.resolvedPath).path();
        const QString newBasename = QFileInfo(entry.resolvedPath).fileName();

        if (oldParent == newParent) {
            // Same parent — rename-only.
            if (!pm.renameItem(entry.path, newBasename)) {
                qWarning() << "MainWindow::applyReconciliationEntry: rename failed for"
                           << entry.path << "->" << newBasename;
            }
        } else {
            // Different parent — move, then rename if the basename also changed.
            const QString sourceBasename = QFileInfo(entry.path).fileName();
            if (!pm.moveItem(entry.path, newParent)) {
                qWarning() << "MainWindow::applyReconciliationEntry: move failed for"
                           << entry.path << "->" << newParent;
                break;
            }
            if (sourceBasename != newBasename) {
                const QString movedPath = newParent.isEmpty()
                    ? sourceBasename
                    : (newParent + QLatin1Char('/') + sourceBasename);
                if (!pm.renameItem(movedPath, newBasename)) {
                    qWarning() << "MainWindow::applyReconciliationEntry: rename after move failed for"
                               << movedPath << "->" << newBasename;
                }
            }
        }
        break;
    }

    case ReconciliationEntry::Remove:
        if (!pm.removeItem(entry.path)) {
            qWarning() << "MainWindow::applyReconciliationEntry: remove failed for"
                       << entry.path;
        }
        break;

    case ReconciliationEntry::RecreateEmpty: {
        const QString absPath = QDir(pm.projectPath()).absoluteFilePath(entry.path);
        QFileInfo fi(absPath);
        if (!fi.exists()) {
            if (!QDir().mkpath(fi.absolutePath())) {
                qWarning() << "MainWindow::applyReconciliationEntry: mkpath failed for"
                           << fi.absolutePath();
                break;
            }
            QFile f(absPath);
            if (!f.open(QIODevice::WriteOnly)) {
                qWarning() << "MainWindow::applyReconciliationEntry: failed to create"
                           << absPath;
                break;
            }
            f.close();
            qDebug() << "MainWindow::applyReconciliationEntry: recreated empty file at" << absPath;
        }
        break;
    }
    }
}
