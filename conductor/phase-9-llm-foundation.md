# Phase 9: LLM Integration (Foundation)

## Objective
Integrate Large Language Models (LLMs) into RPG Forge to provide writing assistance, chat-based project interaction, and a foundation for advanced game analysis.

## Key Components

### 1. `LLMService` (Core Logic)
- **Singleton service** managing asynchronous HTTP communication with LLM providers.
- **Normalization:** Translates a unified `LLMRequest` (messages, model, parameters) into provider-specific JSON payloads (OpenAI, Anthropic, Ollama).
- **Streaming Support:** Uses `readyRead` on `QNetworkReply` to emit partial tokens as they arrive.
- **Provider Support:**
    - **OpenAI:** Standard REST API with Bearer token.
    - **Anthropic:** Messages API with `x-api-key`.
    - **Ollama:** Local REST API (`/api/chat` or `/v1/chat/completions`).

### 2. `ChatPanel` (Sidebar UI)
- **History View:** A `QWebEngineView` displaying the conversation. Messages are rendered using `MarkdownParser` for a rich experience (code blocks, LaTeX).
- **Input Area:** A `KTextEdit` for user queries.
- **Context Integration:** Automatically include relevant project context (e.g., current document content or selection) when the user asks a question.
- **Quick Switcher:** UI to change the active model/provider without opening settings.

### 3. Settings & Configuration
- **`SettingsDialog`:** A new global dialog for application-wide settings.
- **LLM Tab:** Configuration for providers (API Keys, Endpoints, default models).
- **Security:** API keys stored securely using `KWallet`.
- **Prompt Templates Tab:** A UI to manage reusable system prompts (e.g., "Summarize as a worldbuilding entry").

### 4. Editor Integration
- **Context Menu:** Add "AI Actions" submenu to the markdown editor for "Expand", "Rewrite", and "Summarize".
- **Action Flow:** Triggering an action populates the Chat Panel with the request and focuses it.

## Key Files & Context
- `src/llmservice.h/cpp` (New)
- `src/chatpanel.h/cpp` (New)
- `src/settingsdialog.h/cpp` (New)
- `src/mainwindow.h/cpp` (Update: register sidebar panel, add menu actions)

## Implementation Steps

### Phase 9.1: LLM Service & Settings
1.  **Create `LLMService`:** Implement the singleton with `QNetworkAccessManager`. Add logic to format requests and parse streaming JSON chunks for each provider.
2.  **Create `SettingsDialog`:** Implement the UI for configuring providers.
3.  **KWallet Integration:** Update `LLMService` to retrieve keys from KWallet, following the `GitHubService` pattern.
4.  **Prompt Templates:** Implement a simple JSON-based storage for reusable prompts.

### Phase 9.2: Chat UI
1.  **Create `ChatPanel`:** Implement the sidebar panel. Use `QWebEngineView` with a basic HTML/JS bridge to append and update messages efficiently.
2.  **Markdown Rendering:** Use `MarkdownParser` to render AI responses into HTML for the `ChatPanel`.
3.  **MainWindow Integration:** Register the `ChatPanel` in `m_sidebar` and add a toggle action.

### Phase 9.3: Editor Actions
1.  **Context Menu Actions:** Intercept the context menu in `MainWindow` or `KTextEditor::View` to add AI-specific options.
2.  **Context Extraction:** Implement logic to grab selected text and current document content for LLM context.

## Verification & Testing
1.  **Provider Tests:** Verify connectivity and streaming for OpenAI, Anthropic, and Ollama (if available locally).
2.  **Security:** Ensure API keys are NOT stored in plain text in `QSettings` but are correctly handled via `KWallet`.
3.  **UI Fluidity:** Ensure streaming responses update the UI smoothly without blocking the main thread.
4.  **Markdown Rendering:** Verify that AI-generated markdown (e.g., tables, bold text) renders correctly in the chat history.
