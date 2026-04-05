# Phase 12: Data Import & LLM Validation

## Objective
Enable users to migrate existing projects into RPG Forge by importing Scrivener (`.scriv`) projects and Microsoft Word/RTF/PDF documents. Content will be converted to Markdown, embedded media will be extracted and managed safely, and advanced formatting will be preserved. Additionally, enhance the Onboarding Wizard to validate LLM configurations and promote vision-capable models.

## Proposed Solution & Architecture

### 1. `DocumentConverter` (New Utility Class)
- **Role:** Handles communication with the system's `pandoc` installation and manages media extraction.
- **Pandoc Execution:** Runs `pandoc` with the `--extract-media=<temp_dir>` flag to pull embedded images into a temporary staging area.
- **Styling Preservation:** Uses `gfm+raw_html` (GitHub Flavored Markdown with raw HTML enabled) as the output format. This preserves advanced styling lacking a direct Markdown equivalent as inline HTML/CSS, which RPG Forge's preview panel can render.

### 2. Media Handling Strategy (Centralized with Sanitized Prefixing)
To avoid filename collisions and ensures maximum compatibility with command-line tools and scripts:
- **Renaming:** Each extracted image will be renamed using the source document's name or a short hash as a prefix.
- **Strict Sanitization:** All prefixes and filenames will be rigorously sanitized to:
    - **Remove Special Characters:** No shell-sensitive characters like `*`, `"`, `'`, `[`, `]`, `{`, `}`, `(`, `)`, `#`, `!`, `@`, `$`, `%`, `<`, `>`, `&`, `|`, `;`, `?`, or `\`.
    - **No Spaces:** All spaces will be replaced with underscores (`_`).
    - **Allowed Characters:** Only alphanumeric characters, underscores, and hyphens will be permitted (e.g., `Tutorial_Intro_image1.png`).
- **Storage:** Move the renamed and sanitized images to the project's central `media/` folder.
- **Link Updating:** Update the generated Markdown file's image links to point to the new sanitized paths.

### 3. Proprietary Image Format Conversion (.emf / .wmf)
- **Detection & Conversion:** Pandoc extracts Windows Metafile (`.emf`) exactly as it is. RPG Forge's preview and PDF export cannot render EMF files natively.
- **Processing Pipeline:**
  1. Detect `.emf` or `.wmf` files in the extracted media folder.
  2. Use a system dependency like `inkscape` or `imagemagick` (via `QProcess`) to convert the `.emf` into:
     - `.svg` (Primary: Scalable Vector Graphic, retains infinite scalability and transparency).
     - `.png` (Fallback: High-res raster with transparency).
  3. **Link Updating:** Parse the generated markdown file and regex-replace the `.emf` links with `.svg` (or `.png`).

### 4. `ScrivenerImporter` Class
- **Role:** Orchestrates the multi-file import of a `.scriv` project package.
- **Parsing:** Uses `QDomDocument` to parse the `[ProjectName].scrivx` file to understand the binder hierarchy.
- **Mapping:**
  - `DraftFolder` -> RPG Forge's `Manuscript` folder.
  - `ResearchFolder` -> RPG Forge's `Research` folder.
  - Nested Folders -> Recreated as physical directories in the RPG Forge project.
  - `BinderItem Type="Text"` -> Locates the corresponding `Files/Data/[UUID]/content.rtf`, runs it through `DocumentConverter`, and saves it as a `.md` file.
- **Metadata Integration:** Extracts Scrivener's internal Synopsis, Status, and Labels and maps them to RPG Forge's `ProjectTreeItem` metadata.

### 5. Application UI Integration
- Add an **Import** sub-menu to the `File` menu.
- Add an "Import Existing Project" option to the `OnboardingWizard` (Phase 11 extension).
- Add dependency checks on startup to verify `pandoc` and an SVG converter (`inkscape`) are installed.

### 6. Onboarding Wizard Enhancements (LLM Validation)
To ensure the LLM component is functional and reliable:
- **Test Connection Button:** Add a "Test Connection" button next to the AI Provider/Model selection in the Onboarding Wizard.
- **Validation Logic:** Clicking the button will send a minimal test prompt (e.g., "Respond with 'OK'") to the selected provider using the entered API key.
- **UI Feedback:** Display a success indicator (green checkmark/text) or a failure message (red cross/error details) based on the API response.
- **Guidance Text:** Update the instructions on the AI configuration page to strongly suggest configuring at least one cloud-based vision-capable LLM (e.g., `gpt-4o`, `claude-3-5-sonnet`) and/or one local vision-capable LLM (if running Ollama) to ensure maximum feature compatibility in the future.

## Implementation Steps
1. Verify system dependencies (`pandoc`, `inkscape`/converter).
2. Build `DocumentConverter` with temporary staging, `pandoc` execution, EMF conversion, sanitized prefix renaming, and link replacement.
3. Build `ScrivenerImporter` to traverse the XML binder and feed RTF files to the converter.
4. Update `OnboardingWizard` with the "Test Connection" functionality and updated guidance text.
5. Integrate import actions into `MainWindow` and `ProjectTreeModel` to ensure imported files appear in the UI immediately.