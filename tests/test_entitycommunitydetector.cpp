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
 * Phase 5 — community detection tests. Verifies that the modularity-
 * greedy algorithm groups densely-connected entity clusters together
 * and separates them from less-connected ones, and that detectAndPersist
 * writes the result back to entities.community_id.
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>

#include "../src/librariandatabase.h"
#include "../src/entitycommunitydetector.h"

class TestEntityCommunityDetector : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        m_db = new LibrarianDatabase();
        QVERIFY(m_db->open(m_dir->path() + QStringLiteral("/.rpgforge.db")));
    }

    void cleanup()
    {
        delete m_db; m_db = nullptr;
        for (const QString &c : QSqlDatabase::connectionNames()) {
            if (c.startsWith(QLatin1String("Librarian_"))) {
                QSqlDatabase::removeDatabase(c);
            }
        }
        delete m_dir; m_dir = nullptr;
    }

    /// Empty graph: zero entities → zero communities.
    void testEmptyGraphProducesNoCommunities()
    {
        EntityCommunityDetector det(m_db);
        const auto r = det.detect();
        QCOMPARE(r.communityCount, 0);
        QVERIFY(r.assignments.isEmpty());
    }

    /// Singleton: one entity, no edges → one community.
    void testSingleEntityGetsOneCommunity()
    {
        const qint64 a = m_db->addEntity(QStringLiteral("A"),
                                          QStringLiteral("character"),
                                          QStringLiteral("a.md"));
        EntityCommunityDetector det(m_db);
        const auto r = det.detect();
        QCOMPARE(r.communityCount, 1);
        QCOMPARE(r.assignments.size(), 1);
        QCOMPARE(r.assignments.value(a), qint64(0));
    }

    /// Two disconnected triangles: detector should split them into
    /// two distinct communities, each containing exactly its triangle.
    void testTwoDisconnectedTrianglesYieldTwoCommunities()
    {
        // Triangle 1: A-B-C
        const qint64 a = m_db->addEntity(QStringLiteral("A"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        const qint64 b = m_db->addEntity(QStringLiteral("B"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        const qint64 c = m_db->addEntity(QStringLiteral("C"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        m_db->upsertRelationship(a, b, QStringLiteral("knows"), QString(), 0, 1.0);
        m_db->upsertRelationship(b, c, QStringLiteral("knows"), QString(), 0, 1.0);
        m_db->upsertRelationship(a, c, QStringLiteral("knows"), QString(), 0, 1.0);

        // Triangle 2: D-E-F
        const qint64 d = m_db->addEntity(QStringLiteral("D"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        const qint64 e = m_db->addEntity(QStringLiteral("E"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        const qint64 f = m_db->addEntity(QStringLiteral("F"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        m_db->upsertRelationship(d, e, QStringLiteral("knows"), QString(), 0, 1.0);
        m_db->upsertRelationship(e, f, QStringLiteral("knows"), QString(), 0, 1.0);
        m_db->upsertRelationship(d, f, QStringLiteral("knows"), QString(), 0, 1.0);

        EntityCommunityDetector det(m_db);
        const auto r = det.detect();
        QCOMPARE(r.communityCount, 2);

        const qint64 ca = r.assignments.value(a);
        QCOMPARE(r.assignments.value(b), ca);
        QCOMPARE(r.assignments.value(c), ca);

        const qint64 cd = r.assignments.value(d);
        QCOMPARE(r.assignments.value(e), cd);
        QCOMPARE(r.assignments.value(f), cd);

        QVERIFY(ca != cd);     // different communities

        // Modularity for two well-separated triangles should be > 0.4.
        QVERIFY2(r.modularity > 0.4,
                 qPrintable(QStringLiteral("expected Q > 0.4, got %1").arg(r.modularity)));
    }

    /// detectAndPersist writes back to entities.community_id and the
    /// findEntitiesByCommunity query returns the right members.
    void testDetectAndPersistWritesBack()
    {
        const qint64 a = m_db->addEntity(QStringLiteral("A"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        const qint64 b = m_db->addEntity(QStringLiteral("B"),
                                          QStringLiteral("character"),
                                          QStringLiteral("p.md"));
        m_db->upsertRelationship(a, b, QStringLiteral("knows"), QString(), 0, 1.0);

        EntityCommunityDetector det(m_db);
        const auto r = det.detectAndPersist();
        QVERIFY(r.communityCount >= 1);

        const qint64 commA = m_db->getEntityCommunityId(a);
        const qint64 commB = m_db->getEntityCommunityId(b);
        QVERIFY(commA >= 0);
        QCOMPARE(commA, commB);              // single connected pair → one community

        const QList<qint64> members = m_db->findEntitiesByCommunity(commA);
        QCOMPARE(members.size(), 2);
        QVERIFY(members.contains(a));
        QVERIFY(members.contains(b));
    }

    /// Containment edges (parent_id) participate in clustering — a
    /// place inside a region should land in the same community as the
    /// region.
    void testContainmentEdgesParticipateInClustering()
    {
        const qint64 region = m_db->addEntity(QStringLiteral("Region"),
                                                QStringLiteral("place"),
                                                QStringLiteral("p.md"));
        const qint64 hq = m_db->addEntity(QStringLiteral("HQ"),
                                            QStringLiteral("place"),
                                            QStringLiteral("p.md"));
        m_db->setEntityParent(hq, region);

        EntityCommunityDetector det(m_db);
        const auto r = det.detect();
        QCOMPARE(r.assignments.value(hq), r.assignments.value(region));
    }

private:
    QTemporaryDir *m_dir = nullptr;
    LibrarianDatabase *m_db = nullptr;
};

QTEST_MAIN(TestEntityCommunityDetector)
#include "test_entitycommunitydetector.moc"
