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

#include "entitycommunitydetector.h"
#include "librariandatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QHash>
#include <QSet>
#include <QDebug>

#include <algorithm>
#include <cmath>

EntityCommunityDetector::EntityCommunityDetector(LibrarianDatabase *db)
    : m_db(db)
{
}

namespace {

// Compute modularity of a partition.
//   Q = (1/2m) * sum_ij [A_ij - k_i k_j / 2m] * delta(c_i, c_j)
// We expand by community: per-community sum of edge-weights inside
// (sigma_in) minus per-community squared-degree (sigma_tot)^2 / (2m)^2.
//   Q = sum_c [ sigma_in_c / (2m)  -  (sigma_tot_c / 2m)^2 ]
double computeModularity(const QHash<qint64, qint64> &community,
                          const QList<QPair<qint64, qint64>> &edges,
                          const QHash<qint64, double> &degree,
                          double twoM)
{
    if (twoM <= 0.0) return 0.0;
    QHash<qint64, double> sigmaIn;     // community -> intra-community weight
    QHash<qint64, double> sigmaTot;    // community -> total degree

    for (const auto &e : edges) {
        const qint64 cu = community.value(e.first);
        const qint64 cv = community.value(e.second);
        if (cu == cv) sigmaIn[cu] += 2.0;     // each intra edge contributes 2 to sum
    }
    for (auto it = degree.constBegin(); it != degree.constEnd(); ++it) {
        sigmaTot[community.value(it.key())] += it.value();
    }

    double q = 0.0;
    for (auto it = sigmaTot.constBegin(); it != sigmaTot.constEnd(); ++it) {
        const double in  = sigmaIn.value(it.key());
        const double tot = it.value();
        q += (in / twoM) - (tot / twoM) * (tot / twoM);
    }
    return q;
}

} // namespace

EntityCommunityDetector::Result EntityCommunityDetector::detect()
{
    Result r;
    if (!m_db) return r;

    // ---- Step 1: load the graph from the database. -----------------
    // Nodes = entity ids. Edges = relationships ∪ containment.
    // We treat both as undirected and unweighted for clustering. Edges
    // are deduplicated by (min, max) so a relationship that happens
    // to also exist in the parent_id chain is only counted once.
    QSqlDatabase db = m_db->database();
    if (!db.isOpen()) return r;

    QList<qint64> nodes;
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT id FROM entities"))) {
            while (q.next()) nodes.append(q.value(0).toLongLong());
        }
    }
    if (nodes.isEmpty()) return r;

    QSet<QPair<qint64, qint64>> edgeSet;
    auto addEdge = [&](qint64 a, qint64 b) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        edgeSet.insert({a, b});
    };
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT source_id, target_id FROM relationships"))) {
            while (q.next()) {
                addEdge(q.value(0).toLongLong(), q.value(1).toLongLong());
            }
        }
    }
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT id, parent_id FROM entities "
                                    "WHERE parent_id IS NOT NULL"))) {
            while (q.next()) {
                addEdge(q.value(0).toLongLong(), q.value(1).toLongLong());
            }
        }
    }
    QList<QPair<qint64, qint64>> edges = edgeSet.values();

    // ---- Step 2: precompute degrees + adjacency. -------------------
    QHash<qint64, double> degree;
    for (qint64 n : nodes) degree.insert(n, 0.0);
    QHash<qint64, QList<qint64>> adj;
    for (const auto &e : edges) {
        degree[e.first]  += 1.0;
        degree[e.second] += 1.0;
        adj[e.first].append(e.second);
        adj[e.second].append(e.first);
    }
    const double twoM = 2.0 * edges.size();

    // ---- Step 3: greedy modularity optimization. -------------------
    // Each node starts in its own community (label = node id).
    QHash<qint64, qint64> community;
    for (qint64 n : nodes) community.insert(n, n);

    // Iterate node-by-node, moving each to whichever neighbor community
    // gives the highest modularity gain. Recompute modularity from the
    // ground truth after each move — for sparse graphs this is fast and
    // avoids the (subtle) incremental gain formula. Loop until no
    // node moves in a pass, capped to prevent pathological oscillation.
    const int maxIterations = 30;
    bool changed = true;
    for (int it = 0; it < maxIterations && changed; ++it) {
        changed = false;
        for (qint64 n : nodes) {
            const qint64 originalC = community[n];

            // Collect neighbor communities (deduped).
            QSet<qint64> candidateCs;
            candidateCs.insert(originalC);
            for (qint64 nb : adj.value(n)) {
                candidateCs.insert(community.value(nb));
            }
            if (candidateCs.size() < 2) continue;

            const double baseline = computeModularity(community, edges, degree, twoM);
            qint64 bestC = originalC;
            double bestQ = baseline;
            for (qint64 c : candidateCs) {
                if (c == originalC) continue;
                community[n] = c;
                const double q = computeModularity(community, edges, degree, twoM);
                if (q > bestQ + 1e-9) {
                    bestQ = q;
                    bestC = c;
                }
            }
            community[n] = bestC;
            if (bestC != originalC) changed = true;
        }
    }

    // ---- Step 4: relabel communities to dense ids 0, 1, 2, ... -----
    // The raw labels are entity ids picked as initial values, which
    // are sparse. Rewriting them to a contiguous range is friendlier
    // for downstream UI (palette indexing, sorting).
    QHash<qint64, qint64> remap;
    qint64 nextId = 0;
    QHash<qint64, qint64> finalAssignments;
    for (qint64 n : nodes) {
        const qint64 oldC = community[n];
        auto it = remap.find(oldC);
        if (it == remap.end()) {
            it = remap.insert(oldC, nextId++);
        }
        finalAssignments.insert(n, *it);
    }

    r.assignments = finalAssignments;
    r.modularity  = computeModularity(community, edges, degree, twoM);
    r.communityCount = static_cast<int>(remap.size());
    return r;
}

EntityCommunityDetector::Result EntityCommunityDetector::detectAndPersist()
{
    Result r = detect();
    if (!m_db) return r;
    QSqlDatabase db = m_db->database();
    if (!db.isOpen()) return r;

    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE entities SET community_id = :c WHERE id = :id"));
    for (auto it = r.assignments.constBegin(); it != r.assignments.constEnd(); ++it) {
        q.bindValue(QStringLiteral(":c"), it.value());
        q.bindValue(QStringLiteral(":id"), it.key());
        q.exec();
    }
    db.commit();
    qInfo().noquote() << "EntityCommunityDetector: persisted"
                       << r.assignments.size() << "assignments across"
                       << r.communityCount << "communities (Q="
                       << r.modularity << ")";
    return r;
}
