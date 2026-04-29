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

#ifndef LIBRARIANDATABASE_H
#define LIBRARIANDATABASE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

/**
 * @brief One row from the relationships table. Source and target are
 * entity ids; relationship is a free-form type string ('friend',
 * 'member_of', 'located_in', etc.). Strength is 0..1, used for both
 * LLM confidence and graph-view edge weighting.
 */
struct EntityRelationship {
    qint64 id = -1;
    qint64 sourceId = -1;
    qint64 targetId = -1;
    QString relationship;
    QString evidenceFile;
    int evidenceLine = 0;
    double strength = 0.5;
};

/**
 * @brief One alias / nickname / title for an entity. is_primary marks
 * the canonical display name (one row per entity should have it set).
 */
struct EntityAlias {
    qint64 id = -1;
    qint64 entityId = -1;
    QString alias;
    bool isPrimary = false;
};

/**
 * @brief The LibrarianDatabase class manages a flexible EAV (Entity-Attribute-Value)
 * SQLite database for game design data. As of the relationship-graph work it also
 * stores typed entity↔entity relationships, aliases, tags, and aggregate metadata
 * (mention_count, first/last appearance) used by the entity graph view and
 * graph-augmented retrieval.
 */
class LibrarianDatabase : public QObject
{
    Q_OBJECT
public:
    explicit LibrarianDatabase(QObject *parent = nullptr);
    ~LibrarianDatabase();

    bool open(const QString &path);
    void close();

    /**
     * @brief Tear down the current thread's QSqlDatabase connection.
     *
     * Worker threads (QThreadPool runnables) may open a connection via
     * database() to do extraction work. Those threads typically have no
     * event loop, so close() — which runs on the main thread — cannot
     * dispatch the per-connection teardown back to them via
     * BlockingQueuedConnection. The supported pattern is therefore: each
     * worker calls closeCurrentThreadConnection() before returning to the
     * pool, releasing its connection on its own thread. Calling from the
     * main thread is also safe; it just closes main's connection.
     *
     * Idempotent — a no-op if the current thread has no connection.
     */
    void closeCurrentThreadConnection();

    bool beginTransaction();
    bool commit();

    // Entity Management
    qint64 addEntity(const QString &name, const QString &type, const QString &sourceFile);
    bool updateEntity(qint64 id, const QString &name, const QString &type);
    bool deleteEntity(qint64 id);

    // Relationship-graph extensions (Phase 1 of the entity-graph work).
    // setEntitySummary sets the one-line blurb shown in tooltips and graph
    // hovers. NULL summary is treated as "not yet extracted".
    bool setEntitySummary(qint64 id, const QString &summary);
    // Containment hierarchy. parent is another entity (e.g. "Phoenix Cult HQ"
    // contained in "Phoenix Cult region"). Pass -1 to clear.
    bool setEntityParent(qint64 id, qint64 parentId);
    // In-world era / period. Free-form text in v1.
    bool setEntityEra(qint64 id, const QString &era);
    // Read accessors for the descriptive fields. Empty string when unset.
    QString getEntitySummary(qint64 id) const;
    QString getEntityEra(qint64 id) const;
    qint64  getEntityParent(qint64 id) const;
    int     getEntityMentionCount(qint64 id) const;
    qint64  getEntityCommunityId(qint64 id) const;
    QString getEntityName(qint64 id) const;
    QString getEntityType(qint64 id) const;
    QList<qint64> findEntitiesByCommunity(qint64 communityId) const;

    // Attribute Management
    bool setAttribute(qint64 entityId, const QString &key, const QVariant &value);
    QVariant getAttribute(qint64 entityId, const QString &key) const;
    QVariantMap getAttributes(qint64 entityId) const;

    // Aliases. Multiple aliases per entity, one optionally marked as primary.
    // addAlias is idempotent on (entity_id, alias) — duplicate inserts are
    // silently dropped via the UNIQUE constraint.
    bool addAlias(qint64 entityId, const QString &alias, bool isPrimary = false);
    QStringList getAliases(qint64 entityId) const;
    // Resolve a free-form name (or alias) to its canonical entity id. Used by
    // the librarian's three-pass write to map "Ryz" → Ryzen, and by
    // hybridSearch's graph arm to detect entity mentions in the query string.
    // Returns -1 when no match is found. Case-insensitive.
    qint64 resolveEntityByName(const QString &name) const;

    // Tags. Free-form secondary labels; multiple per entity. The query layer
    // can filter the graph view by tag in addition to type.
    bool addTag(qint64 entityId, const QString &tag);
    QStringList getTags(qint64 entityId) const;
    QList<qint64> findEntitiesByTag(const QString &tag) const;

