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

#ifndef PROJECTMETADATA_H
#define PROJECTMETADATA_H

#include <QString>
#include <QVariantMap>
#include <QJsonObject>

/**
 * \brief Typed in-memory representation of the project metadata that lives
 * in the rpgforge.project JSON file.
 *
 * Phase 1 of the tree-refactor introduces this struct as a replacement for
 * the untyped QJsonObject bag that ProjectManager used to carry. The on-disk
 * format is unchanged — fromJson/toJson round-trip the same key names that
 * ProjectKeys defines. The live tree JSON (ProjectKeys::Tree) is NOT stored
 * on this struct; it is still sourced from ProjectTreeModel at save time.
 */
struct ProjectMetadata {
    QString name;
    QString author;
    QString pageSize = QStringLiteral("A4");

    struct Margins {
        double left = 20.0;
        double right = 20.0;
        double top = 20.0;
        double bottom = 20.0;
    } margins;

    bool showPageNumbers = true;
    QString stylesheetPath;
    bool autoSync = true;
    int version = 3;                // ProjectKeys::CurrentVersion

    QVariantMap wordCountCache;
    QVariantMap explorationColors;

    // Opaque — user-authored category definitions. Kept as QJsonObject so
    // the schema stays flexible.
    QJsonObject loreKeeperConfig;

    /**
     * \brief Per-node metadata keyed by project-relative path.
     *
     * Phase 6 of the tree-refactor: tree structure comes from disk, but
     * per-node metadata (synopsis / status / display name / category
     * override) lives here, keyed by path. Each value is a QJsonObject
     * that may contain any combination of:
     *   - "synopsis"          (QString)
     *   - "status"            (QString)
     *   - "displayName"       (QString, reserved; currently unused)
     *   - "categoryOverride"  (int, ProjectTreeItem::Category)
     *
     * Entries are skipped when every field is at its default so the JSON
     * stays compact on disk. Legacy projects whose metadata lived inline
     * in the `tree` JSON are migrated on open; see
     * ProjectManager::migrateLegacyTreeToNodeMetadata().
     */
    QJsonObject nodeMetadata;

    /**
     * \brief Sibling ordering hints keyed by parent path.
     *
     * The key is the parent's project-relative path (empty string for the
     * project root). The value is a QJsonArray of child filenames in the
     * user's preferred display order. Only written when the ordering
     * deviates from the plain alphanumeric sort — the common case stays
     * implicit and the JSON stays quiet.
     */
    QJsonObject orderHints;

    /**
     * \brief Parse a full rpgforge.project JSON object into a typed struct.
     *
     * Respects the legacy v1 flat-margin keys (marginLeft/marginRight/
     * marginTop/marginBottom) and migrates them into the nested margins
     * field. The ProjectKeys::Tree key is RESPECTED but NOT stored on the
     * struct — the tree continues to live on ProjectTreeModel.
     */
    static ProjectMetadata fromJson(const QJsonObject &doc);

    /**
     * \brief Produce a JSON object containing every typed field.
     *
     * The caller (typically ProjectManager::saveProject) layers the tree
     * JSON and any unknown top-level keys on top of the returned object
     * before writing to disk.
     */
    QJsonObject toJson() const;

    /**
     * \brief Names of every top-level JSON key that ProjectMetadata reads
     * or writes. Used by ProjectManager to extract unknown keys into a
     * round-trip preservation bag.
     */
    static QStringList knownKeys();
};

#endif // PROJECTMETADATA_H
