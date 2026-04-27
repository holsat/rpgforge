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

#include "variablespanel.h"
#include "variablemanager.h"
#include "librarianservice.h"
#include "librariandatabase.h"
#include "debuglog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolButton>
#include <QIcon>
#include <QSettings>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <KLocalizedString>

// --- DraggableVariableTree implementation ---

DraggableVariableTree::DraggableVariableTree(QWidget *parent)
    : QTreeWidget(parent)
{
}

QString DraggableVariableTree::fullVariableName(QTreeWidgetItem *item) const
{
    if (!item || item->flags() == Qt::ItemIsEnabled) return QString();
    
    // Check if it belongs to Library
    QTreeWidgetItem *p = item;
    bool isLibrary = false;
    while (p) {
        if (p->text(0) == i18n("LoreKeeper (Auto-extracted)")) {
            isLibrary = true;
            break;
        }
        p = p->parent();
    }

    if (isLibrary) {
        // Librarian path: type.entity.attribute
        QStringList parts;
        p = item;
        while (p && p->text(0) != i18n("LoreKeeper (Auto-extracted)")) {
            // LibrarianService sanitizes keys by removing spaces
            parts.prepend(p->text(0).replace(QStringLiteral(" "), QString()));
            p = p->parent();
        }
        return parts.join(QLatin1Char('.'));
    }

    if (item->parent() && item->parent()->text(0) != i18n("Custom Variables")) {
        // Variant: parent_name.variant_name
        return item->parent()->text(0) + QLatin1Char('.') + item->text(0);
    }
    return item->text(0);
}

void DraggableVariableTree::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
    }
    QTreeWidget::mousePressEvent(event);
}

void DraggableVariableTree::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }

    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }

    QTreeWidgetItem *item = itemAt(m_dragStartPos);
    if (!item) return;

    QString varName = fullVariableName(item);
    if (varName.isEmpty()) return;

    QString insertText = QStringLiteral("{{") + varName + QStringLiteral("}}");

    auto *drag = new QDrag(this);
    auto *mimeData = new QMimeData();
    mimeData->setText(insertText);
    drag->setMimeData(mimeData);
    drag->exec(Qt::CopyAction);
}

// --- VariablesPanel implementation ---

VariablesPanel::VariablesPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadVariables();
}

void VariablesPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    
    m_addButton = new QToolButton(toolbar);
    m_addButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_addButton->setToolTip(i18n("Add Main Variable"));
    connect(m_addButton, &QToolButton::clicked, this, &VariablesPanel::addVariable);

    m_addVariantButton = new QToolButton(toolbar);
    m_addVariantButton->setIcon(QIcon::fromTheme(QStringLiteral("entry-new"))); // Or another appropriate icon
    m_addVariantButton->setToolTip(i18n("Add Variant to Selected Variable"));
    connect(m_addVariantButton, &QToolButton::clicked, this, &VariablesPanel::addVariant);
    
    m_removeButton = new QToolButton(toolbar);
    m_removeButton->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    m_removeButton->setToolTip(i18n("Remove Selected Variable/Variant"));
    connect(m_removeButton, &QToolButton::clicked, this, &VariablesPanel::removeVariable);

    m_reindexButton = new QToolButton(toolbar);
    m_reindexButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_reindexButton->setToolTip(i18n("Re-index Library (Full Scan)"));
    connect(m_reindexButton, &QToolButton::clicked, this, &VariablesPanel::triggerReindex);

    m_loreScanButton = new QToolButton(toolbar);
    m_loreScanButton->setIcon(QIcon::fromTheme(QStringLiteral("document-edit-sign")));
    m_loreScanButton->setToolTip(i18n("Scan current document for lore with LoreKeeper AI"));
    connect(m_loreScanButton, &QToolButton::clicked, this, &VariablesPanel::forceLoreKeeperScan);

    auto *expandAllBtn = new QToolButton(toolbar);
    expandAllBtn->setIcon(QIcon::fromTheme(QStringLiteral("expand-symbolic"),
        QIcon::fromTheme(QStringLiteral("view-list-tree"))));
    expandAllBtn->setToolTip(i18n("Expand All"));
    connect(expandAllBtn, &QToolButton::clicked, this, [this]() { m_treeWidget->expandAll(); });

    auto *collapseAllBtn = new QToolButton(toolbar);
    collapseAllBtn->setIcon(QIcon::fromTheme(QStringLiteral("collapse-symbolic"),
        QIcon::fromTheme(QStringLiteral("view-list-details"))));
    collapseAllBtn->setToolTip(i18n("Collapse All"));
    connect(collapseAllBtn, &QToolButton::clicked, this, [this]() { m_treeWidget->collapseAll(); });

    toolbarLayout->addWidget(m_addButton);
    toolbarLayout->addWidget(m_addVariantButton);
    toolbarLayout->addWidget(m_removeButton);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(expandAllBtn);
    toolbarLayout->addWidget(collapseAllBtn);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(m_reindexButton);
    toolbarLayout->addWidget(m_loreScanButton);
    toolbarLayout->addStretch();
    
    m_loreScanProgress = new QProgressBar(this);
    m_loreScanProgress->setRange(0,0); // Indeterminate
    m_loreScanProgress->setMaximumSize(100, 16);
    m_loreScanProgress->hide();
    toolbarLayout->addWidget(m_loreScanProgress);
    
    layout->addWidget(toolbar);

    // Tree widget (with drag support)
    m_treeWidget = new DraggableVariableTree(this);
    m_treeWidget->setColumnCount(4);
    m_treeWidget->setHeaderLabels({i18n("Name"), i18n("Computed"), i18n("Expression"), i18n("Calc")});
    m_treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    
    m_customVarsRoot = new QTreeWidgetItem(m_treeWidget);
    m_customVarsRoot->setText(0, i18n("Custom Variables"));
    m_customVarsRoot->setFlags(Qt::ItemIsEnabled);
    m_customVarsRoot->setExpanded(true);

    m_libraryRoot = new QTreeWidgetItem(m_treeWidget);
    m_libraryRoot->setText(0, i18n("LoreKeeper (Auto-extracted)"));
    m_libraryRoot->setFlags(Qt::ItemIsEnabled);
    m_libraryRoot->setExpanded(true);
    
    m_treeWidget->setColumnWidth(3, 50);
    
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &VariablesPanel::onItemChanged);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &VariablesPanel::onCustomContextMenu);

    layout->addWidget(m_treeWidget);
}

