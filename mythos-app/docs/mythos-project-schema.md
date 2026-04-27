# Mythos Project Schema

Mythos project files use the `.mythos` extension and store UTF-8 JSON. Version 1 is intentionally small enough for the Tauri prototype, but it names the stable roots needed by the future KDE/core bridge.

## Version 1 Envelope

```json
{
  "schemaVersion": 1,
  "name": "The Sunstone Cycle",
  "metadata": {
    "createdAt": "2026-04-27T00:00:00.000Z",
    "updatedAt": "2026-04-27T00:00:00.000Z",
    "appVersion": "0.1.0"
  },
  "activeDocumentId": "chapter-3",
  "documents": [],
  "variables": [],
  "characters": [],
  "locations": []
}
```

## Collections

`documents` are manuscript files inside the project model. Each document has `id`, `title`, `path`, `body`, optional `contentHtml`, and `wordCount`. The `body` field is the plain-text manuscript fallback; `contentHtml` preserves rich text formatting for the editor.

`variables` are inline manuscript references. Each variable has `id`, `token`, `label`, and `kind`.

`characters` and `locations` currently share the same entity shape: `id`, `name`, `role`, and optional `glyph`.

## Compatibility Rules

Readers should normalize missing optional fields, recompute document word counts when needed, and reject unsupported major schema versions. Writers should preserve the version 1 envelope until a migration layer exists.
