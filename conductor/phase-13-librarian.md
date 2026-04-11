# Phase 13: The Librarian Service & Hybrid Data Model

## Vision
A background service ("The Librarian") that continuously scans the project's Markdown files to build a local-first SQLite (Entity-Attribute-Value) database. This database acts as the project's "Source of Truth," feeding deterministic facts into the existing RAG system and power-loading the IDE's variable, linting, and auto-refactoring systems.

## Key Features

### 1. The Flexible Librarian Database (SQLite)
*   **Non-Rigid Schema:** Uses an EAV (Entity-Attribute-Value) structure to support any game system without predefined paradigms (e.g., Stats, Classes, Spells, or custom tags like "Whisper").
*   **Deterministic RAG Support:** Upon LLM query, structured data from SQLite is retrieved and injected into the prompt context alongside vector-based RAG results to ensure factual accuracy.
*   **Dependency Graph:** Maps references between documents and rules (e.g., "File A uses SocialClass.Slave which is defined in File B").

### 2. The Dual-Path Extraction Service
*   **Heuristic Path (Real-time):** Fast C++ parsing (Regex/AST) of Markdown tables, lists, and YAML. Runs in a background thread, debounced during active typing.
*   **Semantic Path (Asynchronous LLM):** A separate background thread that performs deeper semantic extraction.
    *   Operates on a "Work-then-Sleep" cycle to preserve CPU/API resources.
    *   Manual Override: Invokable via a "Re-index" button in the Variables Pane or a global keyboard shortcut.
*   **Concurrency Safety:** 
    *   Service-level `lock()` mechanisms to prevent race conditions during simultaneous reads/writes.
    *   **Auto-Pause:** Indexing and parsing services are automatically suspended during heavy operations (e.g., Scrivener/Project imports).

### 3. Smart Editor Integration
*   **Consistency Linter (Red Squiggle):** Highlights text that contradicts the Librarian's database.
    *   *Tooltip:* Shows the "Master" value and provides a link to the source file.
    *   *Quick Fix:* "Update text to match Library" or "Update Library to match this text."
*   **Auto-Binding (Green Underline):** When a user writes a definitive data block (e.g., a table), the Librarian extracts it and replaces the text with smart variables (e.g., `{{socialclass.slave.resilience}}`).
    *   This ensures that updating the data in the "Master" location propagates to all mentions project-wide.
*   **Variable Pane V2:** Displays a tree view of all extracted "Objects" and "Attributes." Supports drag-and-drop into the editor and auto-complete integration.

## Implementation Steps

### Step 1: Database Foundation
*   Implement `LibrarianDatabase` using `QSqlDatabase` with a flexible EAV schema.
*   Implement a `DependencyGraph` table to track cross-file rule references.

### Step 2: The Librarian Service Thread
*   Create a standalone `LibrarianService` class inheriting from `QObject` and moved to a `QThread`.
*   Implement the `pause()` and `resume()` slots to be triggered by the `ProjectManager` during imports.

### Step 3: Extraction Logic
*   **Table/List Parser:** Use `cmark-gfm` AST to identify tables and bulleted lists. Convert rows/items into JSON-like key-value objects for SQLite storage.
*   **LLM Semantic Worker:** Integrate with `LLMService` for async deep-scanning.

### Step 4: UI/UX Hooks
*   **KTextEditor Decoration:** Implement custom `KTextEditor::Message` and underline styles (Red for inconsistency, Green for auto-bound variables).
*   **Variable Pane Updates:** Add the "Re-index" button and expand the tree model to include SQLite-derived data.