void VariablesPanel::addVariable()
{
    auto *item = new QTreeWidgetItem(m_customVarsRoot);
    item->setText(0, i18n("new_variable"));
    item->setText(1, QStringLiteral("0"));
    item->setText(2, QStringLiteral("0"));
    item->setCheckState(3, Qt::Checked);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    
    m_treeWidget->editItem(item, 0);
    recalculateAll();
}

void VariablesPanel::addVariant()
{
    auto *parent = m_treeWidget->currentItem();
    if (!parent) return;
    
    // If a variant is selected, add to its parent
    if (parent->parent()) {
        parent = parent->parent();
    }

    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, i18n("v1"));
    item->setText(1, QStringLiteral("0"));
    item->setText(2, QStringLiteral("0"));
    item->setCheckState(3, Qt::Checked);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    
    parent->setExpanded(true);
    m_treeWidget->editItem(item, 0);
    recalculateAll();
}

void VariablesPanel::removeVariable()
{
    auto *item = m_treeWidget->currentItem();
    if (item) {
        delete item;
        saveVariables();
        Q_EMIT variablesChanged();
    }
}

void VariablesPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(item);
    if (column == 1) return; // Ignore changes to computed column

    VariableManager::instance().setPanelVariables(variables());
    
    recalculateAll();
    saveVariables();
    Q_EMIT variablesChanged();
}

void VariablesPanel::onCustomContextMenu(const QPoint &pos)
{
    auto *item = m_treeWidget->itemAt(pos);
    QMenu menu(this);

    // Decide which branch the clicked item lives in by walking up to
    // a known root. Library items come from the librarian DB; custom
    // items from user-authored settings. Each tree has its own menu.
    auto isUnder = [](QTreeWidgetItem *node, QTreeWidgetItem *root) {
        for (auto *p = node; p; p = p->parent()) if (p == root) return true;
        return false;
    };

    const bool inLibrary = item && m_libraryRoot && isUnder(item, m_libraryRoot);

    if (inLibrary) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                       i18n("Remove from Library"), this, &VariablesPanel::removeLibraryEntry);
    } else {
        menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                       i18n("Add Main Variable"), this, &VariablesPanel::addVariable);

        if (item) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("entry-new")),
                           i18n("Add Variant"), this, &VariablesPanel::addVariant);
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                           i18n("Remove"), this, &VariablesPanel::removeVariable);
        }
    }

    menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
}

void VariablesPanel::removeLibraryEntry()
{
    auto *item = m_treeWidget->currentItem();
    if (!item || !m_librarianService || !m_libraryRoot) return;

    // If the user clicked on an attribute row, walk up to the entity
    // node (the direct child of a type group, which is itself the
    // direct child of m_libraryRoot). We always delete the whole
    // entity — per-attribute removal is noise given the cleanup goal.
    QTreeWidgetItem *entityItem = item;
    while (entityItem && entityItem->parent()
           && entityItem->parent()->parent() != m_libraryRoot) {
        entityItem = entityItem->parent();
    }
    if (!entityItem) return;

    const qint64 entityId = entityItem->data(0, Qt::UserRole).toLongLong();
    if (entityId <= 0) return;

    if (m_librarianService->database()->deleteEntity(entityId)) {
        refreshLibrary();
        // Trigger a rescan so libraryVariablesChanged re-emits with the
        // removed entry absent — Writing Assistant and preview drop it.
        m_librarianService->scanAll();
    }
}

