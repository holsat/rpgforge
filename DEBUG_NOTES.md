# Debugging Notes: Variable Autocomplete Crash

## Current Status (2026-04-01, Gemini deep-dive session)
- **Branch:** `fix/variable-system-crashes`
- **State:** Stable core. No crashes.
- **Implementation:** Attempted both "Perfect Flat" and "Strict Hierarchical" models based on `KTextEditor` and `Kate` source code research.
- **Manual Trigger:** `Ctrl+Space` triggers `completionInvoked` and logs data access, but the UI popup remains hidden.

## Key Findings from Kate/KTextEditor Source Research
1. **Hierarchy is Mandatory:** Kate's `KateCompletionModel` (the internal proxy) expects a tree structure: `Root -> Group Header -> Items`. Even if `setHasGroups(false)` is used, the proxy logic often requires this hierarchy to resolve visibility.
2. **Special Roles:**
   - `GroupRole`: Must return `Qt::DisplayRole` for group headers.
   - `InheritanceDepth`: Essential for sorting; headers often use high values (1000+) to appear below code snippets.
   - `CompletionRole`: Must include visibility bits (e.g., `Public`).
3. **Reset Logic:** Calling `beginResetModel` inside `completionInvoked` is the standard pattern, but nesting these calls (calling them in helpers) causes warnings and suppresses updates.

## Current State of Code
- `VariableCompletionModel` implements a 2-level hierarchy:
  - Row 0 (Top Level): Group Header ("Variables")
  - Children of Row 0: Actual variable matches.
- `data()` provides:
  - Name in **Column 3** (Kate's standard Name column).
  - Icon in **Column 1**.
  - `MatchQuality = 10` to bypass filtering.
  - `shouldAbortCompletion = false` to keep popup open.
- `MainWindow` registration is deferred by 500ms to ensure view stability.

## Why is the popup still hidden?
- **Hypothesis A:** The `completionRange` is too narrow or overlaps with `{`, causing Kate to think the filter string is invalid.
- **Hypothesis B:** There is a mismatch in the `InvocationType` (Automatic vs Manual) that causes the `KateCompletionWidget` to suspend itself immediately.
- **Hypothesis C:** The `MatchQuality` needs to be provided on specific columns or for specific roles we haven't mapped yet.

## Next Steps for Claude
1. Inspect `VCM: data()` logs in `dbg.txt` to see which role Kate requests *last* before giving up on the popup.
2. Check if providing a dummy "test" item at the root level (no hierarchy) works, to rule out tree-navigation bugs.
3. Investigate if `m_editorView->setAutomaticInvocationEnabled(true)` is being overridden by global Kate config.
