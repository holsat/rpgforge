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

#ifndef ENTITYCOMMUNITYDETECTOR_H
#define ENTITYCOMMUNITYDETECTOR_H

#include <QObject>
#include <QHash>
#include <QList>

class LibrarianDatabase;

/**
 * @brief Modularity-optimizing greedy community detection over the
 * entity-relationship graph (Phase 5 of the relationship-graph work).
 *
 * Implements a simplified Louvain pass: each node starts in its own
 * community; we iterate and move each node to whichever neighbor's
 * community yields the largest modularity gain, until no node moves.
 * This is the first Louvain phase only — we do not aggregate
 * communities into super-nodes for a second pass. For corpora at the
 * scale of fiction projects (~50-300 entities, sparse graphs) the
 * single-phase result is already useful and the implementation stays
 * dependency-free (no igraph / Boost.Graph).
 *
 * Edges are read from the LibrarianDatabase relationships table and
 * the entities.parent_id containment edges. Direction is ignored —
 * for clustering, undirected adjacency is the right semantics.
 *
 * Persistence: writes community_id back to a new column on entities.
 * The column is added by LibrarianDatabase::initSchema (additive
 * migration). Pre-Phase-5 projects pick it up automatically next open.
 */
class EntityCommunityDetector
{
public:
    explicit EntityCommunityDetector(LibrarianDatabase *db);

    struct Result {
        // entity_id → community_id
        QHash<qint64, qint64> assignments;
        // Computed modularity of the final partition. Useful for
        // diagnostics and tests; not user-facing.
        double modularity = 0.0;
        // Number of distinct communities found.
        int communityCount = 0;
    };

    /// Run detection in-process. Synchronous — for the corpus sizes
    /// we expect (200 entities, sparse), a full pass takes a few
    /// milliseconds. Heavy projects can be moved off-thread later.
    Result detect();

    /// Run detect() and persist the results to entities.community_id.
    /// Returns the same Result struct so callers can inspect modularity
    /// + count. Wraps the writes in a single transaction.
    Result detectAndPersist();

private:
    LibrarianDatabase *m_db;
};

#endif // ENTITYCOMMUNITYDETECTOR_H
