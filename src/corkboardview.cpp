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

void CorkboardView::subscribe()
{
    // The view consumes public signals + folder snapshots only. No friended
    // access to the live model.
    connect(&ProjectManager::instance(), &ProjectManager::treeStructureChanged,
            this, [this] { refresh(); });

    connect(&ProjectManager::instance(), &ProjectManager::treeItemDataChanged,
            this, [this](const QList<int> &roles) {
        using R = ProjectTreeModel::Roles;
        if (roles.contains(R::SynopsisRole) || roles.contains(R::StatusRole)
            || roles.contains(Qt::DisplayRole) || roles.contains(Qt::EditRole)) {
            refresh();
        }
    });

    connect(&ProjectManager::instance(), &ProjectManager::projectClosed,
            this, [this] {
        m_currentFolderPath.clear();
        refresh();
    });
}

void CorkboardView::setFolder(const QString &folderPath)
{
    m_currentFolderPath = folderPath;
    refresh();
}

void CorkboardView::refresh()
{
    rebuildFromSnapshot();
}

void CorkboardView::rebuildFromSnapshot()
{
    clearCards();
    if (m_currentFolderPath.isEmpty()) return;

    auto folder = ProjectManager::instance().folderSnapshot(m_currentFolderPath);
    if (!folder) return;

    // Skip media / stylesheet folders: they have their own panes.
    const QString nameLower = folder->name.toLower();
    if (nameLower == QStringLiteral("media") || nameLower == QStringLiteral("stylesheets")) {
        return;
    }

    for (const auto &child : folder->children) {
        addCard(child);
    }
}

void CorkboardView::clearCards()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        // Use deleteLater so we don't destroy a card widget that may still be
        // on the call stack (e.g., the source of an active QDrag::exec()).
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void CorkboardView::addCard(const TreeNodeSnapshot &node)
{
    if (node.type == static_cast<int>(ProjectTreeItem::Folder)) {
        const QString nameLower = node.name.toLower();
        if (nameLower == QStringLiteral("media") || nameLower == QStringLiteral("stylesheets")) {
            return;
        }
    }

    auto *card = new CorkboardCard(node, this);
    int count = m_layout->count();
    int row = count / 4;
    int col = count % 4;
    m_layout->addWidget(card, row, col);

    if (node.type == static_cast<int>(ProjectTreeItem::File)) {
        const QString path = node.path;
        connect(card, &CorkboardCard::doubleClicked, this, [this, path]() {
            Q_EMIT fileActivated(path);
        });
    }
}

void CorkboardView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-corkboard-card-path"))) {
        event->acceptProposedAction();
    }
}

void CorkboardView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-rpgforge-corkboard-card-path"))) {
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
    if (m_currentFolderPath.isEmpty()) return;

    QByteArray data = event->mimeData()->data(QStringLiteral("application/x-rpgforge-corkboard-card-path"));
    if (data.isEmpty()) return;

    QString draggedPath = QString::fromUtf8(data);

    // Find the drop target card
    QPoint pos = event->position().toPoint();
    QString targetPath;

    for (int i = 0; i < m_layout->count(); ++i) {
        auto *widget = m_layout->itemAt(i)->widget();
        if (widget && widget != m_dropIndicator && widget->geometry().contains(pos)) {
            if (auto *targetCard = qobject_cast<CorkboardCard*>(widget)) {
                targetPath = targetCard->itemPath();
                break;
            }
        }
    }

    if (!draggedPath.isEmpty() && draggedPath != targetPath) {
        Q_EMIT itemsReordered(m_currentFolderPath, draggedPath, targetPath);
    }

    event->acceptProposedAction();
}
