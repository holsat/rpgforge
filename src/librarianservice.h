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

#ifndef LIBRARIANSERVICE_H
#define LIBRARIANSERVICE_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include "librariandatabase.h"

class LLMService;

/**
 * @brief The LibrarianService class handles background project scanning, 
 * data extraction (heuristic and semantic), and database synchronization.
 */
class LibrarianService : public QObject
{
    Q_OBJECT
public:
    explicit LibrarianService(LLMService *llmService, QObject *parent = nullptr);
    ~LibrarianService();

    void setProjectPath(const QString &path);
    bool isPaused() const { return m_paused; }

    LibrarianDatabase* database() const { return m_db; }

public Q_SLOTS:
    void pause();
    void resume();
    void scanAll();
    void scanFile(const QString &filePath);
    void triggerSemanticReindex();

Q_SIGNALS:
    void entityUpdated(qint64 entityId);
    void libraryVariablesChanged(const QMap<QString, QString> &vars);
    void scanningStarted();
    void scanningFinished();
    void errorOccurred(const QString &message);

private Q_SLOTS:
    void onFileChanged(const QString &path);
    void processQueue();
    void runSemanticBatch();
    // Wired to ProjectManager::treeStructureChanged so newly-imported,
    // dragged-in, or otherwise-added files get hashed and queued without
    // waiting for the next full scanAll().
    void onProjectTreeChanged();

private:
    void extractHeuristic(const QString &filePath);
    // The submission hash is captured ONCE at the call site (in
    // runSemanticBatch) so every write inside this function — strike
    // bumps, completion, retry — keys off the same content the LLM was
    // shown. Recomputing the hash inside the completion lambda would
    // attribute analysis of the OLD content to the file's NEW content
    // if the user saved between request and reply.
    void extractSemantic(const QString &filePath, const QString &submissionHash);

    // SHA-256 of the file contents, lowercase hex. Empty string on read
    // failure (logged). Streams the file in chunks so large notes don't
    // load into memory.
    static QString computeFileHash(const QString &path);

    // Heuristic helpers
    void parseMarkdownTables(const QString &content, const QString &sourceFile);
    void parseMarkdownLists(const QString &content, const QString &sourceFile);

    LLMService *m_llmService;
    LibrarianDatabase *m_db;
    QFileSystemWatcher *m_watcher;
    QString m_projectPath;
    QString m_dbPath;

    mutable QRecursiveMutex m_mutex;
    bool m_paused = false;
    QStringList m_pendingFiles;
    // Files awaiting the LLM (semantic) pass. Drained one-per-tick by
    // runSemanticBatch under the LLM rate limit.
    QStringList m_pendingSemantic;
    // Backpressure: true between the moment runSemanticBatch dispatches
    // an LLM call and the moment its completion lambda runs (success or
    // parse-fail). The 30s semantic timer must not pop a new file while
    // this is set, otherwise multiple in-flight librarian LLM calls
    // race each other for DB writes and burn rate-limit budget.
    bool m_semanticInFlight = false;
    // Per-content-hash backoff floor (epoch-ms). After a semantic-pass
    // strike we set the entry to now + 5 minutes; runSemanticBatch must
    // skip any candidate whose hash maps to a future timestamp here.
    // Cleared on success, on strike-cap give-up, and on
    // triggerSemanticReindex. In-memory only — restarts give every file
    // a fresh chance, which is what the persisted strike counter is for.
    QHash<QString, qint64> m_semanticNextAttemptAt;

    QTimer *m_processTimer;
    QTimer *m_semanticTimer;

    bool m_isScanning = false;

    // Whether this service instance has emitted libraryVariablesChanged
    // at least once. The first emit is mandatory even when the
    // processQueue batch was a pure no-op (every file already
    // h=1, s=anything for its hash) so that freshly-constructed
    // observers — VariablesPanel, tests with QSignalSpy — see the
    // current contents of the DB. Subsequent no-op passes skip the
    // emit because nothing changed since the previous one.
    bool m_hasEmittedVarsOnce = false;
};

#endif // LIBRARIANSERVICE_H
