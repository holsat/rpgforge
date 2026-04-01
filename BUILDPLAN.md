# RPG Forge — Build Plan

## Overview

RPG Forge is a KDE-native IDE for RPG game designers. It combines a powerful markdown editor (built on Kate/KTextEditor), project management, version control, a variable/formula system, LLM-powered analysis, and rules simulation into a single integrated application.

**Tech Stack:**
- **Language:** C++17 with Qt6 / KDE Frameworks 6
- **Editor:** KTextEditor (Kate component)
- **Markdown parsing:** cmark-gfm (C library) + custom extensions for variables
- **Preview:** QWebEngineView rendering HTML (CSS/LaTeX via KaTeX.js)
- **Git:** libgit2
- **LLM:** Async HTTP client hitting OpenAI-compatible APIs (covers cloud + Ollama)
- **Build:** CMake + KDE ECM (Extra CMake Modules)
- **Diff:** libgit2 diff / Myers algorithm

---

## Phase 1 — Skeleton App with Embedded Kate Editor

**Status:** Complete

**Goal:** KDE app that opens/saves markdown files with the Kate editor widget, a file explorer sidebar, and essential editor features.

**Prompt:**
> Create a KDE Frameworks 6 / Qt6 C++ application called "RPG Forge" using CMake and KDE ECM. The main window should embed a KTextEditor::View as the central editor widget. Implement basic file operations (new, open, save, save-as) using KIO. Enable Kate's built-in markdown syntax highlighting, code folding by heading level, and block selection mode. The app should have a standard KDE menu bar and toolbar. Use KXmlGuiWindow as the main window base class. Add toggle for showing/hiding line numbers in the editor. Implement Ctrl+G shortcut to open a "Go to Line" dialog that jumps the cursor to the specified line number. Add a file explorer panel on the left side (dockable QDockWidget) using QTreeView with QFileSystemModel, similar to VS Code's file explorer. The file explorer should support: right-click context menu for renaming files/folders and creating new files/folders, drag-and-drop to move files between directories, file type icons (distinct icons for markdown files, images, PDFs, and other file types), and git status indicators next to each file showing whether it is untracked, modified, staged, or committed (using color-coded dots or letter badges like VS Code's M, U, A indicators). The git status should be obtained by shelling out to `git status --porcelain` or using libgit2 if already linked.

**Key Deliverables:**
- CMake build system with KDE ECM
- Main window with KTextEditor embedded
- File open/save/save-as via KIO
- Markdown syntax highlighting active
- Code folding by heading level
- Block selection mode enabled
- Toggle to show/hide line numbers
- Ctrl+G "Go to Line" dialog
- Left-docked file explorer panel (QTreeView + QFileSystemModel)
- Right-click context menu (rename, new file, new folder, delete)
- Drag-and-drop file moving
- File type icons (markdown, image, PDF, other)
- Git status indicators per file (untracked, modified, staged, committed)

---

## Phase 2 — Document Outline Panel + Breadcrumb Navigation

**Status:** Complete

**Goal:** Left sidebar showing heading structure for navigation, plus a VS Code-style breadcrumb bar above the editor.

**Prompt:**
> Add a dockable sidebar panel (QDockWidget) to RPG Forge that parses the current markdown document and displays an outline tree of all headings (H1-H6). The tree should update as the user types (debounced at 300ms). Clicking a heading in the tree should scroll the editor to that heading. Use cmark-gfm to parse the markdown AST and extract headings with their line numbers. The outline should show the heading hierarchy with proper indentation. Additionally, add a breadcrumb navigation bar positioned directly above the editor content (below any toolbar). The breadcrumb shows the path of the current heading context based on cursor position (e.g., "Document > Chapter 3 > Combat Rules > Melee"). Each segment in the breadcrumb is clickable — clicking a segment opens a dropdown list showing all sibling headings at that level. Selecting a heading from the dropdown scrolls the editor to that heading. The breadcrumb updates as the cursor moves through the document. The rightmost segment shows the immediate heading the cursor is under; parent headings appear to the left separated by ">" chevrons.

**Key Deliverables:**
- Left-docked QDockWidget with QTreeView
- cmark-gfm integration for AST parsing
- Heading extraction (H1-H6) with line numbers
- Debounced update on text change (300ms)
- Click-to-navigate from outline to editor
- Breadcrumb bar above the editor showing heading hierarchy at cursor position
- Clickable breadcrumb segments with dropdown lists of sibling headings
- Breadcrumb updates on cursor movement

---

## Phase 3 — Live Preview Panel

**Status:** Complete

**Goal:** Real-time rendered markdown preview with LaTeX and CSS support.

**Prompt:**
> Add a split-pane live preview to RPG Forge using QWebEngineView. Parse the markdown using cmark-gfm (with tables, strikethrough, and autolink extensions) and render the HTML in the preview panel. Include KaTeX JavaScript for LaTeX math rendering. Support inline HTML/CSS passthrough. The preview should update on every edit with a 200ms debounce. Implement synchronized scrolling between the editor and preview. Add a toolbar toggle to show/hide the preview panel.

**Key Deliverables:**
- QWebEngineView in a right split pane
- cmark-gfm rendering with GFM extensions (tables, strikethrough, autolinks)
- KaTeX.js bundled for LaTeX math rendering
- Inline HTML/CSS passthrough
- 200ms debounced preview updates
- Synchronized scrolling between editor and preview
- Toolbar toggle for preview visibility

---

## Phase 4 — Variable System

**Status:** Complete

**Goal:** Define, reference, and auto-complete custom variables in documents.

**Prompt:**
> Implement a variable system for RPG Forge. Variables are defined in a YAML front-matter block or a dedicated `.rpgvars` file per project. Support simple values (`hp_base: 10`), formulas (`starting_hp: "{{race_hp}} + {{resilience}}"`), and variant contexts (`{{variable}}.v1`, `{{variable}}.v2`). Variables referenced as `{{variable_name}}` in the markdown should be resolved and displayed in the preview with their computed values. Formulas should support basic arithmetic (+, -, *, /, parentheses) and reference other variables. Register a KTextEditor autocomplete provider that suggests defined variable names when the user types `{{`. Show unresolved variables as highlighted errors in both the editor and preview.

**Key Deliverables:**
- YAML front-matter and `.rpgvars` file parsing
- Simple variable values and formula expressions
- Variant contexts (`{{var}}.v1`, `{{var}}.v2`)
- Formula evaluation engine (arithmetic + variable references)
- Variable resolution in preview rendering pipeline
- KTextEditor autocomplete provider for `{{` trigger
- Unresolved variable highlighting (editor + preview)

---

## Phase 5 — Project Management (Scrivener-like)

**Status:** Not Started

**Goal:** Multi-document project structure with file/folder tree and organizational tools.

**Prompt:**
> Add project management to RPG Forge. A project is a directory with an `rpgforge.project` JSON config file. Add a project tree panel (left dock, above or tabbed with the outline) showing the folder structure. Support organizing documents into configurable categories (e.g., "Chapters", "Worldbuilding", "Races", "Mechanics", "Notes"). Support drag-and-drop reordering. Implement a "compile" action that concatenates selected documents in tree order into a single output. Each document should support metadata (title, status, synopsis) stored in YAML front-matter. Add a corkboard view (grid of synopsis cards) as an alternative view of any folder.

**Key Deliverables:**
- `rpgforge.project` JSON config file format
- Project tree panel (dockable, left side)
- Configurable document categories
- Drag-and-drop reordering in project tree
- Document metadata in YAML front-matter (title, status, synopsis)
- Compile action (concatenate documents in tree order)
- Corkboard view (grid of synopsis cards per folder)

---

## Phase 6 — Git Integration

**Status:** Not Started

**Goal:** Built-in version control with GitHub support.

**Prompt:**
> Integrate Git into RPG Forge using libgit2. Add a Git panel showing file status (modified, staged, untracked). Support init, add, commit, diff, log, branch, checkout, push, and pull operations through the UI. Show inline git diff markers in the editor gutter (added/modified/deleted lines). Add GitHub integration: configure remote, push/pull with credentials stored in KWallet. Implement a "snapshot" feature that creates a tagged commit with a user description, for marking rule versions. The project tree should show file git status with color-coded icons.

**Key Deliverables:**
- libgit2 integration
- Git status panel (modified, staged, untracked files)
- UI for init, add, commit, diff, log, branch, checkout, push, pull
- Inline gutter diff markers in editor
- GitHub remote configuration
- KWallet credential storage
- "Snapshot" feature (tagged commits for rule versions)
- Color-coded git status icons in project tree

---

## Phase 7 — Visual Diff / Merge Tool (Kompare-style)

**Status:** Not Started

**Goal:** Side-by-side visual diff with inline merge controls.

**Prompt:**
> Add a visual diff/merge view to RPG Forge, inspired by KDE's Kompare. When the user selects two versions to compare (from git history, branches, or snapshots), open a side-by-side split view in the main editor area. Each side shows the document version with a header bar displaying the version identifier (commit hash, tag name, date). Use a diff algorithm (libgit2's diff or a custom implementation of Myers/patience diff) to compute differences at the line level. Display differences with: green background for insertions, red for deletions, yellow for modifications, with changed words highlighted within modified lines. Between the two panes, draw connector lines linking corresponding change regions. For each diff hunk, show arrow buttons in the gutter between panes: a right-arrow (→) to apply the left version's text to the right, and a left-arrow (←) to apply the right version's text to the left. For new insertion blocks, show a single arrow to copy the block to the other side. Add a toolbar with: previous/next change navigation, "accept all left" / "accept all right" bulk actions, and a toggle between side-by-side and unified diff views. The diff view should work for: (1) comparing any two git commits/tags of a file, (2) comparing current working copy vs any historical version, (3) comparing simulation snapshots (rule versions from Phase 9). Integrate into the git panel — right-click a file → "Compare with..." opens a version picker, then the diff view.