    // Relationships. upsertRelationship merges by (source_id, target_id,
    // relationship) so re-extraction over the same evidence does not
    // accumulate duplicates.
    qint64 upsertRelationship(qint64 sourceId,
                               qint64 targetId,
                               const QString &relationship,
                               const QString &evidenceFile,
                               int evidenceLine,
                               double strength);
    QList<EntityRelationship> getRelationshipsFrom(qint64 entityId) const;
    QList<EntityRelationship> getRelationshipsTo(qint64 entityId) const;
    QList<EntityRelationship> getRelationships(qint64 entityId) const;   // both directions

    // Aggregate refresh: after chunk_entities is repopulated by the
    // KnowledgeBase reindex, recompute and persist mention_count,
    // first/last_appearance_file/line on every entity in one query.
    // Reads from the supplied vector-DB connection (chunks +
    // chunk_entities live there), writes to the librarian DB.
    // Returns the number of entities updated.
    int refreshAggregatesFromVectorDb(const QString &vectorDbPath);

    // Dependency Graph (legacy: entity ↔ file references). Kept around
    // for backwards-compat; the new chunk_entities table is the
    // graph-augmented retrieval index, not this one.
    bool addReference(qint64 entityId, const QString &referencingFile);
    QStringList getReferences(qint64 entityId) const;

    // Querying
    qint64 findEntity(const QString &name, const QString &type, const QString &sourceFile) const;
    QList<qint64> findEntitiesByType(const QString &type) const;
    QList<qint64> findEntitiesByAttribute(const QString &key, const QVariant &value) const;
    QList<qint64> allEntityIds() const;

    // ------------------------------------------------------------------
    // Per-file extraction state. The librarian keys extraction status by
    // SHA-256 of file contents (NOT by path or mtime) so that a pure
    // rename/move does not retrigger extraction, while genuine content
    // edits do. heuristic_done and semantic_done are independent — the
    // heuristic regex pass and the LLM pass mark themselves done
    // separately.
    // ------------------------------------------------------------------
    bool getExtractionState(const QString &fileHash,
                            bool *heuristicDone,
                            bool *semanticDone) const;
    bool recordExtractionState(const QString &fileHash,
                               const QString &path,
                               bool heuristicDone,
                               bool semanticDone);
    bool updateExtractionPath(const QString &fileHash, const QString &newPath);
    QString hashForPath(const QString &path) const;
    bool forgetExtraction(const QString &fileHash);
    // Reset semantic_done across all rows. Used by triggerSemanticReindex
    // to force a full re-walk of the LLM pass without disturbing the
    // (cheap) heuristic state.
    bool clearAllSemanticDone();

    // Per-hash semantic strike counter — persisted on the
    // file_extractions row so a permanently-broken file doesn't burn 3
    // LLM calls on every app restart. The increment is UPSERT-style
    // because the row may not exist yet (first attempt on a brand new
    // hash).
    int  getSemanticAttempts(const QString &fileHash) const;
    bool incrementSemanticAttempts(const QString &fileHash);
    bool resetSemanticAttempts(const QString &fileHash);
    bool clearAllSemanticAttempts();

    // Drop file_extractions rows whose last_path no longer exists on
    // disk. Used by triggerSemanticReindex so deleted files don't sit
    // in the queue forever. Returns the number of rows pruned.
    int garbageCollectMissingFiles();

    // Pending (deferred) relationship targets. When the LLM emits a
    // relationship to an entity that has not been ingested yet, we queue
    // it here and re-attempt resolution after every subsequent semantic
    // extraction so the cross-document edge eventually lands.
    bool queuePendingRelationship(qint64 sourceId,
                                  const QString &targetName,
                                  const QString &relationship,
                                  const QString &evidenceFile,
                                  double strength);
    int  resolvePendingRelationships();

    QSqlDatabase database() const;
    QString lastError() const { return m_lastError; }

private:
    bool initSchema(QSqlDatabase &db);
    // Helper used by initSchema to do additive migration of new columns
    // on the entities table without disturbing existing rows.
    bool addColumnIfMissing(QSqlDatabase &db,
                            const QString &table,
                            const QString &column,
                            const QString &columnDef);

    QString m_dbPath;
    QString m_lastError;

    // Connection-name → thread-that-created-it. Populated by database() on
    // every fresh addDatabase, consulted by close() to dispatch the
    // per-connection teardown to the owning thread (or detect that the
    // owner is gone). Mutex guards both the hash and the registration site
    // because database() may run on multiple threads concurrently.
    mutable QHash<QString, QPointer<QThread>> m_connectionThreads;
    mutable QMutex m_connectionThreadsMutex;
};

#endif // LIBRARIANDATABASE_H
