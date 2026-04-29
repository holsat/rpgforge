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

#include "librariandatabase.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QUuid>
#include <QSqlError>
#include <QThread>

LibrarianDatabase::LibrarianDatabase(QObject *parent)
    : QObject(parent)
{
}

LibrarianDatabase::~LibrarianDatabase()
{
    close();
}

bool LibrarianDatabase::open(const QString &path)
{
    m_dbPath = path;
    QSqlDatabase db = database();
    if (!db.isOpen()) {
        m_lastError = db.lastError().text();
        qWarning() << "LibrarianDatabase: Failed to open" << path << ":" << m_lastError;
        return false;
    }

    return initSchema(db);
}

void LibrarianDatabase::close()
{
    // For every QSqlDatabase connection this LibrarianDatabase owns
    // (one per thread that ever called database()), force a full WAL
    // checkpoint and truncate before removing the connection.
    //
    // Without the checkpoint, kill mid-write leaves the WAL holding
    // pages that the main DB cannot validate on the next open —
    // SQLite then reports the file as malformed and `.recover` only
    // salvages orphan pages. PRAGMA wal_checkpoint(TRUNCATE) flushes
    // all WAL frames into the main DB and resets the WAL to zero
    // length, so a process killed shortly after a clean close still
    // leaves a coherent main DB.
    //
    // Per-connection teardown MUST run on the thread that created the
    // connection. Qt logs "QSqlDatabasePrivate::database: requested
    // database does not belong to the calling thread" otherwise. We
    // tracked the owning QThread* in m_connectionThreads at creation
    // time; iterate that map and dispatch each teardown to the right
    // thread, or do it inline when the owner is the current thread or
    // has already finished (worker pool runnables that have returned).
    if (!QCoreApplication::instance()) return;

    // Snapshot then clear under the mutex so re-entry into database()
    // from a stale call site can't trip over a half-mutated map.
    QHash<QString, QPointer<QThread>> snapshot;
    {
        QMutexLocker lock(&m_connectionThreadsMutex);
        snapshot.swap(m_connectionThreads);
    }

    auto teardown = [](const QString &connectionName) {
        if (!QSqlDatabase::contains(connectionName)) return;
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName, /*open=*/false);
            if (db.isValid() && db.isOpen()) {
                QSqlQuery q(db);
                // TRUNCATE: flush WAL into main DB and reset WAL to
                // zero length. Best-effort — if the pragma fails
                // (e.g. read-only handle), we still proceed to close;
                // a missed checkpoint is recoverable on next open,
                // but a never-closed connection leaks.
                if (!q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
                    qWarning().noquote()
                        << "LibrarianDatabase::close: wal_checkpoint failed on"
                        << connectionName << ":" << q.lastError().text();
                }
                db.close();
            }
        }
        // Must drop the QSqlDatabase out of scope BEFORE removeDatabase,
        // otherwise Qt warns about an outstanding handle.
        QSqlDatabase::removeDatabase(connectionName);
    };

    QThread *thisThread = QThread::currentThread();
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
        const QString &connectionName = it.key();
        QThread *owner = it.value().data();   // may be nullptr if QPointer cleared

        if (!owner || owner == thisThread || owner->isFinished()) {
            // Owner is gone (worker returned to pool, thread destroyed),
            // or we ARE the owner — safe to do the work inline. When the
            // worker thread already exited, Qt's own per-thread
            // bookkeeping is gone too; calling removeDatabase from this
            // thread is the only remaining option and Qt accepts it
            // (the connection is treated as orphaned).
            teardown(connectionName);
            continue;
        }

        // Owner thread is alive and isn't us. We can only post a
        // BlockingQueuedConnection to it if it's actually running an
        // event loop — QThreadPool runnables don't, so the post would
        // sit forever and deadlock close() on the destructor path.
        // Probe loopLevel() first; fall through to inline-with-warning
        // when there's no loop.
        if (owner->loopLevel() <= 0) {
            qWarning().noquote()
                << "LibrarianDatabase::close: owning thread for"
                << connectionName
                << "is alive but has no event loop — closing inline"
                << "(may emit a one-line thread-mismatch warning from Qt)";
            teardown(connectionName);
            continue;
        }

        QObject ctx;
        ctx.moveToThread(owner);
        QMetaObject::invokeMethod(
            &ctx, [connectionName, &teardown]() { teardown(connectionName); },
            Qt::BlockingQueuedConnection);
    }
}