void VariablesPanel::recalculateAll()
{
    m_treeWidget->blockSignals(true);
    
    // Helper lambda to recurse
    std::function<void(QTreeWidgetItem*)> calcItem = [&](QTreeWidgetItem *item) {
        QString expression = item->text(2);
        bool shouldCalc = item->checkState(3) == Qt::Checked;
        
        QString result = VariableManager::instance().resolve(expression, shouldCalc);
        item->setText(1, result);
        
        for (int i = 0; i < item->childCount(); ++i) {
            calcItem(item->child(i));
        }
    };

    for (int i = 0; i < m_customVarsRoot->childCount(); ++i) {
        calcItem(m_customVarsRoot->child(i));
    }
    
    m_treeWidget->blockSignals(false);
}

void VariablesPanel::saveVariables()
{
    if (!m_customVarsRoot) return;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("Variables"));
    settings.beginGroup(QStringLiteral("CustomVariables"));
    settings.remove(QString());
    
    for (int i = 0; i < m_customVarsRoot->childCount(); ++i) {
        auto *item = m_customVarsRoot->child(i);
        QString name = item->text(0);
        settings.beginGroup(name);
        settings.setValue(QStringLiteral("expression"), item->text(2));
        settings.setValue(QStringLiteral("calculate"), item->checkState(3) == Qt::Checked);
        
        if (item->childCount() > 0) {
            settings.beginGroup(QStringLiteral("variants"));
            for (int j = 0; j < item->childCount(); ++j) {
                auto *variant = item->child(j);
                settings.beginGroup(variant->text(0));
                settings.setValue(QStringLiteral("expression"), variant->text(2));
                settings.setValue(QStringLiteral("calculate"), variant->checkState(3) == Qt::Checked);
                settings.endGroup();
            }
            settings.endGroup();
        }
        settings.endGroup();
    }
    settings.endGroup();
}

void VariablesPanel::loadVariables()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("Variables"));
    settings.beginGroup(QStringLiteral("CustomVariables"));
    QStringList groups = settings.childGroups();
    
    m_treeWidget->blockSignals(true);
    // Only clear children of customVarsRoot if it exists
    if (m_customVarsRoot) qDeleteAll(m_customVarsRoot->takeChildren());

    for (const QString &name : groups) {
        settings.beginGroup(name);
        auto *item = new QTreeWidgetItem(m_customVarsRoot);
        item->setText(0, name);
        item->setText(2, settings.value(QStringLiteral("expression")).toString());
        bool calc = settings.value(QStringLiteral("calculate"), true).toBool();
        item->setCheckState(3, calc ? Qt::Checked : Qt::Unchecked);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        
        if (settings.childGroups().contains(QStringLiteral("variants"))) {
            settings.beginGroup(QStringLiteral("variants"));
            QStringList variants = settings.childGroups();
            for (const QString &vName : variants) {
                settings.beginGroup(vName);
                auto *vItem = new QTreeWidgetItem(item);
                vItem->setText(0, vName);
                vItem->setText(2, settings.value(QStringLiteral("expression")).toString());
                bool vCalc = settings.value(QStringLiteral("calculate"), true).toBool();
                vItem->setCheckState(3, vCalc ? Qt::Checked : Qt::Unchecked);
                vItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
                settings.endGroup();
            }
            settings.endGroup();
        }
        settings.endGroup();
    }
    
    m_treeWidget->blockSignals(false);
    settings.endGroup();
    
    VariableManager::instance().setPanelVariables(variables());
    recalculateAll();
}

QMap<QString, QString> VariablesPanel::variables() const
{
    QMap<QString, QString> vars;
    if (!m_customVarsRoot) return vars;

    for (int i = 0; i < m_customVarsRoot->childCount(); ++i) {
        auto *item = m_customVarsRoot->child(i);
        QString name = item->text(0);
        bool shouldCalc = item->checkState(3) == Qt::Checked;
        vars.insert(name, shouldCalc ? (QStringLiteral("CALC:") + item->text(2)) : item->text(2));
        
        for (int j = 0; j < item->childCount(); ++j) {
            auto *vItem = item->child(j);
            QString vName = name + QLatin1Char('.') + vItem->text(0);
            bool vShouldCalc = vItem->checkState(3) == Qt::Checked;
            vars.insert(vName, vShouldCalc ? (QStringLiteral("CALC:") + item->text(2)) : item->text(2));
        }

    }
    return vars;
}

