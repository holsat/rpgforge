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

#include "corkboardview.h"
#include "corkboardcard.h"
#include "projecttreemodel.h"
#include "projectmanager.h"
#include "variablemanager.h"

#include <QGridLayout>
#include <QPalette>
#include <QWidget>
#include <QFile>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

CorkboardView::CorkboardView(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setAcceptDrops(true);
    m_contentWidget = new QWidget(this);
    m_layout = new QGridLayout(m_contentWidget);
    m_layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setWidget(m_contentWidget);

    m_dropIndicator = new QWidget(m_contentWidget);
    m_dropIndicator->setFixedWidth(4);
    m_dropIndicator->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(palette().color(QPalette::Highlight).name()));
    m_dropIndicator->hide();
}

CorkboardView::~CorkboardView() = default;

void CorkboardView::setFolder(ProjectTreeItem *folderItem)
{
    m_currentFolder = folderItem;
    clear();
    if (!folderItem) return;

    for (auto *child : folderItem->children) {
        addCard(child);
    }
}

void CorkboardView::clear()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        // Use deleteLater so we don't destroy a card widget that may still be
        // on the call stack (e.g., the source of an active QDrag::exec()).
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void CorkboardView::addCard(ProjectTreeItem *item)
{
    if (item->type == ProjectTreeItem::Folder) {
        QString nameLower = item->name.toLower();
        if (nameLower == QStringLiteral("media") || nameLower == QStringLiteral("stylesheets")) {
            return;
        }
    }

    auto *card = new CorkboardCard(item, this);
    int count = m_layout->count();
    int row = count / 4;
    int col = count % 4;
    m_layout->addWidget(card, row, col);

    if (item->type == ProjectTreeItem::File) {
        QString path = item->path;
        connect(card, &CorkboardCard::doubleClicked, this, [this, path]() {
            Q_EMIT fileActivated(path);
        });
    }
}

void CorkboardView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-corkboard-card"))) {
        event->acceptProposedAction();
    }
}

void CorkboardView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-corkboard-card"))) {
        updateDropIndicator(event->position().toPoint());
        event->acceptProposedAction();
    }
}

void CorkboardView::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    m_dropIndicator->hide();
}

void CorkboardView::updateDropIndicator(const QPoint &pos)
{
    bool found = false;
    for (int i = 0; i < m_layout->count(); ++i) {
        auto *widget = m_layout->itemAt(i)->widget();
        if (widget && widget != m_dropIndicator) {
            QRect geom = widget->geometry();
            // Show indicator to the left of the card we are hovering over
            if (geom.contains(pos)) {
                m_dropIndicator->setGeometry(geom.x() - 4, geom.y(), 4, geom.height());
                m_dropIndicator->show();
                m_dropIndicator->raise();
                found = true;
                break;
            }
            // Also check if we are just to the right of the last card in a row
            if (pos.x() > geom.right() && pos.x() < geom.right() + 20 && pos.y() > geom.top() && pos.y() < geom.bottom()) {
                m_dropIndicator->setGeometry(geom.right() + 2, geom.y(), 4, geom.height());
                m_dropIndicator->show();
                m_dropIndicator->raise();
                found = true;
                break;
            }
        }
    }
    if (!found) m_dropIndicator->hide();
}

void CorkboardView::dropEvent(QDropEvent *event)
{
    m_dropIndicator->hide();
    if (!m_currentFolder) return;

    QByteArray data = event->mimeData()->data(QStringLiteral("application/x-rpgforge-corkboard-card"));
    if (data.isEmpty()) return;

    ProjectTreeItem *draggedItem = *reinterpret_cast<ProjectTreeItem**>(data.data());
    
    // Find the drop target card
    QPoint pos = event->position().toPoint();
    ProjectTreeItem *targetItem = nullptr;
    
    for (int i = 0; i < m_layout->count(); ++i) {
        auto *widget = m_layout->itemAt(i)->widget();
        if (widget && widget != m_dropIndicator && widget->geometry().contains(pos)) {
            if (auto *targetCard = qobject_cast<CorkboardCard*>(widget)) {
                targetItem = targetCard->item();
                break;
            }
        }
    }

    if (draggedItem && draggedItem != targetItem) {
        Q_EMIT itemsReordered(m_currentFolder, draggedItem, targetItem);
    }

    event->acceptProposedAction();
}
