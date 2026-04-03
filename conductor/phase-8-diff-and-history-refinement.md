# Phase 8: Diff Tool Refinement & Advanced Version History

## Objective
Address user feedback regarding the compare tool and version history by:
1.  Ensuring applied diffs are persisted and reloaded in the main editor.
2.  Allowing users to swap the diff direction (compare current vs. old, or vice versa).
3.  Including branch information in the version history.
4.  Allowing history to show versions from all branches, potentially grouped or sorted by branch.

## Key Files & Context
- `src/gitservice.h/cpp`: The core Git logic, specifically `getHistory` and `computeDiff`.
- `src/visualdiffview.h/cpp`: The UI for comparing versions and applying hunks.
- `src/historydialog.h/cpp`: The UI for viewing file history.
- `src/mainwindow.h/cpp`: Orchestrates opening files and switching between views.

## Proposed Changes

### 1. Git Service Enhancements (`src/gitservice.h/cpp`)
- **Standardize Data Structures:** Rename `GitLogEntry` to `VersionInfo` in `gitservice.cpp` to match the header.
- **Update `VersionInfo`:**
    - Add `QStringList branches` to store names of branches that contain this commit.
    - Add `QString author` field.
- **Enhance `getHistory`:**
    - Use `git_revwalk_push_glob(walker, "refs/heads/*")` to include commits from all local branches.
    - Before walking, iterate over all local branches and store their tips in a `QMap<QString, QStringList>` (Commit OID -> Branch Names).
    - As each commit is processed, check the map to see which branches it belongs to.
    - Note: A commit might be on multiple branches.
- **Granular Hunk Grouping:** (Already done in previous step, but ensure it works with swapped directions).

### 2. Visual Diff Tool Refinements (`src/visualdiffview.h/cpp`)
- **Add Toolbar:**
    - **Save Button:** Explicitly saves `m_newDoc` to its original file path.
    - **Swap Button:** Swaps `m_oldDoc` and `m_newDoc` (and their hashes/paths) and triggers a re-diff. This effectively reverses the diff direction.
- **Signals:**
    - `saveRequested(const QString &filePath)`: Emitted when the user saves from the diff view. `MainWindow` should catch this to reload its own document if it's the same file.
- **Direction Awareness:** Ensure `setDiff` and `setFiles` clearly label "Source" and "Target" (or "Left" and "Right") and that the direction is intuitive.

### 3. Version History UI Improvements (`src/historydialog.h/cpp`)
- **Table Columns:**
    - Add a "Branch" column to the `QTableWidget`.
    - If a commit is on multiple branches, show them comma-separated.
- **Sorting/Grouping:**
    - Allow clicking the "Branch" header to sort.
    - Use different colors or icons to distinguish branches if possible.

### 4. Main Window Integration (`src/mainwindow.h/cpp`)
- **Reload Mechanism:**
    - Catch the `saveRequested` signal from `VisualDiffView`.
    - If the saved file is the one currently open in `m_document`, call `m_document->openUrl(url)` to reload it from disk.
- **Status Feedback:** Show a message in the status bar when a diff is applied and saved.

## Verification & Testing
1.  **Diff Persistence:**
    - Open a file's history.
    - Compare with an old version.
    - Apply a change.
    - Click "Save" in the diff view.
    - Switch back to the file in the editor (via Project Tree or breadcrumbs).
    - Verify the change is reflected in the editor.
2.  **Diff Swap:**
    - In the diff view, click the "Swap" button.
    - Verify the "Old" and "New" contents are swapped and the connector bands are recalculated correctly.
3.  **Cross-Branch History:**
    - Create a new branch `exploration-a`.
    - Modify a file and commit.
    - Switch back to `main`.
    - Open history for that file.
    - Verify the version from `exploration-a` is visible and labeled with its branch name.
4.  **Multi-Branch Commit:**
    - Create a branch `exploration-b` from `main`.
    - Verify that commits shared by `main` and `exploration-b` show both branch names.

## Migration & Rollback
- No database or configuration migrations required.
- Standard Git revert if bugs are introduced.
