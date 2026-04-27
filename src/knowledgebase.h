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

#ifndef KNOWLEDGEBASE_H
#define KNOWLEDGEBASE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QList>
#include <QSqlDatabase>
#include <QRecursiveMutex>
#include <functional>

class QFileSystemWatcher;

struct SearchResult {
    QString filePath;
    QString heading;
    QString content;
    float score;
};

/**
 * @brief Manages a local SQLite-based vector index for semantic search.
 */
class KnowledgeBase : public QObject
{
    Q_OBJECT

public:
    static KnowledgeBase& instance();

    void initForProject(const QString &projectPath);
    void close();

    void indexFile(const QString &filePath);
    void reindexProject();

    /**
     * @brief Walks every chunk in the vector store and links it to any
     * entity whose canonical name or alias appears in the chunk text.
     * Writes to chunk_entities. Idempotent (INSERT OR IGNORE).
     *
     * The alias index is loaded from the librarian DB at
     * `<project>/.rpgforge.db` (sorted by alias length descending so
     * "Captain Ryzen" matches before "Ryzen"). Word-boundary aware so
     * "Ryz" doesn't false-match inside "Ryzen".
     *
     * Returns the total number of (chunk, entity) links created or
     * already-present after the pass. 0 when there are no entities to
     * link against (project hasn't been librarian-scanned yet).
     */
    int rebuildChunkEntityLinks();
    void search(const QString &queryText, int topK, const QString &excludeFile, std::function<void(const QList<SearchResult>&)> callback);
    /// Hybrid retrieval: runs the existing vector search in parallel with a
    /// ripgrep literal-match search across the project's markdown corpus, then
    /// merges and de-duplicates the results. Vector search ranks by cosine
    /// similarity; ripgrep results are ranked by hit count + extracted chunk
    /// length and surfaced as additional SearchResults. The caller's callback
    /// receives at most `topK` merged results.
    ///
    /// Why: pure embedding-based RAG misses on proper-noun queries — fiction
    /// is full of made-up names ("Vål'naden", "Sifacsi") that the embedding
    /// model has never seen, so they collapse to near-identical low-info
    /// vectors. Literal grep catches those exact mentions reliably.
    ///
    /// Falls back to vector-only when `rg` is not on PATH.
    void hybridSearch(const QString &queryText, int topK, const QString &excludeFile,
                      std::function<void(const QList<SearchResult>&)> callback);

    void pause();
    void resume();

    /**
     * @brief Dispatches an embedding request through the user's ordered
     * provider fallback chain (llm/provider_order + per-provider
     * embedding_model). On failure or cooldown, tries the next enabled +
     * configured provider until one succeeds or the chain is exhausted.
     * The callback receives the embedding vector on success, or an empty
     * vector if every candidate in the chain failed.
     */
    void generateEmbeddingWithFallback(const QString &text,
                                       std::function<void(const QVector<float>&)> callback);

    /**
     * @brief Extract proper-noun-style keywords from a free-form query.
     * Strips interrogatives (who/how/why/...), short auxiliaries
     * (is/are/was/were/...) and a list of generic prose tokens before
     * picking up to 5 capitalised, non-ASCII, or 5+ char tokens. The
     * stopword list is what makes "Who is Ryzen?" yield ["Ryzen"]
     * rather than ["Who","Ryzen"] — the latter drowns ripgrep with
     * common-word matches before any proper-noun candidate is reached.
     * Public for unit testing of the retrieval-quality regression
     * suite; production callers go through hybridSearch().
     */
    static QStringList extractKeywordTokens(const QString &query);

    /**
     * @brief Sentence-aware sliding-window subdivision of a long text
     * into overlapping chunks. Snaps cut points to "\n\n" or ". "
     * boundaries within the overlap region; falls back to a hard cut
     * if no boundary is found. Texts at or under maxChars are returned
     * as a single-element list. Public for unit testing.
     */
    static QStringList subdivideChunk(const QString &text, int maxChars, int overlap);

Q_SIGNALS:
    void indexingProgress(int current, int total);
    void indexingFinished();

private Q_SLOTS:
    void onFileChanged(const QString &path);

private:
    explicit KnowledgeBase(QObject *parent = nullptr);
    ~KnowledgeBase();

    bool setupDatabase();
    void chunkAndEmbed(const QString &filePath, const QString &content);
    void storeChunk(const QString &filePath, const QString &heading, const QString &content, const QVector<float> &embedding, const QByteArray &fileHash);
    float cosineSimilarity(const QVector<float> &a, const QVector<float> &b);
    
    QSqlDatabase database() const;

    QString m_projectPath;
    QString m_dbPath;
    QFileSystemWatcher *m_watcher = nullptr;
    int m_pendingEmbeddings = 0;
    mutable QRecursiveMutex m_dbMutex;
    bool m_paused = false;
    QStringList m_pendingFiles;
};

#endif // KNOWLEDGEBASE_H
