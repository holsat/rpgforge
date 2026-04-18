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

#ifndef RPGFORGE_DEBUGLOG_H
#define RPGFORGE_DEBUGLOG_H

#include <QDebug>

/**
 * \file debuglog.h
 *
 * Compile-gated debug logging macros. Use these INSTEAD of raw qDebug()
 * for per-feature diagnostic tracing that should stay in the code but
 * vanish in Release builds.
 *
 * The CMake option RPGFORGE_DEBUG_LOGS controls whether the macros expand
 * to real qDebug() calls. It defaults ON for Debug / RelWithDebInfo and
 * OFF for Release, mirroring the existing RPGFORGE_DBUS_TESTING pattern.
 *
 * Usage:
 *
 *     RPGFORGE_DLOG("DND") << "moveItem entry:" << item->path;
 *     RPGFORGE_DLOG("TREE") << "children count=" << parent->children.count();
 *
 * In Debug builds this emits:
 *     [DND] moveItem entry: "research/Chapter_1"
 *
 * In Release builds the entire expression is replaced by a sink that
 * consumes the `<<` arguments without evaluating them (QNoDebug), so there
 * is no runtime cost and no binary footprint.
 *
 * Prefer a short all-caps prefix for the category (e.g. "DND", "TREE",
 * "RENAME", "GIT"). Use grep / QT_LOGGING_RULES-style filtering in your
 * mental model, but there is no runtime filter layered on top — if you
 * need one, upgrade to Q_LOGGING_CATEGORY for the scope in question.
 *
 * Keep RPGFORGE_DLOG calls idiomatic: one line per statement, no side
 * effects in the streamed values, and no computation that is expensive
 * even when the macro is a no-op.
 */

#ifdef RPGFORGE_DEBUG_LOGS
#define RPGFORGE_DLOG(prefix) qDebug().noquote() << "[" prefix "]"
#else
// QMessageLogger::noDebug() returns a QNoDebug that absorbs every
// `<< ...` without evaluating or formatting. No-op in release builds.
#define RPGFORGE_DLOG(prefix) QMessageLogger().noDebug()
#endif

#endif // RPGFORGE_DEBUGLOG_H
