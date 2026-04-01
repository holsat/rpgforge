# Debugging Notes: Variable Autocomplete

## Current Status (2026-04-01, Claude session)
- **Branch:** `fix/variable-system-crashes`
- **State:** No more crashes. Auto-trigger fires correctly. `completionInvoked` is called with correct variable count. But the completion popup still does not appear.
- **Ctrl+Space (manual):** Also does not show popup.
- **Next debugging target:** Why does KateCompletionModel/widget suppress the popup despite `completionInvoked` returning 4 variables?

## Root Causes Found and Fixed

### 1. Infinite Recursion in KateCompletionModel (FIXED)
**Symptom:** Stack overflow (SIGSEGV) with hundreds of recursive frames inside `libKF6TextEditor.so` at offset `0xc9e12` — inside `KateCompletionModel`'s internal tree-walking/filtering code.

**Root cause:** We overrode `rowCount()` to return `m_variables.size()` but **never called `setRowCount()`**. The base class `CodeCompletionModel::index()` uses an internal `d->rowCount` (set only by `setRowCount()`) to validate indices. With `d->rowCount = 0`:
1. Kate calls `rowCount(invalid)` → gets N > 0
2. Kate calls `index(0, 0, invalid)` → base class sees `0 >= 0` → returns **invalid** QModelIndex
3. Kate calls `rowCount(invalid)` on the result → gets N again (invalid.isValid() == false)
4. Infinite recursion → stack overflow

**Fix:** Added `setRowCount(m_variables.size())` at the end of `updateVariables()`.

### 2. Wrong Column for Name Data (FIXED)
**Symptom:** Completion popup never appeared even when model was correctly invoked.

**Root cause:** Kate expects exactly 6 columns (the `Columns` enum):
- 0: Prefix, 1: Icon, 2: Scope, **3: Name**, 4: Arguments, 5: Postfix

We were returning the variable name at `index.column() == 1` (Icon column) instead of `KTextEditor::CodeCompletionModel::Name` (column 3). Kate's widget couldn't find any name text to display.

**Fix:** Changed `data()` to check `index.column() == KTextEditor::CodeCompletionModel::Name` with `Qt::DisplayRole`. Removed our `columnCount()` override (was returning 3; base class correctly returns 6).

### 3. `shouldStartCompletion` insertedText Parameter (FIXED)
**Symptom:** `shouldStartCompletion` was never returning true despite user typing `{{`.

**Root cause:** The `insertedText` parameter is NOT the single character just typed — it's `m_automaticInvocationLine`, the **accumulated text** since the last cursor position change. After typing `{{`, it equals `"{{"` not `"{"`. Our check for `insertedText == "{"` never matched.

**Fix:** Ignore `insertedText` entirely. Instead, read the actual document line and walk backwards from cursor position to check if we're inside `{{...`.

## Previous Fixes (Gemini/earlier sessions)
1. **Infinite Recursion from MovingRange:** Disabled wavy-line error highlights which triggered recursive layout updates. Error highlighting code now just clears ranges.
2. **Signal Feedback Loops:** VariableCompletionModel is a pull-only model (no background signal connections).
3. **Safe Registration:** Deferred by 500ms in MainWindow to ensure editor view is idle.
4. **Resolution Safety:** Removed QJSEngine, added 1MB string length limits.
5. **Cache Safety:** QWebEngine cache cleared at startup.

## What Works Now
- `shouldStartCompletion` correctly detects `{{` context and returns true
- `completionInvoked` is called and reports correct variable count (e.g. 4)
- `setRowCount()` keeps base class `index()` consistent — no more recursion
- `data()` returns names in correct column (3/Name) with correct role (Qt::DisplayRole)
- `setHasGroups(false)` called in constructor (matches KateWordCompletionModel pattern)
- `setAutomaticInvocationEnabled(true)` explicitly set on the view
- No crashes under any test scenario

## What Doesn't Work Yet
- **The completion popup still does not appear** despite all the above being correct
- Both auto-trigger (typing `{{`) and manual trigger (Ctrl+Space) fail to show popup

## Remaining Investigation Areas
1. **KateCompletionModel filtering:** Does Kate's internal proxy model filter out all our items? May need to check `shouldAbortCompletion()` or `matchesItem()` behavior.
2. **Completion range:** Our `completionRange()` returns the partial variable name after `{{`. If Kate uses this range text to filter against item names and the filter doesn't match, all items could be hidden.
3. **Model hierarchy:** KateWordCompletionModel uses a hierarchical model (group header at root, items as children). Our model is flat. Kate may expect the hierarchical structure.
4. **`filterText()` override:** We don't implement `filterText()`. Kate may use a default filter that excludes everything.
5. **`shouldAbortCompletion()` override:** Default implementation may abort if the completion range text doesn't look like an identifier Kate expects.
6. **Debug approach:** Add stderr logging to `data()`, `rowCount()`, and `completionRange()` to see if Kate is actually querying the model after `completionInvoked`.

## Key Kate Source References (in research/ktexteditor/src/)
- `completion/katecompletionwidget.cpp` — Main completion controller. `automaticInvocation()` at line 1380, `insertText()` at line 1343, `startCompletion()` manages `m_completionRanges`.
- `completion/katecompletionmodel.cpp` — Internal proxy/filter model. `currentCompletion()` at line 1499.
- `completion/katewordcompletion.cpp` — Kate's built-in word completion. Reference implementation for how to correctly implement a completion model. Uses hierarchical model with group header.
- `utils/codecompletionmodel.cpp` — Base class defaults. `index()` at line 41 uses `d->rowCount`. `setRowCount()` at line 69.
- `include/ktexteditor/codecompletionmodel.h` — Columns enum (Prefix=0..Postfix=5), ColumnCount=6.