**Key Deliverables:**
- Side-by-side diff view in main editor area
- Version header bars (commit hash, tag, date)
- Line-level and word-level diff highlighting (green/red/yellow)
- Connector lines between corresponding change regions
- Arrow buttons to move changes between sides (per-hunk)
- Previous/next change navigation
- "Accept all left" / "accept all right" bulk actions
- Side-by-side and unified diff view toggle
- Integration with git panel ("Compare with..." context menu)
- Support for commit, tag, and working copy comparisons

---

## Phase 8 — PDF Export

**Status:** Not Started

**Goal:** Export rendered documents to PDF.

**Prompt:**
> Add PDF export to RPG Forge. Use the existing markdown→HTML pipeline and render to PDF via QWebEnginePage::printToPdf(). Support page size (A4, Letter, etc.), margins, headers/footers, and a CSS stylesheet for print styling. Allow the user to configure a project-level print stylesheet. Support exporting a single document or a compiled project. Ensure LaTeX math, inline CSS, and variables are all resolved in the PDF output. Add a print preview dialog.

**Key Deliverables:**
- PDF export via QWebEnginePage::printToPdf()
- Page size options (A4, Letter, etc.)
- Configurable margins, headers, footers
- Project-level print CSS stylesheet
- Single document and compiled project export
- Full variable and LaTeX resolution in output
- Print preview dialog

