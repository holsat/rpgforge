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
#include <QApplication>
#include <QBoxLayout>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QWidget>

DragHandle::DragHandle(QBoxLayout *layout, QWidget *parent)
    : QLabel(parent), m_layout(layout)
{
    setCursor(Qt::OpenHandCursor);
}

void DragHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_layout) {
        QLabel::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_sourceIndex = findMyIndex();
    m_pendingTargetIndex = m_sourceIndex;
    setCursor(Qt::ClosedHandCursor);
    Q_EMIT dragStarted();
    Q_EMIT targetIndexChanged(m_pendingTargetIndex);
    event->accept();
}

void DragHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || !m_layout || m_sourceIndex < 0) {
        QLabel::mouseMoveEvent(event);
        return;
    }

    // Track only; the actual reorder is deferred to mouseRelease via a
    // queued invocation. Reordering layout children during an active
    // mouse event can destroy the widget hierarchy the handler is still
    // unwinding through.
    const int idx = indexUnderGlobalPos(event->globalPosition().toPoint());
    if (idx >= 0 && idx != m_pendingTargetIndex) {
        m_pendingTargetIndex = idx;
        Q_EMIT targetIndexChanged(idx);
    }
    event->accept();
}

void DragHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QLabel::mouseReleaseEvent(event);
        return;
    }

    const int src = m_sourceIndex;
    const int tgt = m_pendingTargetIndex;
    m_pressed = false;
    m_sourceIndex = -1;
    m_pendingTargetIndex = -1;
    setCursor(Qt::OpenHandCursor);
    Q_EMIT dragReleased();
    event->accept();

    if (!m_layout || src < 0 || tgt < 0 || src == tgt) return;

    // Defer the actual layout mutation to the event loop. Posting via
    // QueuedConnection guarantees the mouseRelease handler has fully
    // returned (and any paint/layout events queued during that return
    // have been flushed) before we touch the layout. removeWidget +
    // insertWidget on a plain QBoxLayout doesn't involve Qt's
    // model/view editor bookkeeping, so no persistent-index state can
    // go stale — the widget is simply relocated.
    QPointer<QBoxLayout> layout(m_layout);
    QMetaObject::invokeMethod(qApp, [layout, src, tgt]() {
        if (!layout) return;
        if (src < 0 || src >= layout->count()) return;

        QLayoutItem *item = layout->itemAt(src);
        if (!item) return;
        QWidget *w = item->widget();
        if (!w) return;

        layout->removeWidget(w);
        // After removeWidget, layout->count() has dropped by one.
        const int safeTgt = qBound(0, tgt, layout->count());
        layout->insertWidget(safeTgt, w);
    }, Qt::QueuedConnection);
}

int DragHandle::findMyIndex() const
{
    if (!m_layout) return -1;
    const QWidget *myRow = parentWidget();
    for (int i = 0; i < m_layout->count(); ++i) {
        if (m_layout->itemAt(i) && m_layout->itemAt(i)->widget() == myRow) {
            return i;
        }
    }
    return -1;
}

int DragHandle::indexUnderGlobalPos(const QPoint &globalPos) const
{
    if (!m_layout) return -1;
    // Walk the layout's widgets and pick the one whose frame contains
    // the mouse vertically. If the mouse is past the last row, return
    // the last index; if above the first, return 0.
    int bestIdx = -1;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < m_layout->count(); ++i) {
        QLayoutItem *it = m_layout->itemAt(i);
        QWidget *w = it ? it->widget() : nullptr;
        if (!w) continue;
        const QRect globalRect(w->mapToGlobal(QPoint(0, 0)), w->size());
        if (globalRect.contains(globalPos)) return i;
        const int centerY = globalRect.center().y();
        const int dist = std::abs(centerY - globalPos.y());
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}
