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

#include "ragassistservice.h"

#include "agentgatekeeper.h"
#include "knowledgebase.h"
#include "projectmanager.h"
#include "debuglog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <KLocalizedString>

namespace {

// Rough chars-per-token estimator. All providers treat tokens differently
// (BPE for OpenAI, SentencePiece for Anthropic, etc.) but 4 chars/token is
// a conservative heuristic that over-estimates slightly for English prose,
// which is what we want for budget math — better to underfill the window
// than to overflow it and get a 400 back from the provider.
inline int estimateTokens(const QString &text)
{
    return (text.size() + 3) / 4;
}

// Parse a draft-pass response for an "INFORMATION GAPS:" section and
// return its bullet items as a list of plain-text questions. Permissive
// about formatting: the model may use "- ", "* ", "• ", or just line
// breaks. Returns empty if no gaps section is present or all bullets
// are blank.
QStringList parseGaps(const QString &draftResponse)
{
    static const QRegularExpression heading(
        QStringLiteral("information\\s*gaps\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = heading.match(draftResponse);
    if (!m.hasMatch()) return {};

    const QString tail = draftResponse.mid(m.capturedEnd());
    QStringList raw = tail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QStringList gaps;
    for (QString line : raw) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        // Strip leading bullet markers and numbering.
        static const QRegularExpression bullet(
            QStringLiteral("^\\s*(?:[-*\\u2022\\u2013]|\\d+[.)])\\s*"));
        line.replace(bullet, QString());
        if (line.isEmpty()) continue;
        // Stop at the first line that doesn't look like a gap bullet
        // (keeps us from sucking up trailing prose the model might add
        // after the list).
        if (gaps.isEmpty() && line.length() > 200) break;
        gaps << line;
        if (gaps.size() >= 5) break;     // cap — keep retrieval bounded
    }
    return gaps;
}

// Compose the service's baseline system prompt. The caller's systemPrompt
// is appended; the citation instructions are conditionally included.
QString composeSystemPrompt(const RagAssistRequest &request)
{
    QStringList parts;
    parts << QStringLiteral(
        "You are assisting an author and game designer working on an RPG "
        "project. The project contains manuscript chapters, research notes, "
        "and structured lore. Your task is to synthesize information from "
        "the author's own work — not invent new details — unless explicitly "
        "asked to create new content.");

    if (request.requireCitations) {
        parts << QStringLiteral(
            "When you reference material from the supplied context, cite the "
            "source inline using the EXACT path shown in the [SOURCE: path] "
            "header of the passage you drew from. Format the citation as "
            "(exact/path/from/the/SOURCE/header.md) immediately after the "
            "referenced claim. DO NOT abbreviate, truncate, guess, or invent "
            "paths. DO NOT cite 'Manuscript.md' or similar short names — use "
            "the full path as given. If no SOURCE header is present for a "
            "claim, do not add a citation rather than inventing one.");
    }

    if (!request.entityName.isEmpty()) {
        parts << i18n("Center your answer on the entity: \"%1\". Prioritise "
                      "details directly describing or involving this entity "
                      "over tangentially related material.", request.entityName);
    }

    if (!request.systemPrompt.isEmpty()) {
        parts << request.systemPrompt;
    }

    return parts.join(QStringLiteral("\n\n"));
}

// Entity-aware score boost. A chunk that literally mentions the named
// entity outranks a semantically-similar chunk that does not. 0.15 is
// additive to the cosine similarity (range ~[-1, 1], typically 0.2–0.8
// for hits) — big enough to bubble direct mentions up past marginal
// semantic matches, small enough that a strong semantic match still
// wins over a weak literal one.
constexpr float kEntityBoost = 0.15f;

// Recency boost applied to chunks from files modified within the last
// N days. Intentionally smaller than the entity boost so freshness is a
// tiebreaker, not a primary ranker. Applied as a linear ramp: full
// boost at modified-today, decaying to 0 at kFreshnessWindowDays.
constexpr float kFreshnessBoost = 0.05f;
constexpr int kFreshnessWindowDays = 14;

// MMR lambda. 1.0 = pure relevance (no diversification, equivalent to
// top-K). 0.0 = pure diversity. 0.7 favors relevance while still
// penalising near-duplicates — a good default for project-wide RAG
// where the corpus has a lot of editorial repetition (chapter drafts,
// outline + expanded scene, etc.).
constexpr float kMMRLambda = 0.7f;

// Crude textual Jaccard for MMR's pairwise similarity. True embedding
// similarity would be better but would require holding chunk vectors
// through retrieval — the KB's search() returns SearchResult with only
// the text. A bigram Jaccard catches near-duplicate prose reliably
// enough for MMR's "don't return 10 copies of the same paragraph" job.
float bigramJaccard(const QString &a, const QString &b)
{
    auto bigrams = [](const QString &s) {
        QSet<QString> out;
        const QString normalized = s.toLower();
        for (int i = 0; i + 1 < normalized.size(); ++i) {
            out.insert(normalized.mid(i, 2));
        }
        return out;
    };
    const QSet<QString> A = bigrams(a);
    const QSet<QString> B = bigrams(b);
    if (A.isEmpty() && B.isEmpty()) return 1.0f;

    QSet<QString> intersection = A;
    intersection.intersect(B);
    QSet<QString> unionSet = A;
    unionSet.unite(B);
    if (unionSet.isEmpty()) return 0.0f;
    return static_cast<float>(intersection.size())
         / static_cast<float>(unionSet.size());
}

// Apply entity-aware boost + freshness, then MMR-diversify down to
// finalK. Mutates the input list's scores (as a working copy) so the
// returned list is already in the desired order.
QList<SearchResult> rerankAndTrim(const QList<SearchResult> &input,
                                   const QString &entityName,
                                   const QString &projectPath,
                                   int finalK)
{
    if (input.isEmpty()) return {};

    // Phase 1: apply additive boosts. The KB already returned cosine-
    // similarity scores; we mutate a working copy in place so MMR
    // operates on the boosted ranking.
    QList<SearchResult> boosted = input;

    // Literal-match pattern for the entity. Word-boundary anchors so
    // "Ryz" in "Ryzen" doesn't match a query for "Ryz" accidentally —
    // we want full-token hits. Case-insensitive.
    QRegularExpression entityRegex;
    if (!entityName.trimmed().isEmpty()) {
        entityRegex.setPattern(
            QStringLiteral("\\b") +
            QRegularExpression::escape(entityName.trimmed()) +
            QStringLiteral("\\b"));
        entityRegex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 windowMs = qint64(kFreshnessWindowDays) * 24 * 60 * 60 * 1000;

    for (SearchResult &r : boosted) {
        // Entity-aware literal-match boost.
        if (entityRegex.isValid() && entityRegex.pattern().size() > 0) {
            if (r.content.contains(entityRegex) || r.heading.contains(entityRegex)) {
                r.score += kEntityBoost;
            }
        }

        // Freshness boost — linear ramp from 0 (older than window) to
        // full (modified today). Requires projectPath to resolve the
        // absolute file path; skip gracefully if we don't have one.
        if (!projectPath.isEmpty() && !r.filePath.isEmpty()) {
            const QString abs = QDir(projectPath).absoluteFilePath(r.filePath);
            QFileInfo fi(abs);
            if (fi.exists()) {
                const qint64 ageMs = nowMs - fi.lastModified().toMSecsSinceEpoch();
                if (ageMs >= 0 && ageMs < windowMs) {
                    const float ratio = 1.0f - static_cast<float>(ageMs) / static_cast<float>(windowMs);
                    r.score += kFreshnessBoost * ratio;
                }
            }
        }
    }

    // Phase 2: sort by boosted score, then MMR-select finalK.
    std::sort(boosted.begin(), boosted.end(),
              [](const SearchResult &a, const SearchResult &b) { return a.score > b.score; });

    if (boosted.size() <= finalK) return boosted;

    QList<SearchResult> selected;
    QList<SearchResult> candidates = boosted;
    selected.reserve(finalK);

    // Always take the highest-ranked chunk first.
    selected.append(candidates.takeFirst());

    while (selected.size() < finalK && !candidates.isEmpty()) {
        int bestIdx = 0;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < candidates.size(); ++i) {
            const SearchResult &cand = candidates.at(i);

            // Compute max similarity to any already-selected chunk.
            float maxSimToSelected = 0.0f;
            for (const SearchResult &s : selected) {
                maxSimToSelected = std::max(maxSimToSelected,
                                             bigramJaccard(cand.content, s.content));
            }

            // MMR score: relevance weighted by (1 - max redundancy).
            const float mmr = kMMRLambda * cand.score
                              - (1.0f - kMMRLambda) * maxSimToSelected;
            if (mmr > bestScore) {
                bestScore = mmr;
                bestIdx = i;
            }
        }
        selected.append(candidates.takeAt(bestIdx));
    }

    return selected;
}

// Deduplicate SearchResult chunks against a set of file paths the caller
// already included as extraSources. Removes whole-file matches — a chunk
// of "research/Arakasha.md" is dropped if the caller already sent that
// file as an ActiveDocument extraSource.
QList<SearchResult> dedupAgainstSources(const QList<SearchResult> &results,
                                         const QList<ContextSource> &extraSources,
                                         const QString &activeFilePath)
{
    QSet<QString> excludedPaths;
    for (const ContextSource &src : extraSources) {
        if (!src.citation.isEmpty()) excludedPaths.insert(src.citation);
    }
    if (!activeFilePath.isEmpty()) excludedPaths.insert(activeFilePath);

    QList<SearchResult> kept;
    kept.reserve(results.size());
    for (const SearchResult &r : results) {
        if (excludedPaths.contains(r.filePath)) continue;
        kept.append(r);
    }
    return kept;
}

// Assemble the user-message body: extraSources in priority order, then
// RAG passages with citation headers, then the userPrompt.
//
// Each block is wrapped in clearly-labelled delimiters so the LLM can
// tell sources apart — "--- SOURCE: path (heading) ---" for RAG, and
// the caller-supplied label for extraSources.
QString assembleUserMessage(const RagAssistRequest &request,
                             const QList<SearchResult> &ragPassages)
{
    QStringList blocks;

    // Priority-ordered caller sources first (lower priority value wins).
    QList<ContextSource> sorted = request.extraSources;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const ContextSource &a, const ContextSource &b) {
                         return a.priority < b.priority;
                     });

