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
class ExplorationsPanel;
class OutlinePanel;
class PreviewPanel;
class ProjectTreePanel;
class VariablesPanel;
class ChatPanel;
class ProblemsPanel;
class SimulationPanel;
class Sidebar;
class LibrarianService;
class QWebEngineView;
class QPushButton;
class QTimer;
class QUrl;
class QSplitter;
class QAction;
class QVBoxLayout;
class QStackedLayout;
class QLabel;
class QFrame;
class QProgressBar;
class QLineEdit;
class QCloseEvent;
#include <QPair>
#include <QList>
#include <QUrl>
#include "analyzerservice.h"
#include "llmservice.h"
#include "gitservice.h"
#include "reconciliationtypes.h"

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFileFromUrl(const QUrl &url);
    /**
     * @brief Shows a diff between two files or two Git versions.
     * @param path1 Either an absolute file path OR the first path for git diff.
     * @param oldOrNewPath2 If hash1 is a git hash, this is the other hash (or empty). 
     *                      If path1 is a file path, this is the second file path.
     */
    void showDiff(const QString &path1, const QString &path2OrHash1, const QString &hash2 = QString());

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Single chokepoint for the application's shutdown ceremony.
     *
     * Runs on every quit path: window close (X), File→Quit, and SIGTERM/SIGINT
     * (which routes through QApplication::closeAllWindows()). Performs a
     * best-effort silent save of all documents, pauses long-running services,
     * drains the global thread pool, and explicitly closes the librarian
     * database so the WAL checkpoint runs before SQLite tears connections
     * down. See bugfix-registry.md "2026-04-28 — Unified graceful-shutdown
     * path (UI + signal)" for the binding invariants.
     */
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void newFile();
    void openFile();
    void saveFile();
    void saveFileAs();
    void newProject();
    void openProject();
    void closeProject();
    void importScrivener();
    void importWord();
    void characterGenerator();
    void startSimulation();
    void compareSimulations();
    void cloneProject();
    void projectSettings();
    void globalSettings();
    void compileToPdf();
    void onCursorPositionChanged();
    void updateCursorContext();
    void onTextChanged();
    void navigateToLine(int line);
    void updateProjectStats();
    void updateProjectPreview();
    void toggleFocusMode();
    void togglePreview();
    void collapsePreviewPane();
    void syncScroll();
    void updateErrorHighlighting();
    void updateLibrarianHighlights();
    void performSearch(const QString &text);

    // AI Actions
    void aiExpand();
    void aiRewrite();
    void aiSummarize();
    void onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);
    void onModelNotFound(LLMProvider provider, const QString &invalidModel, const QStringList &available, const QString &serviceName);
    void onForceLoreScan();

    // Explorations slots
    void onSwitchExplorationRequested(const QString &branchName);
    void onIntegrateExplorationRequested(const QString &sourceBranch);
    void onCreateLandmarkRequested(const QString &hash);
    void onRecallVersionRequested(const QString &hashOrPath);
    void onIntegrateFailed();
    void showNextConflict();
    void onVersionSelected(const QString &filePath, const QString &commitHash);

    /**
     * @brief Show the reconciliation dialog in response to
     *        ProjectManager::reconciliationRequired.
     *
     * Populates a ReconciliationDialog with the supplied entries. If the
     * user applies, the entries' user-chosen actions are forwarded to
     * ProjectManager inside a beginBatch()/endBatch() window.
     */
    void showReconciliationDialog(const QList<ReconciliationEntry> &entries);

