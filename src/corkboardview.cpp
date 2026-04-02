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
    if (item->type == ProjectTreeItem::Folder && item->name.toLower() == QStringLiteral("media")) {
        return;
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
        event->acceptProposedAction();
    }
}

void CorkboardView::dropEvent(QDropEvent *event)
{
    if (!m_currentFolder) return;

    QByteArray data = event->mimeData()->data(QStringLiteral("application/x-rpgforge-corkboard-card"));
    if (data.isEmpty()) return;

    CorkboardCard *draggedCard = *reinterpret_cast<CorkboardCard**>(data.data());
    
    // Find the drop target card
    QPoint pos = event->position().toPoint();
    ProjectTreeItem *targetItem = nullptr;
    
    for (int i = 0; i < m_layout->count(); ++i) {
        auto *widget = m_layout->itemAt(i)->widget();
        if (widget && widget->geometry().contains(pos)) {
            if (auto *targetCard = qobject_cast<CorkboardCard*>(widget)) {
                targetItem = targetCard->item();
                break;
            }
        }
    }

    if (draggedCard && draggedCard->item() != targetItem) {
        Q_EMIT itemsReordered(m_currentFolder, draggedCard->item(), targetItem);
    }

    event->acceptProposedAction();
}
