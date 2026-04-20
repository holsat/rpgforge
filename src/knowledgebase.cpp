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
#include "projectmanager.h"
#include "projectkeys.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QSettings>
#include <optional>
#include <QDataStream>
#include <QtMath>
#include <QFileSystemWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QUuid>
#include <KLocalizedString>
#include <QPointer>
#include <QThread>

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

namespace {

// Resolve the LLM provider/model pair KnowledgeBase should use for
// embeddings. Order of precedence:
//   1. llm/embedding_provider  + llm/embedding_model        (explicit)
//   2. llm/provider            + llm/embedding_model        (inherit provider)
//   3. llm/provider            + llm/<provider>/model       (inherit both)
// If the resolved provider is Anthropic (no embeddings API) we return a
// failure sentinel; callers must treat this as "embeddings disabled"
// and emit a diagnostic the user can see.
struct EmbeddingConfig {
    LLMProvider provider;
    QString     model;
    bool        supportsEmbeddings;
    QString     reason;  // human-readable when supportsEmbeddings == false
};

EmbeddingConfig resolveEmbeddingConfig()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    EmbeddingConfig out;

    // Precedence:
    //   1. llm/embedding_provider  (explicit)
    //   2. llm/chat_provider       (if set and not Anthropic — matches
    //                               whatever provider the user just picked
    //                               in the Writing Assistant combo)
    //   3. llm/provider            (global default)
    //   4. Gemini                  (Anthropic-can't-do-embeddings fallback;
    //                               explicit user request — most authors
    //                               configuring Anthropic for chat usually
    //                               also have Gemini available)
    auto pick = [&](const QString &key, bool rejectAnthropic) -> std::optional<LLMProvider> {
        if (!settings.contains(key)) return std::nullopt;
        auto p = static_cast<LLMProvider>(settings.value(key).toInt());
        if (rejectAnthropic && p == LLMProvider::Anthropic) return std::nullopt;
        return p;
    };

    if (auto p = pick(QStringLiteral("llm/embedding_provider"), false)) {
        out.provider = *p;
    } else if (auto p = pick(QStringLiteral("llm/chat_provider"), true)) {
        out.provider = *p;
    } else if (auto p = pick(QStringLiteral("llm/provider"), true)) {
        out.provider = *p;
    } else {
        // Anthropic-or-unset fallback: Gemini.
        out.provider = LLMProvider::Gemini;
        qWarning().noquote()
            << "KnowledgeBase: primary provider does not support embeddings; "
               "falling back to Gemini for RAG. Set llm/embedding_provider "
               "to override.";
    }

    out.model = settings.value(QStringLiteral("llm/embedding_model")).toString();
    if (out.model.isEmpty()) {
        // Try a provider-specific embedding-model setting, then the
        // provider's chat-model as a last resort (works for Ollama/LMStudio
        // whose chat models double as embedding models on many setups;
        // will just fail cleanly with a log line if the model doesn't
        // support embeddings).
        const QString sk = LLMService::providerSettingsKey(out.provider);
        out.model = settings.value(sk + QStringLiteral("/embedding_model")).toString();
        if (out.model.isEmpty()) {
            out.model = settings.value(sk + QStringLiteral("/model")).toString();
        }
    }

    // Anthropic still short-circuits — even if the user explicitly set
    // llm/embedding_provider to Anthropic, there's no endpoint to hit.
    if (out.provider == LLMProvider::Anthropic) {
        out.supportsEmbeddings = false;
        out.reason = QStringLiteral(
            "Anthropic does not provide an embeddings API. Configure "
            "llm/embedding_provider to Gemini, LMStudio, Ollama, or OpenAI.");
        return out;
    }
    if (out.model.isEmpty()) {
        out.supportsEmbeddings = false;
        out.reason = QStringLiteral(
            "No embedding model configured. Set llm/embedding_model or "
            "the provider-specific <provider>/embedding_model in settings.");
        return out;
    }

    out.supportsEmbeddings = true;
    return out;
}

} // namespace

