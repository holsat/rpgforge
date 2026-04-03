# RPG Forge Phase 6 & 7: Seamless Version Control & History

## Objective
Provide writers with an "infinite undo", visual comparison, and storyline exploration system backed by Git and GitHub, while keeping the underlying Git mechanics completely hidden from the user interface. Version control should act as an automated, invisible safety net rather than a manual chore.

## Core Concepts & Terminology (User-Facing)
*   **Sync:** Initializes version control, organizes assets, and pushes to the cloud.
*   **Auto-Sync:** Saves every change automatically as a distinct, restorable version.
*   **History / Versions:** The timeline of changes for a specific file.
*   **Bookmarks / Tags:** Custom labels applied to specific points in time.
*   **Explorations / Drafts:** Alternate timelines (Git branches) for trying out new ideas.

---

## Feature Requirements

### 1. Project Sync & Restructuring (The "Sync" Button)
Located in the Project View, this acts as the initialization and cleanup tool.
*   **Initialization:** Automatically runs `git init` in the background for new projects.
*   **Asset Import & Link Updating:**
    *   Scans all markdown files in the project for links to external files (images, documents outside the project folder).
    *   Notifies the user of external dependencies.
    *   Copies external assets into the project (e.g., placing images into a dedicated `media/` folder).
    *   Automatically rewrites the markdown links to point to the new local paths.
*   **Hierarchy Alignment:** Moves physical files on the hard drive to match the logical folder structure defined in the Project Tree.
*   **Project Import (Clone):**
    *   Add a "Clone Project from URL" option to the startup screen/menu.
    *   Implement `git clone` using `libgit2`.
    *   Restore project state from the `rpgforge.project` metadata file included in the repository.

### 2. Auto-Sync & GitHub Integration
*   **Auto-Sync Toggle:** When enabled, every file save triggers an automatic, background `git commit`. 
*   **Metadata Persistence:** Ensure the `rpgforge.project` file is committed whenever the project structure or settings change, allowing full state restoration on other machines.
*   **GitHub Onboarding:** A user-friendly wizard to connect a GitHub account, set up authentication (using KWallet for security), and create a remote repository.
*   **Auto-Push:** If a GitHub remote is configured, the Auto-Sync feature will automatically push changes upstream after committing.

### 3. File History & Restoration
*   **History View:** Right-clicking a file in the Project Tree opens its History.
*   **Version Display:** Shows versions by an incremental number, date, time, and any user-applied tags.
*   **Safe Checkout:** Selecting a previous version checks it out into a hidden local cache (e.g., `.rpgforge/.versions/`) to prevent disrupting the active workspace.
*   **Actions on Old Versions:**
    *   **View/Edit:** Read the old version in the editor.
    *   **Permanently Restore:** Overwrites the current working version with the old version (creates a new commit reflecting the restoration).
    *   **Compare:** Opens the Visual Diff tool against the current working version or another historical version.

### 4. Visual Diff & Merge (Kompare-Style)
*   **Side-by-Side View:** Displays two versions of a document side-by-side.
*   **Visual Highlights:** Clearly highlights insertions (green), deletions (red), and modifications (yellow) at the word and line level.
*   **Point-and-Click Merging:** Arrow buttons allow the user to easily push individual changes from the old version into the current working version (or vice versa).

### 5. Tagging & Bookmarking
*   Users can apply custom text tags (e.g., "Pre-Combat Rewrite", "Final Draft") to the current state of a file or the whole project. These correspond to lightweight `git tags` under the hood.

### 6. Explorations (Branching)
*   **Create Exploration:** Allows the author to branch off from the current state to try a new narrative direction or mechanics change without breaking the main project.
*   **Revert/Discard:** Easily delete the exploration if the idea didn't work out.
*   **Make Main:** If the exploration is successful, a one-click option to replace the main storyline with the exploration's changes (merges the branch and sets it as the primary).

---

## Step-by-Step Implementation Plan

### Step 1: `libgit2` Core & Auto-Committing Foundation
*   Link `libgit2` to the project.
*   Create a `GitService` singleton running on a background thread.
*   Implement the hidden "Auto-Sync" mechanism: hook into the editor's save signal to trigger automatic commits with generic messages (e.g., `Auto-save: filename.md`).

### Step 2: The "Sync" Button & Project Restructuring
*   Implement the UI for the "Sync" button in the `ProjectTreePanel`.
*   **Parser Update:** Extend `MarkdownParser` to extract all link and image paths.
*   **File Operations:** Implement logic to detect out-of-bounds paths, copy files to `media/`, and move files to match the logical project tree.
*   **Text Replacement:** Implement safe regex/string replacement to update markdown links after files are moved.

### Step 3: GitHub Onboarding Wizard
*   Create a dialog sequence for GitHub setup:
    1.  Explain the benefits (Cloud backup).
    2.  Prompt for Personal Access Token (PAT).
    3.  Store securely in KWallet.
    4.  Create remote repo via GitHub API (or use existing).
*   Hook the background push into the Auto-Sync pipeline.

### Step 4: History Browser & Tagging
*   Create a `HistoryDialog` or docked panel.
*   Use `libgit2` to walk the commit history for a specific file path.
*   Implement the hidden checkout mechanism: when a user clicks a past version, use `libgit2` to extract that blob's content into a temporary file (`.rpgforge/.versions/hash.md`) and open it in the editor.
*   Add UI for creating and viewing tags.

### Step 5: Visual Diff View
*   Create a new Qt View combining two `KTextEditor` instances side-by-side.
*   Integrate a diffing algorithm (libgit2's diff or a custom Myers implementation).
*   Draw connector lines and highlight hunks.
*   Implement the logic for the "merge arrows" to transfer text blocks between the active editors.

### Step 6: Explorations (Branching UI)
*   Add an "Explorations" manager to the Project Tree.
*   Map "Create Exploration" to `git checkout -b`.
*   Map "Discard" to `git branch -D`.
*   Map "Make Main" to checking out `main`, merging the exploration branch, and syncing.