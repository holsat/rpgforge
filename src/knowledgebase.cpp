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

#include "knowledgebase.h"
#include "llmservice.h"
#include "projectmanager.h"
#include "projectkeys.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
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
// ---------------------------------------------------------------------------
// Build the user-ordered embedding chain. Walks llm/provider_order in order,
// skipping disabled / unconfigured providers and entries that have no
// embedding-model setting. Anthropic is always skipped — no embeddings API.
// Returned pairs are {provider, embedding-model}.
// ---------------------------------------------------------------------------
QList<QPair<LLMProvider, QString>> resolveEmbeddingChain()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QList<QPair<LLMProvider, QString>> chain;
    for (LLMProvider p : LLMService::readProviderOrderFromSettings()) {
        if (p == LLMProvider::Anthropic) continue;       // no embeddings endpoint
        if (!LLMService::isProviderEnabled(p)) continue; // user turned it off
        if (!LLMService::instance().isProviderConfigured(p)) continue;
        const QString model = settings.value(
            LLMService::providerSettingsKey(p)
            + QStringLiteral("/embedding_model")).toString().trimmed();
        if (model.isEmpty()) continue;
        chain.append(qMakePair(p, model));
    }
    return chain;
}

// Helper for log output — joins chain into a short human-readable string.
QString describeChain(const QList<QPair<LLMProvider, QString>> &chain)
{
    if (chain.isEmpty()) return QStringLiteral("<empty>");
    QStringList parts;
    for (const auto &e : chain) {
        parts.append(QStringLiteral("%1/%2")
                         .arg(LLMService::providerName(e.first), e.second));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

// ---------------------------------------------------------------------------
// KnowledgeBase::generateEmbeddingWithFallback
// Walks the user-configured embedding chain. For each candidate: skip if
// currently cooled down (from a previous 429), otherwise call
// LLMService::generateEmbedding. On empty vector (request failed),
// recurse to the next candidate. Invokes the user's callback with an
// empty vector only if the full chain is exhausted.
// ---------------------------------------------------------------------------
void KnowledgeBase::generateEmbeddingWithFallback(
    const QString &text, std::function<void(const QVector<float>&)> callback)
{
    auto chainPtr = std::make_shared<QList<QPair<LLMProvider, QString>>>(
        resolveEmbeddingChain());
    if (chainPtr->isEmpty()) {
        qWarning().noquote()
            << "KnowledgeBase: embedding chain is empty — no provider has an "
               "embedding model configured. RAG request skipped.";
        if (callback) callback({});
        return;
    }

    // Recursive self-referential lambda; shared_ptr keeps the function
    // alive across async hops through LLMService callbacks.
    auto tryOne = std::make_shared<std::function<void(int)>>();
    QPointer<KnowledgeBase> weakThis(this);
    *tryOne = [weakThis, chainPtr, text, callback, tryOne](int idx) {
        if (!weakThis || idx >= chainPtr->size()) {
            if (callback) callback({});
            return;
        }
        const auto &p = (*chainPtr)[idx];
        // Skip cooled-down pairs without network traffic. qDebug (not
        // qWarning) because `recordCooldown` already logs a one-shot warning
        // when the cooldown is set — a reminder per embedding request turns
        // the log into a wall of spam during reindex (67 files × N chunks).
        if (LLMService::instance().isCooledDown(p.first, p.second)) {
            qDebug().noquote()
                << "KnowledgeBase: embedding candidate"
                << LLMService::providerName(p.first) << "/" << p.second
                << "is cooling down; trying next.";
            (*tryOne)(idx + 1);
            return;
        }
        LLMService::instance().generateEmbedding(
            p.first, p.second, text,
            [tryOne, callback, idx, provider = p.first, model = p.second](const QVector<float> &vec) {
                if (!vec.isEmpty()) {
                    if (idx > 0) {
                        // Fallback worked — log once so the user knows which
                        // candidate actually served the request when the
                        // preferred one(s) were cooled down or broken.
                        qInfo().noquote()
                            << "KnowledgeBase: embedding served by fallback candidate"
                            << LLMService::providerName(provider) << "/" << model;
                    }
                    if (callback) callback(vec);
                    return;
                }
                qWarning().noquote()
                    << "KnowledgeBase: embedding failed on"
                    << LLMService::providerName(provider) << "/" << model
                    << "— falling back to next candidate.";
                (*tryOne)(idx + 1);
            });
    };
    (*tryOne)(0);
}

void KnowledgeBase::initForProject(const QString &projectPath)
{
    if (projectPath.isEmpty()) return;

    const auto chain = resolveEmbeddingChain();
    qWarning().noquote() << "KnowledgeBase::initForProject:" << projectPath;
    qWarning().noquote() << "KnowledgeBase: embedding chain:" << describeChain(chain);
    if (chain.isEmpty()) {
        qWarning().noquote()
            << "KnowledgeBase: RAG is DISABLED — no provider has an embedding "
               "model configured. Open Settings → LLM and set an embedding "
               "model for at least one enabled provider.";
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

    // Phase 1 of the entity-graph work: chunk↔entity linkage. Index lives
    // in the vector DB so a single SQL JOIN can pull "all chunks linked to
    // any entity in this set" during graph-augmented retrieval (Phase 3).
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS chunk_entities ("
                              "chunk_id INTEGER NOT NULL,"
                              "entity_id INTEGER NOT NULL,"
                              "PRIMARY KEY (chunk_id, entity_id),"
                              "FOREIGN KEY(chunk_id) REFERENCES chunks(id) ON DELETE CASCADE)"))) {
        qWarning() << "KnowledgeBase: Failed to create chunk_entities:"
                   << query.lastError().text();
        return false;
    }
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_chunkent_entity "
                               "ON chunk_entities(entity_id)"));
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

    // Chain is resolved per-chunk via generateEmbeddingWithFallback, but we
    // do a quick pre-check here so we can log a single DISABLED warning
    // instead of one per chunk when nothing is configured.
    if (resolveEmbeddingChain().isEmpty()) {
        static bool warned = false;
        if (!warned) {
            qWarning().noquote()
                << "KnowledgeBase: RAG indexing is DISABLED — no provider has "
                   "an embedding model configured. Open Settings → LLM.";
            warned = true;
        }
        return;
    }

    {
        QSqlDatabase db = database();
        if (db.isOpen()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM chunks WHERE file_path = ?"));
            query.addBindValue(relativePath);
            query.exec();
        }
    }

    // Soft cap and overlap for sentence-aware sub-division of long
    // sections. ~1500 chars stays comfortably inside the 512-token
    // window used by mainstream embedding models. 200-char overlap
    // is enough to keep a sentence that straddles a boundary
    // (~3 short sentences) fully indexed in at least one chunk.
    constexpr int kMaxChunkChars = 1500;
    constexpr int kChunkOverlap  = 200;

    QPointer<KnowledgeBase> weakThis(this);
    for (const QString &chunkText : rawChunks) {
        QString trimmed = chunkText.trimmed();
        if (trimmed.isEmpty()) continue;

        QString heading;
        if (trimmed.startsWith(QLatin1String("# "))) heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(2).trimmed();
        else if (trimmed.startsWith(QLatin1String("## "))) heading = trimmed.section(QLatin1Char('\n'), 0, 0).mid(3).trimmed();
        else heading = i18n("General");

        // Break long sections into overlapping sub-chunks. Short
        // sections come out as a single-element list so the existing
        // heading-bounded behaviour is preserved.
        const QStringList subChunks =
            subdivideChunk(trimmed, kMaxChunkChars, kChunkOverlap);

        for (const QString &sub : subChunks) {
            m_pendingEmbeddings++;
            Q_EMIT indexingProgress(0, m_pendingEmbeddings);

            generateEmbeddingWithFallback(sub, [weakThis, filePath, heading, sub, currentHash](const QVector<float> &embedding) {
                if (!weakThis) return;
                if (!embedding.isEmpty()) {
                    weakThis->storeChunk(filePath, heading, sub, embedding, currentHash);
                }
                weakThis->m_pendingEmbeddings--;
                if (weakThis->m_pendingEmbeddings <= 0) {
                    weakThis->m_pendingEmbeddings = 0;
                    Q_EMIT weakThis->indexingFinished();
                }
            });
        }
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

    if (resolveEmbeddingChain().isEmpty()) {
        static bool warned = false;
        if (!warned) {
            qWarning().noquote()
                << "KnowledgeBase::search: RAG retrieval is DISABLED — no "
                   "provider has an embedding model configured. Returning "
                   "empty result list.";
            warned = true;
        }
        callback({});
        return;
    }

    QPointer<KnowledgeBase> weakThis(this);
    generateEmbeddingWithFallback(queryText, [weakThis, topK, excludeFile, relativeActiveFiles, callback](const QVector<float> &queryVector) {
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

QStringList KnowledgeBase::subdivideChunk(const QString &text, int maxChars, int overlap)
{
    QStringList out;
    if (text.size() <= maxChars) {
        out.append(text);
        return out;
    }
    int start = 0;
    while (start < text.size()) {
        int end = qMin(text.size(), start + maxChars);
        if (end < text.size()) {
            const int searchFloor = qMax(start + maxChars - overlap, start + 1);
            const int paraEnd = text.lastIndexOf(QStringLiteral("\n\n"), end);
            const int sentEnd = text.lastIndexOf(QStringLiteral(". "), end);
            const int boundary = qMax(paraEnd, sentEnd);
            if (boundary >= searchFloor) {
                end = boundary + 1;
            }
        }
        out.append(text.mid(start, end - start));
        if (end >= text.size()) break;
        start = qMax(end - overlap, start + 1);
    }
    return out;
}

QStringList KnowledgeBase::extractKeywordTokens(const QString &query)
{
    QStringList keywords;

    // Quoted phrases first.
    static const QRegularExpression quoteRe(QStringLiteral("\"([^\"]+)\""));
    auto quoteIt = quoteRe.globalMatch(query);
    while (quoteIt.hasNext()) {
        const auto m = quoteIt.next();
        const QString phrase = m.captured(1).trimmed();
        if (!phrase.isEmpty()) keywords.append(phrase);
    }

    static const QSet<QString> stopWords = {
        // Interrogatives — these match a huge fraction of an English
        // corpus and drown out rare proper nouns when OR-joined into a
        // single ripgrep pattern.
        QStringLiteral("who"),   QStringLiteral("whom"),  QStringLiteral("whose"),
        QStringLiteral("what"),  QStringLiteral("when"),  QStringLiteral("where"),
        QStringLiteral("which"), QStringLiteral("why"),   QStringLiteral("how"),
        // Common short verbs / auxiliaries — same problem as the
        // interrogatives. The 3-char minimum already drops "is/be/to/of"
        // but capitalized forms (start of sentence) would still leak
        // through; explicit listing handles that.
        QStringLiteral("are"),   QStringLiteral("was"),   QStringLiteral("were"),
        QStringLiteral("has"),   QStringLiteral("had"),   QStringLiteral("does"),
        QStringLiteral("did"),   QStringLiteral("can"),   QStringLiteral("will"),
        QStringLiteral("may"),   QStringLiteral("might"), QStringLiteral("shall"),
        QStringLiteral("must"),  QStringLiteral("been"),  QStringLiteral("being"),
        QStringLiteral("said"),  QStringLiteral("says"),  QStringLiteral("get"),
        QStringLiteral("got"),   QStringLiteral("make"),  QStringLiteral("made"),
        // Determiners / conjunctions / generic prose words.
        QStringLiteral("tell"),  QStringLiteral("about"), QStringLiteral("with"),
        QStringLiteral("this"),  QStringLiteral("that"),  QStringLiteral("there"),
        QStringLiteral("these"), QStringLiteral("those"), QStringLiteral("they"),
        QStringLiteral("them"),  QStringLiteral("their"), QStringLiteral("here"),
        QStringLiteral("have"),  QStringLiteral("would"), QStringLiteral("could"),
        QStringLiteral("should"),QStringLiteral("from"),  QStringLiteral("into"),
        QStringLiteral("some"),  QStringLiteral("more"),  QStringLiteral("most"),
        QStringLiteral("each"),  QStringLiteral("just"),  QStringLiteral("also"),
        QStringLiteral("then"),  QStringLiteral("only"),  QStringLiteral("very"),
        QStringLiteral("real"),  QStringLiteral("create"),QStringLiteral("examples"),
        QStringLiteral("example"),QStringLiteral("scenarios"),
        QStringLiteral("characters"), QStringLiteral("everything"),
        QStringLiteral("test"),
    };

    // PCRE2 (Qt's regex engine) does not accept "\uXXXX"; it requires
    // "\x{XXXX}" for explicit hex codepoints. The previous "À"
    // form silently invalidated the entire pattern, so this iterator
    // produced ZERO tokens — meaning the ripgrep grep arm of
    // hybridSearch never actually ran. Caught by test_ragquality.
    static const QRegularExpression tokenRe(QStringLiteral("[\\w\\x{00C0}-\\x{FFFF}']+"));
    auto tokIt = tokenRe.globalMatch(query);
    while (tokIt.hasNext() && keywords.size() < 5) {
        const QString tok = tokIt.next().captured(0);
        if (tok.size() < 3) continue;
        const QString lower = tok.toLower();
        if (stopWords.contains(lower)) continue;

        bool hasNonAscii = false;
        for (QChar c : tok) {
            if (c.unicode() > 127) { hasNonAscii = true; break; }
        }
        const bool capitalized = tok[0].isUpper();
        const bool longEnough = tok.size() >= 5;
        if (!(capitalized || hasNonAscii || longEnough)) continue;
        if (keywords.contains(tok, Qt::CaseInsensitive)) continue;
        keywords.append(tok);
    }

    return keywords;
}

namespace {

// Run ripgrep against the project's markdown root for any of `tokens`.
// Returns absolute paths of matching files. ripgrep prints one line per
// match by default; we collapse to unique file paths and cap matches
// per file via -m 3 so a single highly-repeated keyword doesn't blow
// the output buffer.
QStringList runRipgrepFiles(const QStringList &tokens, const QString &projectRoot,
                            int maxFiles)
{
    if (tokens.isEmpty() || projectRoot.isEmpty()) return {};
    static const QString rgPath = QStandardPaths::findExecutable(QStringLiteral("rg"));
    if (rgPath.isEmpty()) return {};

    // Build OR pattern: "(token1|token2|...)". Use QRegularExpression's
    // built-in escape so proper nouns containing punctuation (e.g.
    // "Vål'naden", "Ryz-Ka") match literally rather than getting
    // interpreted as metacharacters by ripgrep's regex engine.
    QStringList escaped;
    for (const QString &t : tokens) {
        escaped.append(QRegularExpression::escape(t));
    }
    const QString pattern = QStringLiteral("(") + escaped.join(QLatin1Char('|')) + QStringLiteral(")");

    QProcess proc;
    proc.start(rgPath, {
        QStringLiteral("-l"),                  // file paths only
        QStringLiteral("-i"),                  // case-insensitive
        QStringLiteral("--type"), QStringLiteral("md"),
        QStringLiteral("--no-messages"),
        pattern,
        projectRoot,
    });
    if (!proc.waitForStarted(2000)) return {};
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(500);
        return {};
    }
    if (proc.exitStatus() != QProcess::NormalExit) return {};

    const QString stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList paths = stdoutText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &p : paths) p = p.trimmed();
    paths.removeAll(QString());
    if (paths.size() > maxFiles) paths = paths.mid(0, maxFiles);
    return paths;
}

// Read `filePath`, find the first occurrence of any of `tokens` (case-
// insensitive), and extract a ~window-char snippet around it. Returns
// (snippet, headingHint) — heading is the closest preceding markdown
// heading, or the file's basename if none found.
struct GrepSnippet {
    QString content;
    QString heading;
};
GrepSnippet extractGrepSnippet(const QString &filePath, const QStringList &tokens,
                               int window = 1200)
{
    GrepSnippet out;
    out.heading = QFileInfo(filePath).completeBaseName();

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    if (text.isEmpty()) return out;

    int hitIdx = -1;
    for (const QString &t : tokens) {
        const int idx = text.indexOf(t, 0, Qt::CaseInsensitive);
        if (idx < 0) continue;
        if (hitIdx < 0 || idx < hitIdx) hitIdx = idx;
    }
    if (hitIdx < 0) {
        // No token actually matched in this file (rg-l said it did, but maybe
        // a pattern flag mismatch). Return the file head as a weak fallback.
        out.content = text.left(window);
        return out;
    }

    // Walk back to find the closest preceding heading line for the heading
    // hint (so the LLM has section context).
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})\\s+(.*)$"),
                                              QRegularExpression::MultilineOption);
    auto headIt = headingRe.globalMatch(text.left(hitIdx));
    QString lastHeading;
    while (headIt.hasNext()) {
        const auto m = headIt.next();
        lastHeading = m.captured(2).trimmed();
    }
    if (!lastHeading.isEmpty()) out.heading = lastHeading;

    const int start = qMax(0, hitIdx - window / 4);
    const int end = qMin(text.size(), hitIdx + (window * 3) / 4);
    out.content = text.mid(start, end - start);
    return out;
}

} // namespace

