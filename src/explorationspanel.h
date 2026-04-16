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

#ifndef EXPLORATIONSPANEL_H
#define EXPLORATIONSPANEL_H

#include "gitservice.h"

#include <QWidget>

class ExplorationGraphView;
class QLabel;
class QVBoxLayout;
class QFrame;
class QToolButton;

class ExplorationsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorationsPanel(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void refresh();

    ExplorationGraphView *graphView() const { return m_graphView; }

Q_SIGNALS:
    void switchRequested(const QString &branchName);
    void integrateRequested(const QString &sourceBranch);
    void createLandmarkRequested(const QString &hash);

private Q_SLOTS:
    void onNewExploration();
    void onStashApply(int stashIndex);
    void onStashDrop(int stashIndex);
    void refreshStashList();

private:
    void setupUi();
    void buildStashEntry(QVBoxLayout *layout, const StashEntry &entry);

    QString m_rootPath;
    ExplorationGraphView *m_graphView   = nullptr;
    QFrame               *m_stashFrame  = nullptr;
    QVBoxLayout          *m_stashLayout = nullptr;
    QLabel               *m_stashHeader = nullptr;
    QToolButton          *m_stashToggle = nullptr;
    bool                  m_stashExpanded = true;
};

#endif // EXPLORATIONSPANEL_H
