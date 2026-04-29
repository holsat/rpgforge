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

#include "entitygraphpanel.h"
#include "entitygraphmodel.h"
#include "librariandatabase.h"

#include <KLocalizedString>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QToolTip>
#include <QWheelEvent>
#include <QtMath>
#include <QRandomGenerator>

// ---------------------------------------------------------------------------
// Node item — circle sized by mention_count, coloured by type.
// ---------------------------------------------------------------------------

class EntityGraphEdgeItem;     // forward decl — used by EntityGraphNodeItem

class EntityGraphNodeItem : public QGraphicsEllipseItem
{
public:
    EntityGraphNodeItem(const EntityGraphNode &node)
        : m_node(node)
    {
        // Sizing: minimum 20px, scales with sqrt(mention_count) so a
        // 100-mention character isn't 100× the size of a 1-mention one.
        const double diameter = 20.0 + 4.0 * std::sqrt(qMax(0, node.mentionCount));
        setRect(-diameter / 2, -diameter / 2, diameter, diameter);
        setPen(QPen(Qt::black, 1.5));
        setBrush(colourForType(node.type));
        setFlag(ItemIsSelectable);
        setFlag(ItemIsMovable);
        setFlag(ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);

        QString tip = QStringLiteral("<b>%1</b>").arg(node.name.toHtmlEscaped());
        if (!node.type.isEmpty()) tip += QStringLiteral(" <i>(%1)</i>").arg(node.type);
        if (!node.summary.isEmpty()) tip += QStringLiteral("<br>%1").arg(node.summary.toHtmlEscaped());
        if (!node.tags.isEmpty()) {
            tip += QStringLiteral("<br><small>tags: %1</small>")
                .arg(node.tags.join(QStringLiteral(", ")).toHtmlEscaped());
        }
        setToolTip(tip);

        // Label rendered as a child text item so it scales / pans with the node.
        m_label = new QGraphicsTextItem(node.name, this);
        m_label->setDefaultTextColor(Qt::black);
        QFont f = m_label->font();
        f.setPointSize(8);
        m_label->setFont(f);
        const QRectF lr = m_label->boundingRect();
        m_label->setPos(-lr.width() / 2, diameter / 2 + 2);
    }

    qint64 entityId() const { return m_node.id; }
    QString entityName() const { return m_node.name; }
    const EntityGraphNode &nodeData() const { return m_node; }

    QPointF velocity() const { return m_velocity; }
    void setVelocity(const QPointF &v) { m_velocity = v; }
    void addForce(const QPointF &f) { m_velocity += f; }

    // Register an edge whose endpoint is this node. Edges call
    // updatePosition() when this node moves so they stay anchored.
    void addEdge(EntityGraphEdgeItem *e) { if (e) m_edges.append(e); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    static QColor colourForType(const QString &type)
    {
        // Stable, distinguishable palette for the typology defined in the
        // plan. Unknown types fall through to neutral grey.
        if (type == QLatin1String("character")) return QColor(QStringLiteral("#7eb6e6"));
        if (type == QLatin1String("place"))     return QColor(QStringLiteral("#a5d28a"));
        if (type == QLatin1String("time"))      return QColor(QStringLiteral("#dac07f"));
        if (type == QLatin1String("concept"))   return QColor(QStringLiteral("#c8a8e0"));
        if (type == QLatin1String("rule"))      return QColor(QStringLiteral("#e0a08a"));
        if (type == QLatin1String("myth"))      return QColor(QStringLiteral("#e689b8"));
        if (type == QLatin1String("magic"))     return QColor(QStringLiteral("#f3d56b"));
        return QColor(QStringLiteral("#cccccc"));
    }

    EntityGraphNode m_node;
    QGraphicsTextItem *m_label = nullptr;
    QPointF m_velocity{0.0, 0.0};
    QList<EntityGraphEdgeItem*> m_edges;
};

// ---------------------------------------------------------------------------
// Edge item — straight line, colour by relationship type.
// ---------------------------------------------------------------------------

class EntityGraphEdgeItem : public QGraphicsLineItem
{
public:
    EntityGraphEdgeItem(EntityGraphNodeItem *src, EntityGraphNodeItem *tgt,
                         const QString &relationship)
        : m_src(src), m_tgt(tgt), m_relationship(relationship)
    {
        // Containment edges (is_part_of) drawn dashed, peer edges solid.
        QPen p(QColor(QStringLiteral("#888888")), 1.2);
        if (relationship == QLatin1String("is_part_of")) {
            p.setStyle(Qt::DashLine);
            p.setColor(QColor(QStringLiteral("#a06090")));
        }
        setPen(p);
        setZValue(-1);          // below nodes
        // Register with both endpoints so they re-anchor us when dragged.
        if (m_src) m_src->addEdge(this);
        if (m_tgt) m_tgt->addEdge(this);
        updatePosition();
        setToolTip(relationship);
    }

