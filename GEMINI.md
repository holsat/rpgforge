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


---

# Global Instructions (Merged)

## Available Subagents

The following personal subagents are available in `.gemini/agents/` and MUST be used at the appropriate stages of any development workflow. You can invoke them explicitly (e.g., `@architect review this file`) or delegate tasks to them automatically.

| Subagent | Purpose |
|---|---|
| `architect` | Reviews code, application and service architecture, code patterns, data validation, error handling, security, and C++17 best practices |
| `developer` | Implements features, fixes bugs, and writes production-quality C++/Qt code |
| `debugger` | Diagnoses crashes, memory leaks, and complex bugs using GDB and sanitizers |
| `requirements-analyst` | Translates ideas into user stories and acceptance criteria |
| `ux-architect` | Reviews UI/UX implications, workflows, accessibility, design consistency, and design metrics |
| `pr-reviewer` | Lightweight review of a specific diff/changeset for regressions, completeness, and edge cases |
| `security-analyst` | Performs security audits for memory safety, injection, and credential handling |
| `documentation-writer` | Writes API docs, READMEs, and technical documentation |
| `build-packager` | Configures CMake and creates Linux packages (Flatpak, AppImage, etc.) |
| `devops-engineer` | Designs and maintains CI/CD pipelines (GitHub Actions, GitLab CI) |
| `release-manager` | Manages versioning, changelogs, and the release process |
| `test-strategist` | Analyzes code to recommend what to test, identifies coverage gaps, and flags testability blockers |
| `test-writer` | Writes QTest-based unit, widget, and integration tests |
| `test-runner` | Builds and runs the test suite, diagnoses failures, fixes broken tests, reports code bugs |
| `gui-tester` | End-to-end GUI testing via AT-SPI accessibility tree and input simulation |
| `performance-analyst` | Finds main-thread blocking, memory issues, and Qt-specific performance pitfalls |

## Mandatory Review Gates

When working on any development task, the following review gates are **required**. The review tier depends on the scope of the change:

### Tiers

| Tier | When | Agents |
|---|---|---|
| **Full** | New features, major refactors, new services/components | All gates below |
| **Standard** | Bug fixes with significant changes, moderate refactors | Post-implementation review + testing (skip planning review unless architecture is affected) |
| **Skip** | Typo fixes, single-line config changes, comment-only edits, or when the user explicitly says to skip | No review required |

### During Planning (Full tier only)
- Before finalizing any implementation plan, invoke the `ux-architect` and `architect` subagents **in parallel** to review the proposed plan.
- Incorporate their feedback into the plan before proceeding with implementation.

### Before Implementation
- Before implementation of any new feature or major effort, ensure that all outstanding changes are committed to git and pushed to origin, then open a new local branch for the new work.

### After Implementation (Full and Standard tiers)

**Phase 1 — Review (run in parallel):**
- `architect` — review the changes for architecture and code quality issues. Apply all recommended changes as long as they will not break asked for or existing functionality. When unsure, ask. Continue to iterate on the code until no major issues are remaining before completion.
- `ux-architect` — only if the implementation involved UI changes.
- `performance-analyst` — only if the changes touch hot paths, models, network calls, or file I/O.

**Phase 2 — Handle findings:**
- **Critical issues**: Auto-fix immediately without asking. These are bugs, crashes, security holes, or data loss risks.
- **Warnings**: Report to the user with recommended fixes. Apply fixes if the user approves or if the fix is low-risk and obvious.
- **Suggestions**: Include in the review summary for the user's awareness. Do not act on them unless asked.

**Phase 3 — Testing (run sequentially):**
1. `test-writer` — write tests for the new or changed code.
2. `test-runner` — build and run the full test suite, fix any test failures.
3. `gui-tester` — only if the feature has UI and the change is Full tier.

### Before Committing / PR
- Invoke `pr-reviewer` to review the full diff against the base branch before creating a commit or PR.
- Commit all changes to the local working Git branch and push that branch upstream to GitHub.

### Exceptions
- If the user explicitly says to skip review, respect that.

## Gemini Added Memories
- If the user reports a machine crash and asks to continue, I must immediately review the most recent session logs (using `gemini --list-sessions` and inspecting the `~/.gemini/tmp/<project_hash>/chats/` directory) to identify the last user prompt. I will then audit the codebase and system state to verify if those instructions were fully executed. If incomplete or inconsistent, I will prioritize completing those actions and restoring the project to the intended state before proceeding.
- Never ever force delete a Git branch (git branch -D). Always ensure branches are pushed and merged upstream, and ask for explicit confirmation before deleting even merged branches.
