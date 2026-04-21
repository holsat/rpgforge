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

#ifndef OUTLINEPANEL_H
#define OUTLINEPANEL_H

#include "markdownparser.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

class OutlinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);
    ~OutlinePanel() override;

    // Call when the document text changes
    void documentChanged(const QString &text);

    // Highlight the heading containing the given line
    void highlightForLine(int line);

    // Access current headings (shared with breadcrumb bar)
    const QVector<HeadingInfo> &headings() const { return m_headings; }

Q_SIGNALS:
    void headingClicked(int line);
    void headingsUpdated(const QVector<HeadingInfo> &headings);

private Q_SLOTS:
    void rebuildTree();
    void onItemClicked(QTreeWidgetItem *item, int column);
    void expandAll();
    void collapseAll();

private:
    QTreeWidget *m_tree = nullptr;
    QTimer *m_debounceTimer = nullptr;
    MarkdownParser m_parser;
    QVector<HeadingInfo> m_headings;
    QString m_pendingText;
};

#endif // OUTLINEPANEL_H