    for (const ContextSource &src : sorted) {
        if (src.content.isEmpty()) continue;
        QString header;
        if (!src.citation.isEmpty()) {
            header = QStringLiteral("--- %1 (%2) ---").arg(src.label, src.citation);
        } else {
            header = QStringLiteral("--- %1 ---").arg(src.label);
        }
        blocks << header + QLatin1Char('\n') + src.content;
    }

    // Then RAG passages.
    if (!ragPassages.isEmpty()) {
        QStringList ragBlocks;
        ragBlocks << QStringLiteral("--- RETRIEVED PROJECT PASSAGES (semantic search) ---");
        for (const SearchResult &r : ragPassages) {
            const QString header = r.heading.isEmpty()
                ? QStringLiteral("[SOURCE: %1]").arg(r.filePath)
                : QStringLiteral("[SOURCE: %1 §%2]").arg(r.filePath, r.heading);
            ragBlocks << header + QLatin1Char('\n') + r.content;
        }
        blocks << ragBlocks.join(QStringLiteral("\n\n"));
    }

    // Finally the user's actual instruction.
    blocks << QStringLiteral("--- USER REQUEST ---");
    blocks << request.userPrompt;

    return blocks.join(QStringLiteral("\n\n"));
}

} // namespace

// ---------------------------------------------------------------------------

