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

#ifndef RPGFORGEDBUS_H
#define RPGFORGEDBUS_H

#include <QDBusAbstractAdaptor>
#include <QList>
#include <QStringList>
#include <QVariantList>

#include "reconciliationtypes.h"

class MainWindow;

/**
 * \brief DBus adaptor exposing RPG Forge's UI surface for automated testing.
 *
 * Registered at /org/kde/rpgforge/MainWindow on the session bus under
 * service org.kde.rpgforge. External test tools call these methods over
 * DBus instead of simulating AT-SPI input.
 *
 * Every method runs on the main thread via Qt's DBus marshalling. Methods
 * that can fail return a bool; methods that query state return the
 * requested data or an empty value if the precondition isn't met
 * (no project open, etc.).
 */
class RpgForgeDBus : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.rpgforge.MainWindow")

public:
    explicit RpgForgeDBus(MainWindow *window);
    ~RpgForgeDBus() override = default;

public Q_SLOTS:
    // ---------------- Application lifecycle ----------------
    /** \brief Returns the app version string. */
    QString version() const;

    /** \brief True when a project is currently loaded. */
    bool isProjectOpen() const;

    /** \brief Absolute path to the open project root, or "" if none. */
    QString currentProjectPath() const;

    /** \brief Open the project whose .project file sits at \a path. */
    bool openProject(const QString &path);

    /** \brief Close the current project. Returns true if something was closed. */
    bool closeProject();

    /** \brief Quit the application cleanly. */
    void quit();

    // ---------------- UI state queries ----------------
    /** \brief True if the main window is visible. */
    bool mainWindowVisible() const;

    /** \brief Rect of the main window in screen coords: [x, y, width, height]. */
    QList<int> mainWindowGeometry() const;

    /** \brief True if the current editor document has unsaved modifications. */
    bool currentDocumentModified() const;

    /** \brief Returns the auto-sync setting (auto-commit on save). */
    bool autoSyncEnabled() const;

    /** \brief Enables/disables auto-sync. Returns the new value. */
    bool setAutoSyncEnabled(bool enabled);

    // ---------------- Sidebar ----------------
    /** \brief Returns the display names of all registered sidebar panels. */
    QStringList sidebarPanels() const;

    /** \brief Returns the display name of the currently visible panel, or "". */
    QString activeSidebarPanel() const;

    /** \brief Switch the visible sidebar panel by display name. Returns true on success. */
    bool showSidebarPanel(const QString &name);

    // ---------------- Editor ----------------
    /** \brief Absolute path of the file currently open in the main editor, or "". */
    QString currentEditorFilePath() const;

    /** \brief Identifier of the current central view: editor, diff, corkboard, imagepreview, or pdf. */
    QString currentCentralView() const;

    /** \brief Open the file at \a absolutePath in the editor. */
    bool openFile(const QString &absolutePath);

    // ---------------- Project tree ----------------
    /** \brief Returns relative paths of every file in the project tree. */
    QStringList projectFiles() const;

    /** \brief Returns absolute paths for every file in the project tree
     *         (convenience over projectFiles()). */
    QStringList projectFilesAbsolute() const;

    /** \brief Returns relative paths for every folder in the project tree. */
    QStringList projectFolders() const;

    /** \brief True if the given relative path is present in the project tree. */
    bool projectContains(const QString &relativePath) const;

    // ---------------- Tree snapshot introspection (Phase 2) ----------------
    /** \brief Returns the whole-tree snapshot serialised as a QVariantMap.
     *  Keys: name, path, synopsis, status, type, category, diskPresent,
     *  isTransient, children (QVariantList of nested maps). Empty map
     *  when no project is open. */
    QVariantMap treeSnapshotJson();

    /** \brief Snapshot for a single folder, with the same shape as
     *  treeSnapshotJson(). Empty map when no project is open, when the
     *  path is missing, or when the path resolves to a non-Folder node. */
    QVariantMap folderSnapshotJson(const QString &path);

    /** \brief True if the given project-relative path is present in the tree.
     *  Thin wrapper over ProjectManager::pathExists; exposed as a dedicated
     *  introspection endpoint so test harnesses do not need to pull the
     *  full snapshot just to assert presence. */
    bool pathExists(const QString &path);

    // ---------------- Tree mutations (Phase 3) ----------------
    // Thin wrappers around the atomic ProjectManager mutation API. Each
    // performs the disk op and the tree update as a single unit; partial
    // failures roll back. Return the ProjectManager result verbatim.

    /** \brief Create a folder at \a relPath (empty to auto-derive from
     *  parent + name) under \a parentPath, creating it on disk as well. */
    bool addFolderAt(const QString &name, const QString &relPath, const QString &parentPath);

    /** \brief Register a file at \a relPath in the tree; creates an empty
     *  file on disk if it doesn't exist yet. */
    bool addFileAt(const QString &name, const QString &relPath, const QString &parentPath);

    /** \brief Rename the item at \a path, renaming the backing file or
     *  folder on disk to match. */
    bool renameItemAt(const QString &path, const QString &newName);

    /** \brief Move the item at \a path to \a newParentPath at \a row, issuing
     *  a disk rename into the new parent directory. */
    bool moveItemTo(const QString &path, const QString &newParentPath, int row);

    /** \brief Remove the item at \a path from both tree and disk. */
    bool removeItemAt(const QString &path);

    // ---------------- Reconciliation (Phase 4) ----------------
    /** \brief Last-emitted ReconciliationRequired payload as a list of
     *  QVariantMap with keys: path, displayName, category, type, action,
     *  resolvedPath, suggestedPath. Empty list when no reconciliation is
     *  pending or no project is open. */
    QVariantList pendingReconciliation();

    /** \brief Drive a Locate action for \a oldPath using \a newPath. Bypasses
     *  the dialog for scripted runs. Returns the ProjectManager result. */
    bool applyReconciliationLocate(const QString &oldPath, const QString &newPath);

    /** \brief Drive a Remove action for \a oldPath. Bypasses the dialog. */
    bool applyReconciliationRemove(const QString &oldPath);

    /** \brief Drive a RecreateEmpty action for \a oldPath. Creates an empty
     *  file at the path on disk if it does not already exist. */
    bool applyReconciliationRecreate(const QString &oldPath);

    // ---------------- Explorations / Git queries ----------------
    /** \brief Returns names of all explorations (branches). */
    QStringList explorationNames() const;

    /** \brief Name of the currently checked-out exploration, or "". */
    QString currentExploration() const;

    /** \brief True if the working tree has uncommitted changes. */
    bool hasUncommittedChanges() const;

    /** \brief Returns detailed commit nodes for the current repo as a list of
     *  maps with keys: hash, branchName, message, date (ISO),
     *  tags (QStringList), wordCount, wordCountDelta, primaryParentHash,
     *  mergeParentHash. Blocks briefly on the ExplorationNode future via
     *  .result(). */
    QVariantList graphNodes() const;

    /** \brief Short-form recent commits on the current branch: list of maps
     *  with hash, message, date. Bounded by \a limit. */
    QVariantList recentCommits(int limit) const;

    /** \brief Names of all tags (landmarks) in the repo. */
    QStringList landmarkNames() const;

    /** \brief Returns parked (stashed) change entries as a list of maps with
     *  keys: index, message, onBranch, date. */
    QVariantList parkedChanges() const;

    // ---------------- Explorations / Git actions ----------------
    /** \brief Create a new exploration with the given name. */
    bool createExploration(const QString &name);

    /** \brief Switch to the named exploration (assumes clean tree; caller may
     *  need to park first). */
    bool switchExploration(const QString &name);

    /** \brief Save all open documents. Returns true on success. */
    bool saveAll();

    /** \brief Commit all working-tree changes. Returns true on success.
     *  Blocks on the future. */
    bool commitAll(const QString &message);

    /** \brief Park (stash) all uncommitted changes with \a message. */
    bool parkChanges(const QString &message);

    /** \brief Restore parked changes at index. */
    bool restoreParkedChanges(int stashIndex);

    /** \brief Discard parked changes at index. */
    bool discardParkedChanges(int stashIndex);

    /** \brief Integrate (merge) a source exploration into the current one.
     *  Returns true if the merge succeeded cleanly. If there are conflicts,
     *  returns false; conflictingFiles() will then list the conflicts. */
    bool integrateExploration(const QString &sourceBranch);

    /** \brief Returns conflicting file paths after a failed integrate. */
    QStringList conflictingFiles() const;

    /** \brief Create a landmark (tag) at the given commit hash. */
    bool createLandmark(const QString &commitHash, const QString &landmarkName);

    /** \brief Recall the version of \a filePath at \a commitHash.
     *  Equivalent to what VersionRecallBrowser's "Recall Version" button does.
     *  Auto-commits current changes first, then overwrites filePath with the
     *  historical content, then triggers editor reload. */
    bool recallVersion(const QString &filePath, const QString &commitHash);

    // ---------------- Conflict state ----------------
    /** \brief Path of the conflict file the conflict banner is currently
     *  showing, or "". */
    QString activeConflictFile() const;

    /** \brief Returns [currentIndex, totalConflicts] for the ongoing
     *  integration, or [0, 0] if none. */
    QList<int> conflictProgress() const;

    // ---------------- Dialog introspection + interaction ----------------
    /** \brief Titles of every currently-visible QDialog owned by the
     *  application. */
    QStringList openDialogTitles() const;

    /** \brief Accept (OK/Yes/primary button) the dialog whose windowTitle
     *  matches. Returns true if a matching dialog was found and accepted. */
    bool acceptDialog(const QString &windowTitle);

    /** \brief Reject (Cancel/Close) the dialog whose windowTitle matches. */
    bool rejectDialog(const QString &windowTitle);

    /** \brief Fill a QLineEdit by objectName inside the dialog whose
     *  windowTitle matches. Returns true on success. Objects without an
     *  objectName won't be found; the test harness is expected to know the
     *  objectName it needs. */
    bool fillDialogLineEdit(const QString &windowTitle,
                            const QString &objectName,
                            const QString &value);

    // ---------------- External-change + filesystem watcher (Phase 5) ----------------
    /** \brief Re-read rpgforge.project from disk and rebuild the live tree.
     *  Returns the ProjectManager result. */
    bool reloadFromDisk();

    /** \brief Open an external-change window on ProjectManager. Returns true
     *  (always — there's no failure mode). */
    bool beginExternalChange();

    /** \brief Close an external-change window on ProjectManager. Returns
     *  true if a matching begin was open, false if the call was
     *  unbalanced. */
    bool endExternalChange();

    /** \brief Block until the filesystem-watcher debounce timer is quiet,
     *  or \a timeoutMs has elapsed. Returns true if quiescence was reached,
     *  false if the timeout fired first. Used by tests to let external
     *  changes settle deterministically. */
    bool waitForFsQuiescence(int timeoutMs);

    // ---------------- Disk authority (Phase 6) ----------------
    /** \brief Walks the project directory on disk and returns the list of
     *  project-relative paths the tree would be populated from. Applies
     *  the same skip list as ProjectTreeModel::buildFromDisk. Empty when
     *  no project is open. Used by tests to assert tree == disk. */
    QStringList diskSnapshot();

    // ---------------- RagAssistService (headless test driver) ----------------
    /**
     *  \brief Synchronously drive RagAssistService::generate() and return
     *  the final response text.
     *
     *  DBus calls are intrinsically blocking, and the service's pipeline
     *  is asynchronous. This method runs a nested QEventLoop until the
     *  service reports completion (or \a timeoutMs fires), then returns.
     *
     *  On success: returns the LLM's final text.
     *  On error: returns an empty QString (check ragLastError()).
     *  On timeout: returns an empty QString with ragLastError set to
     *  "timeout".
     *
     *  Parameters map onto RagAssistRequest:
     *    - \a providerInt    = static_cast<int>(LLMProvider), see llmservice.h
     *    - \a comprehensive  = true → SynthesisDepth::Comprehensive
     *                          (multi-hop + query expansion), false → Quick
     *    - \a activeFilePath may be empty; used for RAG dedup
     *
     *  No history / priorTurns support here — tests that need multi-turn
     *  conversation should issue multiple calls and thread their own
     *  context via extraSources. Streaming is always off on this path.
     */
    QString ragGenerate(const QString &systemPrompt,
                        const QString &userPrompt,
                        const QString &entityName,
                        int providerInt,
                        const QString &model,
                        bool comprehensive,
                        const QString &activeFilePath,
                        int timeoutMs);

    /** \brief Error message from the most recent ragGenerate() call, or
     *  "" if the last call succeeded. */
    QString ragLastError() const { return m_ragLastError; }

    /** \brief List of project-relative file paths whose chunks were
     *  included in the most recent ragGenerate() prompt. Empty when
     *  retrieval returned no passages or no ragGenerate has run. */
    QStringList ragLastRetrievalSources() const { return m_ragLastRetrievalSources; }

private:
    MainWindow *m_window;
    QList<ReconciliationEntry> m_pendingReconciliation;

    // RagAssistService testing state. Populated by ragGenerate(); read
    // by ragLastError() / ragLastRetrievalSources(). Tests interleave
    // these two read methods between generate calls to make assertions
    // about what the retrieval pipeline surfaced.
    mutable QString m_ragLastError;
    mutable QStringList m_ragLastRetrievalSources;
};

#endif // RPGFORGEDBUS_H
