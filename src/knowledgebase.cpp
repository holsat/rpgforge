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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSettings>
#include <QDataStream>
#include <QtMath>
#include <QFileSystemWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QUuid>
#include <KLocalizedString>
#include <QPointer>

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
}

void KnowledgeBase::close()
{
    {
        QMutexLocker locker(&m_dbMutex);
        if (QSqlDatabase::contains(QStringLiteral("rpgforge_vectors"))) {
            QSqlDatabase::removeDatabase(QStringLiteral("rpgforge_vectors"));
        }
    }
    if (m_watcher) {
        QStringList files = m_watcher->files();
        if (!files.isEmpty()) m_watcher->removePaths(files);
        QStringList dirs = m_watcher->directories();
        if (!dirs.isEmpty()) m_watcher->removePaths(dirs);
    }
    m_pendingEmbeddings = 0;
}

void KnowledgeBase::setupDatabase()
{
    QMutexLocker locker(&m_dbMutex);
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
                              "file_hash TEXT,"
                              "embedding BLOB)"));
}

QSqlDatabase KnowledgeBase::getDatabase() const
{
    QString connectionName = QStringLiteral("kb_thread_") + QString::number(size_t(QThread::currentThreadId()));
    if (QSqlDatabase::contains(connectionName)) {
        return QSqlDatabase::database(connectionName);
    }
    
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_dbPath);
    db.open();
    return db;
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
    QString relativePath = QDir(m_projectPath).relativeFilePath(filePath);
    QByteArray currentHash = QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Md5).toHex();

    {
        QMutexLocker locker(&m_dbMutex);
        QSqlDatabase db = getDatabase();
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT file_hash FROM chunks WHERE file_path = ? LIMIT 1"));
            query.addBindValue(relativePath);
            if (query.exec() && query.next()) {
                if (query.value(0).toByteArray() == currentHash) {
                    return; // Content hasn't changed, skip re-indexing
                }
            }
        }
    }

    // Extremely simplified chunker: split by markdown H1/H2
    QStringList rawChunks = content.split(QRegularExpression(QStringLiteral("(?=\\n##? )")));
    
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    
    if (provider == LLMProvider::Anthropic) {
        return;
    }
    
    QString model = settings.value(QStringLiteral("llm/embedding_model"), QString()).toString();

    // Clear old chunks for this file
    {
        QMutexLocker locker(&m_dbMutex);
        QSqlDatabase db = getDatabase();
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM chunks WHERE file_path = ?"));
            query.addBindValue(relativePath);
            query.exec();
        }
    }

    QPointer<KnowledgeBase> weakThis(this);
    for (const QString &chunkText : rawChunks) {
        QString trimmed = chunkText.trimmed();
        if (trimmed.isEmpty()) continue;

        QString heading;
        if (trimmed.startsWith(QLatin1String("# "))) {
            heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(2).trimmed();
        } else if (trimmed.startsWith(QLatin1String("## "))) {
            heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(3).trimmed();
        } else {
            heading = i18n("General");
        }

        m_pendingEmbeddings++;
        Q_EMIT indexingProgress(0, m_pendingEmbeddings);

        LLMService::instance().generateEmbedding(provider, model, trimmed, [weakThis, filePath, heading, trimmed, currentHash](const QVector<float> &embedding) {
            if (!weakThis) return;
            if (!embedding.isEmpty()) {
                weakThis->storeChunk(filePath, heading, trimmed, embedding, currentHash);
            }
            weakThis->m_pendingEmbeddings--;
            if (weakThis->m_pendingEmbeddings <= 0) {
                weakThis->m_pendingEmbeddings = 0;
                Q_EMIT weakThis->indexingFinished();
            }
        });
    }
}

void KnowledgeBase::storeChunk(const QString &filePath, const QString &heading, const QString &content, const QVector<float> &embedding, const QByteArray &fileHash)
{
    QMutexLocker locker(&m_dbMutex);
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) return;

    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << embedding;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO chunks (file_path, heading, content, file_hash, embedding) VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(QDir(m_projectPath).relativeFilePath(filePath));
    query.addBindValue(heading);
    query.addBindValue(content);
    query.addBindValue(fileHash);
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
    if (m_projectPath.isEmpty()) {
        if (callback) callback({});
        return;
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());

    if (provider == LLMProvider::Anthropic) {
        if (callback) callback({});
        return;
    }

    QString model = settings.value(QStringLiteral("llm/embedding_model"), QString()).toString();

    QPointer<KnowledgeBase> weakThis(this);
    LLMService::instance().generateEmbedding(provider, model, queryText, [weakThis, topK, excludeFile, callback](const QVector<float> &queryVector) {
        if (!weakThis || queryVector.isEmpty()) {
            if (callback) callback({});
            return;
        }

        QtConcurrent::run([weakThis, queryVector, topK, excludeFile, callback]() {
            if (!weakThis) return;
            
            QList<SearchResult> results;
            {
                QMutexLocker locker(&weakThis->m_dbMutex);
                QSqlDatabase db = weakThis->getDatabase();
                if (!db.isOpen()) {
                    if (callback) callback({});
                    return;
                }

                QSqlQuery query(db);
                query.prepare(QStringLiteral("SELECT file_path, heading, content, embedding FROM chunks WHERE file_path != ?"));
                query.addBindValue(excludeFile);
                
                if (query.exec()) {
                    while (query.next()) {
                        SearchResult res;
                        res.filePath = query.value(0).toString();
                        res.heading = query.value(1).toString();
                        res.content = query.value(2).toString();
                        
                        QByteArray blob = query.value(3).toByteArray();
                        QVector<float> emb;
                        QDataStream stream(&blob, QIODevice::ReadOnly);
                        stream.setVersion(QDataStream::Qt_6_0);
                        stream >> emb;
                        
                        res.score = weakThis->cosineSimilarity(queryVector, emb);
                        results.append(res);
                    }
                }
            }

            std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
                return a.score > b.score;
            });

            if (results.size() > topK) {
                results = results.mid(0, topK);
            }

            if (callback) callback(results);
        });
    });
}
