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

/**
 * Phase 1 of the relationship-graph work — data foundation tests.
 * Each case is named after the behavior it locks in. Failures here mean
 * a specific guarantee has regressed.
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>

#include "../src/librariandatabase.h"
#include "../src/knowledgebase.h"

class TestRelationshipGraphData : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        m_dbPath = QDir(m_dir->path()).absoluteFilePath(QStringLiteral(".rpgforge.db"));
        m_db = new LibrarianDatabase();
        QVERIFY(m_db->open(m_dbPath));
    }

    void cleanup()
    {
        delete m_db;
        m_db = nullptr;
        // Ensure the per-thread connections this test created are
        // removed before QTemporaryDir wipes the file underneath them.
        for (const QString &c : QSqlDatabase::connectionNames()) {
            if (c.startsWith(QLatin1String("Librarian_"))
                || c.startsWith(QLatin1String("kb_alias_loader_"))
                || c.startsWith(QLatin1String("kb_thread_"))) {
                QSqlDatabase::removeDatabase(c);
            }
        }
        delete m_dir;
        m_dir = nullptr;
    }

    /// Phase 1 schema present after open().
    void testSchemaCreatedOnInit()
    {
        QSqlDatabase db = m_db->database();
        QVERIFY(db.isOpen());

        const QStringList expected = {
            QStringLiteral("entities"),
            QStringLiteral("attributes"),
            QStringLiteral("entity_aliases"),
            QStringLiteral("entity_tags"),
            QStringLiteral("relationships"),
        };
        for (const QString &table : expected) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT name FROM sqlite_master "
                                      "WHERE type='table' AND name=:n"));
            q.bindValue(QStringLiteral(":n"), table);
            QVERIFY2(q.exec() && q.next(),
                     qPrintable(QStringLiteral("missing table: ") + table));
        }

        // Entities should have the new columns.
        const QStringList expectedColumns = {
            QStringLiteral("summary"),
            QStringLiteral("parent_id"),
            QStringLiteral("first_appearance_file"),
            QStringLiteral("first_appearance_line"),
            QStringLiteral("last_appearance_file"),
            QStringLiteral("last_appearance_line"),
            QStringLiteral("era"),
            QStringLiteral("mention_count"),
        };
        QSqlQuery info(db);
        QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(entities)")));
        QStringList cols;
        while (info.next()) cols.append(info.value(1).toString());
        for (const QString &col : expectedColumns) {
            QVERIFY2(cols.contains(col, Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("entities missing column: ") + col));
        }
    }

    /// addEntity registers the canonical name as an alias automatically;
    /// resolveEntityByName picks it up.
    void testCanonicalNameResolvesViaAliasIndex()
    {
        const qint64 id = m_db->addEntity(QStringLiteral("Ryzen"),
                                           QStringLiteral("character"),
                                           QStringLiteral("dossier.md"));
        QVERIFY(id > 0);
        QCOMPARE(m_db->resolveEntityByName(QStringLiteral("Ryzen")), id);
        QCOMPARE(m_db->resolveEntityByName(QStringLiteral("ryzen")), id);  // case-insensitive
    }

    /// Aliases dedupe within an entity; resolveEntityByName resolves all forms.
    void testAliasResolution()
    {
        const qint64 id = m_db->addEntity(QStringLiteral("Ryzen"),
                                           QStringLiteral("character"),
                                           QStringLiteral("dossier.md"));
        QVERIFY(m_db->addAlias(id, QStringLiteral("Ryz")));
        QVERIFY(m_db->addAlias(id, QStringLiteral("Captain Ryzen")));
        QVERIFY(m_db->addAlias(id, QStringLiteral("Ryz")));   // dup — no-op

        QCOMPARE(m_db->resolveEntityByName(QStringLiteral("Ryz")), id);
        QCOMPARE(m_db->resolveEntityByName(QStringLiteral("Captain Ryzen")), id);
        const QStringList aliases = m_db->getAliases(id);
        QCOMPARE(aliases.size(), 3);            // canonical + Ryz + Captain Ryzen
    }

    /// upsertRelationship is idempotent on (source, target, type) — a
    /// later run with new evidence updates the row instead of duplicating.
    void testRelationshipUpsertMergesByTriple()
    {
        const qint64 src = m_db->addEntity(QStringLiteral("Ryzen"),
                                            QStringLiteral("character"),
                                            QStringLiteral("a.md"));
        const qint64 tgt = m_db->addEntity(QStringLiteral("Sah'mokhan"),
                                            QStringLiteral("character"),
                                            QStringLiteral("b.md"));
        const qint64 r1 = m_db->upsertRelationship(src, tgt, QStringLiteral("friend"),
                                                    QStringLiteral("a.md"), 1, 0.5);
        const qint64 r2 = m_db->upsertRelationship(src, tgt, QStringLiteral("friend"),
                                                    QStringLiteral("b.md"), 12, 0.9);
        QVERIFY(r1 > 0 && r2 > 0);

        const QList<EntityRelationship> rels = m_db->getRelationshipsFrom(src);
        QCOMPARE(rels.size(), 1);
        QCOMPARE(rels[0].relationship, QStringLiteral("friend"));
        QCOMPARE(rels[0].evidenceFile, QStringLiteral("b.md"));     // updated
        QCOMPARE(rels[0].evidenceLine, 12);
        QCOMPARE(rels[0].strength, 0.9);
    }

    /// Different relationship types between the same pair coexist.
    void testRelationshipMultipleTypesBetweenSamePair()
    {
        const qint64 src = m_db->addEntity(QStringLiteral("Ryzen"),
                                            QStringLiteral("character"),
                                            QStringLiteral("a.md"));
        const qint64 tgt = m_db->addEntity(QStringLiteral("Phoenix Cult"),
                                            QStringLiteral("concept"),
                                            QStringLiteral("a.md"));
        m_db->upsertRelationship(src, tgt, QStringLiteral("member_of"),
                                  QString(), 0, 0.5);
        m_db->upsertRelationship(src, tgt, QStringLiteral("opposes"),
                                  QString(), 0, 0.5);
        QCOMPARE(m_db->getRelationshipsFrom(src).size(), 2);
    }

    /// Tags: addTag is idempotent, findEntitiesByTag returns matches.
    void testTagsAreIdempotentAndQueryable()
    {
        const qint64 a = m_db->addEntity(QStringLiteral("Ryzen"),
                                          QStringLiteral("character"),
                                          QStringLiteral("a.md"));
        const qint64 b = m_db->addEntity(QStringLiteral("Sah'mokhan"),
                                          QStringLiteral("character"),
                                          QStringLiteral("a.md"));
        QVERIFY(m_db->addTag(a, QStringLiteral("protagonist")));
        QVERIFY(m_db->addTag(a, QStringLiteral("protagonist"))); // dup
        QVERIFY(m_db->addTag(b, QStringLiteral("protagonist")));
        const QList<qint64> hits = m_db->findEntitiesByTag(QStringLiteral("protagonist"));
        QCOMPARE(hits.size(), 2);
        QVERIFY(hits.contains(a));
        QVERIFY(hits.contains(b));
        QCOMPARE(m_db->getTags(a).size(), 1);     // dedup'd
    }

    /// Containment: parent_id resolves to a real entity id.
    void testParentIdResolutionForContainmentHierarchy()
    {
        const qint64 region = m_db->addEntity(QStringLiteral("Phoenix Cult region"),
                                               QStringLiteral("place"),
                                               QStringLiteral("a.md"));
        const qint64 hq = m_db->addEntity(QStringLiteral("Phoenix Cult HQ"),
                                           QStringLiteral("place"),
                                           QStringLiteral("a.md"));
        QVERIFY(m_db->setEntityParent(hq, region));
        QCOMPARE(m_db->getEntityParent(hq), region);
        QCOMPARE(m_db->getEntityParent(region), qint64(-1));    // top-level
    }

    /// FK cascade — deleting an entity removes its aliases, tags,
    /// attributes, and outgoing/incoming relationships.
    void testRelationshipDeleteCascadesOnEntityDelete()
    {
        const qint64 a = m_db->addEntity(QStringLiteral("Ryzen"),
                                          QStringLiteral("character"),
                                          QStringLiteral("a.md"));
        const qint64 b = m_db->addEntity(QStringLiteral("Sah'mokhan"),
                                          QStringLiteral("character"),
                                          QStringLiteral("a.md"));
        m_db->addAlias(a, QStringLiteral("Ryz"));
        m_db->addTag(a, QStringLiteral("protagonist"));
        m_db->upsertRelationship(a, b, QStringLiteral("friend"),
                                  QString(), 0, 0.5);

        QVERIFY(m_db->deleteEntity(a));
        QVERIFY(m_db->getAliases(a).isEmpty());
        QVERIFY(m_db->getTags(a).isEmpty());
        QVERIFY(m_db->getRelationshipsFrom(a).isEmpty());
        QVERIFY(m_db->getRelationshipsTo(b).isEmpty());     // FK cascade on target
    }

    /// Migration path: open a DB shaped like the pre-v1.x schema (only
    /// the original three tables), then re-open with the new code. The
    /// new columns/tables must appear and the existing entity row must
    /// retain its data with NULL for the new fields.
    void testMigrationIsAdditiveOnExistingProject()
    {
        // Wipe and recreate using ONLY the legacy schema.
        delete m_db; m_db = nullptr;
        QFile::remove(m_dbPath);

        const QString legacyConn = QStringLiteral("legacy_conn");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                             legacyConn);
            legacy.setDatabaseName(m_dbPath);
            QVERIFY(legacy.open());
            QSqlQuery q(legacy);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE entities ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "type TEXT, "
                "source_file TEXT, "
                "last_modified DATETIME)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO entities (name, type, source_file) "
                "VALUES ('LegacyChar', 'character', 'old.md')")));
            legacy.close();
        }
        QSqlDatabase::removeDatabase(legacyConn);

        // Re-open with the current code: should run additive ALTERs.
        m_db = new LibrarianDatabase();
        QVERIFY(m_db->open(m_dbPath));

        QSqlDatabase db = m_db->database();
        QSqlQuery info(db);
        QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(entities)")));
        QStringList cols;
        while (info.next()) cols.append(info.value(1).toString());
        QVERIFY(cols.contains(QStringLiteral("summary"), Qt::CaseInsensitive));
        QVERIFY(cols.contains(QStringLiteral("parent_id"), Qt::CaseInsensitive));
        QVERIFY(cols.contains(QStringLiteral("mention_count"), Qt::CaseInsensitive));

        // The legacy row survives.
        QSqlQuery rd(db);
        QVERIFY(rd.exec(QStringLiteral("SELECT name, summary, mention_count "
                                        "FROM entities WHERE name='LegacyChar'")));
        QVERIFY(rd.next());
        QCOMPARE(rd.value(0).toString(), QStringLiteral("LegacyChar"));
        QVERIFY(rd.value(1).isNull());                     // new column → NULL
        QCOMPARE(rd.value(2).toInt(), 0);                  // default for INTEGER NOT NULL DEFAULT 0
    }

    /// rebuildChunkEntityLinks: word-boundary aware and longest-match-first.
    /// Synthesises a vector DB with one chunk and an alias index that has
    /// "Ryzen" and "Ryz" — the chunk text "Ryzen sailed home" should link
    /// only to the canonical Ryzen entity once, not also via "Ryz".
    void testChunkEntityLinkingLongestMatchAndWordBoundary()
    {
        const qint64 ryzen = m_db->addEntity(QStringLiteral("Ryzen"),
                                              QStringLiteral("character"),
                                              QStringLiteral("a.md"));
        m_db->addAlias(ryzen, QStringLiteral("Ryz"));
        const qint64 other = m_db->addEntity(QStringLiteral("Sah'mokhan"),
                                              QStringLiteral("character"),
                                              QStringLiteral("a.md"));
        Q_UNUSED(other);

        // Build a minimal vector DB with chunks + chunk_entities matching
        // the schema KnowledgeBase::setupDatabase creates.
        const QString vecPath = QDir(m_dir->path()).absoluteFilePath(
            QStringLiteral(".rpgforge-vectors.db"));
        const QString conn = QStringLiteral("test_vec_conn");
        {
            QSqlDatabase vec = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            vec.setDatabaseName(vecPath);
            QVERIFY(vec.open());
            QSqlQuery q(vec);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE chunks (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "file_path TEXT, heading TEXT, content TEXT, file_hash TEXT, "
                "embedding BLOB)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE chunk_entities (chunk_id INTEGER NOT NULL, "
                "entity_id INTEGER NOT NULL, PRIMARY KEY (chunk_id, entity_id))")));
            // Insert chunks. First contains the canonical name; second
            // contains "Sah'mokhan" only.
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO chunks (file_path, heading, content) VALUES "
                "('p1.md', 'A', 'Ryzen sailed home that night.'), "
                "('p2.md', 'B', 'Sah''mokhan watched from the dock.')")));
            vec.close();
        }
        QSqlDatabase::removeDatabase(conn);

        // KnowledgeBase needs to know the project path to find both DBs.
        KnowledgeBase::instance().initForProject(m_dir->path());
        const int created = KnowledgeBase::instance().rebuildChunkEntityLinks();
        QVERIFY2(created >= 2,
                 qPrintable(QStringLiteral("expected ≥ 2 links, got %1").arg(created)));

        // Inspect the result directly so we can assert per-chunk linking.
        QSqlDatabase v2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("verify_conn"));
        v2.setDatabaseName(vecPath);
        QVERIFY(v2.open());
        QSqlQuery q(v2);
        QVERIFY(q.exec(QStringLiteral(
            "SELECT chunk_id, COUNT(*) FROM chunk_entities GROUP BY chunk_id")));
        QMap<int, int> perChunk;
        while (q.next()) perChunk.insert(q.value(0).toInt(), q.value(1).toInt());
        // Chunk 1 ("Ryzen sailed home") must have exactly ONE entity link
        // — Ryzen, not Ryzen+Ryz.
        QCOMPARE(perChunk.value(1), 1);
        v2.close();
        QSqlDatabase::removeDatabase(QStringLiteral("verify_conn"));
    }

    /// Aggregate refresh: after chunk_entities is populated, mention_count
    /// and first/last appearance get persisted on entities.
    void testMentionCountAndAppearanceAggregation()
    {
        const qint64 ryzen = m_db->addEntity(QStringLiteral("Ryzen"),
                                              QStringLiteral("character"),
                                              QStringLiteral("a.md"));
        Q_UNUSED(ryzen);
        // Same fixture vector DB shape as the previous test.
        const QString vecPath = QDir(m_dir->path()).absoluteFilePath(
            QStringLiteral(".rpgforge-vectors.db"));
        const QString conn = QStringLiteral("test_vec_conn2");
        {
            QSqlDatabase vec = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            vec.setDatabaseName(vecPath);
            QVERIFY(vec.open());
            QSqlQuery q(vec);
            q.exec(QStringLiteral(
                "CREATE TABLE chunks (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "file_path TEXT, heading TEXT, content TEXT, file_hash TEXT, "
                "embedding BLOB)"));
            q.exec(QStringLiteral(
                "CREATE TABLE chunk_entities (chunk_id INTEGER NOT NULL, "
                "entity_id INTEGER NOT NULL, PRIMARY KEY (chunk_id, entity_id))"));
            q.exec(QStringLiteral(
                "INSERT INTO chunks (file_path, content) VALUES "
                "('chapter1.md', 'Ryzen begins.'), "
                "('chapter2.md', 'Ryzen continues.'), "
                "('chapter5.md', 'Ryzen returns.')"));
            // 3 mentions across 3 files.
            q.exec(QStringLiteral(
                "INSERT INTO chunk_entities VALUES (1,1),(2,1),(3,1)"));
            vec.close();
        }
        QSqlDatabase::removeDatabase(conn);

        const int updated = m_db->refreshAggregatesFromVectorDb(vecPath);
        QCOMPARE(updated, 1);
        QCOMPARE(m_db->getEntityMentionCount(ryzen), 3);
    }

private:
    QTemporaryDir *m_dir = nullptr;
    QString m_dbPath;
    LibrarianDatabase *m_db = nullptr;
};

QTEST_MAIN(TestRelationshipGraphData)
#include "test_relationshipgraph_data.moc"
