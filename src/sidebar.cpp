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

#include "sidebar.h"

#include <KMultiTabBar>

#include <QHBoxLayout>
#include <QStackedWidget>

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabBar = new KMultiTabBar(KMultiTabBar::Left, this);
    m_tabBar->setStyle(KMultiTabBar::KDEV3ICON);
    layout->addWidget(m_tabBar);

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumWidth(250);
    m_stack->hide(); // hidden until a tab is activated
    layout->addWidget(m_stack);
}

Sidebar::~Sidebar() = default;

int Sidebar::addPanel(const QIcon &icon, const QString &text, QWidget *panel)
{
    int id = m_nextId++;

    m_tabBar->appendTab(icon, id, text);
    m_stack->addWidget(panel);
    m_idToName.insert(id, text);

    // Connect tab click to toggle
    KMultiTabBarTab *tab = m_tabBar->tab(id);
    if (tab) {
        connect(tab, &KMultiTabBarTab::clicked, this, [this, id]() {
            togglePanel(id);
        });
    }

    return id;
}

void Sidebar::togglePanel(int id)
{
    if (m_currentId == id) {
        // Hide the current panel
        m_stack->hide();
        m_tabBar->setTab(id, false);
        m_currentId = -1;
        Q_EMIT panelVisibilityChanged(id, false);
    } else {
        showPanel(id);
    }
}

void Sidebar::showPanel(int id)
{
    // Deactivate previous tab
    if (m_currentId >= 0) {
        m_tabBar->setTab(m_currentId, false);
    }

    // Activate new tab and show its panel
    m_tabBar->setTab(id, true);

    // Find the widget index for this ID
    // Panels are added in order, so ID maps to index directly
    if (id < m_stack->count()) {
        m_stack->setCurrentIndex(id);
    }

    m_stack->show();
    m_currentId = id;
    Q_EMIT panelVisibilityChanged(id, true);
}

QWidget *Sidebar::panel(int id) const
{
    if (id >= 0 && id < m_stack->count()) {
        return m_stack->widget(id);
    }
    return nullptr;
}

QStringList Sidebar::panelNames() const
{
    QStringList names;
    names.reserve(m_idToName.size());
    for (auto it = m_idToName.cbegin(); it != m_idToName.cend(); ++it) {
        names.append(it.value());
    }
    return names;
}

QString Sidebar::panelName(int id) const
{
    return m_idToName.value(id);
}

int Sidebar::panelIdFromName(const QString &name) const
{
    for (auto it = m_idToName.cbegin(); it != m_idToName.cend(); ++it) {
        if (it.value() == name) {
            return it.key();
        }
    }
    return -1;
}
