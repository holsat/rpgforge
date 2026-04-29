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
 * Tests for EntityGraphModel — the data layer behind the entity graph
 * view (Phase 2). Verifies load semantics and the three-layer filter
 * composition (type → search → neighborhood).
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSignalSpy>

#include "../src/librariandatabase.h"
#include "../src/entitygraphmodel.h"

class TestEntityGraphModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        m_db = new LibrarianDatabase();
        const QString dbPath = m_dir->path() + QStringLiteral("/.rpgforge.db");
        QVERIFY(m_db->open(dbPath));

        // Fixture: 4 entities + a small relationship topology.
        //
        //   Ryzen ──friend──► Sah'mokhan
        //   Ryzen ──member_of──► Phoenix Cult
        //   Sah'mokhan ──member_of──► Phoenix Cult
        //   Phoenix Cult HQ ─(parent_id)─► Phoenix Cult     (containment)
        m_ryzen = m_db->addEntity(QStringLiteral("Ryzen"),
                                    QStringLiteral("character"),
                                    QStringLiteral("a.md"));
        m_sah = m_db->addEntity(QStringLiteral("Sah'mokhan"),
                                  QStringLiteral("character"),
                                  QStringLiteral("a.md"));
        m_cult = m_db->addEntity(QStringLiteral("Phoenix Cult"),
                                   QStringLiteral("concept"),
                                   QStringLiteral("a.md"));
        m_hq = m_db->addEntity(QStringLiteral("Phoenix Cult HQ"),
                                 QStringLiteral("place"),
                                 QStringLiteral("a.md"));

        m_db->addAlias(m_ryzen, QStringLiteral("Ryz"));
        m_db->addTag(m_ryzen, QStringLiteral("protagonist"));
        m_db->setEntitySummary(m_ryzen, QStringLiteral("A young sailor."));

        m_db->upsertRelationship(m_ryzen, m_sah, QStringLiteral("friend"),
                                  QString(), 0, 0.7);
        m_db->upsertRelationship(m_ryzen, m_cult, QStringLiteral("member_of"),
                                  QString(), 0, 0.5);
        m_db->upsertRelationship(m_sah, m_cult, QStringLiteral("member_of"),
                                  QString(), 0, 0.5);
        m_db->setEntityParent(m_hq, m_cult);
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

    /// reload() pulls all entities + edges including the synthetic
    /// is_part_of edge derived from parent_id.
    void testReloadProducesNodesAndEdges()
    {
        EntityGraphModel m(m_db);
        m.reload();
        QCOMPARE(m.allNodes().size(), 4);
        // 3 explicit relationships + 1 derived parent_id edge = 4 edges.
        QCOMPARE(m.allEdges().size(), 4);

        bool sawIsPartOf = false;
        for (const auto &e : m.allEdges()) {
            if (e.relationship == QStringLiteral("is_part_of")
                && e.sourceId == m_hq && e.targetId == m_cult) {
                sawIsPartOf = true;
                break;
            }
        }
        QVERIFY(sawIsPartOf);
    }

    /// Type filter drops nodes whose type isn't in the allowed set, and
    /// edges that cross out of the visible subgraph.
    void testTypeFilterHidesNonMatchingNodesAndEdges()
    {
        EntityGraphModel m(m_db);
        m.reload();
        m.setTypeFilter({QStringLiteral("character")});

        const auto nodes = m.filteredNodes();
        QCOMPARE(nodes.size(), 2);          // Ryzen + Sah'mokhan only

        const auto edges = m.filteredEdges();
        // Only the friend edge stays; the two member_of edges and the
        // is_part_of edge all touch the concept/place we've hidden.
        QCOMPARE(edges.size(), 1);
        QCOMPARE(edges[0].relationship, QStringLiteral("friend"));
    }

    /// Search by alias resolves to the canonical entity AND brings in
    /// 1-hop neighbors so the graph stays connected.
    void testSearchByAliasIncludesNeighbors()
    {
        EntityGraphModel m(m_db);
        m.reload();
        m.setSearchQuery(QStringLiteral("Ryz"));     // alias of Ryzen

        const auto nodes = m.filteredNodes();
        // Ryzen + Sah'mokhan + Phoenix Cult (Ryzen's 1-hop neighbors).
        // Phoenix Cult HQ is 2 hops away (HQ → Cult → Ryzen) so excluded.
        QSet<qint64> visible;
        for (const auto &n : nodes) visible.insert(n.id);
        QVERIFY(visible.contains(m_ryzen));
        QVERIFY(visible.contains(m_sah));
        QVERIFY(visible.contains(m_cult));
        QVERIFY(!visible.contains(m_hq));
    }

    /// Empty search clears the filter — back to all 4 nodes.
    void testEmptySearchRestoresFullGraph()
    {
        EntityGraphModel m(m_db);
        m.reload();
        m.setSearchQuery(QStringLiteral("Ryz"));
        m.setSearchQuery(QString());
        QCOMPARE(m.filteredNodes().size(), 4);
    }

    /// Neighborhood focus on Sah'mokhan with 1 hop returns Sah +
    /// Ryzen + Phoenix Cult (his direct neighbors).
    void testNeighborhoodFocusOneHop()
    {
        EntityGraphModel m(m_db);
        m.reload();
        m.setNeighborhoodFocus(m_sah, 1);

        QSet<qint64> visible;
        for (const auto &n : m.filteredNodes()) visible.insert(n.id);
        QCOMPARE(visible.size(), 3);
        QVERIFY(visible.contains(m_sah));
        QVERIFY(visible.contains(m_ryzen));
        QVERIFY(visible.contains(m_cult));
    }

    /// 2-hop reaches HQ via the Phoenix Cult parent edge.
    void testNeighborhoodFocusTwoHops()
    {
        EntityGraphModel m(m_db);
        m.reload();
        m.setNeighborhoodFocus(m_sah, 2);

        QSet<qint64> visible;
        for (const auto &n : m.filteredNodes()) visible.insert(n.id);
        QVERIFY(visible.contains(m_hq));    // newly reachable at 2 hops
    }

    /// modelChanged fires on reload + on any filter change, and is
    /// silent when a setter is called with the same value.
    void testModelChangedSignalSemantics()
    {
        EntityGraphModel m(m_db);
        QSignalSpy spy(&m, &EntityGraphModel::modelChanged);

        m.reload();
        QCOMPARE(spy.count(), 1);

        m.setSearchQuery(QStringLiteral("Ryz"));
        QCOMPARE(spy.count(), 2);
        m.setSearchQuery(QStringLiteral("Ryz"));     // duplicate — no signal
        QCOMPARE(spy.count(), 2);
    }

private:
    QTemporaryDir *m_dir = nullptr;
    LibrarianDatabase *m_db = nullptr;
    qint64 m_ryzen = -1;
    qint64 m_sah = -1;
    qint64 m_cult = -1;
    qint64 m_hq = -1;
};

QTEST_MAIN(TestEntityGraphModel)
#include "test_entitygraphmodel.moc"
