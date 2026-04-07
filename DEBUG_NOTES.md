# Debugging Notes: Variable Autocomplete

## Current Status (2026-04-02, Claude session)
- **Branch:** `fix/variable-system-crashes`
- **State:** ✅ FULLY WORKING. Both variable autocomplete (`{{`) and word completion (Ctrl+Space) function correctly.
- **Diagnostic logging:** Still present in `variablecompletionmodel.cpp` and `mainwindow.cpp` — should be removed in a future cleanup pass.

## Summary of All Fixes Applied

### 1. Completion Model: Two-Level Hierarchy (crash fix)
- **File:** `variablecompletionmodel.cpp`
- **Problem:** Flat model structure caused infinite recursion in Kate's internal proxy model (`KateCompletionModel::createItems()`).
- **Fix:** Implemented two-level hierarchy matching `KateWordCompletionModel`: Root → Group Header (internalId=0) → Items (internalId=1). Overrode `index()`, `parent()`, `rowCount()` to implement this tree structure.

### 2. Correct Column for Name Data (display fix)
- **File:** `variablecompletionmodel.cpp`
- **Problem:** Variable names were provided on Column 1 (Icon), but Kate expects them on Column 3 (`KTextEditor::CodeCompletionModel::Name`).
- **Fix:** Changed data() to return names on the `Name` column enum.

### 3. `shouldStartCompletion` Trigger (trigger fix)
- **File:** `variablecompletionmodel.cpp`
- **Problem:** `insertedText` parameter is accumulated text since last cursor change, not a single keystroke. Checking `insertedText == "{"` never matched `{{`.
- **Fix:** Scan the actual document line at cursor position to detect `{{` context by walking backwards over variable-name characters and checking for `{{` prefix.

### 4. Popup Hidden on Exact Match (visibility fix)
- **File:** `variablecompletionmodel.cpp`
- **Problem:** Default `matchingItem()` returns `HideListIfAutomaticInvocation`. With empty filter string, all items get perfect match scores, causing popup to hide during automatic invocation.
- **Fix:** Override `matchingItem()` to return `None`.

### 5. No `beginResetModel()`/`endResetModel()` in `completionInvoked()` (stability fix)
- **File:** `variablecompletionmodel.cpp`
- **Problem:** Kate's built-in models never call model reset signals inside `completionInvoked()`. Doing so interferes with the presentation model's state management.
- **Fix:** Simply call `updateVariables()` directly, matching Kate's built-in pattern.

### 6. QStackedWidget → QVBoxLayout (layout fix)
- **Files:** `mainwindow.h`, `mainwindow.cpp`
- **Problem:** `QStackedWidget` was used to switch between editor, corkboard, image preview, and PDF views. While not the primary cause of the popup issue, QStackedWidget has internal layout machinery that can interfere with child widget coordinate mapping.
- **Fix:** Replaced with a plain `QWidget` + `QVBoxLayout`. All four views are added to the layout. A new `showCentralView(QWidget*)` helper manages visibility — only one view is shown at a time; hidden widgets don't participate in QVBoxLayout.

### 7. Popup Z-Order and Positioning (the critical visibility fix)
- **Files:** `mainwindow.h`, `mainwindow.cpp`
- **Problem:** Two issues combined to make the popup invisible:
  1. **Z-order:** The `KateCompletionWidget` is parented to `MainWindow` (via `setParent(m_view->window())` in its constructor). But `MainWindow`'s `centralWidget` (set via `setCentralWidget()`) was created AFTER the completion widget, putting it higher in the z-order. The central widget fills the entire window, so it covered the popup completely.
  2. **Positioning on Wayland:** Kate's `updatePosition()` uses `parentWidget()->geometry()` for bounds checking. On Wayland, `geometry()` returns position offsets for window decorations (e.g., y=25 for title bar) but `mapFromGlobal()` treats the widget's (0,0) as global (0,0). This caused the bounds check to use `geometry().bottom()` = 25+height-1 instead of just `height`, allowing the popup to extend 25px below the visible window.
- **Fix:** Installed a `QEvent::Show` event filter on the `KateCompletionWidget`. When the popup is shown, a deferred callback (`QTimer::singleShot(0, ...)`) recalculates the position using `cursorToCoordinate()` → `mapTo(parentWidget)` with correct bounds checking against `parentWidget()->height()`, then calls `popup->raise()` to bring it above all sibling widgets.

## Architecture Notes
- Event filter installed during deferred completion model registration (500ms after view creation)
- `showCentralView()` replaces all `m_centralStack->setCurrentWidget()` calls
- Variable names with `CALC:` prefix have the prefix stripped before display
- `shouldAbortCompletion()` returns false to keep popup open during typing
- `filterString()` returns text from completion range start to cursor for progressive filtering
- `executeCompletionItem()` inserts the variable name and appends `}}` if not already present

