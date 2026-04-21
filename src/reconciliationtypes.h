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

#ifndef RECONCILIATIONTYPES_H
#define RECONCILIATIONTYPES_H

#include <QString>
#include <QList>
#include <QMetaType>

/**
 * \brief One row in the reconciliation dialog — represents a tree node whose
 *        on-disk path could not be resolved during project load.
 *
 * Produced by ProjectManager::validateTree() when a File-typed node (or a
 * leaf Folder-typed node that cannot be resolved to a file) has a stored
 * `path` that doesn't exist on disk. Consumed by ReconciliationDialog which
 * lets the user pick a per-row action; the dialog hands the mutated list
 * back to MainWindow, which forwards each entry to the matching
 * ProjectManager mutation.
 */
struct ReconciliationEntry {
    enum Action {
        None,
        Locate,          ///< Resolved via fuzzy resolver or user file-picker; new path is in resolvedPath.
        Remove,          ///< Remove node from the tree.
        RecreateEmpty    ///< Create an empty placeholder file at `path`.
    };

    QString path;         ///< project-relative, as stored in the tree
    QString displayName;  ///< item->name
    int category = 0;     ///< ProjectTreeItem::Category cast
    int type = 0;         ///< ProjectTreeItem::Type cast
    Action action = None;
    QString resolvedPath; ///< populated when action == Locate

    /**
     * Suggestion populated by the validator from the fuzzy resolver — a path
     * that matches by stem, space/underscore-tolerant, within the parent
     * directory. Empty if no suggestion was found. The user can accept
     * (action = Locate + resolvedPath = suggestedPath) or override via the
     * dialog.
     */
    QString suggestedPath;
};

Q_DECLARE_METATYPE(ReconciliationEntry)
Q_DECLARE_METATYPE(QList<ReconciliationEntry>)

#endif // RECONCILIATIONTYPES_H
