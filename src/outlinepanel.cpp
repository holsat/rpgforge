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

#include "outlinepanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

OutlinePanel::OutlinePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Toolbar with expand/collapse buttons
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(4, 2, 4, 2);
    toolbar->setSpacing(4);

    auto *expandBtn = new QToolButton(this);
    expandBtn->setIcon(QIcon::fromTheme(QStringLiteral("expand-all"),
                       QIcon::fromTheme(QStringLiteral("list-add"))));
    expandBtn->setToolTip(QStringLiteral("Expand All"));
    expandBtn->setAutoRaise(true);
    connect(expandBtn, &QToolButton::clicked, this, &OutlinePanel::expandAll);
    toolbar->addWidget(expandBtn);

    auto *collapseBtn = new QToolButton(this);
    collapseBtn->setIcon(QIcon::fromTheme(QStringLiteral("collapse-all"),
                         QIcon::fromTheme(QStringLiteral("list-remove"))));
    collapseBtn->setToolTip(QStringLiteral("Collapse All"));
    collapseBtn->setAutoRaise(true);
    connect(collapseBtn, &QToolButton::clicked, this, &OutlinePanel::collapseAll);
    toolbar->addWidget(collapseBtn);

    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setAnimated(true);
    m_tree->setIndentation(16);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePanel::onItemClicked);

    // Debounce timer for parsing
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(300);
    connect(m_debounceTimer, &QTimer::timeout, this, &OutlinePanel::rebuildTree);
}

OutlinePanel::~OutlinePanel() = default;

void OutlinePanel::documentChanged(const QString &text)
{
    m_pendingText = text;
    m_debounceTimer->start();
}

void OutlinePanel::rebuildTree()
{
    m_headings = m_parser.parseHeadings(m_pendingText);
    m_pendingText.clear();

    m_tree->clear();

    QVector<QTreeWidgetItem *> stack;

    for (const auto &h : m_headings) {
        if (h.level <= 0) continue; // Skip malformed headings

        auto *item = new QTreeWidgetItem();
        item->setText(0, h.text);
        item->setData(0, Qt::UserRole, h.line);
        item->setToolTip(0, QStringLiteral("H%1 — Line %2").arg(h.level).arg(h.line + 1));

        while (stack.size() >= h.level) {
            stack.removeLast();
        }

        QTreeWidgetItem *parent = nullptr;
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (stack[i]) {
                parent = stack[i];
                break;
            }
        }

        if (parent) {
            parent->addChild(item);
        } else {
            m_tree->addTopLevelItem(item);
        }

        while (stack.size() < h.level) {
            stack.append(nullptr);
        }
        stack[h.level - 1] = item;
    }

    m_tree->expandAll();

    Q_EMIT headingsUpdated(m_headings);
}

void OutlinePanel::onItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item) return;
    int line = item->data(0, Qt::UserRole).toInt();
    Q_EMIT headingClicked(line);
}

void OutlinePanel::highlightForLine(int line)
{
    if (m_headings.isEmpty()) {
        m_tree->clearSelection();
        return;
    }

    const auto context = MarkdownParser::headingContextForLine(m_headings, line);
    if (context.isEmpty()) {
        m_tree->clearSelection();
        return;
    }

    int targetLine = context.last().line;

    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == targetLine) {
            m_tree->blockSignals(true);
            m_tree->setCurrentItem(*it);
            m_tree->scrollToItem(*it);
            m_tree->blockSignals(false);
            return;
        }
        ++it;
    }
}

void OutlinePanel::expandAll()
{
    m_tree->expandAll();
}

void OutlinePanel::collapseAll()
{
    m_tree->collapseAll();
}
