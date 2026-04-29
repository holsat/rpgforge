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

#include "projectqaservice.h"

#include "agentgatekeeper.h"
#include "debuglog.h"
#include "projectmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <KLocalizedString>

#include <algorithm>
#include <memory>

// Mutable state shared across all the asynchronous LLM callbacks the
// MAP phase spawns. Wrapped in a shared_ptr so it lives until the
// REDUCE phase completes regardless of when the lambdas fire.
struct ProjectQAService::MapState {
    Request request;
    Callbacks callbacks;
    QStringList files;          // absolute paths, in dispatch order
    QStringList extracts;       // index-aligned with files; empty = irrelevant
    int dispatched = 0;         // how many MAP calls have been started
    int completed = 0;          // how many have returned
    bool errored = false;       // first error short-circuits the rest
};

ProjectQAService& ProjectQAService::instance()
{
    static ProjectQAService inst;
    return inst;
}

ProjectQAService::ProjectQAService(QObject *parent)
    : QObject(parent)
{
}

QString ProjectQAService::stripInvalidCitations(const QString &text,
                                                  const QString &projectPath)
{
    if (projectPath.isEmpty() || text.isEmpty()) return text;
    QDir projectDir(projectPath);
    QString out = text;

    auto pathExists = [&](const QString &raw) {
        QString path = raw.trimmed();
        const int hash = path.indexOf(QLatin1Char('#'));
        if (hash >= 0) path = path.left(hash);
        if (path.startsWith(QStringLiteral("./"))) path = path.mid(2);
        path = path.trimmed();
        if (path.isEmpty()) return false;
        return QFileInfo::exists(projectDir.absoluteFilePath(path));
    };

    auto stripPattern = [&](const QRegularExpression &re) {
        QList<QRegularExpressionMatch> hits;
        auto it = re.globalMatch(out);
        while (it.hasNext()) hits.append(it.next());
        for (int i = hits.size() - 1; i >= 0; --i) {
            const auto &m = hits[i];
            if (pathExists(m.captured(1))) continue;
            int start = m.capturedStart();
            int end = m.capturedEnd();
            if (start > 0 && out.at(start - 1) == QLatin1Char(' ')
                && end < out.size() && out.at(end) == QLatin1Char(' ')) {
                --start;
            }
            out.remove(start, end - start);
        }
    };

    static const QRegularExpression parenRe(
        QStringLiteral(R"(\(([A-Za-z0-9_./\-' ]+\.(?:md|markdown)(?:#[^)]*)?)\))"));
    static const QRegularExpression sourceTagRe(
        QStringLiteral(R"(\[SOURCE:\s*([A-Za-z0-9_./\-' ]+\.(?:md|markdown)(?:#[^\]]*)?)\])"));
    stripPattern(parenRe);
    stripPattern(sourceTagRe);
    return out;
}

QStringList ProjectQAService::enumerateMarkdownFiles(int maxFiles) const
{
    if (!ProjectManager::instance().isProjectOpen()) return {};
    QStringList active = ProjectManager::instance().getActiveFiles();
    QStringList md;
    md.reserve(active.size());
    for (const QString &abs : active) {
        const QString suffix = QFileInfo(abs).suffix().toLower();
        if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
            md.append(abs);
        }
    }
    if (maxFiles > 0 && md.size() > maxFiles) md = md.mid(0, maxFiles);
    return md;
}

void ProjectQAService::ask(const Request &request, const Callbacks &callbacks)
{
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::RagAssist)) {
        if (callbacks.onError) {
            callbacks.onError(i18n("AI Writing Assistant is disabled for this project."));
        }
        return;
    }
    if (request.question.trimmed().isEmpty()) {
        if (callbacks.onError) callbacks.onError(i18n("No question supplied."));
        return;
    }

    QStringList files = enumerateMarkdownFiles(request.maxFiles);
    if (files.isEmpty()) {
        if (callbacks.onError) {
            callbacks.onError(i18n("No markdown files found in the open project."));
        }
        return;
    }

    auto state = std::make_shared<MapState>();
    state->request = request;
    state->callbacks = callbacks;
    state->files = files;
    state->extracts = QStringList(files.size(), QString());

    RPGFORGE_DLOG("PROJQA") << "ask: question=" << request.question
                             << "files=" << files.size()
                             << "concurrency=" << request.concurrency;

    // Kick off the initial wave up to the concurrency limit. Each
    // completion will dispatch the next index in onMapItemDone.
    const int initial = qMin(qMax(1, request.concurrency), files.size());
    for (int i = 0; i < initial; ++i) {
        dispatchMapItem(state, i);
    }
}

