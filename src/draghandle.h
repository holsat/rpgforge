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

#ifndef DRAGHANDLE_H
#define DRAGHANDLE_H

#include <QLabel>

class QBoxLayout;
class QMouseEvent;

/**
 * @brief Grip-style drag handle for reordering sibling widgets inside a
 * QBoxLayout (typically QVBoxLayout). Earlier versions of this class
 * targeted QListWidget + setItemWidget, but that combination proved
 * unstable: Qt's internal "persistent editor" machinery racing with
 * model mutations produced use-after-free crashes during paint.
 *
 * The current implementation works entirely on the layout:
 * on mousePress it records the source widget's layout index; on
 * mouseRelease it takes the index under the cursor and, via a queued
 * invocation, removes the source widget from the layout and
 * re-inserts it at the target position. No model, no persistent
 * editors, no viewport to reparent from.
 *
 * Callers that care about the final order iterate the layout's
 * children in order; no signal is emitted (caller reads on save).
 */
class DragHandle : public QLabel
{
    Q_OBJECT
public:
    DragHandle(QBoxLayout *layout, QWidget *parent = nullptr);

Q_SIGNALS:
    /// Fired on mousePress when a drag begins. Listener (e.g. the parent
    /// settings dialog) can show a drop-indicator overlay.
    void dragStarted();
    /// Fired from mouseMove whenever the layout index the cursor is over
    /// changes. @p targetIndex is the layout index at which the source
    /// row would be inserted if the user released right now.
    void targetIndexChanged(int targetIndex);
    /// Fired on mouseRelease (regardless of whether the row actually
    /// moved). Listener should hide any indicator overlay.
    void dragReleased();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /// Return the layout index of our row widget (the handle's parent).
    /// Returns -1 if the parent is no longer in the layout.
    int findMyIndex() const;

    /// Given a global mouse position, return the layout index whose
    /// corresponding row widget contains (or is closest to) that point.
    /// Returns -1 if no row matches.
    int indexUnderGlobalPos(const QPoint &globalPos) const;

    QBoxLayout *m_layout;
    bool        m_pressed = false;
    int         m_sourceIndex = -1;
    int         m_pendingTargetIndex = -1;
};

#endif // DRAGHANDLE_H
