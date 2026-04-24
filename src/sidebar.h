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

#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QString>
#include <QStringList>

class KMultiTabBar;
class QStackedWidget;

// Kate-style left sidebar: a narrow vertical button bar that toggles
// associated panels. Only one panel is visible at a time.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);
    ~Sidebar() override;

    // Add a panel with an icon and label. Returns the tab ID.
    int addPanel(const QIcon &icon, const QString &text, QWidget *panel);

    // Show a specific panel by ID, or hide if already shown
    void togglePanel(int id);

    // Get a panel widget by ID
    QWidget *panel(int id) const;

    // Get the currently visible panel ID (-1 if none)
    int currentPanel() const { return m_currentId; }

    // Show a panel by ID (without toggling)
    void showPanel(int id);

    // Returns the display names of all registered panels, in tab order.
    QStringList panelNames() const;

    // Returns the display name of the panel with the given ID, or an empty
    // string if no such panel exists.
    QString panelName(int id) const;

    // Returns the ID of the panel with the given display name, or -1 if no
    // such panel exists.
    int panelIdFromName(const QString &name) const;

Q_SIGNALS:
    void panelVisibilityChanged(int id, bool visible);

private:
    KMultiTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    int m_currentId = -1;
    int m_nextId = 0;
    QMap<int, QString> m_idToName;
};

#endif // SIDEBAR_H
