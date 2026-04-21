# `<!-- vars ... -->` Directive — Design Proposal

Status: **approved, deferred** — to be implemented after the InlineAIInvoker lands.

## Problem

`LibrarianService::parseMarkdownTables` and `parseMarkdownLists` currently
extract variables heuristically from any markdown table or key-value list
they find. This produces two classes of problem:

1. **False positives**: plot outlines, prose-style sentences, template
   placeholders with empty values, numbered rule-step lists, and section
   fallbacks (`Global/*`) get promoted to variables. The Writing
   Assistant and variable preview then carry this noise.
2. **Unpredictable namespacing**: variable paths are derived from the
   first header cell or section heading, with markdown stripping applied
   after the fact. Authors can't tell in advance what `{{path}}` to use
   without opening the variables panel after a scan.

Post-hoc filtering (the stricter checks landed after the first cleanup
pass) helps but is still heuristic. What the system actually wants is
**author intent** — a way for the author to say "extract this table,
here's the namespace" or "skip this block, it's narrative prose".

## Proposal: HTML-comment directives

HTML comments are invisible in every markdown renderer we care about
(QTextDocument, cmark-gfm, pandoc, GitHub, Obsidian, VSCode preview).
LoreKeeper's LLM calls see them as a line of text and ignore them
semantically. KnowledgeBase embeds them as part of the chunk content
(minor noise, can optionally be stripped at embed time). They're
already a standard markdown "ignore me" zone.

Directive syntax:

```markdown
<!-- vars [:verb] [attr="value" ...] -->
```

The directive applies to the **next block** (table, list, or file,
depending on verb). Directives more than 3 blank lines away from a
block are ignored.

## Default behavior: opt-in

**Without any directive, no variables are extracted.** Authors
explicitly annotate blocks that should contribute to the variable
namespace. Existing projects migrate with a one-line edit per
canonical rule document. Research notes, plot outlines, character
sketches, and templates produce zero variables by default — which
matches stated intent: "variables should reflect game statistics, not
random text."

A project-level setting `llm/librarian/default_mode = opt_in|opt_out`
lets users flip the default without recompiling. Opt-out mode keeps
the current heuristic (with the already-landed stricter rules) and
the directives become override/skip tools.

## Directive catalogue

### 1. Basic opt-in with auto-naming

```markdown
<!-- vars -->
| **Difficulty** | **Name** | **Target \#** | **Description** |
|:--:|----|:--:|----|
| 0 | Routine | 0 | No brainer |
| 1 | Simple | 4 | ... |
```

**Produces**:
- `difficulty.0.name = "Routine"`
- `difficulty.0.target = "0"`
- `difficulty.0.description = "No brainer"`
- `difficulty.1.name = "Simple"`
- …

Namespace = first header cell (stripped of markdown, lowercased,
non-identifier chars removed). Entity = first cell of each data row
(markdown stripped, case preserved). Keys = remaining header cells
(same cleanup as namespace).

### 2. Explicit namespace override

```markdown
<!-- vars namespace="race" -->
| **Race** | **Insight** | **Prowess** | **Legend** |
|----|:--:|:--:|:--:|
| Arakasha | -1 | +1 | +1 |
```

**Produces**:
- `race.Arakasha.insight = "-1"`
- `race.Arakasha.prowess = "+1"`
- …

The auto-derived name would have been `race` anyway here, but `namespace`
lets you rename tables whose first header is ambiguous or long.

### 3. Explicit skip

```markdown
<!-- vars: skip -->
| Scene | Notes |
|-------|-------|
| Opening | Chkmnaar alone in the desert... |
```

**Produces**: nothing. Parser acknowledges the directive and moves on.

Useful inside plot outlines, templates, and any file that's otherwise
opt-in but contains a decorative table.

### 4. List-style with entity declaration

```markdown
<!-- vars entity="Arakasha" namespace="race" -->
- **Health Points:** 35
- **Prowess:** +1
- **Resilience:** +1
- **Stride:** -1
```

**Produces**:
- `race.Arakasha.healthpoints = "35"`
- `race.Arakasha.prowess = "+1"`
- `race.Arakasha.resilience = "+1"`
- `race.Arakasha.stride = "-1"`

Without `entity`, a list block falls back to the enclosing `##` / `###`
section heading as entity name.

### 5. File-wide opt-out

```markdown
<!-- vars: none -->

# My Research Notes

(everything below this line is skipped for variable extraction)
```

Place at the top of the file. Parser short-circuits the whole file for
variable extraction. RAG and LoreKeeper still process the content
normally (the directive only affects the librarian).

### 6. Column selection

```markdown
<!-- vars namespace="class" skip-cols="description" -->
| **Class** | **HP** | **MP** | **Description** |
|-----------|--------|--------|-----------------|
| Freeborn Peasant | 20 | 0 | A common laborer tilling the fields... |
```

