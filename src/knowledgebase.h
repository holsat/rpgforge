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
    void search(const QString &queryText, int topK, const QString &excludeFile, std::function<void(const QList<SearchResult>&)> callback);

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