void LibrarianDatabase::closeCurrentThreadConnection()
{
    if (!QCoreApplication::instance()) return;
    if (m_dbPath.isEmpty()) return;

    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString connectionName = QStringLiteral("Librarian_%1_%2")
        .arg(QString::number(reinterpret_cast<quintptr>(this), 16), threadId);

    {
        QMutexLocker lock(&m_connectionThreadsMutex);
        m_connectionThreads.remove(connectionName);
    }

    if (!QSqlDatabase::contains(connectionName)) return;
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, /*open=*/false);
        if (db.isValid() && db.isOpen()) {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
                qWarning().noquote()
                    << "LibrarianDatabase::closeCurrentThreadConnection:"
                    << "wal_checkpoint failed on" << connectionName
                    << ":" << q.lastError().text();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QSqlDatabase LibrarianDatabase::database() const
{
    // Shutdown-race guard: QSqlDatabase::addDatabase() emits
    // "QSqlDatabase requires a QCoreApplication" when called after qApp
    // has been destroyed — which happens when a QThreadPool worker
    // (e.g. LibrarianService::processQueue's background task) is still
    // running during application exit. Return a disconnected handle;
    // callers check isOpen() before touching it.
    if (!QCoreApplication::instance()) return QSqlDatabase();
    if (m_dbPath.isEmpty()) return QSqlDatabase();

    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString connectionName = QStringLiteral("Librarian_%1_%2")
        .arg(QString::number(reinterpret_cast<quintptr>(this), 16), threadId);

    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        const_cast<LibrarianDatabase*>(this)->m_lastError = db.lastError().text();
    } else {
        // Enable WAL mode for better concurrency
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        // Enforce FK cascades for entity_aliases / entity_tags / relationships
        // — SQLite has them off by default per-connection.
        q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    }

    // Record the owning QThread for this fresh connection so close() can
    // dispatch the teardown back to the right thread (or detect that the
    // owner is gone). QPointer tracks thread destruction so a worker
    // that exited before close() runs leaves a null entry — close()
    // treats that as "owner gone, tear down inline".
    {
        QMutexLocker lock(&m_connectionThreadsMutex);
        m_connectionThreads.insert(connectionName,
                                    QPointer<QThread>(QThread::currentThread()));
    }
    return db;
}

bool LibrarianDatabase::beginTransaction()
{
    return database().transaction();
}

bool LibrarianDatabase::commit()
{
    return database().commit();
}

