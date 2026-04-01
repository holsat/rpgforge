# Debugging Notes: Variable Autocomplete Crash

## Current Status
- **Branch:** `fix/variable-system-crashes`
- **State:** Stable core. Typing `{{...}}` works and renders correctly in the preview.
- **Completion Model:** Implemented with 3-column support (Kate standard).
- **Manual Trigger:** Manual invocation via `Ctrl+Space` is the current test target.

## What We Fixed (Stabilization Phase)
1. **Infinite Recursion:** Disabled wavy-line error highlights (`MovingRange`) which were triggering recursive layout updates in `libKF6TextEditor`.
2. **Signal Feedback Loops:** Disconnected `VariableCompletionModel` from background signals. It is now a "Pull-Only" model.
3. **Safe Registration:** Registration of the completion model is now deferred by 500ms in `MainWindow` to ensure the editor view is idle.
4. **Resolution Safety:** Removed `QJSEngine` from the resolution path and added 1MB string length limits to prevent stack exhaustion.
5. **Cache Safety:** Application now clears and disables `QWebEngine` cache at startup.

## New Findings (Autocomplete Phase)
- **Invalid Range:** If `completionRange()` returns an invalid range, the editor aborts completion before calling `rowCount` or `data`.
- **Call Flood:** When `rowCount > 0`, the editor's UI widget may query the model thousands of times per second for rendering/layout. Excessive logging in `data()` or `rowCount()` can saturate I/O and worsen hangs (observed 20,000+ lines in `dbg.txt`).
- **Column Mapping:** Kate specifically expects the name of the completion item in **Column 1**. Providing it in Column 0 or having mismatched column counts can cause internal segfaults in the completion widget.

## Current State of implementation
- `VariableCompletionModel` implements `rowCount()`, `columnCount() (3)`, `data() (Col 1)`, and `completionRange()`.
- `shouldStartCompletion` is currently hardcoded to `false` to isolate manual invocation via `Ctrl+Space`.
- `executeCompletionItem` handles insertion and safely adds `}}` if missing.

## Next Steps
1. Verify if the full model (multiple rows, Column 1 mapping) is stable under `Ctrl+Space`.
2. If stable, re-enable `shouldStartCompletion` with a safe, non-recursive check.
3. Once confirmed working, re-integrate the stylesheet toggle feature using similar safe patterns.
