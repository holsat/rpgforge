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

#ifndef CORKBOARDCARD_H
#define CORKBOARDCARD_H

#include <QFrame>
#include <QString>
#include <QPoint>

class QLabel;
struct ProjectTreeItem;

class CorkboardCard : public QFrame
{
    Q_OBJECT

public:
    explicit CorkboardCard(ProjectTreeItem *item, QWidget *parent = nullptr);
    ~CorkboardCard() override;

    QString itemPath() const { return m_itemPath; }

Q_SIGNALS:
    void clicked();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupUi();

    QString m_itemPath;
    QString m_title;
    QString m_status;
    QString m_synopsis;
    QPoint m_dragStartPosition;
};

#endif // CORKBOARDCARD_H