bool LibrarianDatabase::addColumnIfMissing(QSqlDatabase &db,
                                            const QString &table,
                                            const QString &column,
                                            const QString &columnDef)
{
    QSqlQuery probe(db);
    probe.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (!probe.exec()) {
        m_lastError = probe.lastError().text();
        return false;
    }
    while (probe.next()) {
        if (probe.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            return true;    // already present
        }
    }
    QSqlQuery alter(db);
    if (!alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                    .arg(table, column, columnDef))) {
        m_lastError = alter.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::initSchema(QSqlDatabase &db)
{
    QSqlQuery query(db);

    // Entities table — original three columns. New columns are added
    // additively below via addColumnIfMissing so existing projects don't
    // need a destructive migration.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS entities ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT NOT NULL, "
                    "type TEXT, "
                    "source_file TEXT, "
                    "last_modified DATETIME)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Phase 1 additive migration of entity descriptive + spatial-temporal
    // fields. Each one is a separate ALTER TABLE because SQLite cannot
    // batch them. Existing rows get NULL / 0 for the new columns, which
    // the read accessors translate to "unknown".
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("summary"), QStringLiteral("TEXT"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("parent_id"), QStringLiteral("INTEGER"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("first_appearance_file"), QStringLiteral("TEXT"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("first_appearance_line"), QStringLiteral("INTEGER"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("last_appearance_file"), QStringLiteral("TEXT"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("last_appearance_line"), QStringLiteral("INTEGER"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("era"), QStringLiteral("TEXT"))) return false;
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("mention_count"), QStringLiteral("INTEGER DEFAULT 0"))) return false;
    // Phase 5: community_id populated by EntityCommunityDetector. NULL
    // until detection has run for this project.
    if (!addColumnIfMissing(db, QStringLiteral("entities"),
                             QStringLiteral("community_id"), QStringLiteral("INTEGER"))) return false;

    // Attributes table (EAV)
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS attributes ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "entity_id INTEGER, "
                    "key TEXT NOT NULL, "
                    "value TEXT, "
                    "datatype TEXT, "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // entity_aliases — first-class, FK-cascaded. Without this, the chunk
    // recognition pass that powers graph-augmented RAG would fragment
    // every character into N nodes (one per nickname).
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS entity_aliases ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "entity_id INTEGER NOT NULL, "
                    "alias TEXT NOT NULL, "
                    "is_primary INTEGER DEFAULT 0, "
                    "UNIQUE(entity_id, alias), "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // entity_tags — secondary categorization beyond entities.type.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS entity_tags ("
                    "entity_id INTEGER NOT NULL, "
                    "tag TEXT NOT NULL, "
                    "PRIMARY KEY (entity_id, tag), "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // relationships — typed entity↔entity edges. Replaces the dead
    // references_graph table conceptually, though we leave that table
    // in place for backwards compatibility with any external tooling.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS relationships ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "source_id INTEGER NOT NULL, "
                    "target_id INTEGER NOT NULL, "
                    "relationship TEXT NOT NULL, "
                    "evidence_file TEXT, "
                    "evidence_line INTEGER DEFAULT 0, "
                    "strength REAL DEFAULT 0.5, "
                    "last_modified DATETIME, "
                    "UNIQUE(source_id, target_id, relationship), "
                    "FOREIGN KEY(source_id) REFERENCES entities(id) ON DELETE CASCADE, "
                    "FOREIGN KEY(target_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Dependency Graph / References table (legacy)
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS references_graph ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "entity_id INTEGER, "
                    "referencing_file TEXT, "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Phase 1 migration: existing projects have entities rows that
    // predate the entity_aliases table. addEntity() inserts the
    // canonical name as a primary alias on the way in, but old rows
    // missed that path — backfill them here so the alias index is
    // complete for chunk-recognition (Phase 3 graph arm) and entity
    // resolution (Phase 4 navigation).
    {
        QSqlQuery backfill(db);
        if (backfill.exec(QStringLiteral(
                "INSERT OR IGNORE INTO entity_aliases (entity_id, alias, is_primary) "
                "SELECT e.id, e.name, 1 FROM entities e "
                "LEFT JOIN entity_aliases ea ON ea.entity_id = e.id "
                "WHERE ea.entity_id IS NULL"))) {
            const int rows = backfill.numRowsAffected();
            if (rows > 0) {
                qInfo().noquote() << "LibrarianDatabase: backfilled"
                                   << rows << "canonical-name aliases for "
                                              "pre-Phase-1 entity rows";
            }
        }
    }

    // file_extractions — per-file extraction status keyed by content
    // hash (SHA-256). Created via CREATE TABLE IF NOT EXISTS so existing
    // project DBs upgrade cleanly on next open.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS file_extractions ("
                    "file_hash TEXT PRIMARY KEY, "
                    "last_path TEXT NOT NULL, "
                    "heuristic_done INTEGER NOT NULL DEFAULT 0, "
                    "semantic_done INTEGER NOT NULL DEFAULT 0, "
                    "extracted_at DATETIME)"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_extractions_path ON file_extractions(last_path)"));

    // Persisted semantic-attempt strike counter. Old projects that
    // already have file_extractions rows pick up the column with
    // default 0 — no data loss.
    if (!addColumnIfMissing(db, QStringLiteral("file_extractions"),
                             QStringLiteral("attempts"),
                             QStringLiteral("INTEGER NOT NULL DEFAULT 0"))) return false;

    // pending_relationships — LLM-emitted edges whose target couldn't
    // yet be resolved. Re-tried after every successful semantic
    // extraction.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS pending_relationships ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "source_id INTEGER NOT NULL, "
                    "target_name TEXT NOT NULL, "
                    "relationship TEXT NOT NULL, "
                    "evidence_file TEXT, "
                    "strength REAL DEFAULT 0.5, "
                    "queued_at DATETIME, "
                    "UNIQUE(source_id, target_name, relationship), "
                    "FOREIGN KEY(source_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Per-edge GC counter: bumped on each unresolved retry pass; rows
    // exceeding the threshold are deleted by resolvePendingRelationships
    // so the queue can't grow without bound on broken refs.
    if (!addColumnIfMissing(db, QStringLiteral("pending_relationships"),
                             QStringLiteral("attempts"),
                             QStringLiteral("INTEGER NOT NULL DEFAULT 0"))) return false;

    // Indexes for performance
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entities_name ON entities(name)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entities_parent ON entities(parent_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_attributes_entity ON attributes(entity_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_attributes_key ON attributes(key)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alias_lookup ON entity_aliases(alias)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alias_entity ON entity_aliases(entity_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tag_lookup ON entity_tags(tag)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rel_source ON relationships(source_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rel_target ON relationships(target_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rel_type   ON relationships(relationship)"));

    return true;
}

qint64 LibrarianDatabase::addEntity(const QString &name, const QString &type, const QString &sourceFile)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("INSERT INTO entities (name, type, source_file, last_modified) "
                  "VALUES (:name, :type, :source_file, :last_modified)"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":source_file"), sourceFile);
    query.bindValue(QStringLiteral(":last_modified"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }

    const qint64 newId = query.lastInsertId().toLongLong();
    // The canonical name itself is also the primary alias. This makes the
    // alias-resolution path the single source of truth for "given a string,
    // find the entity it refers to".
    addAlias(newId, name, /*isPrimary=*/true);
    return newId;
}

bool LibrarianDatabase::updateEntity(qint64 id, const QString &name, const QString &type)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("UPDATE entities SET name = :name, type = :type, last_modified = :last_modified WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":last_modified"), QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::deleteEntity(qint64 id)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("DELETE FROM entities WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::setEntitySummary(qint64 id, const QString &summary)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("UPDATE entities SET summary = :s WHERE id = :id"));
    q.bindValue(QStringLiteral(":s"), summary);
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    return true;
}

bool LibrarianDatabase::setEntityParent(qint64 id, qint64 parentId)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("UPDATE entities SET parent_id = :p WHERE id = :id"));
    if (parentId < 0) {
        q.bindValue(QStringLiteral(":p"), QVariant());     // NULL parent
    } else {
        q.bindValue(QStringLiteral(":p"), parentId);
    }
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    return true;
}

bool LibrarianDatabase::setEntityEra(qint64 id, const QString &era)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("UPDATE entities SET era = :e WHERE id = :id"));
    q.bindValue(QStringLiteral(":e"), era);
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    return true;
}

QString LibrarianDatabase::getEntitySummary(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT summary FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

QString LibrarianDatabase::getEntityEra(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT era FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

qint64 LibrarianDatabase::getEntityParent(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT parent_id FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next() && !q.value(0).isNull()) return q.value(0).toLongLong();
    return -1;
}

int LibrarianDatabase::getEntityMentionCount(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT mention_count FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

qint64 LibrarianDatabase::getEntityCommunityId(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT community_id FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next() && !q.value(0).isNull()) return q.value(0).toLongLong();
    return -1;
}

QList<qint64> LibrarianDatabase::findEntitiesByCommunity(qint64 communityId) const
{
    QList<qint64> out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT id FROM entities WHERE community_id = :c"));
    q.bindValue(QStringLiteral(":c"), communityId);
    if (q.exec()) {
        while (q.next()) out.append(q.value(0).toLongLong());
    }
    return out;
}

QString LibrarianDatabase::getEntityName(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT name FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

QString LibrarianDatabase::getEntityType(qint64 id) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT type FROM entities WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

bool LibrarianDatabase::setAttribute(qint64 entityId, const QString &key, const QVariant &value)
{
    QSqlDatabase db = database();
    // First, check if attribute already exists
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM attributes WHERE entity_id = :entity_id AND key = :key"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":key"), key);

    if (query.exec() && query.next()) {
        // Update existing
        qint64 attrId = query.value(0).toLongLong();
        query.prepare(QStringLiteral("UPDATE attributes SET value = :value, datatype = :datatype WHERE id = :id"));
        query.bindValue(QStringLiteral(":value"), value.toString());
        query.bindValue(QStringLiteral(":datatype"), QString::fromLatin1(value.typeName()));
        query.bindValue(QStringLiteral(":id"), attrId);
    } else {
        // Insert new
        query.prepare(QStringLiteral("INSERT INTO attributes (entity_id, key, value, datatype) "
                      "VALUES (:entity_id, :key, :value, :datatype)"));
        query.bindValue(QStringLiteral(":entity_id"), entityId);
        query.bindValue(QStringLiteral(":key"), key);
        query.bindValue(QStringLiteral(":value"), value.toString());
        query.bindValue(QStringLiteral(":datatype"), QString::fromLatin1(value.typeName()));
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVariant LibrarianDatabase::getAttribute(qint64 entityId, const QString &key) const
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT value, datatype FROM attributes WHERE entity_id = :entity_id AND key = :key"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":key"), key);

    if (query.exec() && query.next()) {
        QString valStr = query.value(0).toString();
        QString typeName = query.value(1).toString();

        QVariant val(valStr);
        if (val.canConvert(QMetaType::fromName(typeName.toUtf8()))) {
            val.convert(QMetaType::fromName(typeName.toUtf8()));
        }
        return val;
    }

    return QVariant();
}

QVariantMap LibrarianDatabase::getAttributes(qint64 entityId) const
{
    QVariantMap attrs;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT key, value, datatype FROM attributes WHERE entity_id = :entity_id"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);

    if (query.exec()) {
        while (query.next()) {
            QString key = query.value(0).toString();
            QString valStr = query.value(1).toString();
            QString typeName = query.value(2).toString();

            QVariant val(valStr);
            if (val.canConvert(QMetaType::fromName(typeName.toUtf8()))) {
                val.convert(QMetaType::fromName(typeName.toUtf8()));
            }
            attrs.insert(key, val);
        }
    } else {
        qWarning() << "Failed to fetch attributes for entity" << entityId << ":" << query.lastError().text();
    }

    return attrs;
}

bool LibrarianDatabase::addAlias(qint64 entityId, const QString &alias, bool isPrimary)
{
    if (alias.trimmed().isEmpty()) return false;
    QSqlQuery q(database());
    // OR IGNORE relies on the UNIQUE(entity_id, alias) constraint to make
    // duplicate inserts no-ops.
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO entity_aliases "
                              "(entity_id, alias, is_primary) VALUES (:e, :a, :p)"));
    q.bindValue(QStringLiteral(":e"), entityId);
    q.bindValue(QStringLiteral(":a"), alias.trimmed());
    q.bindValue(QStringLiteral(":p"), isPrimary ? 1 : 0);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QStringList LibrarianDatabase::getAliases(qint64 entityId) const
{
    QStringList out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT alias FROM entity_aliases WHERE entity_id = :e"));
    q.bindValue(QStringLiteral(":e"), entityId);
    if (q.exec()) {
        while (q.next()) out.append(q.value(0).toString());
    }
    return out;
}

qint64 LibrarianDatabase::resolveEntityByName(const QString &name) const
{
    if (name.trimmed().isEmpty()) return -1;
    // Alias index includes the canonical name (addEntity inserts it as a
    // primary alias), so this single lookup covers both "Ryzen" and
    // "Ryz". Case-insensitive compare via COLLATE NOCASE.
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT entity_id FROM entity_aliases "
                              "WHERE alias = :a COLLATE NOCASE LIMIT 1"));
    q.bindValue(QStringLiteral(":a"), name.trimmed());
    if (q.exec() && q.next()) return q.value(0).toLongLong();
    return -1;
}

bool LibrarianDatabase::addTag(qint64 entityId, const QString &tag)
{
    if (tag.trimmed().isEmpty()) return false;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO entity_tags (entity_id, tag) "
                              "VALUES (:e, :t)"));
    q.bindValue(QStringLiteral(":e"), entityId);
    q.bindValue(QStringLiteral(":t"), tag.trimmed());
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    return true;
}