RagAssistService& RagAssistService::instance()
{
    static RagAssistService inst;
    return inst;
}

RagAssistService::RagAssistService(QObject *parent)
    : QObject(parent)
{
}

LLMService* RagAssistService::llmService() const
{
    // Test injection wins when set; otherwise use the global singleton.
    return m_llmOverride ? m_llmOverride : &LLMService::instance();
}

QString RagAssistService::generate(const RagAssistRequest &request,
                                    const RagAssistCallbacks &callbacks)
{
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::RagAssist)) {
        qDebug() << "RAG Assist: generate skipped — disabled for this project.";
        return {};
    }
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    RPGFORGE_DLOG("RAG") << "generate: service=" << request.serviceName
                         << "entity=" << request.entityName
                         << "topK=" << request.topK
                         << "depth=" << (request.depth == SynthesisDepth::Comprehensive
                                         ? "Comprehensive" : "Quick")
                         << "id=" << requestId;

    // Pipeline: expand query (optional) → retrieve+rerank → branch on
    // synthesis depth. Every step is async; each completes via
    // callback before the next fires. Failures in the optional steps
    // fall back to the request's bare query and continue.
    QPointer<RagAssistService> weakThis(this);
    expandQuery(request,
        [weakThis, request, callbacks, requestId](const QString &expandedQuery) {
            if (!weakThis) return;
            RPGFORGE_DLOG("RAG") << "query: original=" << request.ragQuery
                                  << "expanded=" << expandedQuery
                                  << "id=" << requestId;

            weakThis->retrieveAndRank(request, expandedQuery,
                [weakThis, request, callbacks, requestId](QList<SearchResult> passages) {
                    if (!weakThis) return;

                    QStringList paths;
                    paths.reserve(passages.size());
                    for (const SearchResult &r : passages) paths << r.filePath;

                    RPGFORGE_DLOG("RAG") << "retrieval: selected=" << passages.size()
                                          << "extraSources=" << request.extraSources.size()
                                          << "id=" << requestId;
                    if (passages.isEmpty() && request.extraSources.isEmpty()) {
                        RPGFORGE_DLOG("RAG") << "  WARNING: no passages and no extraSources"
                                              << "— LLM will respond without project context";
                    }
                    for (const QString &p : paths) {
                        RPGFORGE_DLOG("RAG") << "  passage:" << p;
                    }
                    for (const ContextSource &src : request.extraSources) {
                        RPGFORGE_DLOG("RAG") << "  extraSource:" << src.label
                                              << "citation=" << src.citation
                                              << "chars=" << src.content.size();
                    }

                    // Notify observers (primarily tests) which sources
                    // made it through retrieval → dedup → rerank.
                    if (callbacks.onPassagesRetrieved) {
                        callbacks.onPassagesRetrieved(requestId, paths);
                    }

                    if (request.depth == SynthesisDepth::Comprehensive) {
                        weakThis->runComprehensive(request, passages, callbacks, requestId);
                    } else {
                        weakThis->dispatchGeneration(request, passages, callbacks, requestId);
                    }
                });
        });

    return requestId;
}