---

## Phase 9 — LLM Integration (Foundation)

**Status:** Not Started

**Goal:** Connect to LLMs and provide basic writing assistance.

**Prompt:**
> Add LLM integration to RPG Forge. Create a settings page to configure LLM providers: OpenAI-compatible API (URL + API key), Anthropic API, and Ollama (local URL). Implement a chat sidebar panel where the user can converse with an LLM about their project. The LLM should have access to the current document and project context. Add inline actions: select text → right-click → "Ask AI to expand/rewrite/summarize". Implement a simple prompt template system so users can save reusable prompts. All LLM communication should go through a common async HTTP interface that normalizes the different API formats.

**Key Deliverables:**
- LLM provider settings page (OpenAI-compatible, Anthropic, Ollama)
- API key and endpoint configuration
- Common async HTTP interface normalizing API formats
- Chat sidebar panel with project context
- Inline text actions (expand, rewrite, summarize via right-click)
- Reusable prompt template system
- Streaming response support

---

## Phase 10 — LLM Game Analyzer (Inline Diagnostics)

**Status:** Not Started

**Goal:** Continuous LLM-powered analysis that surfaces rule conflicts, ambiguities, and issues as inline editor diagnostics.

**Prompt:**
> Add an LLM-powered game analyzer to RPG Forge. The analyzer reads all project documents (rules, mechanics, worldbuilding) and checks for: conflicting definitions, ambiguities, redundancies, omissions, playability concerns, and completeness gaps. Results are displayed as inline diagnostics in the KTextEditor using KTextEditor::MovingInterface to place diagnostic ranges — errors (red underline) for conflicts, warnings (yellow) for ambiguities/playability, and info (blue) for suggestions. On mouse hover, show a tooltip with the issue description and cross-references to conflicting locations (clickable to navigate). Implement three run modes toggled from the toolbar: (1) continuous — re-analyzes on every save with a 5-second debounce, (2) on-demand — manual trigger via button/shortcut, (3) paused — disabled entirely. The analyzer sends project context + a system prompt to the configured LLM and parses structured JSON responses containing issue type, severity, affected text ranges, description, and cross-references. Add a "Problems" panel (docked at the bottom, like VS Code) listing all current diagnostics, filterable by severity. The system prompt for analysis is user-customizable in project settings (provide a sensible default). The user selects which LLM provider/model to use for analysis independently from other LLM features (so they can use a cheap local model for continuous analysis and a cloud model for simulation).

