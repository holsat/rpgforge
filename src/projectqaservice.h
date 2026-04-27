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

#ifndef PROJECTQASERVICE_H
#define PROJECTQASERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

#include "llmservice.h"

/**
 * \file projectqaservice.h
 *
 * Whole-corpus question answering. Implements the MAP/REDUCE pipeline:
 * SPLIT (enumerate every markdown file in the project) → MAP (per-file
 * cheap-model LLM call to extract relevance) → REDUCE (single
 * synthesis call over the union of relevant extracts).
 *
 * Use cases the existing single-pass RAG flow cannot answer because
 * the relevant evidence is distributed across the whole corpus rather
 * than concentrated in a few top-K chunks:
 *   - "List every named character with one personality trait each."
 *   - "Find every contradiction in the geography descriptions."
 *   - "Summarise the magic system rules and where they're documented."
 *
 * Cost shape: N cheap-model calls + 1 synthesis call. For ~50 files
 * at ~5K tokens each on Gemini Flash, that is roughly $0.10 a query.
 * Callers are expected to gate this behind explicit user opt-in.
 */
class ProjectQAService : public QObject
{
    Q_OBJECT

public:
    static ProjectQAService& instance();

    struct Request {
        // Which provider/model to use for both MAP and REDUCE. The
        // service uses the same model for both phases by default; if
        // you want a cheap MAP and an expensive REDUCE, you can split
        // them via the optional override fields below.
        LLMProvider provider = LLMProvider::OpenAI;
        QString model;
        QString settingsKey;            // For model-replacement UI on failure
        QString serviceName;            // User-visible label

        // The user's question — passed verbatim into both phases.
        QString question;

        // Streaming control for the REDUCE phase. The MAP phase is
        // always non-streaming (we need the whole snippet before
        // judging relevance).
        bool stream = true;

        // Per-file content cap (chars). Files longer than this are
        // truncated to the prefix. Keeps each MAP call inside the
        // cheap-model context window.
        int perFileMaxChars = 8000;

        // Hard ceiling on number of files dispatched. Bound on cost.
        int maxFiles = 200;

        // Maximum simultaneous in-flight MAP calls. Higher = faster
        // wall-clock but more chance of hitting provider rate limits.
        int concurrency = 4;

        // Optional split: when set, the MAP phase uses these instead
        // of provider/model above. Leave empty to use the same model
        // for both phases.
        std::optional<LLMProvider> mapProvider;
        QString mapModel;
    };

    struct Callbacks {
        // Called as each MAP call completes. processed/total are file
        // counts; currentFile is the relative path of the file just
        // finished (or empty when the MAP phase wraps up).
        std::function<void(int processed, int total, const QString &currentFile)> onProgress;

        // Streaming chunks during the REDUCE phase. Empty when
        // request.stream is false.
        std::function<void(const QString &chunk)> onChunk;

        // Final answer text — fired exactly once on success.
        std::function<void(const QString &fullText)> onComplete;

        // Fired exactly once on failure. If onError fires, onComplete
        // does not (and vice versa).
        std::function<void(const QString &message)> onError;
    };

    /**
     * \brief Kick off the MAP/REDUCE pipeline.
     *
     * Returns immediately; the callbacks fire asynchronously on the
     * main thread. Honors AgentGatekeeper::Service::RagAssist — if the
     * service is disabled for the current project, onError fires
     * synchronously.
     */
    void ask(const Request &request, const Callbacks &callbacks);

private:
    explicit ProjectQAService(QObject *parent = nullptr);
    ~ProjectQAService() override = default;
    ProjectQAService(const ProjectQAService&) = delete;
    ProjectQAService& operator=(const ProjectQAService&) = delete;

    // Strip out (path/file.md) and [SOURCE: path/file.md] citations
    // whose path doesn't exist in the project tree. Same logic as
    // RagAssistService — duplicated here because the synthesis pass
    // dispatches through LLMService directly rather than going
    // through RagAssistService::dispatchGeneration. Kept private and
    // local so the canonical implementation in RagAssistService stays
    // the source of truth and we don't grow a public utility surface.
    static QString stripInvalidCitations(const QString &text,
                                          const QString &projectPath);

    // SPLIT: enumerate every project markdown and slice to maxFiles.
    // Returns absolute paths.
    QStringList enumerateMarkdownFiles(int maxFiles) const;

    // Per-MAP-call: read file, build prompt, dispatch LLM, invoke
    // onMapItemDone with the extract (or empty for irrelevant).
    struct MapState; // see .cpp
    void dispatchMapItem(const std::shared_ptr<MapState> &state,
                          int fileIndex);
    void onMapItemDone(const std::shared_ptr<MapState> &state,
                        int fileIndex,
                        const QString &extract);
    void runReduce(const std::shared_ptr<MapState> &state);
};

#endif // PROJECTQASERVICE_H
