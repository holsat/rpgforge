# RPG Forge — Architecture & UX Analysis Tracking

This document tracks identified architectural debt, anti-patterns, and UX improvements that were discovered during the Phase 2 review but are not yet addressed.

## 🏛️ Architectural Improvements

### 1. Refactor "God Object" (`MainWindow`)
- **Issue:** `MainWindow` is overly centralized, managing service initialization, component orchestration, and even low-level UI logic (like typewriter effects).
- **Goal:** Move orchestration logic to a dedicated `ApplicationController` or `WorkspaceManager`.
- **Priority:** High (for maintainability).

### 2. Dependency Injection / Service Locator
- **Issue:** Ubiquitous use of Singletons makes unit testing and service swapping (e.g., for mocks) difficult.
- **Goal:** Transition from `Service::instance()` to a dependency-injection or service-locator pattern.
- **Priority:** Medium.

### 3. Decouple Policy from `GitService`
- **Issue:** `GitService` contains hardcoded business rules (e.g., filtering for `/manuscript/`).
- **Goal:** Move policy logic to `ProjectManager` or a `GitPolicy` class; keep `GitService` focused on Git primitives.
- **Priority:** Medium.

### 4. Externalize AI Prompts
- **Issue:** System prompts for the Game Analyzer and other AI features are hardcoded in source.
- **Goal:** Move default prompts to a resource file (e.g., `prompts.json`) or make them user-configurable.
- **Priority:** Low.

### 5. Standardize JSON Keys
- **Issue:** Raw string literals are used for JSON keys throughout `ProjectManager`.
- **Goal:** Define a `ProjectKeys` namespace with `static constexpr char*` or `QStringLiteral` constants.
- **Priority:** Low.

### 6. Robust YAML Parsing
- **Issue:** `VariableManager::parseFrontMatter` uses a brittle regex that fails on nested objects or lists.
- **Goal:** Integrate `yaml-cpp` for standard-compliant YAML parsing.
- **Priority:** Medium.

### 7. Project Schema Migration
- **Issue:** No framework for migrating `.project` files when the schema version changes.
- **Goal:** Implement a migration system to upgrade older project files.
- **Priority:** Medium.

### 8. LLM Error Propagation
- **Issue:** Non-streaming LLM failures are logged but not reported to the UI.
- **Goal:** Update `nonStreamCallback` to return an error object or `std::expected`.
- **Priority:** Medium.

### 9. RAII for libgit2
- **Issue:** Manual `goto cleanup` patterns in `GitService` are error-prone.
- **Goal:** Use C++ wrappers (e.g., `std::unique_ptr` with custom deleters) for libgit2 resources.
- **Priority:** Medium.

## 🎨 UX & Accessibility Improvements

### 1. Resolve Shortcut Conflicts
- **Issue:** Focus Mode uses `Ctrl+Shift+F`, which conflicts with the standard "Find in Files" shortcut in IDEs.
- **Goal:** Audit and remap shortcuts to follow platform conventions.
- **Priority:** High.

### 2. Accessibility Audit (Buttons)
- **Issue:** Icon-only buttons lack `setAccessibleName` coverage.
- **Goal:** Ensure all interactive elements have proper labels for screen readers.
- **Priority:** High.

### 3. "Dry Run" for Sync Project
- **Issue:** The "Sync Project" feature can be destructive/disruptive without warning.
- **Goal:** Implement a preview or "Dry Run" mode to show proposed file changes before execution.
- **Priority:** Medium.