QStringList LibrarianDatabase::getTags(qint64 entityId) const
{
    QStringList out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT tag FROM entity_tags WHERE entity_id = :e"));
    q.bindValue(QStringLiteral(":e"), entityId);
    if (q.exec()) {
        while (q.next()) out.append(q.value(0).toString());
    }
    return out;
}

QList<qint64> LibrarianDatabase::findEntitiesByTag(const QString &tag) const
{
    QList<qint64> out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT entity_id FROM entity_tags WHERE tag = :t COLLATE NOCASE"));
    q.bindValue(QStringLiteral(":t"), tag);
    if (q.exec()) {
        while (q.next()) out.append(q.value(0).toLongLong());
    }
    return out;
}

qint64 LibrarianDatabase::upsertRelationship(qint64 sourceId,
                                              qint64 targetId,
                                              const QString &relationship,
                                              const QString &evidenceFile,
                                              int evidenceLine,
                                              double strength)
{
    QSqlQuery q(database());
    // INSERT … ON CONFLICT … DO UPDATE: the UNIQUE constraint covers
    // (source_id, target_id, relationship), so re-running extraction over
    // the same evidence merges rather than duplicates. Strength is
    // overwritten with the latest value rather than averaged — the most
    // recent run is treated as the authoritative observation.
    q.prepare(QStringLiteral(
        "INSERT INTO relationships (source_id, target_id, relationship, "
        "                            evidence_file, evidence_line, strength, last_modified) "
        "VALUES (:s, :t, :r, :ef, :el, :str, :lm) "
        "ON CONFLICT(source_id, target_id, relationship) DO UPDATE SET "
        "  evidence_file = excluded.evidence_file, "
        "  evidence_line = excluded.evidence_line, "
        "  strength      = excluded.strength, "
        "  last_modified = excluded.last_modified"));
    q.bindValue(QStringLiteral(":s"), sourceId);
    q.bindValue(QStringLiteral(":t"), targetId);
    q.bindValue(QStringLiteral(":r"), relationship);
    q.bindValue(QStringLiteral(":ef"), evidenceFile);
    q.bindValue(QStringLiteral(":el"), evidenceLine);
    q.bindValue(QStringLiteral(":str"), strength);
    q.bindValue(QStringLiteral(":lm"), QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<EntityRelationship> LibrarianDatabase::getRelationshipsFrom(qint64 entityId) const
{
    QList<EntityRelationship> out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT id, source_id, target_id, relationship, evidence_file, "
        "       evidence_line, strength FROM relationships WHERE source_id = :e"));
    q.bindValue(QStringLiteral(":e"), entityId);
    if (q.exec()) {
        while (q.next()) {
            EntityRelationship r;
            r.id = q.value(0).toLongLong();
            r.sourceId = q.value(1).toLongLong();
            r.targetId = q.value(2).toLongLong();
            r.relationship = q.value(3).toString();
            r.evidenceFile = q.value(4).toString();
            r.evidenceLine = q.value(5).toInt();
            r.strength = q.value(6).toDouble();
            out.append(r);
        }
    }
    return out;
}

