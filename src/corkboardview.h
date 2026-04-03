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

#ifndef CORKBOARDVIEW_H
#define CORKBOARDVIEW_H

#include <QScrollArea>

class QGridLayout;
struct ProjectTreeItem;

class CorkboardView : public QScrollArea
{
    Q_OBJECT

public:
    explicit CorkboardView(QWidget *parent = nullptr);
    ~CorkboardView() override;

    void setFolder(ProjectTreeItem *folderItem);

Q_SIGNALS:
    void fileActivated(const QString &relativePath);
    void itemsReordered(ProjectTreeItem *folder, ProjectTreeItem *draggedItem, ProjectTreeItem *targetItem);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void clear();
    void addCard(ProjectTreeItem *item);
    void updateDropIndicator(const QPoint &pos);

    QWidget *m_contentWidget = nullptr;
    QGridLayout *m_layout = nullptr;
    ProjectTreeItem *m_currentFolder = nullptr;
    QWidget *m_dropIndicator = nullptr;
};

#endif // CORKBOARDVIEW_H
