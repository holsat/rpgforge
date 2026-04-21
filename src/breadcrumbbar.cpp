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

#include "breadcrumbbar.h"

#include <KColorScheme>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

BreadcrumbBar::BreadcrumbBar(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 2, 6, 2);
    m_layout->setSpacing(0);

    setFixedHeight(26);
    setAutoFillBackground(true);

    // Use KDE color scheme to distinguish from the editor while maintaining theme consistency
    KColorScheme scheme(QPalette::Active, KColorScheme::View);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, scheme.background(KColorScheme::AlternateBackground).color());
    setPalette(pal);

    // Debounce breadcrumb rebuilds — cursor moves rapidly during scrolling
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(100);
    connect(m_debounceTimer, &QTimer::timeout, this, &BreadcrumbBar::rebuildCrumbs);
}

BreadcrumbBar::~BreadcrumbBar() = default;

void BreadcrumbBar::setHeadings(const QVector<HeadingInfo> &headings)
{
    m_headings = headings;
}

void BreadcrumbBar::updateForLine(int line)
{
    m_pendingContext = MarkdownParser::headingContextForLine(m_headings, line);

    // Skip rebuild if context hasn't actually changed
    if (m_pendingContext.size() == m_context.size()) {
        bool same = true;
        for (int i = 0; i < m_context.size(); ++i) {
            if (m_context[i].line != m_pendingContext[i].line) {
                same = false;
                break;
            }
        }
        if (same) return;
    }

    m_debounceTimer->start();
}

void BreadcrumbBar::rebuildCrumbs()
{
    m_context = m_pendingContext;

    // Clear existing widgets
    QLayoutItem *child;
    while ((child = m_layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    if (m_context.isEmpty()) {
        auto *label = new QLabel(QStringLiteral("(no heading)"), this);
        label->setStyleSheet(QStringLiteral("color: gray; font-style: italic; padding-left: 4px;"));
        m_layout->addWidget(label);
        m_layout->addStretch();
        return;
    }

    for (int i = 0; i < m_context.size(); ++i) {
        if (i > 0) {
            auto *sep = new QLabel(QStringLiteral(" \u203A "), this); // › chevron
            sep->setStyleSheet(QStringLiteral("color: gray; padding: 0 2px;"));
            m_layout->addWidget(sep);
        }

        auto *btn = new QToolButton(this);
        btn->setText(m_context[i].text);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setAutoRaise(true);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { padding: 1px 4px; border: none; }"
            "QToolButton:hover { text-decoration: underline; }"));

        // Build the sibling dropdown lazily when the menu is about to show
        auto *menu = new QMenu(btn);
        HeadingInfo ctx = m_context[i];
        connect(menu, &QMenu::aboutToShow, this, [this, menu, ctx]() {
            if (!menu->isEmpty()) return; // already populated
            const auto siblings = MarkdownParser::siblingsOf(m_headings, ctx);
            for (const auto &sibling : siblings) {
                QAction *action = menu->addAction(sibling.text);
                int targetLine = sibling.line;

                if (sibling.line == ctx.line) {
                    QFont font = action->font();
                    font.setBold(true);
                    action->setFont(font);
                }

                connect(action, &QAction::triggered, this, [this, targetLine]() {
                    Q_EMIT headingClicked(targetLine);
                });
            }
        });
        btn->setMenu(menu);

        m_layout->addWidget(btn);
    }

    m_layout->addStretch();

    // Add Toggle Preview button at the far right
    auto *toggleBtn = new QToolButton(this);
    toggleBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    toggleBtn->setToolTip(tr("Toggle Live Preview"));
    toggleBtn->setAutoRaise(true);
    connect(toggleBtn, &QToolButton::clicked, this, &BreadcrumbBar::togglePreviewRequested);
    m_layout->addWidget(toggleBtn);
}