QList<EntityRelationship> LibrarianDatabase::getRelationshipsTo(qint64 entityId) const
{
    QList<EntityRelationship> out;
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "SELECT id, source_id, target_id, relationship, evidence_file, "
        "       evidence_line, strength FROM relationships WHERE target_id = :e"));
    q.bindValue(QStringLiteral(":e"), entityId);
    if (q.exec()) {
        while (q.next()) {
            EntityRelationship r;
            r.id = q.value(0).toLongLong();
            r.sourceId = q.value(1).toLongLong();
            r.targetId = q.value(2).toLongLong();
            r.relationship = q.value(3).toString();
            r.evidenceFile = q.value(4).toString();
            r.evidenceLine = q.value(5).toInt();
            r.strength = q.value(6).toDouble();
            out.append(r);
        }
    }
    return out;
}

QList<EntityRelationship> LibrarianDatabase::getRelationships(qint64 entityId) const
{
    QList<EntityRelationship> out = getRelationshipsFrom(entityId);
    out += getRelationshipsTo(entityId);
    return out;
}

int LibrarianDatabase::refreshAggregatesFromVectorDb(const QString &vectorDbPath)
{
    if (vectorDbPath.isEmpty()) return 0;
    QSqlDatabase db = database();
    if (!db.isOpen()) return 0;

    // Attach the vector DB as `vec` so a single SQL statement can join
    // chunks + chunk_entities (in vec) against entities (in main).
    QSqlQuery attach(db);
    attach.prepare(QStringLiteral("ATTACH DATABASE :p AS vec"));
    attach.bindValue(QStringLiteral(":p"), vectorDbPath);
    if (!attach.exec()) {
        // ATTACH fails if `vec` is already attached — that's fine on a
        // re-entry; just continue. Any other failure means there's
        // nothing to read.
        const QString err = attach.lastError().text().toLower();
        if (!err.contains(QLatin1String("already in use"))) {
            qWarning() << "LibrarianDatabase::refreshAggregates: ATTACH failed:" << err;
            return 0;
        }
    }

    // Compute first/last appearance + mention count per entity.
    // Ordering by chunks.id approximates document order (chunks are
    // inserted in file-then-section order). Good enough for v1; can
    // refine when we have project-tree-aware ordering.
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT entity_id, "
            "       COUNT(*)               AS cnt, "
            "       MIN(c.file_path)       AS first_file, "
            "       MIN(c.id)              AS first_chunk, "
            "       MAX(c.file_path)       AS last_file, "
            "       MAX(c.id)              AS last_chunk "
            "FROM vec.chunk_entities ce "
            "JOIN vec.chunks c ON c.id = ce.chunk_id "
            "GROUP BY entity_id"))) {
        qWarning() << "LibrarianDatabase::refreshAggregates: query failed:"
                   << q.lastError().text();
        QSqlQuery det(db);
        det.exec(QStringLiteral("DETACH DATABASE vec"));
        return 0;
    }

    int updated = 0;
    db.transaction();
    while (q.next()) {
        const qint64 entityId = q.value(0).toLongLong();
        const int    cnt      = q.value(1).toInt();
        const QString firstFile = q.value(2).toString();
        const QString lastFile  = q.value(4).toString();

        QSqlQuery up(db);
        up.prepare(QStringLiteral(
            "UPDATE entities SET "
            "  mention_count = :c, "
            "  first_appearance_file = :ff, "
            "  last_appearance_file  = :lf "
            "WHERE id = :id"));
        up.bindValue(QStringLiteral(":c"), cnt);
        up.bindValue(QStringLiteral(":ff"), firstFile);
        up.bindValue(QStringLiteral(":lf"), lastFile);
        up.bindValue(QStringLiteral(":id"), entityId);
        if (up.exec()) ++updated;
    }
    db.commit();

    QSqlQuery det(db);
    det.exec(QStringLiteral("DETACH DATABASE vec"));
    return updated;
}

