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

#ifndef CORKBOARDVIEW_H
#define CORKBOARDVIEW_H

#include <QScrollArea>

#include "treenodesnapshot.h"

class QGridLayout;

class CorkboardView : public QScrollArea
{
    Q_OBJECT

public:
    explicit CorkboardView(QWidget *parent = nullptr);
    ~CorkboardView() override;

    /**
     * \brief Subscribe to ProjectManager's public signals and rebuild the
     * corkboard whenever the tree changes.
     *
     * Replaces the old attachToProjectTree() pattern, which reached the
     * private model via friended access. The view now consumes only
     * snapshots (via folderSnapshot) and public signals. Call once after
     * construction.
     */
    void subscribe();

    void setFolder(const QString &folderPath);
    QString currentFolderPath() const { return m_currentFolderPath; }

Q_SIGNALS:
    void fileActivated(const QString &relativePath);
    void itemsReordered(const QString &folderPath, const QString &draggedPath, const QString &targetPath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void refresh();
    void rebuildFromSnapshot();
    void clearCards();
    void addCard(const TreeNodeSnapshot &node);
    void updateDropIndicator(const QPoint &pos);

    QWidget *m_contentWidget = nullptr;
    QGridLayout *m_layout = nullptr;
    QString m_currentFolderPath;
    QWidget *m_dropIndicator = nullptr;
};

#endif // CORKBOARDVIEW_H
