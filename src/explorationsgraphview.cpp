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

#include "explorationsgraphview.h"

#include <KLocalizedString>

#include <QAction>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

const QList<QColor> ExplorationGraphView::kColorPalette = {
    QColor(QStringLiteral("#888888")),  // 0: main
    QColor(QStringLiteral("#4DA6FF")),  // 1: blue
    QColor(QStringLiteral("#4CAF50")),  // 2: green
    QColor(QStringLiteral("#F44336")),  // 3: red
    QColor(QStringLiteral("#AB47BC")),  // 4: purple
    QColor(QStringLiteral("#FF9800")),  // 5: orange
    QColor(QStringLiteral("#00BCD4")),  // 6: cyan
    QColor(QStringLiteral("#795548")),  // 7: brown
};

ExplorationGraphView::ExplorationGraphView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(260);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

// --- Color management ---

QColor ExplorationGraphView::colorForBranch(const QString &branchName)
{
    auto it = m_laneColor.constFind(branchName);
    if (it != m_laneColor.constEnd())
        return it.value();

    const QColor color = kColorPalette[m_laneColor.size() % kColorPalette.size()];
    m_laneColor.insert(branchName, color);
    Q_EMIT colorMapChanged();
    return color;
}

QColor ExplorationGraphView::colorForBranchConst(const QString &branchName) const
{
    auto it = m_laneColor.constFind(branchName);
    if (it != m_laneColor.constEnd())
        return it.value();
    return QColor(QStringLiteral("#888888"));
}

void ExplorationGraphView::loadColorMap(const QVariantMap &data)
{
    m_laneColor.clear();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        m_laneColor.insert(it.key(), QColor(it.value().toString()));
    }
}

QVariantMap ExplorationGraphView::saveColorMap() const
{
    QVariantMap map;
    for (auto it = m_laneColor.constBegin(); it != m_laneColor.constEnd(); ++it) {
        map.insert(it.key(), it.value().name());
    }
    return map;
}

// --- Data setters ---

void ExplorationGraphView::setRepoPath(const QString &path)
{
    m_repoPath = path;
}

void ExplorationGraphView::setCurrentBranch(const QString &branch)
{
    m_currentBranch = branch;
    update();
}

// --- Refresh / layout ---

void ExplorationGraphView::refresh()
{
    m_nodes.clear();
    m_layout.clear();
    update();

    GitService::instance().getExplorationGraph(m_repoPath)
        .then(this, [this](QList<ExplorationNode> nodes) {
            m_nodes = std::move(nodes);
            layoutNodes();
            update();
        });
}

void ExplorationGraphView::layoutNodes()
{
    m_layout.clear();
    m_laneMap.clear();

    if (m_nodes.isEmpty())
        return;

    // Collect unique branch names and count occurrences
    QMap<QString, int> branchCount;
    for (const auto &node : m_nodes) {
        branchCount[node.branchName]++;
    }

    // Find the "main" branch: the branch whose earliest commit has no parent,
    // or the most frequent branch as fallback.
    QString mainBranch;
    for (int i = m_nodes.size() - 1; i >= 0; --i) {
        if (m_nodes[i].primaryParentHash.isEmpty()) {
            mainBranch = m_nodes[i].branchName;
            break;
        }
    }
    if (mainBranch.isEmpty()) {
        int maxCount = 0;
        for (auto it = branchCount.constBegin(); it != branchCount.constEnd(); ++it) {
            if (it.value() > maxCount) {
                maxCount = it.value();
                mainBranch = it.key();
            }
        }
    }

    // Assign lane indices: 0 for main, then alternating 1, -1, 2, -2, ...
    QStringList otherBranches;
    for (auto it = branchCount.constBegin(); it != branchCount.constEnd(); ++it) {
        if (it.key() != mainBranch)
            otherBranches.append(it.key());
    }
    otherBranches.sort();

    m_laneMap.insert(mainBranch, 0);
    int laneSlot = 1;
    for (const QString &branch : std::as_const(otherBranches)) {
        int laneIndex = (laneSlot % 2 == 1) ? ((laneSlot + 1) / 2) : -((laneSlot) / 2);
        m_laneMap.insert(branch, laneIndex);
        ++laneSlot;
    }

    // Assign colors in lane order (main first, then by lane index)
    colorForBranch(mainBranch);
    for (const QString &branch : std::as_const(otherBranches)) {
        colorForBranch(branch);
    }

    // Nodes arrive sorted newest-first; keep that order for vertical placement
    const int centerX = width() / 2;

    m_layout.reserve(m_nodes.size());
    for (int i = 0; i < m_nodes.size(); ++i) {
        const ExplorationNode &node = m_nodes[i];
        const int laneIndex = m_laneMap.value(node.branchName, 0);
        const int x = centerX + laneIndex * kLaneWidth;
        const int y = kTopPad + i * kNodeSpacing;

        int radius;
        if (!node.tags.isEmpty()) {
            radius = kMaxRadius;
        } else {
            radius = qBound(kMinRadius,
                static_cast<int>(std::sqrt(static_cast<double>(std::abs(node.wordCountDelta))) * 1.5),
                kMaxRadius);
        }

        m_layout.append(NodeLayout{&node, QPoint(x, y), radius});
    }

    m_contentHeight = kTopPad + m_nodes.size() * kNodeSpacing + 20;

    // Clamp scroll offset after layout
    m_scrollOffset = qBound(0, m_scrollOffset, qMax(0, m_contentHeight - height()));
}

