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

#ifndef IMAGEDIFFVIEW_H
#define IMAGEDIFFVIEW_H

#include <QWidget>

class QLabel;
class QSplitter;
class ImagePreview;

/// Side-by-side image comparison view. Used instead of Kompare's blank
/// text-diff part when the user compares two image files — e.g. viewing
/// an older version of a PNG against the current one after an overwrite.
class ImageDiffView : public QWidget
{
    Q_OBJECT

public:
    explicit ImageDiffView(QWidget *parent = nullptr);
    ~ImageDiffView() override;

    /// Load `leftPath` on the left pane and `rightPath` on the right pane.
    /// Each side shows its file's basename above the image by default;
    /// pass non-empty label strings to override.
    bool setImages(const QString &leftPath, const QString &rightPath,
                   const QString &leftLabel = QString(),
                   const QString &rightLabel = QString());

    /// Recognize the file types we render side-by-side. Lets MainWindow
    /// route to ImageDiffView vs. VisualDiffView based on input paths.
    static bool isImagePath(const QString &path);

private:
    ImagePreview *m_left = nullptr;
    ImagePreview *m_right = nullptr;
    QLabel *m_leftCaption = nullptr;
    QLabel *m_rightCaption = nullptr;
    QSplitter *m_splitter = nullptr;
};

#endif // IMAGEDIFFVIEW_H