public:
    KTextEditor::Document* editorDocument() const { return m_document; }
    KTextEditor::View* activeView() const;
    KTextEditor::Document* activeDocument() const;

    /// Accessor for the D-Bus surface — lets the rpgforge.dbus
    /// interface drive the entity graph view directly for end-to-end
    /// tests. Returns nullptr if no project is open.
    class EntityGraphPanel *entityGraphPanel() const { return m_entityGraphPanel; }
    /// Switches the central view to the entity graph panel and triggers
    /// a refresh from the librarian DB. No-op if no project is open.
    void showEntityGraph();

    /// Phase 4 navigation — resolves the entity by name (or alias) and
    /// opens its dossier file. Returns true on success. The dossier
    /// search uses the same lorekeeper/research roots the entity-graph
    /// panel's openDossier handler walks. Driven by Ctrl+E "Go to
    /// entity" and the corresponding D-Bus endpoint.
    bool navigateToEntityDossier(const QString &entityName);

private:
    /// Post-extraction maintenance chain. Called automatically after
    /// either the KnowledgeBase finishes indexing or the LibrarianService
    /// finishes a scan — those are the two events that can leave
    /// chunk_entities, mention_count, or community membership stale.
    /// Runs idempotent re-link + aggregate refresh + (Phase 5) community
    /// detection. Communities are an internal index used by retrieval;
    /// the user never triggers this directly and never sees the result.
    void runEntityIndexMaintenance();

public:

    /**
     * @brief Returns the KTextEditor document currently open in the main editor,
     *        or nullptr if no document is loaded. Used by the DBus adaptor.
     */
    KTextEditor::Document* currentDocument() const { return m_document; }

    /**
     * @brief Returns the identifier of the central view that is currently visible.
     *
     * The returned value is one of: "editor", "diff", "corkboard",
     * "imagepreview", "pdf", or an empty string if no central widget is
     * visible. Used by the DBus adaptor.
     */
    QString currentViewId() const;

    /**
     * @brief Returns the application's sidebar widget (non-owning pointer).
     *
     * Used by the DBus adaptor to enumerate and switch panels.
     */
    Sidebar* sidebar() const { return m_sidebar; }

    /**
     * @brief Returns the application's LibrarianService (variable extractor)
     * instance. Used by the project-tree panel for per-document rescan.
     */
    LibrarianService* librarianService() const { return m_librarianService; }

    /**
     * @brief Saves every open KTextEditor document that has an associated URL.
     *
     * Intended for "Save All" — also used by the DBus adaptor. Returns true if
     * every open document with a URL saved successfully (or if there was
     * nothing to save). Used by the DBus adaptor.
     */
    bool saveAllDocuments();

    /**
     * @brief Returns the path of the conflict file currently shown in the
     *        conflict banner, or an empty string if no integration conflict
     *        is active. Used by the DBus adaptor.
     */
    QString activeConflictFile() const;

    /**
     * @brief Returns conflict-resolution progress as [currentIndex, totalConflicts].
     *
     * currentIndex is zero-based. If no conflict resolution is active, both
     * values are zero. Used by the DBus adaptor.
     */
    QList<int> conflictProgress() const;

    /**
     * @brief Synchronous wrapper over onVersionSelected() so callers (e.g. the
     *        DBus adaptor) can trigger a version-recall without first showing
     *        the VersionRecallBrowser dialog.
     */
    void invokeVersionRecall(const QString &filePath, const QString &commitHash);

private Q_SLOTS:
    void showEditorContextMenu(KTextEditor::View *view, QMenu *menu);
    /**
     * @brief Apply saved splitter sizes after the window has a real geometry.
     *
     * Scheduled via QTimer::singleShot(0, ...) at the end of restoreSession()
     * so that QSplitter::restoreState consults laid-out widget sizes rather
     * than the unsized splitters present during MainWindow construction.
     * Without this deferral the proportional split applied against an
     * unsized splitter starves the editor pane on startup.
     */
    void restoreSplittersDeferred();