bool LibrarianDatabase::addReference(qint64 entityId, const QString &referencingFile)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("INSERT INTO references_graph (entity_id, referencing_file) VALUES (:entity_id, :file)"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":file"), referencingFile);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QStringList LibrarianDatabase::getReferences(qint64 entityId) const
{
    QStringList refs;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT referencing_file FROM references_graph WHERE entity_id = :entity_id"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);

    if (query.exec()) {
        while (query.next()) {
            refs << query.value(0).toString();
        }
    }
    return refs;
}

qint64 LibrarianDatabase::findEntity(const QString &name, const QString &type, const QString &sourceFile) const
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT id FROM entities WHERE name = :name AND type = :type AND source_file = :source_file"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":source_file"), sourceFile);

    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return -1;
}

QList<qint64> LibrarianDatabase::findEntitiesByType(const QString &type) const
{
    QList<qint64> ids;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT id FROM entities WHERE type = :type"));
    query.bindValue(QStringLiteral(":type"), type);

    if (query.exec()) {
        while (query.next()) {
            ids << query.value(0).toLongLong();
        }
    }
    return ids;
}

QList<qint64> LibrarianDatabase::findEntitiesByAttribute(const QString &key, const QVariant &value) const
{
    QList<qint64> ids;
    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT entity_id FROM attributes WHERE key = :key AND value = :value"));
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value.toString());

    if (query.exec()) {
        while (query.next()) {
            ids << query.value(0).toLongLong();
        }
    }
    return ids;
}

QList<qint64> LibrarianDatabase::allEntityIds() const
{
    QList<qint64> ids;
    QSqlQuery q(database());
    if (q.exec(QStringLiteral("SELECT id FROM entities"))) {
        while (q.next()) ids.append(q.value(0).toLongLong());
    }
    return ids;
}

