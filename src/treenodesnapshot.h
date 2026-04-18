/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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

#ifndef TREENODESNAPSHOT_H
#define TREENODESNAPSHOT_H

#include <QString>
#include <QList>

/**
 * \brief Thread-safe, deep-copy POD of a project-tree node.
 *
 * Snapshots are built under ProjectTreeModel's internal lock and returned
 * by value. No raw pointers to ProjectTreeItem or the live model escape,
 * so snapshots are safe to carry across threads and to store beyond the
 * lifetime of any particular tree node.
 *
 * Fields mirror ProjectTreeItem 1:1 with integer casts for the enum
 * members (type, category), chosen to keep this header free of any
 * model-internal includes.
 */
struct TreeNodeSnapshot {
    QString name;
    QString path;
    QString synopsis;
    QString status;
    int type = 0;         // ProjectTreeItem::Type cast
    int category = 0;     // ProjectTreeItem::Category cast
    bool diskPresent = true;
    bool isTransient = false;
    QList<TreeNodeSnapshot> children;

    /**
     * \brief Recursive find by project-relative path.
     *
     * Returns a pointer into this snapshot (or a descendant) whose `path`
     * exactly equals \a relPath, or nullptr if not found. Comparison is
     * case-sensitive and matches ProjectTreeModel::findItem semantics.
     * An empty \a relPath returns `this` (the root of the snapshot).
     *
     * The returned pointer is valid for the lifetime of the snapshot
     * object (or until it is mutated).
     */
    const TreeNodeSnapshot* find(const QString &relPath) const;
};

#endif // TREENODESNAPSHOT_H