private:
    void setupEditor();
    void setupSidebar();
    void setupActions();
    void updateTitle();
    void saveSession();
    void restoreSession();
    void showCentralView(QWidget *widget);
    void insertProjectLinksAtCursor(const QList<QPair<QString, QUrl>> &items);

    /**
     * @brief Forward a user-confirmed reconciliation entry to ProjectManager.
     *
     * Called by showReconciliationDialog() for each row the user applied. The
     * caller wraps the whole batch in ProjectManager::beginBatch/endBatch so
     * there is exactly one save and one treeStructureChanged emission at the
     * end.
     */
    void applyReconciliationEntry(const ReconciliationEntry &entry);

    KTextEditor::Editor *m_editor = nullptr;
    KTextEditor::Document *m_document = nullptr;
    KTextEditor::View *m_editorView = nullptr;

    KTextEditor::Document *m_researchDocument = nullptr;
    KTextEditor::View *m_researchView = nullptr;
    QWidget *m_researchPane = nullptr; // wraps m_researchView with title + close btn
    QSplitter *m_editorSplitter = nullptr;

    QStackedLayout *m_centralViewLayout = nullptr;
    CorkboardView *m_corkboardView = nullptr;
    ImagePreview *m_imagePreview = nullptr;
    VisualDiffView *m_diffView = nullptr;
    class ImageDiffView *m_imageDiffView = nullptr;
    QWebEngineView *m_pdfViewer = nullptr;

    Sidebar *m_sidebar = nullptr;
    FileExplorer *m_fileExplorer = nullptr;
    ProjectTreePanel *m_projectTree = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    GitPanel *m_gitPanel = nullptr;
    ExplorationsPanel *m_explorationsPanel = nullptr;
    BreadcrumbBar *m_breadcrumbBar = nullptr;
    PreviewPanel *m_previewPanel = nullptr;
    VariablesPanel *m_variablesPanel = nullptr;
    ChatPanel *m_chatPanel = nullptr;
    ProblemsPanel *m_problemsPanel = nullptr;
    class EntityGraphPanel *m_entityGraphPanel = nullptr;
    SimulationPanel *m_simulationPanel = nullptr;
    QLabel *m_diagnosticsStatus = nullptr;
    QLabel *m_wordCountStatus = nullptr;
    QLabel *m_projectStatsStatus = nullptr;
    QLabel *m_syncStatusLabel = nullptr;
    QProgressBar *m_syncProgressBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_vSplitter = nullptr;
    QAction *m_togglePreviewAction = nullptr;
    bool m_diffClientAdded = false;
    /// Last user-chosen width of the preview pane. Saved whenever the user
    /// manually resizes the splitter while preview is visible; restored on
    /// the next togglePreview(true). Persists across app restarts via
    /// QSettings("preview/paneWidth"). 0 means "use the 30% default".
    int m_preferredPreviewWidth = 0;
    /// Cached editor/typewriterScrolling setting — read from QSettings at
    /// startup and refreshed when Settings is accepted. Prevents a per-
    /// keystroke QSettings() construction + disk/registry read inside
    /// onCursorPositionChanged().
    bool m_typewriterScrolling = false;

    QTimer *m_cursorDebounce = nullptr;
    QTimer *m_textChangeDebounce = nullptr;
    QTimer *m_analyzerDebounce = nullptr;
    QTimer *m_saveExplorationDataTimer = nullptr; // debounces colorMapChanged -> disk persistence

    int m_fileExplorerId = -1;
    int m_projectTreeId = -1;
    int m_outlineId = -1;
    int m_gitId = -1;
    int m_explorationsId = -1;
    int m_variablesId = -1;
    int m_chatId = -1;
    int m_simulationId = -1;

    QList<KTextEditor::MovingRange*> m_errorRanges;
    QList<KTextEditor::MovingRange*> m_diagnosticRanges;
    QList<KTextEditor::MovingRange*> m_librarianRanges;

    QUrl m_currentUrl;
    LibrarianService *m_librarianService = nullptr;
    QString m_lastSearchText;

    // Conflict resolution state
    QList<ConflictFile> m_conflictFiles;
    int m_conflictIndex = 0;
    QFrame *m_conflictBanner = nullptr;
    QLabel *m_conflictBannerLabel = nullptr;
    QPushButton *m_conflictNextBtn = nullptr;
};

#endif // MAINWINDOW_H
