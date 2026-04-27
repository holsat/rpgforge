# Mythos Architecture Pivot: The Hybrid Core

## Vision
Transition "RPG Forge" to **Mythos**, a premium "Worldbuilder's IDE" targeting the Apple ecosystem (macOS, iPadOS). The aesthetic is high-end, dark charcoal with gold/brass accents, and elegant typography.

## Architecture: Tauri v2 + C++ Core
To achieve native Apple performance and iPadOS support without discarding our robust C++ backend, we are adopting a **Sidecar/Hybrid Architecture**.

1.  **The Core (`librpgforge` - C++):**
    *   Retain and isolate business logic: `GitService`, `LibrarianService`, `ProjectManager`, `LLMService`, `AnalyzerService`.
    *   **Goal:** Strip out all `QWidget` and GUI dependencies from these services. They must run headlessly.
    *   **Bridge:** Expose these services via a local IPC mechanism, MCP (Model Context Protocol), or FFI to be consumed by the frontend.

2.  **The Shell (Tauri v2 - Rust/Web):**
    *   Replaces the Qt GUI layer. Tauri v2 provides a lightweight webview that compiles to native macOS (AppKit/WebKit) and iPadOS (Swift/WKWebView).
    *   Handles window management, OS integration, and bridging to the C++ core.

3.  **The UI (React + TypeScript + Tailwind CSS):**
    *   **Editor:** **TipTap** (ProseMirror-based) for the rich text "Manuscript" view. Custom extensions will be built for `@Character` autocomplete, inline LLM highlights, and section folding.
    *   **Aesthetic:** Implementation of the **"Mythos" Design System** (documented in `conductor/mythos-design-system.md`).
    *   **Views:** 
        *   **Draft (Editor):** The main "Manuscript" view for pure writing.
        *   **The Chronicle (Sidebar):** Slim vertical navigation for Library, Story Map, and Codex.
        *   **The Codex (Entities):** Visual dossiers and relationship maps for characters, locations, and items.
        *   **Lumina (Focus Mode):** Frosted glass, ethereal distraction-free writing environment.

## Execution Phases

### Phase 1: The Core Decoupling
*   Audit `src/` to identify tight coupling between UI (`QWidgets`, `QMainWindow`) and Data Services.
*   Refactor `ProjectManager`, `LibrarianService`, and `LLMService` to emit agnostic signals/events rather than manipulating UI elements directly.
*   Establish the communication bridge (e.g., setting up the C++ core as a local background process or compiling it as a dynamic library loaded by Rust).

### Phase 2: Tauri Foundation & The TipTap Editor
*   Initialize the Tauri v2 workspace within the repository.
*   Setup React + Tailwind CSS with the **Mythos Design System** (Charcoal `#1E1E24`, Brass `#B5A642`, Gold `#D4AF37`).
*   Implement the base TipTap editor and ensure it successfully loads, edits, and saves Markdown files to the local disk.

### Phase 3: Rebuilding the Workshop (Sidebars)
*   Build the frontend React components for the 5 Sidebar Utility Panels in **The Chronicle**: 
    *   **Library:** Multi-column file explorer.
    *   **Story Map:** Vertical timeline of "Explorations" and "Milestones".
    *   **The Codex:** Character and variable dossier cards with artistic frames.
    *   **Muse:** Context-aware AI chat and tool integration.
    *   **Ledger:** Settings and project status.
*   Connect these frontend panels to the headless C++ services via the Tauri bridge.

### Phase 4: The Advanced Views (Map & Timeline)
*   Implement the Interactive Map Canvas (allowing image uploads and pin-dropping).
*   Link Map Pins to the `LibrarianService` data via the bridge.
*   Implement the horizontal chronological Timeline view.

### Phase 5: Polish & Deployment
*   Implement **"Lumina" Focus Mode** (glassmorphism theme switching).
*   Test cross-compilation for macOS (DMG/App) and iPadOS.
*   Finalize the "One-Click Compile" export pipeline.

## Reference Mockups
- **The Manuscript (Core):** `nanobanana-output/a_highfidelity_ui_mockup_for_a_p.png`
- **Lumina Focus Mode:** `nanobanana-output/a_highfidelity_ui_mockup_for_the.png`
- **The Codex (Dossier):** `nanobanana-output/a_highfidelity_ui_mockup_for_the_1.png`
- **Story Map (History):** `nanobanana-output/a_highfidelity_ui_mockup_of_a_mo.png`
- **Recall Browser (Time Machine):** `nanobanana-output/a_ui_mockup_of_a_version_recall_.png`