void RagAssistService::expandQuery(const RagAssistRequest &request,
                                    std::function<void(const QString&)> onDone)
{
    const QString baseQuery = request.ragQuery.isEmpty()
        ? (request.entityName.isEmpty()
               ? request.userPrompt
               : request.entityName + QLatin1Char(' ') + request.userPrompt)
        : request.ragQuery;

    // Default: expansion on for Comprehensive, off for Quick.
    const bool enabled = request.enableQueryExpansion.value_or(
        request.depth == SynthesisDepth::Comprehensive);
    if (!enabled) {
        onDone(baseQuery);
        return;
    }

    LLMRequest req;
    req.provider = request.provider;
    req.model = request.model;
    req.serviceName = i18n("Query Expansion");
    req.settingsKey = request.settingsKey;
    req.maxTokens = 256;
    req.temperature = 0.3;
    req.stream = false;
    req.messages << LLMMessage{
        QStringLiteral("system"),
        QStringLiteral(
            "You are a search query expansion assistant for an RPG writing "
            "project's semantic index. Rewrite the user's query to include "
            "aliases, related terms, descriptors, and synonyms that might "
            "help find relevant passages in the author's manuscript, "
            "research notes, and lore. Keep it concise — a single enriched "
            "query string, under 30 words. Output ONLY the expanded query "
            "text — no labels, no explanations, no quotes, no bullet points.")
    };
    req.messages << LLMMessage{QStringLiteral("user"), baseQuery};

    llmService()->sendNonStreamingRequest(req,
        [baseQuery, onDone](const QString &response) {
            const QString expanded = response.trimmed();
            // Fall back to the bare query on any failure — expansion is
            // nice-to-have, not load-bearing.
            onDone(expanded.isEmpty() ? baseQuery : expanded);
        });
}