// --- Paint ---

void ExplorationGraphView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), palette().window());

    if (m_layout.isEmpty()) {
        p.setPen(palette().placeholderText().color());
        p.drawText(rect(), Qt::AlignCenter,
                   i18n("No explorations yet.\nStart writing to begin."));
        return;
    }

    // Draw lane paths behind nodes
    for (auto it = m_laneMap.constBegin(); it != m_laneMap.constEnd(); ++it) {
        drawLanePath(p, it.key());
    }

    // Draw branch labels at top of each lane
    for (auto it = m_laneMap.constBegin(); it != m_laneMap.constEnd(); ++it) {
        const int laneX = width() / 2 + it.value() * kLaneWidth;
        int topY = height();
        for (const NodeLayout &nl : std::as_const(m_layout)) {
            if (nl.node->branchName == it.key()) {
                const int screenY = nl.center.y() - m_scrollOffset;
                topY = qMin(topY, screenY);
            }
        }
        if (topY < height())
            drawBranchLabel(p, it.key(), laneX, topY - 24);
    }

    // Draw nodes
    for (int i = 0; i < m_layout.size(); ++i) {
        const NodeLayout &nl = m_layout[i];
        const QPoint screenCenter(nl.center.x(), nl.center.y() - m_scrollOffset);

        // Skip nodes entirely off-screen
        if (screenCenter.y() < -kMaxRadius || screenCenter.y() > height() + kMaxRadius)
            continue;

        // Build a temporary layout with screen coords for drawing
        NodeLayout screenNl{nl.node, screenCenter, nl.radius};
        drawNode(p, screenNl);

        if (!nl.node->tags.isEmpty())
            drawLandmarkLabel(p, screenNl, colorForBranchConst(nl.node->branchName));

        // HEAD indicator: first node on the current branch is HEAD
        if (nl.node->branchName == m_currentBranch && !m_nodes.isEmpty()
            && nl.node->hash == m_nodes.first().hash)
            drawCurrentHeadRing(p, screenNl);

        // Keyboard selection ring
        if (i == m_selectedIndex) {
            p.setPen(QPen(palette().highlight().color(), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(screenCenter, nl.radius + 4, nl.radius + 4);
        }
    }
}

void ExplorationGraphView::drawLanePath(QPainter &p, const QString &branchName)
{
    // Collect nodes for this branch, sorted by y ascending (top to bottom on screen)
    QList<const NodeLayout *> branchNodes;
    for (const NodeLayout &nl : std::as_const(m_layout)) {
        if (nl.node->branchName == branchName)
            branchNodes.append(&nl);
    }
    if (branchNodes.size() < 2)
        return;

    // Nodes are newest-first (lowest y index = top), so they are already in y order
    QColor lineColor = colorForBranchConst(branchName);
    lineColor.setAlpha(200);
    p.setPen(QPen(lineColor, 2));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    const QPoint first(branchNodes[0]->center.x(),
                       branchNodes[0]->center.y() - m_scrollOffset);
    path.moveTo(first);

    for (int i = 1; i < branchNodes.size(); ++i) {
        const QPoint pt(branchNodes[i]->center.x(),
                        branchNodes[i]->center.y() - m_scrollOffset);
        const QPoint prev(branchNodes[i - 1]->center.x(),
                          branchNodes[i - 1]->center.y() - m_scrollOffset);

        if (prev.x() == pt.x()) {
            path.lineTo(pt);
        } else {
            // S-curve between lanes
            const int midY = (prev.y() + pt.y()) / 2;
            path.cubicTo(QPoint(prev.x(), midY), QPoint(pt.x(), midY), pt);
        }
    }
    p.drawPath(path);

    // Draw merge connections: thin curved lines to merge parents in other lanes
    for (const NodeLayout *nl : std::as_const(branchNodes)) {
        if (nl->node->mergeParentHash.isEmpty())
            continue;

        // Find the merge parent in the full layout
        const NodeLayout *mergeParent = nullptr;
        for (const NodeLayout &candidate : std::as_const(m_layout)) {
            if (candidate.node->hash == nl->node->mergeParentHash) {
                mergeParent = &candidate;
                break;
            }
        }
        if (!mergeParent)
            continue;

        QColor mergeColor = lineColor;
        mergeColor.setAlpha(120);
        p.setPen(QPen(mergeColor, 1.5, Qt::DashLine));

        const QPoint from(nl->center.x(), nl->center.y() - m_scrollOffset);
        const QPoint to(mergeParent->center.x(), mergeParent->center.y() - m_scrollOffset);
        const int midY = (from.y() + to.y()) / 2;

        QPainterPath mergePath;
        mergePath.moveTo(from);
        mergePath.cubicTo(QPoint(from.x(), midY), QPoint(to.x(), midY), to);
        p.drawPath(mergePath);
    }
}

void ExplorationGraphView::drawNode(QPainter &p, const NodeLayout &nl)
{
    const QColor nodeColor = colorForBranchConst(nl.node->branchName);
    p.setBrush(nodeColor);
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(nl.center, nl.radius, nl.radius);

    // Landmark glow ring
    if (!nl.node->tags.isEmpty()) {
        QColor glowColor = nodeColor;
        glowColor.setAlpha(128);
        p.setPen(QPen(glowColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(nl.center, nl.radius + 3, nl.radius + 3);
    }
}

void ExplorationGraphView::drawLandmarkLabel(QPainter &p, const NodeLayout &nl,
                                              const QColor &color)
{
    if (nl.node->tags.isEmpty())
        return;

    const QString text = nl.node->tags.first();
    QFont labelFont = font();
    labelFont.setPointSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);

    const QFontMetrics fm(labelFont);
    const int textWidth = fm.horizontalAdvance(text);
    const int pillWidth = textWidth + 12;
    const int pillHeight = fm.height() + 6;

    // Place to the right by default, flip left if near edge
    int labelX = nl.center.x() + nl.radius + 8;
    if (labelX + pillWidth > width() - kSidePad) {
        labelX = nl.center.x() - nl.radius - 8 - pillWidth;
    }
    const int labelY = nl.center.y() - pillHeight / 2;

    const QRect pillRect(labelX, labelY, pillWidth, pillHeight);
    p.setPen(Qt::NoPen);
    p.setBrush(color.darker(120));
    p.drawRoundedRect(pillRect, 4, 4);

    p.setPen(Qt::white);
    p.drawText(pillRect, Qt::AlignCenter, text);
}

void ExplorationGraphView::drawBranchLabel(QPainter &p, const QString &branchName,
                                            int laneX, int topY)
{
    QFont labelFont = font();
    labelFont.setPointSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);

    QString label = branchName;
    if (label.length() > 14) {
        label = label.left(13) + QStringLiteral("\u2026");
    }

    p.setPen(colorForBranchConst(branchName));
    const QFontMetrics fm(labelFont);
    const int textWidth = fm.horizontalAdvance(label);
    p.drawText(QPoint(laneX - textWidth / 2, topY), label);
}

void ExplorationGraphView::drawCurrentHeadRing(QPainter &p, const NodeLayout &nl)
{
    p.setPen(QPen(Qt::white, 2, Qt::DotLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(nl.center, nl.radius + 5, nl.radius + 5);
}

// --- Hit testing ---

const ExplorationGraphView::NodeLayout *ExplorationGraphView::hitTest(const QPoint &pos) const
{
    for (const NodeLayout &nl : m_layout) {
        const QPoint screenCenter(nl.center.x(), nl.center.y() - m_scrollOffset);
        const int dx = pos.x() - screenCenter.x();
        const int dy = pos.y() - screenCenter.y();
        const int hitRadius = nl.radius + 4;
        if (dx * dx + dy * dy <= hitRadius * hitRadius)
            return &nl;
    }
    return nullptr;
}

// --- Input events ---

void ExplorationGraphView::wheelEvent(QWheelEvent *event)
{
    m_scrollOffset = qBound(0,
        m_scrollOffset - event->angleDelta().y() / 3,
        qMax(0, m_contentHeight - height()));
    update();
    event->accept();
}

void ExplorationGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const NodeLayout *hit = hitTest(event->pos());
    if (hit) {
        m_selectedIndex = static_cast<int>(
            std::distance(m_layout.constData(), hit));
    } else {
        m_selectedIndex = -1;
    }
    update();
}

void ExplorationGraphView::mouseMoveEvent(QMouseEvent *event)
{
    // Show tooltip for branch labels when hovering near them
    for (auto it = m_laneMap.constBegin(); it != m_laneMap.constEnd(); ++it) {
        const int laneX = width() / 2 + it.value() * kLaneWidth;

        int topY = height();
        for (const NodeLayout &nl : std::as_const(m_layout)) {
            if (nl.node->branchName == it.key()) {
                const int screenY = nl.center.y() - m_scrollOffset;
                topY = qMin(topY, screenY);
            }
        }
        const int labelY = topY - 24;
        const QRect labelRegion(laneX - 40, labelY - 10, 80, 20);

        if (labelRegion.contains(event->pos())) {
            QToolTip::showText(event->globalPosition().toPoint(), it.key(), this);
            return;
        }
    }
    QToolTip::hideText();
}

void ExplorationGraphView::contextMenuEvent(QContextMenuEvent *event)
{
    const NodeLayout *hit = hitTest(event->pos());
    if (!hit)
        return;

    showContextMenuForNode(*hit, event->globalPos());
}

void ExplorationGraphView::showContextMenuForNode(const NodeLayout &nl,
                                                   const QPoint &globalPos)
{
    const ExplorationNode *node = nl.node;

    QMenu menu(this);
    auto *switchAct = menu.addAction(
        i18n("Switch to \"%1\"", node->branchName));
    auto *integrateAct = menu.addAction(
        i18n("Integrate \"%1\" into Current Path", node->branchName));
    menu.addSeparator();
    auto *landmarkAct = menu.addAction(i18n("Create Landmark..."));

    switchAct->setEnabled(node->branchName != m_currentBranch);
    integrateAct->setEnabled(node->branchName != m_currentBranch);

    auto *chosen = menu.exec(globalPos);
    if (chosen == switchAct)
        Q_EMIT switchRequested(node->branchName);
    else if (chosen == integrateAct)
        Q_EMIT integrateRequested(node->branchName);
    else if (chosen == landmarkAct)
        Q_EMIT createLandmarkRequested(node->hash);
}

void ExplorationGraphView::keyPressEvent(QKeyEvent *event)
{
    if (m_layout.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        m_selectedIndex = qMax(0, m_selectedIndex - 1);
        update();
        break;
    case Qt::Key_Down:
        m_selectedIndex = qMin(m_layout.size() - 1, m_selectedIndex + 1);
        update();
        break;
    case Qt::Key_Return:
    case Qt::Key_Space:
        if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
            const NodeLayout &nl = m_layout[m_selectedIndex];
            const QPoint screenCenter(nl.center.x(), nl.center.y() - m_scrollOffset);
            showContextMenuForNode(nl, mapToGlobal(screenCenter));
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void ExplorationGraphView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_nodes.isEmpty()) {
        layoutNodes();
        update();
    }
}
