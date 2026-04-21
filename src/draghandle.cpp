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

#include "draghandle.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPointer>
#include <QMetaObject>

DragHandle::DragHandle(QListWidget *list, QWidget *parent)
    : QLabel(parent), m_list(list)
{
    setCursor(Qt::OpenHandCursor);
}

void DragHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_list) {
        QLabel::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_sourceRow = findMyRow();
    m_pendingTargetRow = m_sourceRow;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void DragHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || !m_list || m_sourceRow < 0) {
        QLabel::mouseMoveEvent(event);
        return;
    }

    // Only track which row the cursor is over; do NOT mutate the list
    // here. Calling takeItem / insertItem mid-event would reparent (or
    // destroy) the row widget this handle lives in, dangling `this`
    // before the handler returns. The real move is deferred to
    // mouseRelease via a queued invocation — see below.
    const QPoint viewportPos = m_list->viewport()->mapFromGlobal(
        event->globalPosition().toPoint());
    if (QListWidgetItem *target = m_list->itemAt(viewportPos)) {
        m_pendingTargetRow = m_list->row(target);
    }
    event->accept();
}

void DragHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QLabel::mouseReleaseEvent(event);
        return;
    }

    const int src = m_sourceRow;
    const int tgt = m_pendingTargetRow;
    m_pressed = false;
    m_sourceRow = -1;
    m_pendingTargetRow = -1;
    setCursor(Qt::OpenHandCursor);
    event->accept();

    if (!m_list || src < 0 || tgt < 0 || src == tgt) return;

    // Defer the actual move to the event loop. Running takeItem +
    // insertItem + setItemWidget from inside mouseRelease can still
    // destroy the widget hierarchy the release is propagating through,
    // which has been observed to crash on certain Qt builds. The
    // queued invocation runs after this handler has fully returned and
    // the event stack has unwound.
    QPointer<QListWidget> list(m_list);
    QMetaObject::invokeMethod(m_list, [list, src, tgt]() {
        if (!list) return;
        if (src >= list->count()) return;

        QListWidgetItem *item = list->item(src);
        if (!item) return;
        QWidget *rowWidget = list->itemWidget(item);

        // Detach the row widget from the list before the model mutation
        // so Qt's internal persistent-index cleanup doesn't schedule it
        // for deletion when the row disappears. We'll reparent it back
        // when we re-bind with setItemWidget after the insert.
        if (rowWidget) rowWidget->setParent(nullptr);

        list->takeItem(src);
        // Clamp tgt in case a row count change slipped in between the
        // release and the queued callback.
        const int safeTgt = qBound(0, tgt, list->count());
        list->insertItem(safeTgt, item);
        if (rowWidget) list->setItemWidget(item, rowWidget);
    }, Qt::QueuedConnection);
}

int DragHandle::findMyRow() const
{
    if (!m_list) return -1;
    // The handle sits inside a composite row widget (grip | group box |
    // toggle). The list's itemWidget for our item is our parent widget.
    const QWidget *myRow = parentWidget();
    for (int r = 0; r < m_list->count(); ++r) {
        if (m_list->itemWidget(m_list->item(r)) == myRow) return r;
    }
    return -1;
}
