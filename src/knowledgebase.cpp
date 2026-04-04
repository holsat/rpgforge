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

#include "knowledgebase.h"
#include "llmservice.h"
#include "markdownparser.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileSystemWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QDebug>
#include <QtMath>
#include <QByteArray>
#include <QDataStream>
#include <QRegularExpression>
#include <KLocalizedString>

KnowledgeBase& KnowledgeBase::instance()
{
    static KnowledgeBase inst;
    return inst;
}

KnowledgeBase::KnowledgeBase(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &KnowledgeBase::onFileChanged);
}

KnowledgeBase::~KnowledgeBase()
{
    close();
}

void KnowledgeBase::initForProject(const QString &projectPath)
{
    close();

    m_projectPath = projectPath;
    m_dbPath = QDir(projectPath).absoluteFilePath(QStringLiteral(".rpgforge-vectors.db"));

    setupDatabase();

    // Optionally index all markdown files if they haven't been indexed
    // For now, we'll just set up the watcher on all .md files in the project
    // A complete implementation would recursively scan the project and index missing files.
}

void KnowledgeBase::close()
{
    if (QSqlDatabase::contains(QStringLiteral("rpgforge_vectors"))) {
        QSqlDatabase::removeDatabase(QStringLiteral("rpgforge_vectors"));
    }
    if (m_watcher) {
        m_watcher->removePaths(m_watcher->files());
        m_watcher->removePaths(m_watcher->directories());
    }
    m_pendingEmbeddings = 0;
}

void KnowledgeBase::setupDatabase()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rpgforge_vectors"));
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        qWarning() << "Failed to open vector database:" << db.lastError().text();
        return;
    }

    QSqlQuery query(db);
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS chunks ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "file_path TEXT,"
                              "heading TEXT,"
                              "content TEXT,"
                              "embedding BLOB)"));
}

void KnowledgeBase::indexFile(const QString &filePath)
{
    if (m_projectPath.isEmpty()) return;
    
    // Make sure it's being watched
    if (!m_watcher->files().contains(filePath)) {
        m_watcher->addPath(filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QString content = QString::fromUtf8(file.readAll());
    chunkAndEmbed(filePath, content);
}

void KnowledgeBase::onFileChanged(const QString &path)
{
    indexFile(path);
}

void KnowledgeBase::chunkAndEmbed(const QString &filePath, const QString &content)
{
    // Extremely simplified chunker: split by markdown H1/H2
    QStringList rawChunks = content.split(QRegularExpression(QStringLiteral("(?=\\n##? )")));
    
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    
    // Anthropic doesn't support embeddings in our code right now
    if (provider == LLMProvider::Anthropic) {
        qWarning() << "Anthropic selected, skipping embeddings.";
        return;
    }
    
    QString model = settings.value(QStringLiteral("llm/embedding_model"), QString()).toString();

    // Clear old chunks for this file
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("rpgforge_vectors"));
    if (db.isOpen()) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("DELETE FROM chunks WHERE file_path = ?"));
        query.addBindValue(QDir(m_projectPath).relativeFilePath(filePath));
        query.exec();
    }

    for (const QString &chunkText : rawChunks) {
        QString trimmed = chunkText.trimmed();
        if (trimmed.isEmpty()) continue;

        // Try to extract a heading name
        QString heading;
        if (trimmed.startsWith(QLatin1String("# "))) {
            heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(2).trimmed();
        } else if (trimmed.startsWith(QLatin1String("## "))) {
            heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(3).trimmed();
        } else {
            heading = i18n("General");
        }

        m_pendingEmbeddings++;
        Q_EMIT indexingProgress(0, m_pendingEmbeddings); // just indicating busy

        LLMService::instance().generateEmbedding(provider, model, trimmed, [this, filePath, heading, trimmed](const QVector<float> &embedding) {
            if (!embedding.isEmpty()) {
                storeChunk(filePath, heading, trimmed, embedding);
            }
            m_pendingEmbeddings--;
            if (m_pendingEmbeddings <= 0) {
                m_pendingEmbeddings = 0;
                Q_EMIT indexingFinished();
            }
        });
    }
}

void KnowledgeBase::storeChunk(const QString &filePath, const QString &heading, const QString &content, const QVector<float> &embedding)
{
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("rpgforge_vectors"));
    if (!db.isOpen()) return;

    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << embedding;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO chunks (file_path, heading, content, embedding) VALUES (?, ?, ?, ?)"));
    query.addBindValue(QDir(m_projectPath).relativeFilePath(filePath));
    query.addBindValue(heading);
    query.addBindValue(content);
    query.addBindValue(blob);
    query.exec();
}

float KnowledgeBase::cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.isEmpty() || a.size() != b.size()) return 0.0f;

    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (int i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    
    if (normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (qSqrt(normA) * qSqrt(normB));
}

void KnowledgeBase::search(const QString &queryText, int topK, const QString &excludeFile, std::function<void(const QList<SearchResult>&)> callback)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    
    if (provider == LLMProvider::Anthropic) {
        if (callback) callback({});
        return;
    }

    QString model = settings.value(QStringLiteral("llm/embedding_model"), QString()).toString();

    LLMService::instance().generateEmbedding(provider, model, queryText, [this, topK, excludeFile, callback](const QVector<float> &queryVector) {
        if (queryVector.isEmpty()) {
            if (callback) callback({});
            return;
        }

        QSqlDatabase db = QSqlDatabase::database(QStringLiteral("rpgforge_vectors"));
        if (!db.isOpen()) {
            if (callback) callback({});
            return;
        }

        QList<SearchResult> results;
        QString excludeRel = excludeFile.isEmpty() ? QString() : QDir(m_projectPath).relativeFilePath(excludeFile);

        QSqlQuery query(db);
        if (excludeRel.isEmpty()) {
            query.exec(QStringLiteral("SELECT file_path, heading, content, embedding FROM chunks"));
        } else {
            query.prepare(QStringLiteral("SELECT file_path, heading, content, embedding FROM chunks WHERE file_path != ?"));
            query.addBindValue(excludeRel);
            query.exec();
        }

        while (query.next()) {
            QString path = query.value(0).toString();
            QString head = query.value(1).toString();
            QString cont = query.value(2).toString();
            QByteArray blob = query.value(3).toByteArray();

            QVector<float> vec;
            QDataStream stream(&blob, QIODevice::ReadOnly);
            stream.setVersion(QDataStream::Qt_6_0);
            stream >> vec;

            float sim = cosineSimilarity(queryVector, vec);
            
            // Basic filtering threshold could be added here
            if (sim > 0.5f) { 
                results.append({path, head, cont, sim});
            }
        }

        // Sort descending
        std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
            return a.score > b.score;
        });

        // Top K
        if (results.size() > topK) {
            results = results.mid(0, topK);
        }

        if (callback) {
            callback(results);
        }
    });
}
