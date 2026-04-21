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

#ifndef DRAGHANDLE_H
#define DRAGHANDLE_H

#include <QLabel>

class QListWidget;
class QMouseEvent;

/**
 * @brief Grip-style drag handle for reordering rows in a QListWidget whose
 * items use setItemWidget() for their content. Qt's built-in
 * InternalMove drag doesn't fire in that configuration because child
 * widgets (line edits, combos, toggles) absorb the mouse events before
 * the list viewport can detect the press-and-move.
 *
 * This handle bypasses the drag-drop framework entirely: on mousePress
 * it records the source row; on each mouseMove it computes the row
 * under the cursor and, if different from the current row, does a
 * takeItem+insertItem on the host list, re-setting the item widget so
 * the composite row contents survive the move. Release clears the
 * tracking. Callers that care about the new order read it from the
 * list's current row sequence; no signal needed.
 */
class DragHandle : public QLabel
{
    Q_OBJECT
public:
    DragHandle(QListWidget *list, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /// Look up the row this handle belongs to by searching the list for
    /// the QListWidgetItem whose item-widget is this handle's parent.
    /// Returns -1 if not found (shouldn't happen during normal use).
    int findMyRow() const;

    QListWidget *m_list;
    bool m_pressed = false;
    int  m_sourceRow = -1;
    /// Row the cursor is currently over. Updated during mouseMove; the
    /// actual list mutation is deferred to mouseRelease (via a queued
    /// invocation) so we never rearrange while the event handler is
    /// still running — mid-move reordering would destroy the row widget
    /// this handle lives in and crash on the next mouse event.
    int  m_pendingTargetRow = -1;
};

#endif // DRAGHANDLE_H
