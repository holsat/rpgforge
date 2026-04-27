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

#ifndef ENTITYGRAPHPANEL_H
#define ENTITYGRAPHPANEL_H

#include <QWidget>
#include <QHash>

class EntityGraphModel;
class LibrarianDatabase;
class QGraphicsView;
class QGraphicsScene;
class QLineEdit;
class QToolBar;
class QPushButton;
class QHBoxLayout;
class QCheckBox;

class EntityGraphNodeItem;
class EntityGraphEdgeItem;

/**
 * @brief Full-tab graph view panel for the entity / relationship graph.
 *
 * Layout:
 *   [Toolbar: refresh | search box | type filter checkboxes | reset focus]
 *   [QGraphicsView canvas with force-directed entity nodes and edges]
 *
 * UI behavior:
 *   - Click node    → emits openDossier(filePathOrEntityName) so MainWindow
 *                     can route to the LoreKeeper dossier file.
 *   - Right-click   → context menu: focus on neighborhood, open dossier,
 *                     clear focus.
 *   - Hover node    → tooltip with type + summary + tags.
 *   - Pan / zoom    → standard QGraphicsView mouse + scroll behavior.
 *   - Search box    → live-filters the graph; neighbors of matches stay
 *                     visible so the result is connected, not isolated.
 *   - Type filters  → checkbox per type discovered in the loaded data.
 *   - Refresh button → re-pulls data from LibrarianDatabase via
 *                      EntityGraphModel::reload().
 */
class EntityGraphPanel : public QWidget
{
    Q_OBJECT
public:
    explicit EntityGraphPanel(LibrarianDatabase *db, QWidget *parent = nullptr);
    ~EntityGraphPanel() override;

    /// Bind the panel to a LibrarianDatabase. Safe to call repeatedly
    /// (e.g. on project open / close). Triggers a refresh if non-null.
    void setDatabase(LibrarianDatabase *db);

    /// Re-populate the graph from the database. Cheap to call; the
    /// underlying model dedupes on no-op filter changes.
    void refresh();

    // ----- Test / D-Bus introspection accessors. ---------------------
    // These exist so the rpgforge.dbus interface can drive end-to-end
    // tests of the graph view from outside the process. Production UI
    // never calls these; production code goes through the model and
    // signals.
    QStringList allNodeNames() const;
    QStringList filteredNodeNames() const;
    int filteredNodeCount() const;
    int filteredEdgeCount() const;
    void setTypeFilterFromList(const QStringList &allowed);
    void setSearchQueryString(const QString &query);
    void setNeighborhoodFocusByName(const QString &entityName, int hops);
    void clearFocus();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    /// Emitted when the user activates a node (click or context menu →
    /// "Open dossier"). The argument is the entity NAME — the
    /// MainWindow translates that into a LoreKeeper file path using
    /// the existing dossier resolution logic. Empty string is never
    /// emitted.
    void openDossierRequested(const QString &entityName);

private Q_SLOTS:
    void onModelChanged();
    void onSearchTextChanged(const QString &text);
    void onTypeCheckboxToggled();
    void onResetFocusClicked();
    void onRefreshClicked();

private:
    void setupUi();
    void rebuildScene();
    void rebuildTypeFilterRow();   // recreate type checkboxes after a reload
    void runForceLayout(QList<EntityGraphNodeItem*> &nodes,
                         const QList<QPair<int,int>> &edgeIndices);
    void zoomBy(double factor);

    LibrarianDatabase *m_db;
    EntityGraphModel  *m_model;

    QGraphicsView   *m_view = nullptr;
    QGraphicsScene  *m_scene = nullptr;
    QLineEdit       *m_searchEdit = nullptr;
    QToolBar        *m_toolbar = nullptr;
    QHBoxLayout     *m_typeFilterLayout = nullptr;
    QHash<QString, QCheckBox*> m_typeChecks;

    QList<EntityGraphNodeItem*> m_nodeItems;
    QList<EntityGraphEdgeItem*> m_edgeItems;

    bool m_suppressFilterRebuild = false;
};

#endif // ENTITYGRAPHPANEL_H