void KnowledgeBase::hybridSearch(const QString &queryText, int topK,
                                 const QString &excludeFile,
                                 std::function<void(const QList<SearchResult>&)> callback)
{
    // Capture state we need on the worker side; project path is read on the
    // main thread before we hop to a worker.
    const QString projectRoot = m_projectPath;
    QPointer<KnowledgeBase> weak(this);

    // Phase 1: vector search via existing async path. Phase 2 (ripgrep)
    // runs after the vector search returns so its tokens have a callback to
    // hop back into. ripgrep itself runs on a worker thread to avoid
    // blocking the UI when the corpus is large.
    search(queryText, topK, excludeFile,
           [weak, queryText, topK, excludeFile, projectRoot, callback]
           (const QList<SearchResult> &vectorResults) {
        if (!weak) return;
        const QStringList tokens = extractKeywordTokens(queryText);
        if (tokens.isEmpty() || projectRoot.isEmpty()) {
            // Nothing useful for grep — return vector results as-is.
            if (callback) callback(vectorResults);
            return;
        }

        const QString vecDbPath = weak ? weak->m_dbPath : QString();
        auto future = QtConcurrent::run([tokens, projectRoot, excludeFile, vecDbPath]() {
            QList<SearchResult> grepResults;
            const QStringList markdownRoots = {
                QDir(projectRoot).absoluteFilePath(QStringLiteral("manuscript")),
                QDir(projectRoot).absoluteFilePath(QStringLiteral("lorekeeper")),
                QDir(projectRoot).absoluteFilePath(QStringLiteral("research")),
            };
            QStringList allFiles;
            for (const QString &root : markdownRoots) {
                if (!QFileInfo::exists(root)) continue;
                allFiles += runRipgrepFiles(tokens, root, /*maxFiles=*/10);
            }
            allFiles.removeDuplicates();
            const QString excludeAbs = excludeFile.isEmpty()
                ? QString()
                : QDir(projectRoot).absoluteFilePath(excludeFile);
            for (const QString &filePath : allFiles) {
                if (!excludeAbs.isEmpty() && filePath == excludeAbs) continue;
                const GrepSnippet snip = extractGrepSnippet(filePath, tokens);
                if (snip.content.isEmpty()) continue;
                SearchResult r;
                // Store project-relative path to match the vector path shape.
                r.filePath = QDir(projectRoot).relativeFilePath(filePath);
                r.heading = snip.heading;
                r.content = snip.content;
                // Score grep hits at 0.9 — above all but the strongest
                // vector matches. Reasoning: a literal-match for a token
                // the user typed is a STRONGER recall signal than a
                // semantic similarity to a topically-related passage.
                // Made-up proper nouns from fiction (character names,
                // place names) collapse to near-identical low-info
                // vectors during embedding, so the ranking has to favour
                // the grep arm to surface them at all.
                r.score = 0.9f;
                grepResults.append(r);
            }

            // ---- Phase 3: graph traversal arm. -------------------------
            // Resolve query tokens to entity ids via the librarian's
            // alias index, traverse 1 hop in relationships, then pull
            // every chunk linked to any entity in the neighborhood.
            //
            // Score 0.85 — between vector (~0.7-0.85) and grep (0.9).
            // Rationale: a chunk that doesn't literally mention the
            // queried name but is linked via a relationship is high-
            // confidence supplementary context — better than a weak
            // vector hit, but a literal mention is still stronger.
            const QString libDbPath = QDir(projectRoot).absoluteFilePath(
                QStringLiteral(".rpgforge.db"));
            const bool haveDbs = QFileInfo::exists(libDbPath)
                                 && !vecDbPath.isEmpty()
                                 && QFileInfo::exists(vecDbPath);
            if (haveDbs) {
                QSet<qint64> seedIds;
                {
                    const QString conn = QStringLiteral("kb_graph_lib_%1")
                        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
                    // Inner scope: the QSqlDatabase local AND every
                    // QSqlQuery built on it MUST destruct before
                    // removeDatabase, otherwise Qt warns "connection X
                    // is still in use" and silently leaks the handle.
                    {
                        QSqlDatabase lib = QSqlDatabase::contains(conn)
                            ? QSqlDatabase::database(conn)
                            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                        lib.setDatabaseName(libDbPath);
                        if (lib.open()) {
                            QSqlQuery q(lib);
                            for (const QString &t : tokens) {
                                q.prepare(QStringLiteral(
                                    "SELECT entity_id FROM entity_aliases "
                                    "WHERE alias = :a COLLATE NOCASE LIMIT 1"));
                                q.bindValue(QStringLiteral(":a"), t);
                                if (q.exec() && q.next()) seedIds.insert(q.value(0).toLongLong());
                            }
                            // 1-hop expansion: add every direct neighbor of
                            // each seed via the relationships table. Two
                            // queries (outgoing + incoming) since edges are
                            // directed but membership is symmetric for
                            // retrieval purposes.
                            QSet<qint64> neighborhood = seedIds;
                            if (!seedIds.isEmpty()) {
                                QStringList placeholders;
                                for (int i = 0; i < seedIds.size(); ++i) {
                                    placeholders.append(QStringLiteral("?"));
                                }
                                const QString inList = placeholders.join(QStringLiteral(","));
                                const QString sqlOut = QStringLiteral(
                                    "SELECT target_id FROM relationships WHERE source_id IN (")
                                    + inList + QStringLiteral(")");
                                const QString sqlIn = QStringLiteral(
                                    "SELECT source_id FROM relationships WHERE target_id IN (")
                                    + inList + QStringLiteral(")");
                                QSqlQuery qo(lib);
                                qo.prepare(sqlOut);
                                for (qint64 id : seedIds) qo.addBindValue(id);
                                if (qo.exec()) while (qo.next()) neighborhood.insert(qo.value(0).toLongLong());
                                QSqlQuery qi(lib);
                                qi.prepare(sqlIn);
                                for (qint64 id : seedIds) qi.addBindValue(id);
                                if (qi.exec()) while (qi.next()) neighborhood.insert(qi.value(0).toLongLong());
                                // Also include containment hierarchy via parent_id.
                                QSqlQuery qp(lib);
                                qp.prepare(QStringLiteral(
                                    "SELECT id, parent_id FROM entities "
                                    "WHERE parent_id IS NOT NULL"));
                                if (qp.exec()) {
                                    while (qp.next()) {
                                        const qint64 c = qp.value(0).toLongLong();
                                        const qint64 p = qp.value(1).toLongLong();
                                        if (seedIds.contains(c)) neighborhood.insert(p);
                                        if (seedIds.contains(p)) neighborhood.insert(c);
                                    }
                                }
                            }
                            seedIds = neighborhood;
                            lib.close();
                        }
                    }
                    QSqlDatabase::removeDatabase(conn);
                }

                if (!seedIds.isEmpty()) {
                    // Open the vector DB and pull chunks for entities
                    // in the neighborhood. SELECT joins chunk_entities
                    // against chunks so we get the full snippet payload
                    // in one query.
                    const QString conn = QStringLiteral("kb_graph_vec_%1")
                        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
                    // Inner scope so QSqlDatabase + QSqlQuery destruct
                    // before removeDatabase — see the kb_graph_lib block
                    // above for rationale.
                    {
                        QSqlDatabase vec = QSqlDatabase::contains(conn)
                            ? QSqlDatabase::database(conn)
                            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                        vec.setDatabaseName(vecDbPath);
                        if (vec.open()) {
                            QStringList ids;
                            for (qint64 id : seedIds) ids.append(QString::number(id));
                            QSqlQuery q(vec);
                            // Inline the id list — values come from a
                            // private QSet<qint64>, no user input, so SQL
                            // injection isn't a concern. ce.entity_id IN
                            // (…) bounded by the neighborhood size.
                            const QString sql = QStringLiteral(
                                "SELECT DISTINCT c.file_path, c.heading, c.content "
                                "FROM chunk_entities ce "
                                "JOIN chunks c ON c.id = ce.chunk_id "
                                "WHERE ce.entity_id IN (") + ids.join(QLatin1Char(',')) +
                                QStringLiteral(") LIMIT 60");
                            if (q.exec(sql)) {
                                while (q.next()) {
                                    SearchResult r;
                                    r.filePath = q.value(0).toString();
                                    r.heading = q.value(1).toString();
                                    r.content = q.value(2).toString();
                                    r.score = 0.85f;
                                    grepResults.append(r);
                                }
                            }
                            vec.close();
                        }
                    }
                    QSqlDatabase::removeDatabase(conn);
                }
            }

            return grepResults;
        });

        // Hop back to the main thread to merge + invoke the user callback.
        // QFutureWatcher is the canonical Qt6 marshaller; using the more
        // compact lambda form to avoid the watcher member.
        auto *watcher = new QFutureWatcher<QList<SearchResult>>();
        QObject::connect(watcher, &QFutureWatcher<QList<SearchResult>>::finished,
                         weak, [weak, watcher, vectorResults, topK, callback]() {
            const QList<SearchResult> grepResults = watcher->result();
            watcher->deleteLater();
            if (!weak) return;

            // Merge: union vector + grep results. Vector may have
            // multiple chunks per file (different sections); we keep
            // those because a long file can have multiple relevant
            // passages. For grep, we add the literal-match snippet
            // alongside, even if the same file already has vector
            // chunks — the grep snippet is anchored on the proper-noun
            // hit, which is often a section the embedding pass missed.
            //
            // Downstream MMR diversification in RagAssistService will
            // collapse near-duplicates by content similarity, so we do
            // not need to dedupe by filePath here.
            QList<SearchResult> merged = vectorResults;
            merged.append(grepResults);

            // Sort by score descending so high-confidence grep hits
            // (literal proper-noun matches at 0.9) outrank weak vector
            // hits and survive the rerank+trim stage.
            std::sort(merged.begin(), merged.end(),
                      [](const SearchResult &a, const SearchResult &b) {
                          return a.score > b.score;
                      });
            // Cap to topK + small overflow so the rerank stage has
            // working room without blowing the LLM context window.
            const int cap = topK + 5;
            if (merged.size() > cap) merged = merged.mid(0, cap);

            // grepResults now contains both literal-match (score 0.9)
            // and graph-traversal (score 0.85) augmented hits — the
            // single list is the simplest carrier across the worker
            // boundary. Count each tier separately for diagnostics.
            int grepHits = 0, graphHits = 0;
            for (const auto &r : grepResults) {
                if (r.score >= 0.89f) ++grepHits; else ++graphHits;
            }
            qInfo().noquote()
                << "KnowledgeBase::hybridSearch: vector=" << vectorResults.size()
                << "grep=" << grepHits
                << "graph=" << graphHits
                << "merged=" << merged.size();
            if (callback) callback(merged);
        });
        watcher->setFuture(future);
    });
}