**Key Deliverables:**
- Project-wide document analysis via LLM
- Inline diagnostics using KTextEditor::MovingInterface
- Three severity levels: error (red), warning (yellow), info (blue)
- Hover tooltips with issue description and cross-references
- Clickable cross-references to navigate to conflicting locations
- Three run modes: continuous (debounced on save), on-demand, paused
- "Problems" panel (bottom dock) with severity filtering
- Independent LLM model selection for analyzer
- User-customizable system prompt for analysis
- Structured JSON response parsing

---

## Phase 11 — Rules Simulation Engine (Core)

**Status:** Not Started

**Goal:** Define and run basic game simulations using LLM-as-GM.

**Prompt:**
> Build the rules simulation engine for RPG Forge. The user creates a "Simulation" which includes: (1) a reference to the current ruleset documents, (2) a scenario description in markdown, (3) player profiles with character sheets defined in a structured YAML/JSON format. When a simulation runs, the system snapshots the current project state via a git tag. The LLM acts as GM: it receives the rules, scenario, and character sheets as context, then narrates the scenario step-by-step, making dice rolls according to the rules, and resolving actions. Output is a structured log (markdown) of the simulation. Start with a single-prompt mode where the entire simulation runs to completion with no user input. Store simulation inputs, outputs, and the git tag reference together for later comparison. Add a simulation manager panel to list, run, and compare past simulations.

**Key Deliverables:**
- Simulation definition format (ruleset refs, scenario, player profiles)
- Automatic git tag snapshot on simulation run
- LLM-as-GM execution (rules + scenario + characters as context)
- Dice roll resolution per rules
- Structured markdown output log
- Simulation manager panel (list, run, compare)
- Simulation input/output storage linked to git tags
- Cross-version comparison of simulation results

---

## Phase 12 — Multi-Agent Simulation

**Status:** Not Started

**Goal:** Player sub-agents that make autonomous decisions during simulation.

**Prompt:**
> Extend the simulation engine to support multi-agent execution. Each player character becomes a sub-agent with its own LLM context (character sheet, personality profile, goals). The GM agent narrates the situation and requests actions from player agents. Player agents respond in-character based on their profiles. The GM resolves outcomes per the rules. Implement this as a turn-based loop: GM narrates → each player agent responds → GM resolves → repeat until scenario completes or turn limit reached. The user can configure: which LLM to use for GM vs players, max turns, and whether to pause for user input between turns or run fully automated. Add a simulation replay viewer that shows the turn-by-turn narrative in a readable format.

