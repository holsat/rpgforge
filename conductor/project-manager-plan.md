# Project Manager Evaluation & Implementation Plan

## Analysis of `PROJECT_MANAGER.md`

The document outlines a clear vision for the `ProjectManager` as the absolute "Source of Truth" for the application. This is a crucial architectural principle for RPG Forge.

### 1. Source of Truth & Exclusion
*   **Requirement:** The project tree (as stored in the `ProjectManager`'s JSON model) is the ultimate authority. Files on disk that are not registered in the project manager must be ignored by all internal services. The project tree hierarchy may be purely logical, but the file nodes represent real, absolute paths on the user's hard drive.
*   **Current State:** Partially implemented. `ProjectTreeModel` serializes to JSON, but services like `LibrarianService` often rely on raw filesystem scans (e.g., `QDirIterator`) rather than querying the `ProjectManager`'s registered paths.
*   **Action:** We must refactor all support services (Librarian, LoreKeeper, LLM agents) to request the list of active project files directly from the `ProjectManager` rather than walking the disk themselves.

### 2. Default Project Structure & Migration
*   **Requirement:** A specific hierarchy must be created upon project initialization:
    *   `Manuscript/` (Chapters -> Scenes)
    *   `Research/` (Files here open in split-screen)
    *   `LoreKeeper/` (Auto-updated LLM sketches - initially read-only and dynamically generated)
    *   `Stylesheets/style.css`
    *   `Media/` (Auto-created when non-text files are added; drag-and-drop generates markdown links)
*   **Current State:** `ProjectManager::createProject` creates a basic skeleton (a flat `manuscript/` folder and a `stylesheets/` folder). `CharacterDossierService` currently uses a hardcoded `library/Character Sketches/` path.
*   **Action:** 
    *   Update `setupDefaultProject` to generate the exact folder structure requested.
    *   **Migration:** When opening older projects that lack this structure, `ProjectManager::migrate()` must be updated to automatically create the missing root folders (`LoreKeeper`, `Research`, `Media`) and update the project tree JSON to reflect the new standard. Existing `library/` folders should be renamed or migrated to `LoreKeeper/`.

### 3. LoreKeeper Customization (v1: Read-Only)
*   **Requirement:** The user must be able to configure the "LoreKeeper" (formerly Historian) to track custom entities like "Characters", "Races", "Classes", or "Locations", complete with custom templates/prompts for each category.
*   **Current State:** The `CharacterDossierService` is hardcoded strictly for characters.
*   **Action:** 
    *   Rename/Refactor `CharacterDossierService` to a generalized `LoreKeeperService`.
    *   This service will read a list of configured tracking targets from the `ProjectManager` (configurable via `ProjectSettingsDialog`).
    *   When generating files, LoreKeeper must automatically create the necessary category folders (e.g., `LoreKeeper/Races/`) and markdown files directly underneath the `LoreKeeper` project entry in the tree view and on disk.
    *   **v1 Constraint:** The entire `LoreKeeper/` section, its subfolders, and its files will be strictly read-only to the user. The tree view will disable renaming/deleting, and the editor will lock the documents.

### 4. UI/UX Interaction Gating
*   **Requirement:** Any update/modification of the project view should trigger a pause of all support services to prevent race conditions.
*   **Feedback Integration:** Pausing on hover is overly aggressive. We will implement "Pause on Active Modification."
*   **Action:** Add event hooks in `ProjectTreePanel` to notify `AgentGatekeeper` when an active modification operation (drag-and-drop, rename, delete, or opening a context menu that might lead to a modification) begins and ends.

---

## Implementation Plan

### Phase 0: Pre-flight Check (Git Workflow)
1. Ensure all current tests pass.
2. Commit any outstanding work on the current branch.
3. Push the current branch to origin.
4. Create and checkout a new local branch (e.g., `feature/project-manager-refactor`).

### Phase 1: Structural Alignment & Migration
1.  **Implementation:**
    *   Update `ProjectManager::createProject` to build the required skeleton (`Manuscript/`, `Research/`, `LoreKeeper/`, `Stylesheets/`, `Media/`). Note: `LoreKeeper` is empty by default; services populate it.
    *   Enhance `ProjectManager::migrate()` to detect old project structures and safely migrate them (e.g., moving `library/` to `LoreKeeper/`, creating `Research/` and `Media/` if missing).
    *   Implement drag-and-drop in `ProjectTreePanel` for the `Media` folder: When an image is dragged into the editor from this folder, generate `![alt_text](media/filename.ext)`.
2.  **Code Quality:** Add detailed Doxygen-style comments to `migrate()` explaining the schema transition logic. Add `qDebug()` statements tracking the creation of each default folder.
3.  **Testing:**
    *   *Unit Test:* Add `testV3SchemaMigration` to `tests/test_projectmanager.cpp` simulating an old project file to verify folders are created and the JSON tree is updated.
    *   *Smoke Test:* Manually create a new project in the UI and verify the folder structure on disk and in the Project Tree.

### Phase 2: The Generalized LoreKeeper
1.  **Implementation:**
    *   Rename/Refactor `CharacterDossierService` to `LoreKeeperService`.
    *   Add a `loreKeeperConfig` JSON object to the `ProjectManager`'s schema. This object will hold categories and their specific LLM prompts.
    *   Create a UI tab in `ProjectSettingsDialog` to manage these categories.
    *   Update `LoreKeeperService` to dynamically create category folders and markdown files under the `LoreKeeper` node in the `ProjectTreeModel` and sync to disk using a real LLM connection (Gemini).
    *   Enforce read-only UI restrictions for items under `LoreKeeper/` in `ProjectTreeModel` and the editor.
2.  **Code Quality:** Heavily comment the `LoreKeeperService` dynamic folder creation logic. Add debug output showing the prompt being used and the path being written to.
3.  **Testing:**
    *   *Integration Test:* Create `tests/test_lorekeeper.cpp` that uses a **real LLM connection (Gemini)** to verify that custom categories correctly trigger the generation of a valid markdown file based on prompt context, and that it gets added to the project tree model.
    *   *Smoke Test:* Add a new category in settings, write a paragraph about it in the manuscript, and verify the file appears in the tree populated with real LLM content and is uneditable by the user.

### Phase 3: The Source of Truth Migration
1.  **Implementation:**
    *   Implement `ProjectManager::getActiveFiles()`, which parses the JSON tree and returns a flat list of all absolute file paths currently registered.
    *   Refactor `LibrarianService`, `LoreKeeperService`, and `AnalyzerService` to **only** process files returned by this method.
2.  **Code Quality:** Comment the `getActiveFiles` tree traversal. Add debug logging in the services to output the number of files they are processing.
3.  **Testing:**
    *   *Unit Test:* Add a test in `test_projectmanager.cpp` verifying `getActiveFiles()` returns the correct absolute physical paths and ignores physical files placed in the directory but not added to the logical tree.
    *   *Smoke Test:* Create a file via the OS file manager inside the project folder. Verify the Librarian and LoreKeeper do not parse it.

### Phase 4: UX Integration (Research Split & Gating)
1.  **Implementation:**
    *   Modify `MainWindow::openFile()`: If the file path contains `/Research/`, force it to open in a split-screen view alongside the primary manuscript editor.
    *   Add hooks to `ProjectTreePanel` to notify `AgentGatekeeper` during active modifications (e.g., `mousePressEvent` initiating a drag, `closeEditor` finishing an inline rename, context menu execution) to temporarily pause and then resume all active background agents.
2.  **Code Quality:** Document the specific events triggering the pause/resume cycle. Add debug logs when the gatekeeper is engaged by the UI.
3.  **Testing:**
    *   *Smoke Test:* Open a file in the `Research/` folder and verify it opens in split-pane. Rename a file in the project tree and verify `AgentGatekeeper` pauses the services via the debug logs.