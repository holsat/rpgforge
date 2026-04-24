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

#include "imagepreview.h"

#include <QVBoxLayout>
#include <QResizeEvent>

ImagePreview::ImagePreview(QWidget *parent)
    : QScrollArea(parent)
{
    setAlignment(Qt::AlignCenter);
    setWidgetResizable(true);
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    setWidget(m_label);
}

ImagePreview::~ImagePreview() = default;

bool ImagePreview::loadImage(const QString &path)
{
    m_pixmap = QPixmap(path);
    if (m_pixmap.isNull()) {
        m_label->setText(tr("Failed to load image: %1").arg(path));
        return false;
    }
    updatePixmap();
    return true;
}

void ImagePreview::clear()
{
    m_pixmap = QPixmap();
    m_label->clear();
}

void ImagePreview::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    if (!m_pixmap.isNull()) {
        updatePixmap();
    }
}

void ImagePreview::updatePixmap()
{
    if (m_pixmap.isNull()) return;
    
    QSize areaSize = viewport()->size();
    if (m_pixmap.width() > areaSize.width() || m_pixmap.height() > areaSize.height()) {
        m_label->setPixmap(m_pixmap.scaled(areaSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_label->setPixmap(m_pixmap);
    }
}
