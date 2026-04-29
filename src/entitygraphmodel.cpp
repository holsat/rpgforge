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

#include "entitygraphmodel.h"
#include "librariandatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>

EntityGraphModel::EntityGraphModel(LibrarianDatabase *db, QObject *parent)
    : QObject(parent), m_db(db)
{
}

void EntityGraphModel::reload()
{
    m_nodes.clear();
    m_edges.clear();
    m_indexById.clear();
    m_aliasesById.clear();
    if (!m_db) {
        m_visibleIds.clear();
        Q_EMIT modelChanged();
        return;
    }

    QSqlDatabase db = m_db->database();
    if (!db.isOpen()) {
        m_visibleIds.clear();
        Q_EMIT modelChanged();
        return;
    }

    // Nodes — single SELECT with LEFT JOIN to grab tags inline as a
    // comma-separated string so we don't N+1 the database. mention_count
    // is a cached column populated by refreshAggregatesFromVectorDb.
    QSqlQuery nodeQ(db);
    if (!nodeQ.exec(QStringLiteral(
            "SELECT e.id, e.name, e.type, e.summary, e.mention_count, "
            "       (SELECT GROUP_CONCAT(tag, '|') FROM entity_tags "
            "          WHERE entity_id = e.id) AS tags "
            "FROM entities e ORDER BY e.id"))) {
        m_visibleIds.clear();
        Q_EMIT modelChanged();
        return;
    }
    while (nodeQ.next()) {
        EntityGraphNode n;
        n.id = nodeQ.value(0).toLongLong();
        n.name = nodeQ.value(1).toString();
        n.type = nodeQ.value(2).toString();
        n.summary = nodeQ.value(3).toString();
        n.mentionCount = nodeQ.value(4).toInt();
        const QString tagBlob = nodeQ.value(5).toString();
        if (!tagBlob.isEmpty()) {
            n.tags = tagBlob.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        }
        m_indexById.insert(n.id, m_nodes.size());
        m_nodes.append(n);
    }

    // Aliases for search-by-alias.
    QSqlQuery aliasQ(db);
    if (aliasQ.exec(QStringLiteral("SELECT entity_id, alias FROM entity_aliases"))) {
        while (aliasQ.next()) {
            m_aliasesById[aliasQ.value(0).toLongLong()].append(
                aliasQ.value(1).toString());
        }
    }

    // Peer-relationship edges from the relationships table.
    QSqlQuery relQ(db);
    if (relQ.exec(QStringLiteral(
            "SELECT source_id, target_id, relationship, strength "
            "FROM relationships"))) {
        while (relQ.next()) {
            EntityGraphEdge e;
            e.sourceId = relQ.value(0).toLongLong();
            e.targetId = relQ.value(1).toLongLong();
            e.relationship = relQ.value(2).toString();
            e.strength = relQ.value(3).toDouble();
            m_edges.append(e);
        }
    }

    // Containment edges derived from entities.parent_id. Stored as a
    // synthetic 'is_part_of' edge so the UI can render it like any
    // other relationship while still being able to special-case its
    // semantics (breadcrumb navigation, etc.) via the edge type string.
    QSqlQuery parQ(db);
    if (parQ.exec(QStringLiteral(
            "SELECT id, parent_id FROM entities WHERE parent_id IS NOT NULL"))) {
        while (parQ.next()) {
            EntityGraphEdge e;
            e.sourceId = parQ.value(0).toLongLong();
            e.targetId = parQ.value(1).toLongLong();
            e.relationship = QStringLiteral("is_part_of");
            e.strength = 1.0;        // containment is a hard structural edge
            m_edges.append(e);
        }
    }

    recomputeFilter();
    Q_EMIT modelChanged();
}

QSet<QString> EntityGraphModel::allTypes() const
{
    QSet<QString> out;
    for (const auto &n : m_nodes) {
        if (!n.type.isEmpty()) out.insert(n.type);
    }
    return out;
}

void EntityGraphModel::setTypeFilter(const QSet<QString> &allowed)
{
    if (m_typeFilter == allowed) return;
    m_typeFilter = allowed;
    recomputeFilter();
    Q_EMIT modelChanged();
}

