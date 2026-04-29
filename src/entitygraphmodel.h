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

#ifndef ENTITYGRAPHMODEL_H
#define ENTITYGRAPHMODEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QHash>
#include <QList>

class LibrarianDatabase;

/**
 * @brief One node in the entity graph. Mirrors a row from the entities
 * table plus aggregated tags and a cached mention_count for sizing.
 */
struct EntityGraphNode {
    qint64 id = -1;
    QString name;
    QString type;            // "character" / "place" / "time" / "concept" / ...
    QString summary;         // one-line blurb, may be empty
    int mentionCount = 0;
    QStringList tags;
};

/**
 * @brief One edge in the entity graph. Sourced from the relationships
 * table OR derived from entities.parent_id (containment hierarchy).
 */
struct EntityGraphEdge {
    qint64 sourceId = -1;
    qint64 targetId = -1;
    QString relationship;    // 'friend', 'member_of', 'is_part_of' (containment), ...
    double strength = 0.5;
};

/**
 * @brief Pure data model for the entity graph view. Loads from
 * LibrarianDatabase, applies UI filters, exposes filtered node/edge
 * lists for rendering. No Qt UI dependencies — instantiable and
 * testable from a unit test.
 *
 * Filtering layers (composed left-to-right; an item must pass all of
 * them to appear in the filtered output):
 *   1. Type filter — set of allowed entity types. Default: all types
 *      from the loaded data.
 *   2. Search query — substring match against name and aliases. Empty
 *      string disables the filter.
 *   3. Neighborhood focus — when set to an entity id, only that entity
 *      and its N-hop neighbors are visible.
 *
 * reload() reads fresh data from the database. setXxxFilter() methods
 * update the filter state and emit modelChanged() so views re-render.
 */
class EntityGraphModel : public QObject
{
    Q_OBJECT
public:
    explicit EntityGraphModel(LibrarianDatabase *db, QObject *parent = nullptr);

    /// Re-pull all entities + relationships from the database. Call
    /// after the librarian finishes a re-extraction or the user
    /// triggers a manual refresh. Emits modelChanged().
    void reload();

    // Unfiltered accessors — operate on the raw loaded data.
    QList<EntityGraphNode> allNodes() const { return m_nodes; }
    QList<EntityGraphEdge> allEdges() const { return m_edges; }
    QSet<QString> allTypes() const;

    // Filtered accessors — apply the current filter state.
    QList<EntityGraphNode> filteredNodes() const;
    QList<EntityGraphEdge> filteredEdges() const;

    // Filter mutators. Each emits modelChanged() if the filter actually
    // changed. setTypeFilter with an empty set is treated as "all types".
    void setTypeFilter(const QSet<QString> &allowed);
    void setSearchQuery(const QString &query);
    void setNeighborhoodFocus(qint64 entityId, int hops);
    void clearNeighborhoodFocus();

    // Read-back of current filter state — useful for tests and for the
    // panel's filter widgets to reflect model state.
    QSet<QString> typeFilter() const { return m_typeFilter; }
    QString searchQuery() const { return m_searchQuery; }
    qint64 focusEntity() const { return m_focusEntity; }
    int focusHops() const { return m_focusHops; }

    // Lookup helpers used by the rendering layer.
    const EntityGraphNode *nodeById(qint64 id) const;
    QList<qint64> neighborsOf(qint64 entityId, int hops = 1) const;

Q_SIGNALS:
    void modelChanged();

private:
    void recomputeFilter();    // updates m_visibleIds based on filters

    LibrarianDatabase *m_db;
    QList<EntityGraphNode> m_nodes;
    QList<EntityGraphEdge> m_edges;
    QHash<qint64, int>     m_indexById;     // id → index into m_nodes
    QHash<qint64, QStringList> m_aliasesById;

    // Filter state.
    QSet<QString> m_typeFilter;             // empty = no type filter (all)
    QString       m_searchQuery;
    qint64        m_focusEntity = -1;
    int           m_focusHops = 1;

    // Cached visible-id set, recomputed from filters on each
    // mutator call. Lookup-by-id during rendering stays O(1).
    QSet<qint64>  m_visibleIds;
};

#endif // ENTITYGRAPHMODEL_H
