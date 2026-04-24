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

#ifndef IMAGEPREVIEW_H
#define IMAGEPREVIEW_H

#include <QScrollArea>
#include <QLabel>
#include <QPixmap>

class ImagePreview : public QScrollArea
{
    Q_OBJECT

public:
    explicit ImagePreview(QWidget *parent = nullptr);
    ~ImagePreview() override;

    bool loadImage(const QString &path);
    void clear();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePixmap();

    QLabel *m_label = nullptr;
    QPixmap m_pixmap;
};

#endif // IMAGEPREVIEW_H