## 2026-04-03: Diff Tool & History Refinement (Phase 8 Foundation)
- **Fixed Stale Content:** Added 'Save Changes' to `VisualDiffView` with signals (`saveRequested`, `reloadRequested`) to `MainWindow` for auto-reloading the editor document.
- **Added Diff Direction Swap:** Implemented bi-directional diffing in `VisualDiffView` via a 'Swap Direction' button.
- **Enhanced Cross-Branch History:** Updated `GitService::getHistory` to walk all local branches (`refs/heads/*`) and correctly map commits to their respective branches.
- **Improved History UI:** Added 'Branch' column to `HistoryDialog` and enabled multi-column sorting.
- **Visual Refinements:** Standardized Kompare-like color bands (Salmon/Green/Blue) and implemented 40% control point Bezier curves for smoother flow.
- **Build Fixes:** Resolved several libgit2 and KF6 API usage errors (e.g., `git_repository_head_id`, `git_signature_name`, and `KTextEditor::View` scrolling methods).

## 2026-04-03: LLM Integration Foundation (Phase 9)
- **`LLMService` Implementation:** Created a singleton service to handle OpenAI, Anthropic, and Ollama APIs. Implemented SSE (Server-Sent Events) streaming for real-time response updates.
- **Secure Key Storage:** Integrated `KWallet` for secure storage of LLM API keys.
- **`SettingsDialog`:** Added a global configuration dialog with tabs for LLM Providers and reusable Prompt Templates.
- **`ChatPanel` UI:** Implemented a new sidebar panel using `QWebEngineView` for rich message history and `QTextEdit` for input.
- **Editor AI Actions:** Added "Expand", "Rewrite", and "Summarize" actions that send selected text to the AI with pre-configured prompts.
- **Build Integration:** Updated `CMakeLists.txt` and `rpgforgeui.rc` to register the new service, dialog, and panel.

## 2026-04-03: LLM Game Analyzer & RAG (Phase 10)
- **`KnowledgeBase` Implementation:** Created a SQLite-backed local vector index for semantic search across project markdown files.
- **RAG Integration:** Added `generateEmbedding` to `LLMService` to augment analysis context with semantically similar project chunks.
- **`AnalyzerService`:** Implemented background debounced analysis (5s) using JSON-mode LLM prompts to detect rule conflicts.
- **`ProblemsPanel`:** Added a bottom dock panel listing project-wide diagnostics, filterable by severity, with double-click navigation.
- **Inline Diagnostics:** Rendered diagnostic results directly in `KTextEditor` using squiggly underlines via `MovingRange`.
- **Status Bar Integration:** Added a live diagnostic counter to the IDE's status bar.

## 2026-04-04: Phase 13 & 14 Completion (Simulation Engine)
- **`SimulationManager` Implementation:** Logic-first architecture where C++ handles the "Truth" (state and dice) and LLMs handle "Intent" and "Narrative".
- **Multi-Agent Architecture:** Implemented `SimulationArbiter` (Judge), `SimulationGriot` (Storyteller), and `SimulationActor` (Autonomous agents).
- **RAG & RAG-Logic Loop:** The Arbiter uses vector search to find specific rules before applying outcomes.
- **GOAP & Memory:** Enhanced actors with persistent memory and goal-oriented planning prompts.
- **Tactical Aggression:** Added a UI slider to control AI strategic intensity (Level 1-5).
- **MCP Server:** Added `--mcp` flag to expose rules and sim-state via Model Context Protocol.

## 2026-04-04: Phase 17 Completion (Polish & Hardening)
- **UX/UI Polish:** Added shortcuts (`Ctrl+Shift+C`, `F5`, `Ctrl+Shift+D`), Breeze symbolic icons, and a "Typewriter Effect" for AI text insertion.
- **Incremental RAG:** Optimized indexing using MD5 hash checks, skipping redundant re-embedding of unchanged files.
- **LLM Resilience:** Implemented a 3x auto-retry mechanism for transient network failures in `LLMService`.
- **Simulation Comparison:** Created a semantic JSON diffing UI (`SimulationCompareDialog`) to analyze mechanical shifts between rules versions.
- **Onboarding Demo:** Integrated a "Bridge Ambush" sample encounter and demo character sheets into the default project template.


## 2026-04-04: Phase 15 & 16 Completion (Generator & MCP Bridge)
- **AI Character Generator:** Implemented a multi-step wizard using RAG to generate rule-compliant character sheets with iterative refinement.
- **Bi-directional MCP Expansion:** 
    - Added `write_file` and `append_to_file` tools to allow external AIs to update project markdown.
    - Implemented `update_sim_state` to let external agents manipulate the live simulation.
    - Added strict `isPathSafe` checks to prevent file operations outside the project root.
- **Simulation Refinements:** Added Scenario Loading, Actor Library (Drag-and-Drop), and styled HTML logging.



## 2026-04-03: Phase 10 Refinements & AI Assistant UX
- **Dynamic Model Fetching:** Implemented `/v1/models` (OpenAI/Anthropic) and `/api/tags` (Ollama) integration to auto-populate model dropdowns.
- **Insert into Document:** Added "Insert at Cursor" buttons to AI responses using `QWebChannel` for secure JS-to-C++ communication.
- **Smart Context (RAG):** Implemented automatic context injection for chat. Large files (>32KB) now use vector search to pull the top 10 relevant snippets instead of sending the whole file.
- **Anthropic Hardening:** Fixed 404 errors by implementing system message merging, compact JSON, exact 2026 model IDs (`claude-sonnet-4-6`), and HTTP/2 support.
- **UX Polish:** Added `Enter` to send (with `Ctrl+Enter` for newline) and integrated AI actions into the editor's right-click context menu.
- **Security:** Hardened `.gitignore` to protect `.rpgforge-vectors.db` and credential files.