    void updatePosition()
    {
        if (!m_src || !m_tgt) return;
        setLine(QLineF(m_src->pos(), m_tgt->pos()));
    }

    EntityGraphNodeItem *src() const { return m_src; }
    EntityGraphNodeItem *tgt() const { return m_tgt; }

private:
    EntityGraphNodeItem *m_src = nullptr;
    EntityGraphNodeItem *m_tgt = nullptr;
    QString m_relationship;
};

QVariant EntityGraphNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged) {
        for (auto *e : m_edges) {
            if (e) e->updatePosition();
        }
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

// ---------------------------------------------------------------------------
// EntityGraphPanel
// ---------------------------------------------------------------------------

EntityGraphPanel::EntityGraphPanel(LibrarianDatabase *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    m_model = new EntityGraphModel(db, this);
    setupUi();
    connect(m_model, &EntityGraphModel::modelChanged,
            this, &EntityGraphPanel::onModelChanged);
}

EntityGraphPanel::~EntityGraphPanel() = default;

void EntityGraphPanel::setDatabase(LibrarianDatabase *db)
{
    if (m_db == db) return;
    m_db = db;
    // Tear down and rebuild the model so it picks up the new
    // connection. Cheap: model construction is just member init.
    if (m_model) m_model->deleteLater();
    m_model = new EntityGraphModel(db, this);
    connect(m_model, &EntityGraphModel::modelChanged,
            this, &EntityGraphPanel::onModelChanged);
    if (db) refresh();
    else rebuildScene();
}

void EntityGraphPanel::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    QAction *refresh = m_toolbar->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        i18n("Refresh"));
    connect(refresh, &QAction::triggered, this, &EntityGraphPanel::onRefreshClicked);

    QAction *resetFocus = m_toolbar->addAction(
        QIcon::fromTheme(QStringLiteral("zoom-fit-best")),
        i18n("Reset View"));
    connect(resetFocus, &QAction::triggered, this, &EntityGraphPanel::onResetFocusClicked);

    m_toolbar->addSeparator();
    auto *searchLabel = new QLabel(i18n("Search:"), this);
    m_toolbar->addWidget(searchLabel);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(i18n("Filter by name or alias…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMaximumWidth(220);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &EntityGraphPanel::onSearchTextChanged);
    m_toolbar->addWidget(m_searchEdit);

    m_toolbar->addSeparator();
    auto *typeLabel = new QLabel(i18n("Types:"), this);
    m_toolbar->addWidget(typeLabel);
    // Type-filter checkboxes are added directly to the toolbar (via
    // addWidget → QAction) by rebuildTypeFilterRow(). We deliberately do
    // NOT wrap them in a QWidget/QHBoxLayout container — QToolBar does
    // not reliably re-layout when grandchildren are added, which would
    // leave the row visually empty.

    root->addWidget(m_toolbar);

    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QBrush(QColor(QStringLiteral("#f8f8f8"))));
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing, true);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Enable wheel-to-zoom by intercepting wheelEvent through an event
    // filter. Simpler than subclassing QGraphicsView for v1.
    m_view->viewport()->installEventFilter(this);
    root->addWidget(m_view, 1);
}

void EntityGraphPanel::refresh()
{
    m_model->reload();
}

void EntityGraphPanel::onRefreshClicked()
{
    refresh();
}

void EntityGraphPanel::onResetFocusClicked()
{
    m_model->clearNeighborhoodFocus();
    if (m_view && m_scene) {
        m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30),
                           Qt::KeepAspectRatio);
    }
}

void EntityGraphPanel::onSearchTextChanged(const QString &text)
{
    m_model->setSearchQuery(text);
}

void EntityGraphPanel::onTypeCheckboxToggled()
{
    if (m_suppressFilterRebuild) return;
    QSet<QString> allowed;
    for (auto it = m_typeChecks.constBegin(); it != m_typeChecks.constEnd(); ++it) {
        if (it.value()->isChecked()) allowed.insert(it.key());
    }
    // If every checkbox is on (or none exist), don't actually engage the
    // filter — that's "all types" semantics.
    if (allowed.size() == m_typeChecks.size()) allowed.clear();
    m_model->setTypeFilter(allowed);
}

