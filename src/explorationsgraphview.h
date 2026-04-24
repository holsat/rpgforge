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

#ifndef EXPLORATIONSGRAPHVIEW_H
#define EXPLORATIONSGRAPHVIEW_H

#include "gitservice.h"

#include <QWidget>
#include <QMap>
#include <QColor>
#include <QVariantMap>

/**
 * \brief Custom widget that renders the exploration (branch) graph.
 *
 * ExplorationGraphView paints a vertical, lane-based commit graph for
 * all local branches in the repository.  Each branch occupies a coloured
 * lane; merge edges are drawn between lanes.  Commit nodes scale with
 * their word-count delta so writers can see at a glance where the most
 * productive commits are.
 *
 * The widget supports scroll via mouse wheel and keyboard arrows,
 * node selection via click, and a context menu for branch-management
 * actions.  It does not perform any Git I/O itself — refresh() delegates
 * to GitService::getExplorationGraph() on a worker thread.
 *
 * \sa ExplorationsPanel, GitService::getExplorationGraph()
 */
class ExplorationGraphView : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorationGraphView(QWidget *parent = nullptr);

    /**
     * \brief Sets the repository whose graph will be displayed.
     *
     * Does not trigger a repaint; call refresh() after this to load
     * and render the graph.
     *
     * \param path Absolute path to the root of the Git repository.
     */
    void setRepoPath(const QString &path);

    /**
     * \brief Reloads the exploration graph from GitService and repaints.
     *
     * Delegates to GitService::getExplorationGraph() on a worker thread.
     * The widget is repainted once the future resolves on the main thread.
     * It is safe to call this method from the main thread at any time.
     */
    void refresh();

    /**
     * \brief Restores the branch-colour map from a previously serialized map.
     *
     * Must be called before the first refresh() so that branch colours are
     * consistent across sessions.
     *
     * \param data Map of branch name strings to colour strings (e.g. "#a0c4ff"),
     *             as produced by saveColorMap().
     * \sa saveColorMap()
     */
    void loadColorMap(const QVariantMap &data);

    /**
     * \brief Serializes the current branch-colour assignments to a QVariantMap.
     *
     * \return Map of branch name strings to colour strings suitable for
     *         passing to ProjectManager::saveExplorationData().
     * \sa loadColorMap()
     */
    QVariantMap saveColorMap() const;

    /**
     * \brief Updates the widget's notion of which branch is currently checked out.
     *
     * The HEAD node for this branch is rendered with a distinct ring.
     * Does not reload graph data; call refresh() if the branch has changed
     * because of a checkout.
     *
     * \param branch Name of the currently active branch.
     */
    void setCurrentBranch(const QString &branch);

Q_SIGNALS:
    /**
     * \brief Emitted when the user requests to switch to a branch via the context menu.
     *
     * Fired on the main thread.
     *
     * \param branchName Name of the branch to switch to.
     */
    void switchRequested(const QString &branchName);

    /**
     * \brief Emitted when the user requests to integrate (merge) a branch via the context menu.
     *
     * Fired on the main thread.
     *
     * \param sourceBranch Name of the branch to merge into the current branch.
     */
    void integrateRequested(const QString &sourceBranch);

    /**
     * \brief Emitted when the user requests a landmark tag on a commit.
     *
     * Fired on the main thread.
     *
     * \param hash Full commit OID of the node on which the landmark is requested.
     */
    void createLandmarkRequested(const QString &hash);

    /**
     * \brief Emitted when the user assigns a new colour to a branch lane.
     *
     * Fired on the main thread.  Callers should persist the updated map via
     * saveColorMap() and ProjectManager::saveExplorationData().
     */
    void colorMapChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override { Q_UNUSED(next); return false; }

private:
    struct NodeLayout {
        const ExplorationNode *node;
        QPoint center;
        int radius;
    };

    void layoutNodes();
    QColor colorForBranch(const QString &branchName);
    QColor colorForBranchConst(const QString &branchName) const;
    void drawLanePath(QPainter &p, const QString &branchName);
    void drawNode(QPainter &p, const NodeLayout &nl);
    void drawLandmarkLabel(QPainter &p, const NodeLayout &nl, const QColor &color);
    void drawBranchLabel(QPainter &p, const QString &branchName, int laneX, int topY,
                         QList<QRect> &drawnLabelRects);
    void drawCurrentHeadRing(QPainter &p, const NodeLayout &nl);
    const NodeLayout *hitTest(const QPoint &pos) const;
    void showContextMenuForNode(const NodeLayout &nl, const QPoint &globalPos);

    QString m_repoPath;
    QString m_currentBranch;
    QList<ExplorationNode> m_nodes;

    QList<NodeLayout> m_layout;
    QMap<QString, int> m_laneMap;
    QMap<QString, int> m_branchTopY;   // branch name -> minimum content-space Y

    QMap<QString, QColor> m_laneColor;
    static const QList<QColor> kColorPalette;

    int m_scrollOffset = 0;
    int m_contentHeight = 0;
    int m_selectedIndex = -1;

    static constexpr int kLaneWidth   = 72;
    static constexpr int kNodeSpacing = 64;
    static constexpr int kMinRadius   = 6;
    static constexpr int kMaxRadius   = 20;
    static constexpr int kTopPad      = 40;
    static constexpr int kSidePad     = 16;
};

#endif // EXPLORATIONSGRAPHVIEW_H
