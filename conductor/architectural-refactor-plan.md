# Architectural Refactoring Plan: JSON Keys & Schema Migration

## Objective
Address items 5 and 7 from the `ARCHITECTURE_ANALYSIS.md` to improve the maintainability and forward-compatibility of the `ProjectManager`.

## Scope
1. **Item 5 (JSON Keys):** Define a standard namespace for JSON keys to replace scattered `QStringLiteral` usages.
2. **Item 7 (Schema Migration):** Implement a version-aware migration pipeline to upgrade `.project` files from older schemas.

## Proposed Changes

### 1. `src/projectkeys.h`
Create a new header to hold all `QJsonObject` string keys and versioning constants.

```cpp
#ifndef PROJECTKEYS_H
#define PROJECTKEYS_H

#include <QString>

namespace ProjectKeys {
    // Project Metadata
    inline constexpr auto Name = "name";
    inline constexpr auto Author = "author";
    inline constexpr auto PageSize = "pageSize";
    inline constexpr auto Margins = "margins"; // New in v2
    
    // Legacy keys (v1)
    inline constexpr auto MarginLeft = "marginLeft";
    inline constexpr auto MarginRight = "marginRight";
    inline constexpr auto MarginTop = "marginTop";
    inline constexpr auto MarginBottom = "marginBottom";

    // Project Settings
    inline constexpr auto ShowPageNumbers = "showPageNumbers";
    inline constexpr auto StylesheetPath = "stylesheetPath";
    inline constexpr auto AutoSync = "autoSync";
    inline constexpr auto Tree = "tree";
    inline constexpr auto Version = "version";

    // Tree Node keys
    inline constexpr auto Type = "type";
    inline constexpr auto Path = "path";
    inline constexpr auto Children = "children";

    // Current schema version
    inline constexpr int CurrentVersion = 2;
}

#endif // PROJECTKEYS_H
```

### 2. Migration Framework (`src/projectmanager.cpp`)
We will increment the `rpgforge.project` schema version to `2`. To demonstrate the migration, we will group the flat `margin*` keys from Version 1 into a nested `margins` object in Version 2.

**Migration Logic:**
We'll introduce a private `void migrate(QJsonObject &data)` method called during `openProject()`.

```cpp
void ProjectManager::migrate(QJsonObject &data)
{
    int version = data.value(QLatin1String(ProjectKeys::Version)).toInt(1);

    // Upgrade from v1 to v2
    if (version < 2) {
        QJsonObject margins;
        margins[QLatin1String("left")] = data.value(QLatin1String(ProjectKeys::MarginLeft)).toDouble(20.0);
        margins[QLatin1String("right")] = data.value(QLatin1String(ProjectKeys::MarginRight)).toDouble(20.0);
        margins[QLatin1String("top")] = data.value(QLatin1String(ProjectKeys::MarginTop)).toDouble(20.0);
        margins[QLatin1String("bottom")] = data.value(QLatin1String(ProjectKeys::MarginBottom)).toDouble(20.0);

        data[QLatin1String(ProjectKeys::Margins)] = margins;

        data.remove(QLatin1String(ProjectKeys::MarginLeft));
        data.remove(QLatin1String(ProjectKeys::MarginRight));
        data.remove(QLatin1String(ProjectKeys::MarginTop));
        data.remove(QLatin1String(ProjectKeys::MarginBottom));

        version = 2;
    }

    data[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
}
```

### 3. Apply `ProjectKeys`
Refactor all getters/setters and initialization code in `ProjectManager.cpp` to use the constants (e.g., `m_data[QLatin1String(ProjectKeys::Name)]`). Update the margin getters/setters to read from/write to the new nested `margins` object.

### 4. Validation
Ensure `ProjectManager` successfully loads an older version 1 project, upgrades the JSON in-memory, and writes it back as version 2 upon saving. Ensure no functionality is lost.