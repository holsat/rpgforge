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

#include "explorersidebar.h"
#include "projecttreepanel.h"
#include "fileexplorer.h"

#include <QVBoxLayout>
#include <QSplitter>

ExplorerSidebar::ExplorerSidebar(ProjectTreePanel *projectTree, FileExplorer *fileExplorer, QWidget *parent)
    : QWidget(parent), m_projectTree(projectTree), m_fileExplorer(fileExplorer)
{
    setupUi();
}

ExplorerSidebar::~ExplorerSidebar() = default;

void ExplorerSidebar::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_projectTree);
    m_splitter->addWidget(m_fileExplorer);
    
    // Initial distribution: 50/50
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    
    layout->addWidget(m_splitter);
}