void ProjectQAService::dispatchMapItem(const std::shared_ptr<MapState> &state,
                                        int fileIndex)
{
    if (state->errored) return;
    if (fileIndex < 0 || fileIndex >= state->files.size()) return;

    state->dispatched = qMax(state->dispatched, fileIndex + 1);

    const QString filePath = state->files[fileIndex];
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        // Skip silently — treat as irrelevant. A file we can't read
        // shouldn't kill the whole pipeline.
        onMapItemDone(state, fileIndex, QString());
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();

    if (content.size() > state->request.perFileMaxChars) {
        content = content.left(state->request.perFileMaxChars);
    }

    const QString relPath = QDir(ProjectManager::instance().projectPath())
                                .relativeFilePath(filePath);

    LLMRequest req;
    req.provider = state->request.mapProvider.value_or(state->request.provider);
    req.model = state->request.mapModel.isEmpty() ? state->request.model
                                                   : state->request.mapModel;
    req.serviceName = state->request.serviceName.isEmpty()
        ? i18n("Project Q&A — MAP")
        : state->request.serviceName + i18n(" (MAP)");
    req.settingsKey = state->request.settingsKey;
    req.temperature = 0.1;
    req.maxTokens = 1024;
    req.stream = false;

    req.messages << LLMMessage{QStringLiteral("system"), QStringLiteral(
        "You are a search relevance assistant. Given a user question and a "
        "single document from a creative writing project, extract ONLY the "
        "exact sentences from the document that directly inform an answer "
        "to the question. Output rules:\n"
        " - If the document has no directly relevant sentence, output the "
        "single word IRRELEVANT and nothing else.\n"
        " - Otherwise, output the relevant sentences verbatim, separated "
        "by single line breaks. No bullets, no commentary, no paraphrase.\n"
        " - Preserve proper nouns and capitalisation exactly.\n"
        " - Do NOT invent or summarise — only quote what is in the document.")};
    req.messages << LLMMessage{QStringLiteral("user"), QStringLiteral(
        "Question: %1\n\nDocument path: %2\n\n--- BEGIN DOCUMENT ---\n%3\n--- END DOCUMENT ---")
        .arg(state->request.question.trimmed(), relPath, content)};

    QPointer<ProjectQAService> weakThis(this);
    LLMService::instance().sendNonStreamingRequestDetailed(req,
        [weakThis, state, fileIndex](const QString &response, const QString &error) {
            if (!weakThis) return;
            if (state->errored) return;

            QString extract;
            if (error.isEmpty() && !response.isEmpty()) {
                const QString trimmed = response.trimmed();
                // Match the IRRELEVANT sentinel anywhere in the trimmed
                // response — small models occasionally pad it with
                // pleasantries despite the system prompt.
                static const QRegularExpression irrelevantRe(
                    QStringLiteral("\\bIRRELEVANT\\b"),
                    QRegularExpression::CaseInsensitiveOption);
                if (!irrelevantRe.match(trimmed).hasMatch()) {
                    extract = trimmed;
                }
            }
            // Per-file errors are non-fatal. Log and continue with an
            // empty extract — the synthesis pass tolerates gaps.
            if (!error.isEmpty()) {
                RPGFORGE_DLOG("PROJQA") << "MAP error on" << state->files[fileIndex]
                                         << ":" << error;
            }
            weakThis->onMapItemDone(state, fileIndex, extract);
        });
}

void ProjectQAService::onMapItemDone(const std::shared_ptr<MapState> &state,
                                      int fileIndex,
                                      const QString &extract)
{
    if (state->errored) return;

    state->extracts[fileIndex] = extract;
    state->completed += 1;

    // Progress callback: report which file just finished and how many
    // total are done. Empty currentFile signals the MAP phase wrapped.
    if (state->callbacks.onProgress) {
        const QString rel = QDir(ProjectManager::instance().projectPath())
                                .relativeFilePath(state->files[fileIndex]);
        state->callbacks.onProgress(state->completed, state->files.size(), rel);
    }

    // Dispatch the next file (if any) to keep concurrency saturated.
    if (state->dispatched < state->files.size()) {
        dispatchMapItem(state, state->dispatched);
    }

    // All MAP calls complete? Move to REDUCE.
    if (state->completed >= state->files.size()) {
        if (state->callbacks.onProgress) {
            state->callbacks.onProgress(state->completed, state->files.size(), QString());
        }
        runReduce(state);
    }
}

