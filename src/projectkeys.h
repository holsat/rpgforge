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

#ifndef PROJECTKEYS_H
#define PROJECTKEYS_H

#include <QString>

namespace ProjectKeys {
    // Project Metadata
    inline constexpr auto Name = "name";
    inline constexpr auto Author = "author";
    inline constexpr auto PageSize = "pageSize";
    inline constexpr auto Margins = "margins";
    
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
    inline constexpr int CurrentVersion = 3;

    // Standard Folder Names
    inline constexpr auto FolderManuscript = "Manuscript";
    inline constexpr auto FolderResearch = "Research";
    inline constexpr auto FolderLoreKeeper = "LoreKeeper";
    inline constexpr auto FolderMedia = "Media";
    inline constexpr auto FolderStylesheets = "Stylesheets";

    // Project Data Keys
    inline constexpr auto LoreKeeperConfig = "loreKeeperConfig";
}

#endif // PROJECTKEYS_H
