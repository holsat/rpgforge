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

#ifndef ANALYZERSERVICE_H
#define ANALYZERSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QStringList>
#include <QHash>
#include <QTimer>
#include "knowledgebase.h"

struct DiagnosticReference {
    QString filePath;
    int line;
};

enum class DiagnosticSeverity { Error, Warning, Info };

struct Diagnostic {
    QString filePath;
    int line;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    QString message;
    QList<DiagnosticReference> references;
};

/**
 * @brief Service responsible for LLM-powered background analysis and RAG.
 *
 * Persistence: when initForProject() is called, the service opens a
 * per-project SQLite cache (.rpgforge-analyzer.db) keyed by file path
 * and content hash. analyzeDocument() compares the supplied content's
 * hash against the cache and skips the LLM round-trip when they match,
 * re-emitting the previously stored diagnostics. Cached diagnostics are
 * also broadcast on project open so the UI populates immediately
 * instead of waiting for a fresh round of analysis.
 *
 * Background catch-up: kickBackgroundScan() walks the project tree and
 * queues any markdown file whose hash differs from the cache (or has
 * no cache row). Once the queue drains, the service stays idle until
 * the user types in a document — the on-disk cache means the next
 * restart picks up exactly where the last one left off.
 */
class AnalyzerService : public QObject
{
    Q_OBJECT

public:
    static AnalyzerService& instance();

    /**
     * @brief Open the per-project analyzer cache and emit cached
     * diagnostics for every file already on record. Idempotent — safe
     * to call multiple times for the same path. Pass an empty string
     * to closeProject().
     */
    void initForProject(const QString &projectPath);
    void closeProject();

    /**
     * @brief Walk the project's markdown files and queue any whose
     * on-disk content hash differs from the persisted cache. Files
     * that match the cache are skipped — their cached diagnostics
     * were already emitted by initForProject(). Bounded by the
     * existing pause/queue machinery so this can be cancelled by
     * disabling the analyzer per-project.
     */
    void kickBackgroundScan();

    /**
     * @brief Triggers an analysis for the given file and content.
     * Searches KnowledgeBase for RAG context and requests JSON
     * diagnostics from the LLM. If the supplied content's hash
     * matches the cached entry, the LLM call is skipped and the
     * cached diagnostics are emitted directly.
     */
    void analyzeDocument(const QString &filePath, const QString &content);

    void pause();
    void resume();

    void suppressDiagnostic(const QString &message);
    bool isSuppressed(const QString &message) const;

    /**
     * @brief Parses a JSON array response into a list of diagnostics.
     */
    static QList<Diagnostic> parseDiagnostics(const QString &jsonResponse);

Q_SIGNALS:
    void analysisStarted(const QString &filePath);
    void diagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);
    void analysisFailed(const QString &filePath, const QString &error);

private Q_SLOTS:
    void onRagSearchCompleted(const QString &filePath, const QString &content, const QList<struct SearchResult> &results);

private:
    explicit AnalyzerService(QObject *parent = nullptr);
    ~AnalyzerService() override;

    AnalyzerService(const AnalyzerService&) = delete;
    AnalyzerService& operator=(const AnalyzerService&) = delete;

    // ---------- Persistence helpers ----------
    bool ensureSchema();
    static QString hashContent(const QString &content);
    // Returns true and fills `outHash` + `outDiagnostics` if a row
    // exists for filePath. Otherwise leaves outputs untouched.
    bool loadCacheRow(const QString &filePath,
                      QString &outHash,
                      QList<Diagnostic> &outDiagnostics) const;
    void storeCacheRow(const QString &filePath,
                       const QString &hash,
                       const QList<Diagnostic> &diagnostics);
    void emitAllCached();
    static QString diagnosticsToJson(const QList<Diagnostic> &diagnostics);
    static QList<Diagnostic> diagnosticsFromJson(const QString &json);

    // ---------- State ----------
    QString m_activeAnalysisFile;
    bool m_paused = false;
    struct PendingAnalysis {
        QString filePath;
        QString content;
    };
    QList<PendingAnalysis> m_queue;
    QStringList m_suppressionList;

    // Project-scoped persistence. Empty until initForProject() is
    // called; analyzeDocument and friends fall back to in-memory only
    // when m_dbPath is empty (e.g. during unit tests).
    QString m_projectPath;
    QString m_dbPath;

    // Background catch-up: paces the queue draining so we don't dump
    // the entire project on the LLM at once. Each tick takes one item
    // off the queue and runs it — but only if no analysis is already
    // in flight.
    QTimer *m_bgTimer = nullptr;
};

#endif // ANALYZERSERVICE_H