void ProjectQAService::runReduce(const std::shared_ptr<MapState> &state)
{
    // Aggregate non-empty extracts. Order = file enumeration order so
    // citations come out roughly grouped by location.
    QStringList relevantBlocks;
    int relevantFiles = 0;
    QDir projectDir(ProjectManager::instance().projectPath());
    for (int i = 0; i < state->files.size(); ++i) {
        const QString &snippet = state->extracts[i];
        if (snippet.isEmpty()) continue;
        const QString rel = projectDir.relativeFilePath(state->files[i]);
        relevantBlocks << QStringLiteral("[SOURCE: %1]\n%2").arg(rel, snippet);
        ++relevantFiles;
    }

    RPGFORGE_DLOG("PROJQA") << "REDUCE: relevantFiles=" << relevantFiles
                             << "/" << state->files.size();

    if (relevantBlocks.isEmpty()) {
        if (state->callbacks.onComplete) {
            state->callbacks.onComplete(i18n(
                "No documents in the project contained material relevant "
                "to that question. Try rephrasing, or check that the "
                "project is fully indexed."));
        }
        return;
    }

    LLMRequest req;
    req.provider = state->request.provider;
    req.model = state->request.model;
    req.serviceName = state->request.serviceName.isEmpty()
        ? i18n("Project Q&A — Synthesis")
        : state->request.serviceName + i18n(" (Synthesis)");
    req.settingsKey = state->request.settingsKey;
    req.temperature = 0.4;
    req.maxTokens = 4096;
    req.stream = state->request.stream;

    req.messages << LLMMessage{QStringLiteral("system"), QStringLiteral(
        "You are a research assistant synthesising an answer for the "
        "author of a creative writing project. You have been provided "
        "with relevant extracts from the project's manuscript, lore, "
        "and research notes. Synthesise an answer using ONLY those "
        "extracts.\n\n"
        "Rules:\n"
        " - Cite each claim with the path of its source extract using "
        "(path/to/file.md) immediately after the referenced fact. Use "
        "the EXACT path shown in the [SOURCE: ...] header.\n"
        " - Do not invent details that are not in the extracts.\n"
        " - If the extracts contradict each other, surface the "
        "contradiction explicitly.\n"
        " - If the extracts do not actually answer the question, say "
        "so plainly rather than padding.")};
    req.messages << LLMMessage{QStringLiteral("user"), QStringLiteral(
        "Question: %1\n\nExtracts from %2 relevant document(s):\n\n%3")
        .arg(state->request.question.trimmed())
        .arg(relevantFiles)
        .arg(relevantBlocks.join(QStringLiteral("\n\n---\n\n")))};

    if (state->request.stream) {
        // Streaming REDUCE: relay LLMService signals into the caller's
        // chunk/complete/error callbacks. Same one-shot connection
        // pattern as RagAssistService::dispatchGeneration.
        QPointer<ProjectQAService> weakThis(this);
        auto chunkConn = std::make_shared<QMetaObject::Connection>();
        auto finishConn = std::make_shared<QMetaObject::Connection>();
        auto errorConn = std::make_shared<QMetaObject::Connection>();
        auto *llm = &LLMService::instance();

        Callbacks cb = state->callbacks;
        *chunkConn = connect(llm, &LLMService::responseChunk, this,
            [cb](const QString &, const QString &chunk) {
                if (cb.onChunk) cb.onChunk(chunk);
            });
        *finishConn = connect(llm, &LLMService::responseFinished, this,
            [chunkConn, finishConn, errorConn, cb]
            (const QString &, const QString &full) {
                const QString cleaned = stripInvalidCitations(
                    full, ProjectManager::instance().projectPath());
                if (cb.onComplete) cb.onComplete(cleaned);
                QObject::disconnect(*chunkConn);
                QObject::disconnect(*finishConn);
                QObject::disconnect(*errorConn);
            });
        *errorConn = connect(llm, &LLMService::errorOccurred, this,
            [chunkConn, finishConn, errorConn, cb]
            (const QString &message) {
                if (cb.onError) cb.onError(message);
                QObject::disconnect(*chunkConn);
                QObject::disconnect(*finishConn);
                QObject::disconnect(*errorConn);
            });

        llm->sendRequest(req);
    } else {
        Callbacks cb = state->callbacks;
        LLMService::instance().sendNonStreamingRequestDetailed(req,
            [cb](const QString &response, const QString &error) {
                if (!error.isEmpty()) {
                    if (cb.onError) cb.onError(error);
                    return;
                }
                if (response.isEmpty()) {
                    if (cb.onError) cb.onError(i18n("Empty synthesis response."));
                    return;
                }
                const QString cleaned = stripInvalidCitations(
                    response, ProjectManager::instance().projectPath());
                if (cb.onComplete) cb.onComplete(cleaned);
            });
    }
}
