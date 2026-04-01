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

#include "corkboardcard.h"
#include "projecttreemodel.h"
#include "projectmanager.h"
#include "variablemanager.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QFile>
#include <QDir>

CorkboardCard::CorkboardCard(ProjectTreeItem *item, QWidget *parent)
    : QFrame(parent), m_item(item)
{
    m_title = item->name;
    m_status = item->status;
    m_synopsis = item->synopsis;

    if (item->type == ProjectTreeItem::File) {
        QString projectPath = ProjectManager::instance().projectPath();
        QString fullPath = QDir(projectPath).absoluteFilePath(item->path);
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            auto meta = VariableManager::extractMetadata(content);
            if (!meta.title.isEmpty()) m_title = meta.title;
            if (!meta.status.isEmpty()) m_status = meta.status;
            if (!meta.synopsis.isEmpty()) m_synopsis = meta.synopsis;
        }
    }

    setFixedSize(200, 150);
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setStyleSheet(QStringLiteral(
        "CorkboardCard { background-color: #fff9c4; border: 1px solid #fbc02d; border-radius: 4px; }"
        "CorkboardCard:hover { border: 2px solid #f9a825; }"
        "QLabel { color: #000000; }"
    ));
    
    setupUi();
}

CorkboardCard::~CorkboardCard() = default;

void CorkboardCard::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto *titleLabel = new QLabel(m_title.isEmpty() ? QStringLiteral("Untitled") : m_title, this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    if (!m_status.isEmpty()) {
        auto *statusLabel = new QLabel(m_status, this);
        statusLabel->setStyleSheet(QStringLiteral("color: #7f8c8d; font-size: 10px; font-style: italic;"));
        layout->addWidget(statusLabel);
    }

    auto *synopsisLabel = new QLabel(m_synopsis, this);
    synopsisLabel->setWordWrap(true);
    synopsisLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    synopsisLabel->setStyleSheet(QStringLiteral("color: #2c3e50; font-size: 11px;"));
    layout->addWidget(synopsisLabel, 1);
}

void CorkboardCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPosition = event->pos();
        Q_EMIT clicked();
    }
    QFrame::mousePressEvent(event);
}

void CorkboardCard::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
        return;

    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData();

    QByteArray data;
    auto *ptr = this;
    data.append(reinterpret_cast<const char*>(&ptr), sizeof(ptr));
    mimeData->setData(QStringLiteral("application/x-rpgforge-corkboard-card"), data);
    
    drag->setMimeData(mimeData);
    drag->setPixmap(grab());
    drag->setHotSpot(event->pos());

    drag->exec(Qt::MoveAction);
}

void CorkboardCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        Q_EMIT doubleClicked();
    }
    QFrame::mouseDoubleClickEvent(event);
}