#include "librarianservice.h"

void VariablesPanel::setLibrarianService(LibrarianService *service)
{
    m_librarianService = service;
    if (!m_librarianService) return;

    // Debounce refreshLibrary. The librarian emits entityUpdated from
    // its worker thread for every single entity write, which during a
    // heuristic table parse can be hundreds of times per file. Each
    // refresh runs a SELECT + per-row getAttributes() — racing against
    // the worker thread's INSERTs on the WAL. We were hitting
    // "disk I/O error Unable to fetch row" when SQLite's WAL got
    // checkpoint-truncated mid-cursor.
    //
    // Coalesce the rapid-fire entityUpdated signals into one refresh
    // every 750ms. scanningFinished still triggers an immediate final
    // refresh so the tree shows the complete result the moment the
    // scan ends.
    auto *refreshDebounce = new QTimer(this);
    refreshDebounce->setSingleShot(true);
    refreshDebounce->setInterval(750);
    connect(refreshDebounce, &QTimer::timeout, this, &VariablesPanel::refreshLibrary);

    connect(m_librarianService, &LibrarianService::entityUpdated, this,
            [refreshDebounce](qint64) {
        if (!refreshDebounce->isActive()) refreshDebounce->start();
        // If already running, the existing 750ms window catches this
        // update — no need to reset (resetting would starve the user
        // of progress feedback during a long scan).
    });
    connect(m_librarianService, &LibrarianService::scanningFinished,
            this, [this, refreshDebounce]() {
        refreshDebounce->stop();
        refreshLibrary();
    });

    refreshLibrary();
}

void VariablesPanel::onLoreScanStarted(const QString &filePath)
{
    m_loreScanButton->setEnabled(false);
    m_loreScanProgress->show();
}

void VariablesPanel::onLoreScanFinished(const QString &filePath)
{
    m_loreScanButton->setEnabled(true);
    m_loreScanProgress->hide();
}

void VariablesPanel::triggerReindex()
{
    RPGFORGE_DLOG("VARS") << "VariablesPanel::triggerReindex clicked";
    if (m_librarianService) {
        m_librarianService->scanAll();
    } else {
        RPGFORGE_DLOG("VARS") << "  no librarianService wired — re-index cannot run";
    }
}

void VariablesPanel::refreshLibrary()
{
    if (!m_librarianService || !m_libraryRoot || !m_librarianService->database()->database().isOpen()) return;

    m_treeWidget->blockSignals(true);
    qDeleteAll(m_libraryRoot->takeChildren());

    // Use the main connection from the service
    QSqlDatabase db = m_librarianService->database()->database();
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT id, name, type FROM entities"))) {
        qWarning() << "Librarian Refresh failed:" << query.lastError().text();
        m_treeWidget->blockSignals(false);
        return;
    }

    QMap<QString, QTreeWidgetItem*> typeGroups;

    while (query.next()) {
        qint64 id = query.value(0).toLongLong();
        QString name = query.value(1).toString();
        QString type = query.value(2).toString();

        if (type.isEmpty()) type = i18n("General");

        if (!typeGroups.contains(type)) {
            auto *group = new QTreeWidgetItem(m_libraryRoot);
            group->setText(0, type);
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
            typeGroups.insert(type, group);
        }

        auto *entityItem = new QTreeWidgetItem(typeGroups[type]);
        entityItem->setText(0, name);
        // Library entities are selectable (so right-click can remove
        // them) but not currently inline-editable — a rename would be
        // overwritten by the next scan unless we persist overrides
        // separately. Use the right-click "Remove from Library" action
        // to purge bad entries.
        entityItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        entityItem->setData(0, Qt::UserRole, id);

        // Fetch attributes
        QVariantMap attrs = m_librarianService->database()->getAttributes(id);
        for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it) {
            auto *attrItem = new QTreeWidgetItem(entityItem);
            attrItem->setText(0, it.key());
            attrItem->setText(1, it.value().toString());
            attrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            attrItem->setData(0, Qt::UserRole, id);
        }
    }

    m_treeWidget->blockSignals(false);

    int totalEntities = 0;
    int totalAttrs = 0;
    for (int g = 0; g < m_libraryRoot->childCount(); ++g) {
        auto *group = m_libraryRoot->child(g);
        totalEntities += group->childCount();
        for (int e = 0; e < group->childCount(); ++e) {
            totalAttrs += group->child(e)->childCount();
        }
    }
    RPGFORGE_DLOG("VARS") << "VariablesPanel::refreshLibrary:"
                          << m_libraryRoot->childCount() << "type group(s),"
                          << totalEntities << "entities,"
                          << totalAttrs << "attributes total";
}
