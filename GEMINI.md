# RPG Forge — Project Context (GEMINI.md)

## Project Overview
RPG Forge is a KDE-native IDE specifically designed for RPG game designers. It integrates markdown editing, project management, version control, and LLM-powered game analysis into a single application.

- **Primary Technologies:** C++17, Qt6, KDE Frameworks 6 (KF6).
- **Core Components:**
    - **Editor:** `KTextEditor` (embedded Kate component).
    - **Markdown Parsing:** `cmark-gfm` (GitHub Flavored Markdown).
    - **Version Control:** `libgit2`.
    - **UI Framework:** `KXmlGui` for standard KDE actions and menus.
- **Current Status:** Phase 1 (Skeleton App) and Phase 2 (Outline Panel & Breadcrumbs) are complete.

## Building and Running
The project uses CMake with KDE's Extra CMake Modules (ECM).

- **Build Command:**
  ```bash
  ./build.sh
  ```
  *Note: The script handles paths to avoid conflicts with Anaconda environments and generates `compile_commands.json`.*
- **Clean Build:**
  ```bash
  ./build.sh clean
  ```
- **Run Command:**
  ```bash
  ./build/bin/rpgforge
  ```
- **Dependencies:**
    - `Qt6` (Widgets, Core, Gui)
    - `KF6` (CoreAddons, I18n, XmlGui, TextEditor, KIO, WidgetsAddons, Parts)
    - `libgit2`
    - `libcmark-gfm`

## Development Conventions

### Architecture
- **Main Window:** Based on `KXmlGuiWindow`. UI structure is defined in `src/rpgforgeui.rc`.
- **Sidebar System:** A custom `Sidebar` class manages multiple dockable panels (File Explorer, Outline, Git).
- **Markdown Handling:** All markdown parsing is centralized in `MarkdownParser` (wrapping `cmark-gfm`). **Crucial:** `MarkdownParser::init()` must be called once at startup (currently in `main.cpp`).
- **Session Management:** The application automatically saves and restores window geometry, active panels, and the last opened file using `QSettings`.

### Coding Standards
- **Standard Library:** C++17 features are encouraged.
- **Qt/KDE Patterns:**
    - Use `QStringLiteral` for constant strings.
    - Use `i18n()` for user-facing strings.
    - Leverage signals/slots for decoupled communication between components (e.g., `OutlinePanel` informing `BreadcrumbBar` of heading changes).
- **Performance:** UI updates involving document parsing (like the outline) are debounced (300ms for text changes, 100ms for cursor movements).

### Key Files
- `CMakeLists.txt`: Build configuration and dependencies.
- `BUILDPLAN.md`: Detailed roadmap of the project's 14 development phases.
- `src/mainwindow.cpp`: Central orchestration of the IDE components.
- `src/markdownparser.cpp`: interface for markdown AST extraction.
- `src/rpgforgeui.rc`: XML definition for menus and toolbars.

## Future Roadmap (Highlights)
- **Phase 3:** Live split-pane preview using `QWebEngineView`.
- **Phase 4:** Variable/formula system (`{{hp_base}} + 10`).
- **Phase 6-7:** Deep `libgit2` integration and visual diff/merge tools.
- **Phase 9-10:** LLM integration for writing assistance and game rule analysis.
- **Phase 11-12:** Rules simulation engine with LLM-as-GM.