**Produces**:
- `class.Freeborn Peasant.hp = "20"`
- `class.Freeborn Peasant.mp = "0"`

The description column is dropped. Useful when a table mixes stats with
flavor text.

### 7. Type hints (enables math)

```markdown
<!-- vars namespace="age" value-type="int" -->
| **Age** | **Ability Levels** | **Skill Levels** |
|---------|--------------------|--------------------|
| Adolescent | -1 | -1 |
| Adult | 0 | 0 |
| Senior | 1 | 1 |
```

**Produces** values stored as integers, so expressions like
`{{CALC: age.senior.skilllevels + 2}}` work directly without string
parsing.

Supported types: `int`, `float`, `string` (default), `bool`, `dice`
(validated against `\d+d\d+(?:[+-]\d+)?`).

### 8. Aliases

```markdown
<!-- vars namespace="race" alias="races" -->
| **Race** | **Prowess** |
|----------|-------------|
| Arakasha | +1 |
```

**Produces the same values under both paths**:
- `race.Arakasha.prowess` AND `races.Arakasha.prowess`

Handy when one author prefers singular and another plural.

### 9. Computed columns

```markdown
<!-- vars namespace="difficulty" compute="target=level*4" -->
| **Level** | **Name** |
|-----------|----------|
| 0 | Routine |
| 1 | Simple |
```

**Produces**:
- `difficulty.0.name = "Routine"`, `difficulty.0.target = 0` (computed)
- `difficulty.1.name = "Simple"`, `difficulty.1.target = 4` (computed)
- …

Compute expressions reference other cells in the row (`level`) or other
extracted variables in scope. Stored alongside the declared values.

## Interaction with existing extractors

- `parseMarkdownTables` and `parseMarkdownLists` gain a "directive is
  required" gate in opt-in mode. Opt-out mode uses the heuristic path
  (with landed stricter rules) when no directive is present.
- `<!-- vars: skip -->` takes precedence over any heuristic — opt-out
  mode honours it.
- Directive parsing is a small regex over the 3 blank-line window
  preceding a block. Attributes are `key="value"` pairs; unknown
  attributes are recorded and logged but don't fail the directive.

## Interaction with other services

- **LoreKeeper**: unaffected. Sees HTML comments as text; the LLM
  ignores them. LoreKeeper doesn't care about variable extraction.
- **KnowledgeBase (RAG)**: comments become part of chunks sent to the
  embedding model. Minor signal noise. Optional cleanup pass strips
  `<!-- vars ... -->` lines before embedding. ~10 lines of code.
- **Markdown preview / PDF export / Kompare diff**: zero impact.
  HTML comments are invisible everywhere the author ever sees the file.
- **Inline AI invoker (`@lore`, `@forge`)**: unaffected. Operates on
  editor text; directives just pass through the prompts as-is.

## Implementation plan

1. `DirectiveParser` class/namespace (~60 lines): regex-based detector
   for `<!-- vars ... -->`, attribute-value splitter, `Directive`
   struct with fields for verb, namespace, entity, skip-cols,
   value-type, alias, compute.
2. Integration with `parseMarkdownTables` / `parseMarkdownLists`
   (~60 lines): each block checks for a preceding directive; extracts
   accordingly.
3. File-level opt-out scan before table/list parsing (~10 lines).
4. Settings toggle `llm/librarian/default_mode` (~10 lines).
5. Optional: KnowledgeBase::chunkAndEmbed strips `<!-- vars ... -->`
   lines before embedding (~15 lines).
6. Documentation file `docs/vars-directives.md` with every example
   above, readable by authors in the project itself.

Total: ~165 lines code, 1 focused session. No new dependencies.

## Migration path

Shipping this with opt-in-default means existing projects will see
their library variables disappear on first upgrade. Two mitigations:

1. **First-run migration prompt**: on project open, if the librarian
   DB has extracted variables AND no file contains a `<!-- vars -->`
   directive, show a one-time dialog: "Libvars are now opt-in. Keep
   legacy heuristic mode for this project?" Choice stored as
   `llm/librarian/default_mode` for the project.
2. **Docs page** showing the minimum directive set needed to recover
   existing extractions (typically 3–5 annotations per canonical rule
   doc).

## Author instructions (to be written at implementation time)

When the feature lands, ship a companion `docs/vars-directives.md`
containing every directive example in this proposal plus:

- Getting started: "I want my difficulty table to be usable as
  `{{difficulty.0.name}}`" — shows the single-directive workflow.
- Migration from heuristic mode: "My project already has library
  variables. How do I keep them?" — shows the opt-out toggle and the
  annotation workflow.
- Troubleshooting: "My directive isn't picked up" — shows the 3-blank-
  line rule, common typos, log-line format to grep for.
- Examples for each of the 9 directive shapes above, rendered both as
  markdown source and as the resulting variable paths.

Keep the doc concise — authors using this feature are not engineers;
the examples should read like recipe cards, not a spec.
