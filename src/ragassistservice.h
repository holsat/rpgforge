/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RAGASSISTSERVICE_H
#define RAGASSISTSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include <optional>

#include "llmservice.h"

struct SearchResult;

/**
 * \file ragassistservice.h
 *
 * RagAssistService is the single RAG-augmented LLM pipeline used by every
 * AI-assisted feature in the app (Writing Assistant chat, LoreKeeper
 * dossier generation, character generator, simulation arbiter, MCP, ...).
 *
 * It owns the context-assembly policy — retrieval from KnowledgeBase,
 * deduplication, source prioritization, citation-preserving formatting,
 * context-window budgeting — so every surface benefits from the same
 * quality improvements without duplicating the pipeline.
 *
 * The service does NOT own:
 * - UI concerns (widgets, streaming-to-widget, transcripts).
 * - Per-caller post-processing (writing results to disk, updating trees).
 * - Model selection (picked by the caller based on its settings scope).
 *
 * Callers build a RagAssistRequest describing what they want, hand it
 * over, and receive the final generated text (or streaming chunks) via
 * callbacks.
 */

/**
 * \brief A prioritized block of text to include in the prompt verbatim.
 *
 * Examples: the file currently open in the editor ("active doc"), an
 * existing LoreKeeper dossier being updated, a snippet the user
 * explicitly pinned. The service emits these before retrieved RAG
 * passages, in priority order, and trims the tail first when the token
 * budget is exceeded.
 */
struct ContextSource {
    QString label;      // Shown to the LLM as the section heading, e.g. "Existing Dossier", "Active Document".
    QString content;    // The actual text.
    QString citation;   // Optional: file path / identifier used in citation tags; empty if this source isn't citable.
    int priority = 0;   // Lower values = higher priority. 0 is "must keep".
};

/**
 * \brief Synthesis depth. Controls whether the service does a single
 * retrieval-plus-generation pass, or a multi-hop pass that drafts an
 * initial answer, extracts gaps, and retrieves again before producing
 * the final output.
 *
 * Comprehensive mode roughly doubles the LLM cost per request; it is
 * intended for synthesis-heavy use cases like LoreKeeper dossier
 * generation where breadth matters more than latency. Quick mode is
 * the default for interactive chat.
 */
enum class SynthesisDepth {
    Quick,          // single retrieval, single generation
    Comprehensive,  // expand -> retrieve -> draft+gaps -> retrieve -> final
};

/**
 * \brief A complete request to the RAG-augmented LLM pipeline.
 *
 * Callers populate the fields that matter to them and leave the rest
 * at defaults. The service fills in sensible behavior for unset
 * optional fields (e.g. ragQuery defaults to userPrompt when empty).
 */
struct RagAssistRequest {
    // --- Provider selection (caller-scoped settings) ---
    LLMProvider provider = LLMProvider::OpenAI;
    QString model;                          // Empty = caller's default
    QString serviceName;                    // User-facing label, e.g. "Writing Assistant", "LoreKeeper Generator"
    QString settingsKey;                    // Settings path for model-replacement UI

    // --- The actual ask ---
    QString systemPrompt;                   // Role framing, appended to the service's base system prompt
    QString userPrompt;                     // The instruction / question
    QString ragQuery;                       // Retrieval query; defaults to userPrompt if empty

    // --- Conversational turns (chat panel use case) ---
    // Prior turns from an ongoing conversation, in order. The service
    // builds the LLM message array as:
    //     [system(composed), ...priorTurns, user(assembled w/ context)]
    // so provider-side role tracking and prompt caching stay intact.
    // Leave empty for single-shot use cases (LoreKeeper, background jobs).
    QList<LLMMessage> priorTurns;

    // --- Priority context the caller has in hand already ---
    // Emitted in order, de-duplicated against retrieved RAG passages.
    // Used for: existing dossier content being updated (LoreKeeper),
    // the active editor document (Writing Assistant), any user-pinned
    // material.
    QList<ContextSource> extraSources;

    // --- Entity of interest (optional) ---
    // When set: (a) chunks literally mentioning this string get a score
    // boost during re-ranking so direct mentions outrank semantically
    // similar but unrelated chunks; (b) the system prompt includes an
    // instruction to center the answer on this entity.
    QString entityName;

    // --- Retrieval / assembly knobs ---
    int topK = 30;                          // Wide initial retrieval
    int finalK = 12;                        // After MMR re-ranking
    bool requireCitations = true;           // System prompt instructs the model to cite sources inline

