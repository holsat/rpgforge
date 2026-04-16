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

#ifndef RPGFORGEDBUS_H
#define RPGFORGEDBUS_H

#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QVariantList>

class MainWindow;

/**
 * \brief DBus adaptor exposing RPG Forge's UI surface for automated testing.
 *
 * Registered at /org/kde/rpgforge/MainWindow on the session bus under
 * service org.kde.rpgforge. External test tools call these methods over
 * DBus instead of simulating AT-SPI input.
 *
 * Every method runs on the main thread via Qt's DBus marshalling. Methods
 * that can fail return a bool; methods that query state return the
 * requested data or an empty value if the precondition isn't met
 * (no project open, etc.).
 */
class RpgForgeDBus : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.rpgforge.MainWindow")

public:
    explicit RpgForgeDBus(MainWindow *window);
    ~RpgForgeDBus() override = default;

public Q_SLOTS:
    // Application lifecycle
    /** \brief Returns the app version string. */
    QString version() const;

    /** \brief True when a project is currently loaded. */
    bool isProjectOpen() const;

    /** \brief Absolute path to the open project root, or "" if none. */
    QString currentProjectPath() const;

    /** \brief Open the project whose .project file sits at \a path. */
    bool openProject(const QString &path);

    /** \brief Close the current project. Returns true if something was closed. */
    bool closeProject();

    /** \brief Quit the application cleanly. */
    void quit();

    // Sidebar
    /** \brief Returns the display names of all registered sidebar panels. */
    QStringList sidebarPanels() const;

    /** \brief Returns the display name of the currently visible panel, or "". */
    QString activeSidebarPanel() const;

    /** \brief Switch the visible sidebar panel by display name. Returns true on success. */
    bool showSidebarPanel(const QString &name);

    // Editor
    /** \brief Absolute path of the file currently open in the main editor, or "". */
    QString currentEditorFilePath() const;

    /** \brief Identifier of the current central view: editor, diff, corkboard, imagepreview, or pdf. */
    QString currentCentralView() const;

    /** \brief Open the file at \a absolutePath in the editor. */
    bool openFile(const QString &absolutePath);

    // Project tree
    /** \brief Returns relative paths of every file in the project tree. */
    QStringList projectFiles() const;

    // Explorations
    /** \brief Returns names of all explorations (branches). */
    QStringList explorationNames() const;

    /** \brief Name of the currently checked-out exploration, or "". */
    QString currentExploration() const;

    /** \brief Create a new exploration with the given name. */
    bool createExploration(const QString &name);

    /** \brief Switch to the named exploration (assumes clean tree; caller may need to park first). */
    bool switchExploration(const QString &name);

    /** \brief Returns parked (stashed) change entries as a list of maps with keys: index, message, onBranch, date. */
    QVariantList parkedChanges() const;

private:
    MainWindow *m_window;
};

#endif // RPGFORGEDBUS_H
