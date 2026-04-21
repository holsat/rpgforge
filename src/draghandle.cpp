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
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void DragHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || !m_list || m_sourceRow < 0) {
        QLabel::mouseMoveEvent(event);
        return;
    }

    // Compute the row under the cursor in the host list's viewport.
    const QPoint viewportPos = m_list->viewport()->mapFromGlobal(
        event->globalPosition().toPoint());
    QListWidgetItem *target = m_list->itemAt(viewportPos);
    if (!target) return;

    const int targetRow = m_list->row(target);
    if (targetRow < 0 || targetRow == m_sourceRow) return;

    // Move our item. takeItem only unlinks; we still own the pointer.
    // setItemWidget binds a widget to an item — the binding is on the
    // list, so after takeItem the widget is not shown. We re-apply it
    // after insertItem to restore the composite row contents.
    QListWidgetItem *srcItem = m_list->item(m_sourceRow);
    QWidget *srcWidget = m_list->itemWidget(srcItem);
    m_list->takeItem(m_sourceRow);
    m_list->insertItem(targetRow, srcItem);
    if (srcWidget) m_list->setItemWidget(srcItem, srcWidget);

    m_sourceRow = targetRow;
    event->accept();
}

void DragHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QLabel::mouseReleaseEvent(event);
        return;
    }
    m_pressed = false;
    m_sourceRow = -1;
    setCursor(Qt::OpenHandCursor);
    event->accept();
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
