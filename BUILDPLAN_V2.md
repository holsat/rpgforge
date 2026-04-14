# RPG Forge 2.0 — Build Plan (The Block Engine)

## Project Vision
To evolve RPG Forge from a technical IDE into a modern, block-based "Game Design Laboratory." The tool will prioritize cross-platform accessibility (Windows, macOS, Linux, Web), flexible data structures (non-rigid), and advanced cognitive aids for designers (memory offloading, consistency enforcement).

---

## Architectural Foundation
*   **Backend:** Rust (using Tauri for desktop, pure Rust for web/SaaS).
*   **Frontend:** React + TypeScript (Vite-powered for instant preview).
*   **Data Model:** Local-first SQLite (Entity-Attribute-Value) + Markdown.
*   **Sync:** CRDT (Conflict-Free Replicated Data Types) for real-time state; Git for project snapshots.

---

## Phase 1: The Headless Engine Refactor (The Portable Brain)
### Objectives
Extract existing RPG logic from the KDE/Qt UI into a portable C++ library or Rust-based core.
### Implementation
1.  **Isolate Markdown Parser:** Decouple `cmark-gfm` logic from `MarkdownParser.cpp`.
2.  **Portable Dice Engine:** Ensure the `DiceEngine` has no Qt dependencies.
3.  **Variable Engine V2:** Transition from simple string replacement to a structural "Observer" pattern.
4.  **Verification:** Build a CLI-based test runner to prove the "Brain" works without a GUI.

---

## Phase 2: The "Librarian" Service (Flexible Data)
### Objectives
Build an autonomous background indexer that builds a relational rules-graph without a rigid schema.
### Implementation
1.  **SQLite EAV Schema:** Implement a table structure: `Entities (id, name, source_file)`, `Attributes (id, entity_id, key, value, type)`.
2.  **Heuristic Indexer:** A background worker that scans Markdown for `Key: Value` patterns and populates the database.
3.  **Dependency Graph:** Map which documents reference which rules (essential for the "Guardian").
4.  **Verification:** Query the database to find "All entities with AC > 15" and verify it finds them in raw text files.

---

## Phase 3: The Universal Chassis (Tauri Strategy)
### Objectives
Create the cross-platform application shell and high-performance "Bridge."
### Implementation
1.  **Tauri Setup:** Initialize the Rust project and window management.
2.  **The Rust Bridge:** Implement the IPC (Inter-Process Communication) between the UI and the Phase 1/2 Engine.
3.  **Hot-Reloading Preview:** Implement a "Surgical Render" system where only modified blocks are re-rendered, ensuring <16ms latency.
4.  **Verification:** Launch a single codebase on Linux and Windows simultaneously.

---

## Phase 4: The Executive UI (Command Palette & Blocks)
### Objectives
Implement the "Next Gen" user experience from the approved mockups.
### Implementation
1.  **Omni-Search (Command Palette):** `Cmd+K` interface with live previews of rules/entities.
2.  **The Block Canvas:** A ProseMirror or TipTap based editor where "Rules" are interactive UI components.
3.  **Rules Palette:** A sidebar for dragging/dropping master entities into the document.
4.  **Visual Distinction:** Implement the "Violet Glow" for new data extraction.

---

## Phase 5: The "Consistency Guardian" (Cognitive Aid)
### Objectives
Implement the "Amber Pulse" safety net for designers with memory/executive functioning needs.
### Implementation
1.  **Real-time Conflict Detection:** Compare active typing against the SQLite Indexer.
2.  **Amber UI Feedback:** Soft-pulse animations for contradictory data.
3.  **The Resolver Panel:** A side-by-side comparison UI to "Update Global" or "Revert to Master."
4.  **Verification:** Intentionality test—type a conflicting rule and verify the "Guardian" catches it within 500ms.

---

## Testing & Validation Strategy
*   **Logic:** Unit tests in Rust/C++ for every math/parsing function.
*   **UI:** Snapshot testing for the Block components.
*   **Performance:** Benchmarking the "Type-to-Preview" latency to ensure it stays under 30ms.
*   **Accessibility:** Manual audit to ensure visual cues (Glows/Pulses) are distinct and helpful for TBI/Executive functioning support.
