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

#include "projectmetadata.h"
#include "projectkeys.h"

#include <QStringList>

namespace {

// Legacy key identifiers for reserved exploration state. These were never
// promoted to ProjectKeys but the existing implementation wrote/read them
// as raw string literals, so mirror that verbatim.
constexpr auto WordCountCacheKey    = "wordCountCache";
constexpr auto ExplorationColorsKey = "explorationColors";
constexpr auto NodeMetadataKey      = "nodeMetadata";
constexpr auto OrderHintsKey        = "orderHints";

double marginValue(const QJsonObject &obj,
                   const char *key,
                   double fallback,
                   const char *legacyFlatKey,
                   const QJsonObject &root)
{
    // Prefer the nested margins object, fall back to the v1 flat key,
    // finally to the schema default.
    if (obj.contains(QLatin1String(key))) {
        return obj.value(QLatin1String(key)).toDouble(fallback);
    }
    if (root.contains(QLatin1String(legacyFlatKey))) {
        return root.value(QLatin1String(legacyFlatKey)).toDouble(fallback);
    }
    return fallback;
}

} // anonymous namespace

ProjectMetadata ProjectMetadata::fromJson(const QJsonObject &doc)
{
    ProjectMetadata meta;

    meta.name = doc.value(QLatin1String(ProjectKeys::Name)).toString();
    meta.author = doc.value(QLatin1String(ProjectKeys::Author)).toString();
    meta.pageSize = doc.value(QLatin1String(ProjectKeys::PageSize))
                        .toString(QStringLiteral("A4"));

    // Margins: the current schema (v2+) stores a nested object. The v1
    // schema used four flat keys. Accept either — nested takes precedence
    // when both are present.
    const QJsonObject marginsObj =
        doc.value(QLatin1String(ProjectKeys::Margins)).toObject();
    meta.margins.left =
        marginValue(marginsObj, "left",  20.0, ProjectKeys::MarginLeft,   doc);
    meta.margins.right =
        marginValue(marginsObj, "right", 20.0, ProjectKeys::MarginRight,  doc);
    meta.margins.top =
        marginValue(marginsObj, "top",   20.0, ProjectKeys::MarginTop,    doc);
    meta.margins.bottom =
        marginValue(marginsObj, "bottom",20.0, ProjectKeys::MarginBottom, doc);

    meta.showPageNumbers =
        doc.value(QLatin1String(ProjectKeys::ShowPageNumbers)).toBool(true);
    meta.stylesheetPath =
        doc.value(QLatin1String(ProjectKeys::StylesheetPath)).toString();
    meta.autoSync =
        doc.value(QLatin1String(ProjectKeys::AutoSync)).toBool(true);

    // Version: read what's on disk (for migration callers to inspect),
    // but toJson() always emits the current version.
    meta.version = doc.value(QLatin1String(ProjectKeys::Version))
                       .toInt(ProjectKeys::CurrentVersion);

    meta.loreKeeperConfig =
        doc.value(QLatin1String(ProjectKeys::LoreKeeperConfig)).toObject();

    meta.wordCountCache =
        doc.value(QLatin1String(WordCountCacheKey)).toObject().toVariantMap();
    meta.explorationColors =
        doc.value(QLatin1String(ExplorationColorsKey)).toObject().toVariantMap();

    // Phase 6: per-node metadata + sibling order hints. Absent on legacy
    // projects — ProjectManager migrates them from the tree JSON on first
    // open.
    meta.nodeMetadata =
        doc.value(QLatin1String(NodeMetadataKey)).toObject();
    meta.orderHints =
        doc.value(QLatin1String(OrderHintsKey)).toObject();

    return meta;
}

QJsonObject ProjectMetadata::toJson() const
{
    QJsonObject obj;

    obj[QLatin1String(ProjectKeys::Name)] = name;
    obj[QLatin1String(ProjectKeys::Author)] = author;
    obj[QLatin1String(ProjectKeys::PageSize)] = pageSize;

    QJsonObject marginsObj;
    marginsObj[QStringLiteral("left")] = margins.left;
    marginsObj[QStringLiteral("right")] = margins.right;
    marginsObj[QStringLiteral("top")] = margins.top;
    marginsObj[QStringLiteral("bottom")] = margins.bottom;
    obj[QLatin1String(ProjectKeys::Margins)] = marginsObj;

    obj[QLatin1String(ProjectKeys::ShowPageNumbers)] = showPageNumbers;
    obj[QLatin1String(ProjectKeys::StylesheetPath)] = stylesheetPath;
    obj[QLatin1String(ProjectKeys::AutoSync)] = autoSync;

    // Always emit the current schema version. Migration completes on save.
    obj[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;

    obj[QLatin1String(ProjectKeys::LoreKeeperConfig)] = loreKeeperConfig;

    // Emit exploration caches only when populated. The legacy code wrote
    // these keys in saveExplorationData and never added them during an
    // ordinary saveProject; an unconditional emission here would introduce
    // spurious empty keys on projects that never used explorations.
    if (!wordCountCache.isEmpty()) {
        obj[QLatin1String(WordCountCacheKey)] =
            QJsonObject::fromVariantMap(wordCountCache);
    }
    if (!explorationColors.isEmpty()) {
        obj[QLatin1String(ExplorationColorsKey)] =
            QJsonObject::fromVariantMap(explorationColors);
    }

    // Phase 6: only emit these keys when there is actually something to
    // save. Keeps the JSON clean for projects without per-node metadata
    // or custom sibling ordering.
    if (!nodeMetadata.isEmpty()) {
        obj[QLatin1String(NodeMetadataKey)] = nodeMetadata;
    }
    if (!orderHints.isEmpty()) {
        obj[QLatin1String(OrderHintsKey)] = orderHints;
    }

    return obj;
}

QStringList ProjectMetadata::knownKeys()
{
    return {
        QLatin1String(ProjectKeys::Name),
        QLatin1String(ProjectKeys::Author),
        QLatin1String(ProjectKeys::PageSize),
        QLatin1String(ProjectKeys::Margins),
        QLatin1String(ProjectKeys::MarginLeft),
        QLatin1String(ProjectKeys::MarginRight),
        QLatin1String(ProjectKeys::MarginTop),
        QLatin1String(ProjectKeys::MarginBottom),
        QLatin1String(ProjectKeys::ShowPageNumbers),
        QLatin1String(ProjectKeys::StylesheetPath),
        QLatin1String(ProjectKeys::AutoSync),
        QLatin1String(ProjectKeys::Version),
        QLatin1String(ProjectKeys::LoreKeeperConfig),
        QLatin1String(ProjectKeys::Tree),          // owned by ProjectTreeModel
        QLatin1String(WordCountCacheKey),
        QLatin1String(ExplorationColorsKey),
        QLatin1String(NodeMetadataKey),
        QLatin1String(OrderHintsKey),
    };
}