void EntityGraphPanel::rebuildTypeFilterRow()
{
    m_suppressFilterRebuild = true;
    // Wipe the previous row: each checkbox was added via addWidget(cb),
    // which returns a QAction owning the wrapping widget. Removing the
    // action and deleteLater'ing it cleans up both.
    for (QAction *act : std::as_const(m_typeFilterActions)) {
        if (!act) continue;
        m_toolbar->removeAction(act);
        act->deleteLater();
    }
    m_typeFilterActions.clear();
    m_typeChecks.clear();

    const QSet<QString> types = m_model->allTypes();
    QStringList sorted(types.begin(), types.end());
    sorted.sort();
    for (const QString &type : sorted) {
        auto *cb = new QCheckBox(type, this);
        cb->setChecked(true);
        connect(cb, &QCheckBox::toggled,
                this, &EntityGraphPanel::onTypeCheckboxToggled);
        QAction *act = m_toolbar->addWidget(cb);
        m_typeFilterActions.append(act);
        m_typeChecks.insert(type, cb);
    }
    m_suppressFilterRebuild = false;
}

void EntityGraphPanel::onModelChanged()
{
    rebuildScene();
    // Rebuild the type filter row only if the discovered type set changed.
    // For simplicity, we rebuild it on every reload.
    rebuildTypeFilterRow();
}

void EntityGraphPanel::rebuildScene()
{
    m_scene->clear();
    m_nodeItems.clear();
    m_edgeItems.clear();

    const QList<EntityGraphNode> nodes = m_model->filteredNodes();
    const QList<EntityGraphEdge> edges = m_model->filteredEdges();
    if (nodes.isEmpty()) {
        // Stub: a friendly placeholder so the user knows the view loaded
        // but found nothing to draw (e.g. fresh project, no librarian
        // run yet, or a too-restrictive filter).
        auto *label = m_scene->addText(i18n(
            "No entities to show. Run the Librarian to extract entities, "
            "or relax the type filter."));
        label->setDefaultTextColor(QColor(QStringLiteral("#888888")));
        return;
    }

    QHash<qint64, int> idToIndex;
    for (int i = 0; i < nodes.size(); ++i) {
        auto *item = new EntityGraphNodeItem(nodes[i]);
        m_scene->addItem(item);
        // Random-ish initial position so the force layout has somewhere
        // to start. Spread within a circle whose radius scales with
        // node count so we don't pack 100 nodes into a 200×200 box.
        const double r = 60.0 + 12.0 * std::sqrt(double(nodes.size()));
        const double angle = 2.0 * M_PI * QRandomGenerator::global()->generateDouble();
        const double dist  = r * std::sqrt(QRandomGenerator::global()->generateDouble());
        item->setPos(dist * std::cos(angle), dist * std::sin(angle));
        idToIndex.insert(nodes[i].id, i);
        m_nodeItems.append(item);
    }

    QList<QPair<int,int>> edgeIndices;
    for (const auto &e : edges) {
        const int si = idToIndex.value(e.sourceId, -1);
        const int ti = idToIndex.value(e.targetId, -1);
        if (si < 0 || ti < 0) continue;
        edgeIndices.append({si, ti});
        auto *edgeItem = new EntityGraphEdgeItem(m_nodeItems[si], m_nodeItems[ti],
                                                  e.relationship);
        m_scene->addItem(edgeItem);
        m_edgeItems.append(edgeItem);
    }

    runForceLayout(m_nodeItems, edgeIndices);

    // Update edge positions after layout settles.
    for (auto *e : m_edgeItems) e->updatePosition();

    m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30),
                       Qt::KeepAspectRatio);
}