void RagAssistService::retrieveAndRank(const RagAssistRequest &request,
                                        const QString &expandedQuery,
                                        std::function<void(QList<SearchResult>)> onDone)
{
    QPointer<RagAssistService> weakThis(this);
    KnowledgeBase::instance().search(
        expandedQuery, request.topK, request.activeFilePath,
        [weakThis, request, onDone](const QList<SearchResult> &raw) {
            if (!weakThis) { onDone({}); return; }
            QList<SearchResult> deduped = dedupAgainstSources(
                raw, request.extraSources, request.activeFilePath);
            const QString projectPath = ProjectManager::instance().projectPath();
            QList<SearchResult> selected = rerankAndTrim(
                deduped, request.entityName, projectPath, request.finalK);
            onDone(selected);
        });
}

void RagAssistService::runComprehensive(const RagAssistRequest &request,
                                         const QList<SearchResult> &firstPassages,
                                         const RagAssistCallbacks &callbacks,
                                         const QString &requestId)
{
    // Phase A: draft + gap-list. A non-streaming call with a modest
    // output budget — we only need a draft and a short bullet list.
    LLMRequest draft;
    draft.provider = request.provider;
    draft.model = request.model;
    draft.serviceName = request.serviceName + QStringLiteral(" (Draft)");
    draft.settingsKey = request.settingsKey;
    draft.maxTokens = 2048;
    draft.temperature = request.temperature;
    draft.stream = false;

    const QString baseSystem = composeSystemPrompt(request);
    const QString system = baseSystem + QStringLiteral(
        "\n\nAfter writing your draft answer, identify up to 5 specific "
        "follow-up questions whose answers would improve the draft by "
        "filling in missing context from the project. List them under the "
        "heading INFORMATION GAPS: (one question per line, each as a "
        "bullet). Use this exact output format:\n\n"
        "DRAFT:\n<your draft here>\n\n"
        "INFORMATION GAPS:\n- <question 1>\n- <question 2>\n");
    const QString user = assembleUserMessage(request, firstPassages);

    draft.messages << LLMMessage{QStringLiteral("system"), system};
    for (const LLMMessage &m : request.priorTurns) draft.messages << m;
    draft.messages << LLMMessage{QStringLiteral("user"), user};

    RPGFORGE_DLOG("RAG") << "comprehensive: draft dispatch id=" << requestId;

    QPointer<RagAssistService> weakThis(this);
    llmService()->sendNonStreamingRequest(draft,
        [weakThis, request, callbacks, requestId, firstPassages]
        (const QString &draftResponse) {
            if (!weakThis) return;

            const QStringList gaps = parseGaps(draftResponse);
            RPGFORGE_DLOG("RAG") << "comprehensive: gaps=" << gaps.size()
                                  << "id=" << requestId;

            if (gaps.isEmpty()) {
                // No gaps identified (or the draft call failed). Fall
                // back to a single-pass generation with the original
                // passages.
                weakThis->dispatchGeneration(request, firstPassages, callbacks, requestId);
                return;
            }

            // Phase B: one more retrieval, this time driven by the
            // concatenated gap questions. Single round — we don't chain
            // further hops. The merged passages feed the final pass.
            const QString gapQuery = gaps.join(QStringLiteral(" | "));
            weakThis->retrieveAndRank(request, gapQuery,
                [weakThis, request, callbacks, requestId, firstPassages]
                (QList<SearchResult> gapPassages) {
                    if (!weakThis) return;

                    // Merge passages from both rounds, deduping by
                    // filePath + heading so we don't double-count the
                    // same section if retrieval returned it twice.
                    QList<SearchResult> merged = firstPassages;
                    QSet<QString> seen;
                    for (const SearchResult &p : firstPassages) {
                        seen.insert(p.filePath + QLatin1Char('|') + p.heading);
                    }
                    for (const SearchResult &p : gapPassages) {
                        const QString key = p.filePath + QLatin1Char('|') + p.heading;
                        if (!seen.contains(key)) {
                            merged.append(p);
                            seen.insert(key);
                        }
                    }
                    // Cap at 2× finalK to keep the window bounded.
                    const int cap = request.finalK * 2;
                    if (merged.size() > cap) merged = merged.mid(0, cap);

                    RPGFORGE_DLOG("RAG") << "comprehensive: merged passages="
                                          << merged.size() << "id=" << requestId;
                    weakThis->dispatchGeneration(request, merged, callbacks, requestId);
                });
        });
}