void EntityGraphModel::setSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (m_searchQuery == trimmed) return;
    m_searchQuery = trimmed;
    recomputeFilter();
    Q_EMIT modelChanged();
}

void EntityGraphModel::setNeighborhoodFocus(qint64 entityId, int hops)
{
    if (m_focusEntity == entityId && m_focusHops == hops) return;
    m_focusEntity = entityId;
    m_focusHops = qMax(0, hops);
    recomputeFilter();
    Q_EMIT modelChanged();
}

void EntityGraphModel::clearNeighborhoodFocus()
{
    setNeighborhoodFocus(-1, 1);
}

const EntityGraphNode *EntityGraphModel::nodeById(qint64 id) const
{
    auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) return nullptr;
    return &m_nodes[*it];
}

QList<qint64> EntityGraphModel::neighborsOf(qint64 entityId, int hops) const
{
    QSet<qint64> out;
    if (entityId < 0 || hops < 0) return out.values();
    out.insert(entityId);
    QSet<qint64> frontier{entityId};
    for (int h = 0; h < hops; ++h) {
        QSet<qint64> next;
        for (qint64 cur : frontier) {
            for (const EntityGraphEdge &e : m_edges) {
                if (e.sourceId == cur && !out.contains(e.targetId)) {
                    next.insert(e.targetId);
                }
                if (e.targetId == cur && !out.contains(e.sourceId)) {
                    next.insert(e.sourceId);
                }
            }
        }
        out += next;
        frontier = next;
        if (frontier.isEmpty()) break;
    }
    return out.values();
}

void EntityGraphModel::recomputeFilter()
{
    m_visibleIds.clear();

    // Start with the type-filtered set. An empty m_typeFilter means
    // "all types pass" — use the unfiltered list directly.
    const bool hasTypeFilter = !m_typeFilter.isEmpty();
    QSet<qint64> base;
    for (const auto &n : m_nodes) {
        if (hasTypeFilter && !m_typeFilter.contains(n.type)) continue;
        base.insert(n.id);
    }

    // Search query: name or alias contains the query (case-insensitive).
    if (!m_searchQuery.isEmpty()) {
        const QString q = m_searchQuery.toLower();
        QSet<qint64> matched;
        for (const auto &n : m_nodes) {
            if (!base.contains(n.id)) continue;
            if (n.name.toLower().contains(q)) {
                matched.insert(n.id);
                continue;
            }
            const QStringList aliases = m_aliasesById.value(n.id);
            for (const QString &a : aliases) {
                if (a.toLower().contains(q)) {
                    matched.insert(n.id);
                    break;
                }
            }
        }
        // Always include direct neighbors of search hits — keeps the
        // graph connected so the user sees context, not isolated dots.
        QSet<qint64> withNeighbors = matched;
        for (qint64 id : matched) {
            const QList<qint64> neighbors = neighborsOf(id, 1);
            for (qint64 n : neighbors) {
                if (base.contains(n)) withNeighbors.insert(n);
            }
        }
        base = withNeighbors;
    }

    // Neighborhood focus: restrict to focus + N-hop neighbors.
    if (m_focusEntity >= 0) {
        const QList<qint64> neighborhood = neighborsOf(m_focusEntity, m_focusHops);
        QSet<qint64> focused;
        for (qint64 id : neighborhood) {
            if (base.contains(id)) focused.insert(id);
        }
        base = focused;
    }

    m_visibleIds = base;
}

QList<EntityGraphNode> EntityGraphModel::filteredNodes() const
{
    QList<EntityGraphNode> out;
    out.reserve(m_visibleIds.size());
    for (const auto &n : m_nodes) {
        if (m_visibleIds.contains(n.id)) out.append(n);
    }
    return out;
}

QList<EntityGraphEdge> EntityGraphModel::filteredEdges() const
{
    QList<EntityGraphEdge> out;
    out.reserve(m_edges.size());
    for (const auto &e : m_edges) {
        if (m_visibleIds.contains(e.sourceId) && m_visibleIds.contains(e.targetId)) {
            out.append(e);
        }
    }
    return out;
}
