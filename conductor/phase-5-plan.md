# RPG Forge Phase 5 Implementation Plan

## Objective
Implement a Scrivener-like project management system with multi-document support, logical organization, document metadata (synopsis/status), and PDF export with professional pagination and styling.

## Key Files & Context
- `src/projectmanager.h/cpp`: New singleton for project configuration (`rpgforge.project`).
- `src/projecttreepanel.h/cpp`: New sidebar panel for logical project organization.
- `src/corkboardview.h/cpp`: New view for folder synopsis cards.
- `src/pdfexporter.h/cpp`: New service for generating PDFs via `QWebEnginePage`.
- `src/mainwindow.cpp`: Integration of the new components and "Project" menu.

## Implementation Steps

### 1. Project Infrastructure
- Define `rpgforge.project` JSON format:
    - Metadata (Name, Author).
    - PDF Settings (Margins, Page Size, Page Numbers).
    - Logical Structure (Folders and File links).
- Create `ProjectManager` class to load, save, and manage this state.
- Add "New Project", "Open Project", and "Project Settings" actions to `MainWindow`.

### 2. Project Tree Panel
- Implement `ProjectTreePanel` using `QTreeView` and a custom `QAbstractItemModel`.
- Support:
    - Logical folders (not necessarily matching disk structure).
    - Manual drag-and-drop reordering.
    - Right-click context menu (Add Folder, Link Existing File, Remove).
    - Double-click to open documents in the editor.

### 3. Document Metadata & Corkboard
- Enhance YAML front-matter parsing to extract `status`, `label`, and `synopsis`.
- Implement `CorkboardView` (a `QScrollArea` containing cards).
- Selecting a folder in the `ProjectTree` offers a toggle between "File View" and "Corkboard View".

### 4. Stylesheets & Pagination
- Support a project-wide CSS file for styling the PDF and preview.
- Implement CSS Paged Media logic:
    - `@page` for margins and headers/footers.
    - `counter(page)` for page numbers.
    - `break-before: page` for chapter starts.

### 5. PDF Export
- Implement the "Compiler" that concatenates documents based on the logical tree.
- Use `QWebEnginePage::printToPdf()` to generate the output.
- Add a progress dialog for PDF generation.

## Verification & Testing
- Create a sample project with multiple files and folders.
- Verify that reordering in the Project Tree reflects in the "Compile" output.
- Export to PDF and verify pagination, margins, and page numbers.
- Check that variable resolution still works in the compiled output.