void EntityGraphPanel::runForceLayout(QList<EntityGraphNodeItem*> &nodes,
                                       const QList<QPair<int,int>> &edgeIndices)
{
    if (nodes.isEmpty()) return;

    // Fruchterman-Reingold one-shot. Parameters tuned for ~10–500
    // node graphs typical of fiction projects.
    const int N = nodes.size();
    const int iterations = qBound(40, 80 + 4 * N, 200);
    const double area = std::pow(80.0 * std::sqrt(double(N)) + 200.0, 2.0);
    const double k = std::sqrt(area / double(N));   // ideal edge length

    // Temperature: max displacement per iteration, decays linearly.
    double temp = 60.0 + 4.0 * std::sqrt(double(N));

    QVector<QPointF> positions(N);
    for (int i = 0; i < N; ++i) positions[i] = nodes[i]->pos();

    for (int it = 0; it < iterations; ++it) {
        QVector<QPointF> disp(N, QPointF(0.0, 0.0));

        // Repulsive forces between every pair.
        for (int i = 0; i < N; ++i) {
            for (int j = i + 1; j < N; ++j) {
                QPointF delta = positions[i] - positions[j];
                double dist = std::hypot(delta.x(), delta.y());
                if (dist < 0.01) {
                    // Jitter a co-located pair so they actually diverge.
                    delta = QPointF(0.5 - QRandomGenerator::global()->generateDouble(),
                                    0.5 - QRandomGenerator::global()->generateDouble());
                    dist = 0.01;
                }
                const double force = (k * k) / dist;
                const QPointF d = (delta / dist) * force;
                disp[i] += d;
                disp[j] -= d;
            }
        }

        // Attractive forces along each edge.
        for (const auto &e : edgeIndices) {
            QPointF delta = positions[e.first] - positions[e.second];
            const double dist = std::max(0.01, std::hypot(delta.x(), delta.y()));
            const double force = (dist * dist) / k;
            const QPointF d = (delta / dist) * force;
            disp[e.first]  -= d;
            disp[e.second] += d;
        }

        // Apply displacement, clamped by the temperature.
        for (int i = 0; i < N; ++i) {
            const double mag = std::max(0.01, std::hypot(disp[i].x(), disp[i].y()));
            const double clamped = std::min(mag, temp);
            positions[i] += (disp[i] / mag) * clamped;
        }

        temp *= 0.98;     // cool
    }

    for (int i = 0; i < N; ++i) {
        nodes[i]->setPos(positions[i]);
    }
}

void EntityGraphPanel::zoomBy(double factor)
{
    if (!m_view) return;
    m_view->scale(factor, factor);
}

bool EntityGraphPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        const double factor = (we->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        m_view->scale(factor, factor);
        return true;
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent*>(event);
        const QPointF scenePos = m_view->mapToScene(me->pos());
        for (auto *item : std::as_const(m_nodeItems)) {
            if (item->shape().contains(item->mapFromScene(scenePos))) {
                Q_EMIT openDossierRequested(item->entityName());
                return true;
            }
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::RightButton) {
            const QPointF scenePos = m_view->mapToScene(me->pos());
            for (auto *item : std::as_const(m_nodeItems)) {
                if (!item->shape().contains(item->mapFromScene(scenePos))) continue;
                QMenu menu;
                QAction *focusOne = menu.addAction(i18n("Focus 1-hop neighborhood"));
                QAction *focusTwo = menu.addAction(i18n("Focus 2-hop neighborhood"));
                menu.addSeparator();
                QAction *open = menu.addAction(i18n("Open dossier"));
                menu.addSeparator();
                QAction *clear = menu.addAction(i18n("Clear focus"));
                QAction *chosen = menu.exec(me->globalPosition().toPoint());
                if (chosen == focusOne) {
                    m_model->setNeighborhoodFocus(item->entityId(), 1);
                } else if (chosen == focusTwo) {
                    m_model->setNeighborhoodFocus(item->entityId(), 2);
                } else if (chosen == open) {
                    Q_EMIT openDossierRequested(item->entityName());
                } else if (chosen == clear) {
                    m_model->clearNeighborhoodFocus();
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

// ----- Test / D-Bus introspection accessors -----------------------------
// Production UI does not call these. They're the test surface that the
// rpgforge D-Bus interface (and unit tests) drive directly so the graph
// view's behavior is verifiable without simulating mouse input.

QStringList EntityGraphPanel::allNodeNames() const
{
    QStringList out;
    for (const auto &n : m_model->allNodes()) out.append(n.name);
    return out;
}

QStringList EntityGraphPanel::filteredNodeNames() const
{
    QStringList out;
    for (const auto &n : m_model->filteredNodes()) out.append(n.name);
    return out;
}

int EntityGraphPanel::filteredNodeCount() const
{
    return m_model->filteredNodes().size();
}

int EntityGraphPanel::filteredEdgeCount() const
{
    return m_model->filteredEdges().size();
}

void EntityGraphPanel::setTypeFilterFromList(const QStringList &allowed)
{
    m_model->setTypeFilter(QSet<QString>(allowed.begin(), allowed.end()));
}

void EntityGraphPanel::setSearchQueryString(const QString &query)
{
    if (m_searchEdit) m_searchEdit->setText(query);
    m_model->setSearchQuery(query);
}

void EntityGraphPanel::setNeighborhoodFocusByName(const QString &entityName, int hops)
{
    if (!m_db) return;
    const qint64 id = m_db->resolveEntityByName(entityName);
    if (id < 0) return;
    m_model->setNeighborhoodFocus(id, hops);
}

void EntityGraphPanel::clearFocus()
{
    m_model->clearNeighborhoodFocus();
}