// ---------------------------------------------------------------------------
// Below is the private continuation of generate() — split out so the async
// KB callback doesn't capture a massive lambda. Not declared in the header
// because nothing outside this TU calls it.

void RagAssistService::dispatchGeneration(const RagAssistRequest &request,
                                           const QList<SearchResult> &ragPassages,
                                           const RagAssistCallbacks &callbacks,
                                           const QString &requestId)
{
    LLMRequest llmReq;
    llmReq.provider = request.provider;
    llmReq.model = request.model;
    llmReq.serviceName = request.serviceName;
    llmReq.settingsKey = request.settingsKey;
    llmReq.temperature = request.temperature;
    llmReq.maxTokens = request.maxTokens;
    llmReq.stream = request.stream;

    const QString system = composeSystemPrompt(request);
    const QString user = assembleUserMessage(request, ragPassages);

    RPGFORGE_DLOG("RAG") << "dispatch: systemTokens~" << estimateTokens(system)
                         << "userTokens~" << estimateTokens(user)
                         << "maxOutput=" << request.maxTokens
                         << "id=" << requestId;

    llmReq.messages << LLMMessage{QStringLiteral("system"), system};
    // Conversational history (empty for one-shot callers like LoreKeeper).
    // Inserted between system and the freshly-assembled user turn so the
    // provider's role tracking and conversation-aware prompt caching
    // work as expected.
    for (const LLMMessage &m : request.priorTurns) {
        llmReq.messages << m;
    }
    llmReq.messages << LLMMessage{QStringLiteral("user"), user};

    if (request.stream) {
        // Streaming path: wire LLMService's signals to the caller's
        // callbacks. The connections are one-shot per request; we
        // disconnect once the response finishes or errors.
        QPointer<RagAssistService> weakThis(this);
        auto *llm = llmService();

        // The LLMService emits its own internal requestId when the
        // stream starts. We relay the caller's requestId instead so
        // that the caller can correlate against the value we returned
        // from generate().
        auto chunkConn = std::make_shared<QMetaObject::Connection>();
        auto finishConn = std::make_shared<QMetaObject::Connection>();
        auto errorConn = std::make_shared<QMetaObject::Connection>();

        *chunkConn = connect(llm, &LLMService::responseChunk, this,
            [callbacks, requestId](const QString &, const QString &chunk) {
                if (callbacks.onChunk) callbacks.onChunk(requestId, chunk);
            });
        *finishConn = connect(llm, &LLMService::responseFinished, this,
            [chunkConn, finishConn, errorConn, callbacks, requestId]
            (const QString &, const QString &full) {
                if (callbacks.onComplete) callbacks.onComplete(requestId, full);
                QObject::disconnect(*chunkConn);
                QObject::disconnect(*finishConn);
                QObject::disconnect(*errorConn);
            });
        *errorConn = connect(llm, &LLMService::errorOccurred, this,
            [chunkConn, finishConn, errorConn, callbacks, requestId]
            (const QString &message) {
                if (callbacks.onError) callbacks.onError(requestId, message);
                QObject::disconnect(*chunkConn);
                QObject::disconnect(*finishConn);
                QObject::disconnect(*errorConn);
            });

        llm->sendRequest(llmReq);
    } else {
        // Non-streaming path: one callback at the end with the full
        // text and an (empty-on-success) error string. When the provider
        // returns 429 / auth failure / etc. the detailed callback carries
        // the real provider message so the UI can show "quota exceeded,
        // retry in 3h13m" instead of a generic "Empty response from LLM".
        llmService()->sendNonStreamingRequestDetailed(
            llmReq,
            [callbacks, requestId](const QString &response, const QString &error) {
                if (!error.isEmpty()) {
                    if (callbacks.onError) callbacks.onError(requestId, error);
                    return;
                }
                if (response.isEmpty()) {
                    if (callbacks.onError) {
                        callbacks.onError(requestId, i18n("Empty response from LLM"));
                    }
                    return;
                }
                if (callbacks.onComplete) callbacks.onComplete(requestId, response);
            });
    }
}