void KnowledgeBase::reindexProject()
{
    if (m_projectPath.isEmpty()) {
        qWarning() << "KnowledgeBase::reindexProject: skipped — no project open";
        return;
    }

    const auto chain = resolveEmbeddingChain();
    qWarning().noquote() << "KnowledgeBase::reindexProject: starting";
    qWarning().noquote() << "  embedding chain:" << describeChain(chain);
    if (chain.isEmpty()) {
        qWarning().noquote()
            << "  ABORTING: no provider has an embedding model configured. "
               "Open Settings → LLM.";
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

int KnowledgeBase::rebuildChunkEntityLinks()
{
    if (m_projectPath.isEmpty()) return 0;

    // ---- Step 1: load the alias index from the librarian DB. -----------
    // Open a private connection (separate from the librarian's per-thread
    // connection naming so we don't clash). Read-only — we never mutate
    // .rpgforge.db from here.
    const QString libDbPath = QDir(m_projectPath).absoluteFilePath(
        QStringLiteral(".rpgforge.db"));
    if (!QFileInfo::exists(libDbPath)) {
        qInfo().noquote() << "KnowledgeBase::rebuildChunkEntityLinks: "
                              "librarian DB does not exist yet — skipping";
        return 0;
    }

    struct AliasEntry { QString alias; QString lower; qint64 entityId; };
    QList<AliasEntry> aliases;
    {
        const QString conn = QStringLiteral("kb_alias_loader_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        bool openFailed = false;
        // Inner scope so QSqlDatabase + QSqlQuery destruct before
        // removeDatabase — Qt warns "connection X is still in use"
        // and silently leaks the handle otherwise.
        {
            QSqlDatabase libDb = QSqlDatabase::contains(conn)
                ? QSqlDatabase::database(conn)
                : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            libDb.setDatabaseName(libDbPath);
            if (!libDb.open()) {
                qWarning() << "KnowledgeBase::rebuildChunkEntityLinks: "
                              "failed to open librarian DB:" << libDb.lastError().text();
                openFailed = true;
            } else {
                QSqlQuery q(libDb);
                if (q.exec(QStringLiteral(
                        "SELECT alias, entity_id FROM entity_aliases"))) {
                    while (q.next()) {
                        AliasEntry e;
                        e.alias    = q.value(0).toString();
                        e.entityId = q.value(1).toLongLong();
                        e.lower    = e.alias.toLower();
                        if (!e.alias.isEmpty()) aliases.append(e);
                    }
                }
                libDb.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        if (openFailed) return 0;
    }

    if (aliases.isEmpty()) {
        qInfo().noquote() << "KnowledgeBase::rebuildChunkEntityLinks: "
                              "no entities indexed yet — skipping";
        return 0;
    }

    // Sort longest-first so "Captain Ryzen" matches before "Ryzen" matches
    // before "Ryz". This is the rule that lets us mask out shorter overlaps
    // after a longer match has been recorded.
    std::sort(aliases.begin(), aliases.end(),
              [](const AliasEntry &a, const AliasEntry &b) {
                  return a.alias.size() > b.alias.size();
              });

    // ---- Step 2: walk every chunk and find matches. --------------------
    QSqlDatabase db = database();
    if (!db.isOpen()) return 0;

    QSqlQuery scan(db);
    if (!scan.exec(QStringLiteral("SELECT id, content FROM chunks"))) {
        qWarning() << "KnowledgeBase::rebuildChunkEntityLinks: "
                       "chunk scan failed:" << scan.lastError().text();
        return 0;
    }

    int totalLinks = 0;
    int chunksProcessed = 0;
    db.transaction();

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT OR IGNORE INTO chunk_entities "
                                "(chunk_id, entity_id) VALUES (:c, :e)"));

    while (scan.next()) {
        const qint64 chunkId = scan.value(0).toLongLong();
        const QString content = scan.value(1).toString();
        if (content.isEmpty()) continue;
        const QString lowerContent = content.toLower();
        ++chunksProcessed;

        // Mask: tracks character indices already consumed by a longer
        // alias match, so "Ryz" inside "Ryzen" doesn't double-link.
        QVector<bool> consumed(content.size(), false);
        QSet<qint64> linkedEntities;

        for (const AliasEntry &a : aliases) {
            if (a.lower.size() < 3) continue;     // skip 1-2 char aliases (false positives)
            int from = 0;
            while (true) {
                const int hit = lowerContent.indexOf(a.lower, from);
                if (hit < 0) break;
                from = hit + 1;
                const int end = hit + a.lower.size();

                // Word-boundary check: the char before/after must not be a
                // letter/digit, otherwise we'd false-match "Ryz" inside
                // "Ryzen". '_' and apostrophes count as word chars (so
                // names like "Vål'naden" stay intact).
                auto isWordChar = [](QChar c) {
                    return c.isLetterOrNumber() || c == QLatin1Char('_')
                        || c == QLatin1Char('\'');
                };
                if (hit > 0 && isWordChar(content.at(hit - 1))) continue;
                if (end < content.size() && isWordChar(content.at(end))) continue;

                // Mask check: any char in [hit, end) already consumed by a
                // longer match? Skip if so.
                bool overlaps = false;
                for (int i = hit; i < end; ++i) {
                    if (consumed[i]) { overlaps = true; break; }
                }
                if (overlaps) continue;
                for (int i = hit; i < end; ++i) consumed[i] = true;

                if (linkedEntities.contains(a.entityId)) continue;
                linkedEntities.insert(a.entityId);

                ins.bindValue(QStringLiteral(":c"), chunkId);
                ins.bindValue(QStringLiteral(":e"), a.entityId);
                if (ins.exec()) ++totalLinks;
            }
        }
    }

    db.commit();
    qInfo().noquote() << "KnowledgeBase::rebuildChunkEntityLinks:"
                       << "scanned" << chunksProcessed << "chunks,"
                       << "wrote" << totalLinks << "links across"
                       << aliases.size() << "aliases";
    return totalLinks;
}
