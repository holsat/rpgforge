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

#ifndef EXPLORATIONSPANEL_H
#define EXPLORATIONSPANEL_H

#include "gitservice.h"

#include <QWidget>

class ExplorationGraphView;
class QLabel;
class QVBoxLayout;
class QFrame;
class QToolButton;

/**
 * \brief Side-panel widget that hosts the exploration graph and stash manager.
 *
 * ExplorationsPanel composes an ExplorationGraphView with a collapsible
 * stash section that lists current Git stash entries and provides apply/drop
 * actions.  It also exposes a "New Exploration" toolbar button for creating
 * branches directly from the panel.
 *
 * All signals from the embedded graph view are re-emitted from this class
 * so that parent widgets can connect to a single panel object rather than
 * to the inner graph view.
 *
 * \sa ExplorationGraphView, GitService
 */
class ExplorationsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorationsPanel(QWidget *parent = nullptr);

    /**
     * \brief Sets the project root directory and propagates it to child views.
     *
     * Does not trigger a repaint; call refresh() after this.
     *
     * \param path Absolute path to the project root (must be a Git repository).
     */
    void setRootPath(const QString &path);

    /**
     * \brief Reloads the exploration graph and stash list from Git.
     *
     * Delegates graph data loading to ExplorationGraphView::refresh() and
     * rebuilds the stash widget list synchronously via
     * GitService::listStashes().  Safe to call from the main thread.
     */
    void refresh();

    /**
     * \brief Returns the embedded ExplorationGraphView.
     *
     * Provided for callers that need to connect to colour-map signals or
     * invoke serialization methods directly on the graph view.
     *
     * \return Non-owning pointer to the embedded graph view; never null
     *         after construction.
     */
    ExplorationGraphView *graphView() const { return m_graphView; }

Q_SIGNALS:
    /**
     * \brief Re-emitted from the embedded ExplorationGraphView.
     *
     * Fired on the main thread when the user requests a branch switch.
     *
     * \param branchName Name of the branch to switch to.
     */
    void switchRequested(const QString &branchName);

    /**
     * \brief Re-emitted from the embedded ExplorationGraphView.
     *
     * Fired on the main thread when the user requests a branch integration.
     *
     * \param sourceBranch Name of the branch to merge into the current branch.
     */
    void integrateRequested(const QString &sourceBranch);

    /**
     * \brief Re-emitted from the embedded ExplorationGraphView.
     *
     * Fired on the main thread when the user requests a landmark on a commit.
     *
     * \param hash Full commit OID of the target node.
     */
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
