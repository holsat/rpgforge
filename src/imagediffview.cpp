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

#include "imagediffview.h"
#include "imagepreview.h"

#include <KLocalizedString>
#include <QFileInfo>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
constexpr auto kImageExts = std::array{"png", "jpg", "jpeg", "gif", "svg", "webp", "bmp"};
} // namespace

ImageDiffView::ImageDiffView(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    auto buildPane = [this](QLabel **caption, ImagePreview **preview) -> QWidget * {
        auto *pane = new QWidget(m_splitter);
        auto *layout = new QVBoxLayout(pane);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);
        *caption = new QLabel(pane);
        (*caption)->setAlignment(Qt::AlignCenter);
        (*caption)->setStyleSheet(QStringLiteral("font-weight: bold;"));
        *preview = new ImagePreview(pane);
        layout->addWidget(*caption);
        layout->addWidget(*preview, /*stretch=*/1);
        return pane;
    };

    m_splitter->addWidget(buildPane(&m_leftCaption, &m_left));
    m_splitter->addWidget(buildPane(&m_rightCaption, &m_right));
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({1, 1});

    outer->addWidget(m_splitter);
}

ImageDiffView::~ImageDiffView() = default;

bool ImageDiffView::setImages(const QString &leftPath, const QString &rightPath,
                              const QString &leftLabel, const QString &rightLabel)
{
    const QString lcap = leftLabel.isEmpty() ? QFileInfo(leftPath).fileName() : leftLabel;
    const QString rcap = rightLabel.isEmpty() ? QFileInfo(rightPath).fileName() : rightLabel;
    m_leftCaption->setText(lcap);
    m_rightCaption->setText(rcap);
    const bool leftOk = m_left->loadImage(leftPath);
    const bool rightOk = m_right->loadImage(rightPath);
    return leftOk && rightOk;
}

bool ImageDiffView::isImagePath(const QString &path)
{
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    for (const char *ext : kImageExts) {
        if (suffix == QByteArray(ext)) return true;
    }
    return false;
}
