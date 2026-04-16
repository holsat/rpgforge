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

#ifndef EXPLORATIONSGRAPHVIEW_H
#define EXPLORATIONSGRAPHVIEW_H

#include "gitservice.h"

#include <QWidget>
#include <QMap>
#include <QColor>
#include <QVariantMap>

class ExplorationGraphView : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorationGraphView(QWidget *parent = nullptr);

    void setRepoPath(const QString &path);
    void refresh();

    void loadColorMap(const QVariantMap &data);
    QVariantMap saveColorMap() const;

    void setCurrentBranch(const QString &branch);

Q_SIGNALS:
    void switchRequested(const QString &branchName);
    void integrateRequested(const QString &sourceBranch);
    void createLandmarkRequested(const QString &hash);
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
    void drawBranchLabel(QPainter &p, const QString &branchName, int laneX, int topY);
    void drawCurrentHeadRing(QPainter &p, const NodeLayout &nl);
    const NodeLayout *hitTest(const QPoint &pos) const;
    void showContextMenuForNode(const NodeLayout &nl, const QPoint &globalPos);

    QString m_repoPath;
    QString m_currentBranch;
    QList<ExplorationNode> m_nodes;

    QList<NodeLayout> m_layout;
    QMap<QString, int> m_laneMap;

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