void KnowledgeBase::initForProject(const QString &projectPath)
{
    if (projectPath.isEmpty()) return;

    const EmbeddingConfig emb = resolveEmbeddingConfig();
    qWarning().noquote() << "KnowledgeBase::initForProject:" << projectPath;
    qWarning().noquote() << "KnowledgeBase: embedding provider=" << static_cast<int>(emb.provider)
                         << "model=" << emb.model
                         << "supportsEmbeddings=" << emb.supportsEmbeddings;
    if (!emb.supportsEmbeddings) {
        qWarning().noquote() << "KnowledgeBase: RAG is DISABLED — reason:" << emb.reason;
    }

    QMutexLocker locker(&m_dbMutex);
    close();

    m_projectPath = projectPath;
    m_dbPath = QDir(projectPath).absoluteFilePath(QStringLiteral(".rpgforge-vectors.db"));
    m_pendingFiles.clear();

    if (!setupDatabase()) {
        qWarning() << "KnowledgeBase: Failed to initialize vector database schema.";
    } else {
        qWarning().noquote() << "KnowledgeBase: vector DB ready at" << m_dbPath;
    }
}

void KnowledgeBase::close()
{
    QMutexLocker locker(&m_dbMutex);
    QString connectionName = QStringLiteral("kb_thread_") + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::database(connectionName).close();
        QSqlDatabase::removeDatabase(connectionName);
    }

    if (m_watcher) {
        QStringList files = m_watcher->files();
        if (!files.isEmpty()) m_watcher->removePaths(files);
    }
    m_pendingEmbeddings = 0;
    m_pendingFiles.clear();
}

bool KnowledgeBase::setupDatabase()
{
    QSqlDatabase db = database();
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS chunks ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "file_path TEXT,"
                              "heading TEXT,"
                              "content TEXT,"
                              "file_hash TEXT,"
                              "embedding BLOB)"))) {
        qWarning() << "KnowledgeBase: Failed to create schema:" << query.lastError().text();
        return false;
    }
    return true;
}

QSqlDatabase KnowledgeBase::database() const
{
    // Shutdown-race guard: if QCoreApplication is gone the worker thread
    // is racing program exit. QSqlDatabase::addDatabase() requires qApp
    // and will emit "QSqlDatabase requires a QCoreApplication" otherwise.
    // Return a disconnected handle — callers already check isOpen().
    if (!QCoreApplication::instance()) return QSqlDatabase();
    if (m_dbPath.isEmpty()) return QSqlDatabase();

    QString connectionName = QStringLiteral("kb_thread_") + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    QMutexLocker locker(&m_dbMutex);

    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isOpen()) {
            return db;
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        qWarning() << "KnowledgeBase: Failed to open database on thread" << QThread::currentThreadId() << ":" << db.lastError().text();
    } else {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL;"));
    }
    return db;
}


