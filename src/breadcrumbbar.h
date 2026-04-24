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

#ifndef BREADCRUMBBAR_H
#define BREADCRUMBBAR_H

#include "markdownparser.h"

#include <QWidget>
#include <QVector>

class QHBoxLayout;
class QTimer;

// A VS Code-style breadcrumb navigation bar showing the heading hierarchy
// at the current cursor position. Each segment is clickable and shows a
// dropdown of sibling headings at that level.
class BreadcrumbBar : public QWidget
{
    Q_OBJECT

public:
    explicit BreadcrumbBar(QWidget *parent = nullptr);
    ~BreadcrumbBar() override;

    // Update the full heading list (called when document changes)
    void setHeadings(const QVector<HeadingInfo> &headings);

    // Update the breadcrumb to reflect the heading context at the given line
    void updateForLine(int line);

Q_SIGNALS:
    void headingClicked(int line);
    void togglePreviewRequested();

private Q_SLOTS:
    void rebuildCrumbs();

private:
    QHBoxLayout *m_layout = nullptr;
    QTimer *m_debounceTimer = nullptr;
    QVector<HeadingInfo> m_headings;
    QVector<HeadingInfo> m_context;  // current heading ancestor chain
    QVector<HeadingInfo> m_pendingContext; // context waiting to be rendered
};

#endif // BREADCRUMBBAR_H
