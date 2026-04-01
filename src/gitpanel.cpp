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

#include "gitpanel.h"

#include <QLabel>
#include <QVBoxLayout>

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_statusLabel = new QLabel(QStringLiteral("Git integration coming in Phase 6."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_statusLabel);
    layout->addStretch();
}

GitPanel::~GitPanel() = default;

void GitPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    // TODO: show git status summary here in Phase 6
}