void KnowledgeBase::indexFile(const QString &filePath)
{
    if (m_projectPath.isEmpty() || filePath.isEmpty()) return;
    
    if (!ProjectManager::instance().getActiveFiles().contains(filePath)) {
        return;
    }

    if (m_paused) {
        if (!m_pendingFiles.contains(filePath)) m_pendingFiles.append(filePath);
        return;
    }

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
        QSqlDatabase db = database();
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT file_hash FROM chunks WHERE file_path = ? LIMIT 1"));
            query.addBindValue(relativePath);
            if (query.exec() && query.next()) {
                if (query.value(0).toByteArray() == currentHash) {
                    return;
                }
            }
        }
    }

    QStringList rawChunks = content.split(QRegularExpression(QStringLiteral("(?=\\n##? )")));

    const EmbeddingConfig emb = resolveEmbeddingConfig();
    if (!emb.supportsEmbeddings) {
        static bool warned = false;
        if (!warned) {
            qWarning().noquote()
                << "KnowledgeBase: RAG indexing is DISABLED —" << emb.reason
                << "— the Writing Assistant will have no project context.";
            warned = true;
        }
        return;
    }
    const LLMProvider provider = emb.provider;
    const QString model = emb.model;

    {
        QSqlDatabase db = database();
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
        if (trimmed.startsWith(QLatin1String("# "))) heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(2).trimmed();
        else if (trimmed.startsWith(QLatin1String("## "))) heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(3).trimmed();
        else heading = i18n("General");

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
    QSqlDatabase db = database();
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

void KnowledgeBase::pause() { m_paused = true; }
void KnowledgeBase::resume()
{
    if (!m_paused) return;
    m_paused = false;
    while (!m_pendingFiles.isEmpty()) indexFile(m_pendingFiles.takeFirst());
}

void KnowledgeBase::search(const QString &queryText, int topK, const QString &excludeFile, std::function<void(const QList<SearchResult>&)> callback)
{
    if (!callback) return;
    if (m_projectPath.isEmpty()) {
        // Honour the callback contract even when the KB isn't initialised
        // for a project — callers (notably LoreKeeperService::updateEntityLore)
        // chain further async work on the callback and will stall forever
        // if it never fires.
        callback({});
        return;
    }

    // Get the list of active files from the project manager to ensure we only return relevant lore.
    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    if (activeFiles.isEmpty()) {
        callback({});
        return;
    }

    // Convert absolute paths to relative for DB matching
    QStringList relativeActiveFiles;
    for (const QString &abs : activeFiles) {
        relativeActiveFiles.append(QDir(m_projectPath).relativeFilePath(abs));
    }

    const EmbeddingConfig emb = resolveEmbeddingConfig();
    if (!emb.supportsEmbeddings) {
        static bool warned = false;
        if (!warned) {
            qWarning().noquote()
                << "KnowledgeBase::search: RAG retrieval is DISABLED —" << emb.reason
                << "— returning empty result list.";
            warned = true;
        }
        callback({});
        return;
    }
    const LLMProvider provider = emb.provider;
    const QString model = emb.model;

    QPointer<KnowledgeBase> weakThis(this);
    LLMService::instance().generateEmbedding(provider, model, queryText, [weakThis, topK, excludeFile, relativeActiveFiles, callback](const QVector<float> &queryVector) {
        if (!weakThis || queryVector.isEmpty()) {
            if (callback) callback({});
            return;
        }

        // Fire-and-forget: we don't hold the QFuture (callback is delivered
        // via QueuedConnection from inside the task). QThreadPool::start
        // expresses that intent directly and avoids the [[nodiscard]]
        // warning from QtConcurrent::run.
        QThreadPool::globalInstance()->start([weakThis, queryVector, topK, excludeFile, relativeActiveFiles, callback]() {
            if (!weakThis) return;
            
            QList<SearchResult> results;
            int totalChunks = 0;
            int filteredByActive = 0;
            {
                QSqlDatabase db = weakThis->database();
                if (db.isOpen()) {
                    QSqlQuery query(db);
                    // Filter chunks to only those belonging to files currently in the project manager's logical tree.
                    QString sql = QStringLiteral("SELECT file_path, heading, content, embedding FROM chunks WHERE file_path != ?");

                    query.prepare(sql);
                    query.addBindValue(excludeFile);
                    if (query.exec()) {
                        while (query.next()) {
                            ++totalChunks;
                            QString chunkPath = query.value(0).toString();
                            if (!relativeActiveFiles.contains(chunkPath)) {
                                ++filteredByActive;
                                continue;
                            }

                            SearchResult res;
                            res.filePath = chunkPath;
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
                    } else {
                        qWarning().noquote() << "KnowledgeBase::search: chunk query failed:"
                                              << query.lastError().text();
                    }
                } else {
                    qWarning().noquote() << "KnowledgeBase::search: DB not open — no chunks to search";
                }
            }
            qWarning().noquote() << "KnowledgeBase::search: totalChunks=" << totalChunks
                                  << "afterActiveFilter=" << (totalChunks - filteredByActive)
                                  << "kept=" << results.size();

            std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
                return a.score > b.score;
            });

            if (results.size() > topK) results = results.mid(0, topK);

            // CRITICAL: Invoke callback on the main thread!
            QMetaObject::invokeMethod(weakThis.data(), [callback, results]() {
                callback(results);
            }, Qt::QueuedConnection);
        });
    });
}

void KnowledgeBase::reindexProject()
{
    if (m_projectPath.isEmpty()) {
        qWarning() << "KnowledgeBase::reindexProject: skipped — no project open";
        return;
    }

    const EmbeddingConfig emb = resolveEmbeddingConfig();
    qWarning().noquote() << "KnowledgeBase::reindexProject: starting";
    qWarning().noquote() << "  embedding provider=" << static_cast<int>(emb.provider)
                          << "model=" << emb.model
                          << "supportsEmbeddings=" << emb.supportsEmbeddings;
    if (!emb.supportsEmbeddings) {
        qWarning().noquote() << "  ABORTING: embeddings disabled —" << emb.reason;
        return;
    }

    QStringList files = ProjectManager::instance().getActiveFiles();
    int mdFiles = 0;
    for (const QString &path : files) {
        QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
            ++mdFiles;
            indexFile(path);
        }
    }
    qWarning().noquote() << "KnowledgeBase::reindexProject: queued" << mdFiles
                          << "markdown files for embedding";
}