    // When true, the service makes one cheap LLM call before retrieval
    // to expand the query with aliases, related terms, and descriptors.
    // Improves recall when the user's phrasing differs from indexed
    // text. Adds ~1s latency per request. Defaults to true for
    // Comprehensive depth, false for Quick.
    // Set explicitly to override the per-depth default.
    std::optional<bool> enableQueryExpansion;

    // --- Generation knobs ---
    int maxTokens = 4096;                   // Output cap; Anthropic requires this in request body
    double temperature = 0.7;
    bool stream = false;                    // true for interactive UIs, false for background work

    // --- Pipeline mode ---
    SynthesisDepth depth = SynthesisDepth::Quick;

    // Active file path (optional) — excluded from RAG retrieval so the
    // service doesn't send a file we already included as an extraSource.
    QString activeFilePath;
};

/**
 * \brief Callbacks delivered back to the caller.
 *
 * For streaming requests: onChunk fires incrementally as tokens arrive,
 * onComplete fires once at the end with the full text.
 *
 * For non-streaming requests: onChunk is never called; onComplete fires
 * once with the full text.
 *
 * onError fires at most once per request. A request never fires BOTH
 * onComplete and onError.
 */
struct RagAssistCallbacks {
    std::function<void(const QString &requestId, const QString &chunk)> onChunk;
    std::function<void(const QString &requestId, const QString &fullText)> onComplete;
    std::function<void(const QString &requestId, const QString &message)> onError;

    // Fires after retrieval + re-rank, before the generation pass. paths
    // are project-relative, deduped, already trimmed to finalK. Primarily
    // useful for tests that need to assert "the RAG pipeline pulled chunks
    // from files X, Y, Z" without round-tripping the full response. Optional;
    // production callers can leave unset.
    std::function<void(const QString &requestId, const QStringList &paths)> onPassagesRetrieved;
};

class RagAssistService : public QObject
{
    Q_OBJECT

public:
    static RagAssistService& instance();

    /**
     * \brief Dispatch a RAG-augmented generation request.
     *
     * \returns A request-id string the caller can use to correlate
     *          streamed chunks with the final response (useful when
     *          multiple requests are in flight concurrently).
     *
     * Ownership: the callbacks are copied into the request's lifetime
     * and invoked on the main thread.
     */
    QString generate(const RagAssistRequest &request,
                     const RagAssistCallbacks &callbacks);

    /**
     * \brief Test-only injection seam for swapping in a mock LLMService.
     *
     * Production code uses LLMService::instance() — pass nullptr to
     * restore that behavior. Tests that want to intercept prompts or
     * stub out LLM responses supply a custom LLMService subclass here
     * before calling generate().
     *
     * Non-owning; caller retains the lifetime of the mock. Call with
     * nullptr in the test's cleanup step.
     */
    void setLlmServiceForTesting(LLMService *llm) { m_llmOverride = llm; }

private:
    explicit RagAssistService(QObject *parent = nullptr);
    ~RagAssistService() override = default;
    RagAssistService(const RagAssistService&) = delete;
    RagAssistService& operator=(const RagAssistService&) = delete;

    // Private continuation of generate(): runs after KnowledgeBase::search
    // has returned, given the filtered/trimmed passages to include. Kept
    // out-of-line so the async retrieval callback doesn't capture a huge
    // lambda body.
    void dispatchGeneration(const RagAssistRequest &request,
                            const QList<SearchResult> &ragPassages,
                            const RagAssistCallbacks &callbacks,
                            const QString &requestId);

    // Step 1 of the pipeline (optional): ask the LLM to expand the
    // retrieval query with aliases / related terms. Fires onDone with
    // the expanded query (or the original on failure) so the rest of
    // the pipeline never has to wait for an error path.
    void expandQuery(const RagAssistRequest &request,
                     std::function<void(const QString&)> onDone);

    // Step 2 of the pipeline: retrieve + dedup + rerank.
    void retrieveAndRank(const RagAssistRequest &request,
                         const QString &expandedQuery,
                         std::function<void(QList<SearchResult>)> onDone);

    // Comprehensive mode: retrieve → draft + gap-list → retrieve gaps →
    // final. Two extra LLM calls versus Quick. Falls back to the same
    // code path as Quick if the draft phase fails or produces no gaps.
    void runComprehensive(const RagAssistRequest &request,
                          const QList<SearchResult> &firstPassages,
                          const RagAssistCallbacks &callbacks,
                          const QString &requestId);

    LLMService* llmService() const;

    LLMService *m_llmOverride = nullptr;
};

#endif // RAGASSISTSERVICE_H
