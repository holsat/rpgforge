# Plan: "Explorations" — Visual Versioning for Creators

## Objective
Transform the technical Git integration into a user-friendly "Explorations" system. This provides writers and game designers with a visual timeline of their project's evolution, the ability to selectively carry changes between paths, and an intuitive "Time Machine" for document restoration.

## Key Visual Requirements (Fidelity Mandate)
The implementation MUST maintain high fidelity with the approved mockups:
- **Story Map Graph**: Vertical timeline with curved, color-coded branch paths. Milestone nodes must vary in size based on word count deltas.
- **Landmark Tags**: Clear ribbon/label icons for tagged milestones (e.g., "Draft 1").
- **Recall Browser**: Multi-column list (Date, Path, Tag, Word Count) with an integrated preview pane.
- **UI Integration**: The **Explorations Pane** will live within the existing sidebar system (replacing the current Git panel). The existing primary sidebar icons and structure will remain unchanged.

## Proposed Solution

### 1. Writer-Centric Terminology
- **Branch** $\rightarrow$ **Exploration** (A creative path).
- **Commit** $\rightarrow$ **Milestone** (A saved point in history).
- **Merge** $\rightarrow$ **Integrate** (Combining paths).
- **Tag** $\rightarrow$ **Landmark** (A significant version).

### 2. The Explorations Sidebar (The "Story Map")
- **Custom View**: `ExplorationGraphView` (custom `QWidget`).
- **Graphing**: Uses `libgit2`'s `revwalk` to calculate branch swimlanes and parent-child connections.
- **Interactivity**: Right-click nodes to "Integrate into Main Path" or "Switch to this Exploration."

### 3. Project Manager: "Recall from Any Path"
- **Trigger**: Right-click file in Project Manager $\rightarrow$ "Recall Version..."
- **Logic**: Uses `GitService::extractVersion`.
- **Scope**: Searches all branches/tags for that specific file path.
- **Browser**: A dialog allowing the user to browse, preview, and restore a historical version into the current workspace.

### 4. Selective Carry-over & Conflict Resolution
- **Switching**: When switching Explorations with unsaved changes, show a checklist of modified files to "carry over" to the new path.
- **Conflict Arbiter**: Leverages the existing Kompare-style `VisualDiffView`.
- **Resolution**: Automatically triggers when an "Integration" results in conflicts, allowing paragraph-by-paragraph choices.

## Implementation Phases

### Phase 1: Architectural Foundation
- Rename `GitPanel` to `ExplorationsPanel`.
- Extend `GitService` to provide repo-wide history with parent OIDs and word count deltas.
- Implement `GitService::getConflictingFiles()`.

### Phase 2: High-Fidelity UI Implementation
- Build the `ExplorationGraphView` using standard Qt graphics.
- Implement the `VersionRecallBrowser` dialog with integrated text preview.
- Update `src/rpgforgeui.rc` and context menus.

### Phase 3: Workflow Integration
- Implement the "Selective Carry-over" logic using Git stashing.
- Fix the `switchExploration` race condition (awaiting background commits).
- Hook the `VisualDiffView` into the merge/integration failure path.

## Reference Mockups
- **Story Map**: `nanobanana-output/a_highfidelity_ui_mockup_of_a_mo.png`
- **Recall Browser**: `nanobanana-output/a_ui_mockup_of_a_version_recall_.png`
- **Conflict Arbiter**: `nanobanana-output/a_highfidelity_ui_mockup_of_a_co.png`