bool LibrarianDatabase::getExtractionState(const QString &fileHash,
                                            bool *heuristicDone,
                                            bool *semanticDone) const
{
    if (heuristicDone) *heuristicDone = false;
    if (semanticDone)  *semanticDone  = false;
    if (fileHash.isEmpty()) return false;

    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT heuristic_done, semantic_done "
                              "FROM file_extractions WHERE file_hash = :h"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    if (q.exec() && q.next()) {
        if (heuristicDone) *heuristicDone = q.value(0).toInt() != 0;
        if (semanticDone)  *semanticDone  = q.value(1).toInt() != 0;
        return true;
    }
    return false;
}

bool LibrarianDatabase::recordExtractionState(const QString &fileHash,
                                               const QString &path,
                                               bool heuristicDone,
                                               bool semanticDone)
{
    if (fileHash.isEmpty()) return false;

    QSqlQuery q(database());
    // INSERT … ON CONFLICT … DO UPDATE so we don't lose existing
    // last_path or extracted_at on flag-only updates. Flags monotonically
    // OR with their previous value — once heuristic_done is true, a
    // subsequent semantic-only update mustn't clear it.
    q.prepare(QStringLiteral(
        "INSERT INTO file_extractions "
        "(file_hash, last_path, heuristic_done, semantic_done, extracted_at) "
        "VALUES (:h, :p, :hd, :sd, :ts) "
        "ON CONFLICT(file_hash) DO UPDATE SET "
        "  last_path      = excluded.last_path, "
        "  heuristic_done = MAX(file_extractions.heuristic_done, excluded.heuristic_done), "
        "  semantic_done  = MAX(file_extractions.semantic_done,  excluded.semantic_done), "
        "  extracted_at   = excluded.extracted_at"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    q.bindValue(QStringLiteral(":p"), path);
    q.bindValue(QStringLiteral(":hd"), heuristicDone ? 1 : 0);
    q.bindValue(QStringLiteral(":sd"), semanticDone  ? 1 : 0);
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::updateExtractionPath(const QString &fileHash, const QString &newPath)
{
    if (fileHash.isEmpty()) return false;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("UPDATE file_extractions SET last_path = :p WHERE file_hash = :h"));
    q.bindValue(QStringLiteral(":p"), newPath);
    q.bindValue(QStringLiteral(":h"), fileHash);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QString LibrarianDatabase::hashForPath(const QString &path) const
{
    if (path.isEmpty()) return QString();
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT file_hash FROM file_extractions WHERE last_path = :p LIMIT 1"));
    q.bindValue(QStringLiteral(":p"), path);
    if (q.exec() && q.next()) return q.value(0).toString();
    return QString();
}

bool LibrarianDatabase::forgetExtraction(const QString &fileHash)
{
    if (fileHash.isEmpty()) return false;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("DELETE FROM file_extractions WHERE file_hash = :h"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::clearAllSemanticDone()
{
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral("UPDATE file_extractions SET semantic_done = 0"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::queuePendingRelationship(qint64 sourceId,
                                                  const QString &targetName,
                                                  const QString &relationship,
                                                  const QString &evidenceFile,
                                                  double strength)
{
    if (sourceId < 0 || targetName.trimmed().isEmpty() || relationship.trimmed().isEmpty()) {
        return false;
    }
    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO pending_relationships "
        "(source_id, target_name, relationship, evidence_file, strength, queued_at) "
        "VALUES (:s, :t, :r, :ef, :str, :ts)"));
    q.bindValue(QStringLiteral(":s"),  sourceId);
    q.bindValue(QStringLiteral(":t"),  targetName.trimmed());
    q.bindValue(QStringLiteral(":r"),  relationship.trimmed());
    q.bindValue(QStringLiteral(":ef"), evidenceFile);
    q.bindValue(QStringLiteral(":str"), strength);
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

int LibrarianDatabase::resolvePendingRelationships()
{
    QSqlDatabase db = database();
    if (!db.isOpen()) return 0;

    // Snapshot the queue first so we can iterate without invalidating the
    // SELECT cursor when we issue DELETE/INSERT mid-loop.
    struct Pending {
        qint64 id;
        qint64 sourceId;
        QString targetName;
        QString relationship;
        QString evidenceFile;
        double strength;
    };
    QVector<Pending> rows;
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral(
                "SELECT id, source_id, target_name, relationship, evidence_file, strength "
                "FROM pending_relationships"))) {
            m_lastError = q.lastError().text();
            return 0;
        }
        while (q.next()) {
            Pending p;
            p.id           = q.value(0).toLongLong();
            p.sourceId     = q.value(1).toLongLong();
            p.targetName   = q.value(2).toString();
            p.relationship = q.value(3).toString();
            p.evidenceFile = q.value(4).toString();
            p.strength     = q.value(5).toDouble();
            rows.append(p);
        }
    }

    int resolved = 0;
    QVector<qint64> unresolvedIds;
    for (const Pending &p : rows) {
        const qint64 targetId = resolveEntityByName(p.targetName);
        if (targetId < 0) {
            unresolvedIds.append(p.id);
            continue;
        }
        if (targetId == p.sourceId) {
            // Self-loop — don't write, but drop the pending row so we
            // don't keep retrying.
            QSqlQuery del(db);
            del.prepare(QStringLiteral("DELETE FROM pending_relationships WHERE id = :id"));
            del.bindValue(QStringLiteral(":id"), p.id);
            del.exec();
            continue;
        }
        // Containment is stored as a parent_id on the entities row, not
        // as a relationships row. Synthetic "is_part_of" rows queued by
        // the librarian's pass-3 parent-resolution miss are applied via
        // setEntityParent here.
        bool applied = false;
        if (p.relationship == QLatin1String("is_part_of")) {
            applied = setEntityParent(p.sourceId, targetId);
        } else {
            applied = (upsertRelationship(p.sourceId, targetId, p.relationship,
                                           p.evidenceFile, /*line=*/0, p.strength) >= 0);
        }
        if (applied) {
            QSqlQuery del(db);
            del.prepare(QStringLiteral("DELETE FROM pending_relationships WHERE id = :id"));
            del.bindValue(QStringLiteral(":id"), p.id);
            if (del.exec()) ++resolved;
        } else {
            // Insert collided / failed — count as unresolved so we GC
            // it eventually.
            unresolvedIds.append(p.id);
        }
    }

    // Bump attempts on still-unresolved rows. Done in a single bulk
    // UPDATE to avoid one round-trip per row.
    if (!unresolvedIds.isEmpty()) {
        QStringList placeholders;
        placeholders.reserve(unresolvedIds.size());
        for (int i = 0; i < unresolvedIds.size(); ++i) {
            placeholders << QStringLiteral("?");
        }
        QSqlQuery bump(db);
        bump.prepare(QStringLiteral(
            "UPDATE pending_relationships SET attempts = attempts + 1 "
            "WHERE id IN (%1)").arg(placeholders.join(QLatin1Char(','))));
        for (qint64 id : unresolvedIds) bump.addBindValue(id);
        bump.exec();
    }

    // GC: drop rows that have failed too many times. 10 retries ≈ 10
    // semantic-batch ticks of failed resolution; enough slack for slow
    // multi-document projects but small enough to keep the queue
    // bounded.
    constexpr int kMaxAttempts = 10;
    QSqlQuery gc(db);
    gc.prepare(QStringLiteral(
        "DELETE FROM pending_relationships WHERE attempts >= :n"));
    gc.bindValue(QStringLiteral(":n"), kMaxAttempts);
    if (gc.exec()) {
        const int dropped = gc.numRowsAffected();
        if (dropped > 0) {
            qInfo().noquote()
                << "LibrarianDatabase: GC'd" << dropped
                << "pending relationship(s) after >=" << kMaxAttempts
                << "failed resolution attempts";
        }
    }

    return resolved;
}

int LibrarianDatabase::getSemanticAttempts(const QString &fileHash) const
{
    if (fileHash.isEmpty()) return 0;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT attempts FROM file_extractions WHERE file_hash = :h"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

bool LibrarianDatabase::incrementSemanticAttempts(const QString &fileHash)
{
    if (fileHash.isEmpty()) return false;
    QSqlQuery q(database());
    // UPSERT-style: if the row doesn't yet exist (first-ever attempt
    // before any successful extraction), insert with attempts=1 and
    // last_path empty; the next recordExtractionState will fill it in.
    // If the row exists, just bump.
    q.prepare(QStringLiteral(
        "INSERT INTO file_extractions "
        "(file_hash, last_path, heuristic_done, semantic_done, extracted_at, attempts) "
        "VALUES (:h, '', 0, 0, :ts, 1) "
        "ON CONFLICT(file_hash) DO UPDATE SET "
        "  attempts = file_extractions.attempts + 1"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::resetSemanticAttempts(const QString &fileHash)
{
    if (fileHash.isEmpty()) return false;
    QSqlQuery q(database());
    q.prepare(QStringLiteral("UPDATE file_extractions SET attempts = 0 WHERE file_hash = :h"));
    q.bindValue(QStringLiteral(":h"), fileHash);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::clearAllSemanticAttempts()
{
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral("UPDATE file_extractions SET attempts = 0"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

int LibrarianDatabase::garbageCollectMissingFiles()
{
    QSqlDatabase db = database();
    if (!db.isOpen()) return 0;

    // Walk the table and collect hashes whose last_path no longer
    // exists. We do the existence check in C++ (not SQL) because SQLite
    // has no native filesystem function.
    QStringList toDelete;
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral(
                "SELECT file_hash, last_path FROM file_extractions"))) {
            m_lastError = q.lastError().text();
            return 0;
        }
        while (q.next()) {
            const QString hash = q.value(0).toString();
            const QString path = q.value(1).toString();
            if (path.isEmpty()) continue;
            if (!QFileInfo::exists(path)) toDelete.append(hash);
        }
    }
    if (toDelete.isEmpty()) return 0;

    int dropped = 0;
    for (const QString &hash : toDelete) {
        if (forgetExtraction(hash)) ++dropped;
    }
    return dropped;
}