**Key Deliverables:**
- Player character sub-agents with individual LLM contexts
- Character personality profiles and goals
- Turn-based execution loop (GM narrates → players respond → GM resolves)
- Configurable LLM per role (GM model vs player model)
- Max turn limit configuration
- Pause-for-input vs fully automated modes
- Simulation replay viewer (turn-by-turn narrative)

---

## Phase 13 — Character Generator

**Status:** Not Started

**Goal:** Tools for generating characters per the project's rules.

**Prompt:**
> Build a character generator for RPG Forge. The generator reads the project's variable definitions and rule documents to understand character creation rules. Provide a form-based UI where the user selects race, class, and other choices defined in the rules. Use the variable/formula system to compute derived stats. Support random generation (roll stats, random selections) and manual point-buy. Generated characters are saved as structured YAML files in the project. Characters can be used directly in simulations. Add an "NPC generator" mode that uses the LLM to generate a complete character with backstory based on parameters.

**Key Deliverables:**
- Form-based character creation UI
- Integration with variable/formula system for derived stats
- Random generation (dice rolls, random selections)
- Manual point-buy mode
- Characters saved as structured YAML
- Direct integration with simulation engine
- LLM-powered NPC generator (character + backstory)

---

## Phase 14 — Polish and Advanced Features

**Status:** Not Started

**Goal:** Refinements, markdown flavor switching, and extended styling.

**Prompt:**
> Add these polish features to RPG Forge: (1) Markdown flavor selector (CommonMark, GFM, CommonMark + extensions) that changes the parser configuration. (2) Markdown attributes support ({.class #id key=value} syntax) using cmark extensions. (3) A style theme system for the preview (fantasy, sci-fi, modern, custom CSS). (4) Cross-reference support: link between documents with autocomplete. (5) Word count / progress tracking per document and project. (6) Session targets ("write 500 words today"). (7) Full-text search across the project. (8) KDE system tray integration and global shortcuts.

**Key Deliverables:**
- Markdown flavor selector (CommonMark, GFM, extended)
- Markdown attributes ({.class #id key=value})
- Preview theme system (fantasy, sci-fi, modern, custom CSS)
- Cross-document reference links with autocomplete
- Word count and progress tracking
- Session writing targets
- Full-text project search
- KDE system tray integration
- Global keyboard shortcuts

---

## Dependency Graph

```
Phase 1  (Skeleton App)
  ├──→ Phase 2  (Outline Panel)
  │      └──→ Phase 3  (Live Preview)
  │             ├──→ Phase 4  (Variables)
  │             └──→ Phase 8  (PDF Export)
  ├──→ Phase 5  (Project Management)
  │      └──→ Phase 6  (Git Integration)
  │             └──→ Phase 7  (Visual Diff/Merge)
  └──→ Phase 9  (LLM Foundation)
         ├──→ Phase 10 (Game Analyzer)
         └──→ Phase 11 (Simulation Core)
                └──→ Phase 12 (Multi-Agent Simulation)
                       └──→ Phase 13 (Character Generator)

Phase 14 (Polish) — ongoing, can start after Phase 4 + Phase 6
```

## Build Order (Sequential)

| Order | Phase | Name                     | Dependencies       |
|-------|-------|--------------------------|-------------------|
| 1     | 1     | Skeleton App             | None              |
| 2     | 2     | Document Outline Panel   | Phase 1           |
| 3     | 3     | Live Preview Panel       | Phase 2           |
| 4     | 4     | Variable System          | Phase 3           |
| 5     | 5     | Project Management       | Phase 1           |
| 6     | 6     | Git Integration          | Phase 5           |
| 7     | 7     | Visual Diff/Merge        | Phase 6           |
| 8     | 8     | PDF Export               | Phase 3           |
| 9     | 9     | LLM Foundation           | Phase 1           |
| 10    | 10    | LLM Game Analyzer        | Phase 9           |
| 11    | 11    | Simulation Core          | Phase 9, Phase 6  |
| 12    | 12    | Multi-Agent Simulation   | Phase 11          |
| 13    | 13    | Character Generator      | Phase 12, Phase 4 |
| 14    | 14    | Polish & Advanced        | Phase 4, Phase 6  |
