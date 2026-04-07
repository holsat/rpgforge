# RPG Forge: UI/UX Remediation Plan

**Objective:** Reduce cognitive load, improve focus, and ensure theme-consistent accessibility across the IDE.

## 1. Visual & Layout Optimization
**Problem:** The initial three-pane layout (Sidebar-Editor-Preview) creates a cramped workspace by default.
**Recommendation:** 
- Start with the Preview panel (`PreviewPanel`) **disabled by default**. This ensures the writer's primary workspace (the editor) is prioritized from the first launch.
- **Metric:** Maximizes the initial "Ready-to-Write" real estate and reduces visual distraction.

## 2. Contextual AI Integration
**Problem:** AI actions are currently in the top `MenuBar` and Sidebar, requiring significant mouse movement.
**Recommendation:**
- Inject AI actions (Expand, Rewrite, Summarize) into the `KTextEditor` context menu.
- **Heuristic:** Proximity/Fitts's Law. High-frequency actions should be adjacent to the target (the text).

## 3. Sidebar Restructuring
**Problem:** 5+ top-level icons create scannability friction.
**Recommendation:**
- Refine icon set for distinctiveness (monochromatic, high-contrast).
- Implement a "Focus Mode" shortcut that collapses the sidebar entirely.
- **Metric:** Reduces "Mystery Meat" navigation errors.

## 4. Accessibility & Theme Integrity
**Problem:** Hardcoded `darker(110)` for the `BreadcrumbBar` may fail contrast checks in dark themes.
**Recommendation:**
- Use `KColorScheme` from the `KDE Frameworks` to derive backgrounds from the system palette.
- **a11y:** Ensures WCAG 2.1 contrast compliance regardless of the active KDE theme (Breeze/Nord/Breeze Dark).

---

## Proposed Implementation Steps

### Phase 1: Contextual AI (Quick Win)
- Modify `MainWindow::showEditorContextMenu` to insert a separator and the `aiExpand`, `aiRewrite`, and `aiSummarize` actions.

### Phase 2: Theme-Aware Breadcrumbs
- Update `breadcrumbbar.cpp` to use `KColorScheme(QPalette::Active, KColorScheme::View)` instead of manual color manipulation.

### Phase 3: Layout Refinement
- Update `MainWindow::setupSidebar` to call `m_previewPanel->hide()` and initialize `m_mainSplitter` sizes with the preview at 0.
- Ensure the `togglePreview` action correctly shows/hides the panel and restores a useful splitter size.

---

## Verification
- **Visual Audit:** Verify contrast ratio using `KDE's Kontrast` tool.
- **Workflow Test:** Perform an "AI Rewrite" without leaving the text area.
- **Theme Test:** Switch between "Breeze" and "Breeze Dark" to verify breadcrumb legibility.
