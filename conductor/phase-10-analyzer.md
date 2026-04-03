# Phase 10: LLM Game Analyzer (Inline Diagnostics & RAG)

## Objective
Implement a continuous, RAG-powered background analysis system that identifies rule conflicts, ambiguities, and suggestions within the project. The system will leverage a local vector index to retrieve relevant project context, surfacing issues as inline editor diagnostics and a centralized "Problems" panel.

## Key Components

### 1. `KnowledgeBase` & `VectorService` (The Memory)
- **Responsibility:** Indexes the entire project for semantic retrieval.
- **Embedding Generation:** Uses `LLMService` to generate embeddings for document chunks (using OpenAI's `text-embedding-3-small` or local Ollama embeddings like `mxbai-embed-large`).
- **Local Storage:** Stores embeddings and text chunks in a local SQLite database (optionally with a vector extension or a simple cosine similarity search in C++).
- **Background Indexing:** A `QFileSystemWatcher` monitors Markdown and RPGVars files, re-indexing chunks on save.
- **RAG Protocol:** When a query or analysis task is triggered, the service retrieves the most semantically relevant snippets to supplement the "Core Context" (current file + global variables).

### 2. `AnalyzerService` (The Brain)
- **Responsibility:** Orchestrates the analysis process.
- **Run Modes:** Continuous (5s debounce), On-Demand, or Paused.
- **Augmented Prompting:** 
    1.  Extracts the current document state.
    2.  Queries the `KnowledgeBase` for related rules/lore in other files.
    3.  Sends the "Augmented" context to the LLM.
- **Structured JSON Output:** Requests issues with file, line, severity, and cross-references.

### 3. Inline Diagnostics (KTextEditor Integration)
- **Visuals:** Red/Yellow/Blue squiggly underlines using `MovingInterface`.
- **Interactivity:** Tooltips with LLM explanations and clickable cross-references to other project files.

### 4. `ProblemsPanel` (Bottom Dock)
- **UI:** Table view of all project-wide issues.
- **Features:** Severity filtering, double-click to navigate, and status bar counters.

### 5. Settings & Configuration
- **RAG Settings:** Configure embedding model, chunk size, and "Top-K" retrieval count.
- **Analyzer Tab:** Model selection and system prompt customization.

## Implementation Steps

### Phase 10.1: Knowledge Base & Indexing
1.  **Embeddings Support:** Add embedding generation to `LLMService`.
2.  **Vector Index:** Implement a basic SQLite-based storage for text chunks and their embedding vectors.
3.  **Chunking Logic:** Implement a smart Markdown chunker that respects heading boundaries.

### Phase 10.2: Analyzer Service & RAG
1.  **Retrieve & Augment:** Implement the retrieval step in `AnalyzerService` before calling the LLM.
2.  **Analysis Logic:** Implement the JSON response parsing and diagnostic mapping.

### Phase 10.3: UI & Feedback
1.  **Problems Panel:** Implement the bottom dockable table view.
2.  **Inline Highlights:** Connect the analyzer results to `KTextEditor` highlights and tooltips.

## Key Files
- `src/knowledgebase.h/cpp` (New)
- `src/analyzerservice.h/cpp` (New)
- `src/problemspanel.h/cpp` (New)

## Verification
1.  **Semantic Retrieval:** Verify that the "Expand" action or "Analyzer" pulls in relevant context from unrelated files (e.g., pulling "Combat Rules" when analyzing "Sword Item").
2.  **Performance:** Ensure indexing and retrieval don't cause UI lag.
3.  **Accuracy:** Verify that LLM diagnostics correctly identify cross-file rule contradictions.
