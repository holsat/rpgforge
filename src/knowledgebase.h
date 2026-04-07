/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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

struct SearchResult {
    QString filePath;
    QString heading;
    QString content;
    float score;
};

class QSqlDatabase;
class QFileSystemWatcher;

/**
 * @brief Manages local project knowledge base with vector embeddings.
 */
class KnowledgeBase : public QObject
{
    Q_OBJECT

public:
    static KnowledgeBase& instance();

    /**
     * @brief Initializes the database for the current project.
     */
    void initForProject(const QString &projectPath);

    /**
     * @brief Closes the database.
     */
    void close();

    /**
     * @brief Re-indexes a specific file.
     */
    void indexFile(const QString &filePath);

    /**
     * @brief Searches the knowledge base for similar chunks.
     * @param query The text to search for.
     * @param topK The maximum number of results to return.
     * @param excludeFile Optional file path to exclude from results.
     */
    void search(const QString &query, int topK, const QString &excludeFile, std::function<void(const QList<SearchResult>&)> callback);

Q_SIGNALS:
    void indexingProgress(int current, int total);
    void indexingFinished();

private Q_SLOTS:
    void onFileChanged(const QString &path);

private:
    explicit KnowledgeBase(QObject *parent = nullptr);
    ~KnowledgeBase() override;

    KnowledgeBase(const KnowledgeBase&) = delete;
    KnowledgeBase& operator=(const KnowledgeBase&) = delete;

    void setupDatabase();
    void chunkAndEmbed(const QString &filePath, const QString &content);
    void storeChunk(const QString &filePath, const QString &heading, const QString &content, const QVector<float> &embedding, const QByteArray &fileHash);
    float cosineSimilarity(const QVector<float> &a, const QVector<float> &b);

    QString m_projectPath;
    QString m_dbPath;
    QFileSystemWatcher *m_watcher = nullptr;
    int m_pendingEmbeddings = 0;
};

#endif // KNOWLEDGEBASE_H
