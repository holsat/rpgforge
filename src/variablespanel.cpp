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

#include "variablespanel.h"
#include "variablemanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolButton>
#include <QIcon>
#include <QSettings>
#include <KLocalizedString>

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

    toolbarLayout->addWidget(m_addButton);
    toolbarLayout->addWidget(m_addVariantButton);
    toolbarLayout->addWidget(m_removeButton);
    toolbarLayout->addStretch();
    
    layout->addWidget(toolbar);

    // Tree widget
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setColumnCount(4);
    m_treeWidget->setHeaderLabels({i18n("Name"), i18n("Computed"), i18n("Expression"), i18n("Calc")});
    m_treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_treeWidget->setRootIsDecorated(true);
    
    m_treeWidget->setColumnWidth(3, 50);
    
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &VariablesPanel::onItemChanged);

    layout->addWidget(m_treeWidget);
}

void VariablesPanel::addVariable()
{
    auto *item = new QTreeWidgetItem(m_treeWidget);
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

    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        calcItem(m_treeWidget->topLevelItem(i));
    }
    
    m_treeWidget->blockSignals(false);
}

void VariablesPanel::saveVariables()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("Variables"));
    settings.beginGroup(QStringLiteral("CustomVariables"));
    settings.remove(QString());
    
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto *item = m_treeWidget->topLevelItem(i);
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
    m_treeWidget->clear();

    for (const QString &name : groups) {
        settings.beginGroup(name);
        auto *item = new QTreeWidgetItem(m_treeWidget);
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
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto *item = m_treeWidget->topLevelItem(i);
        QString name = item->text(0);
        bool shouldCalc = item->checkState(3) == Qt::Checked;
        vars.insert(name, shouldCalc ? (QStringLiteral("CALC:") + item->text(2)) : item->text(2));
        
        for (int j = 0; j < item->childCount(); ++j) {
            auto *vItem = item->child(j);
            QString vName = name + QLatin1Char('.') + vItem->text(0);
            bool vShouldCalc = vItem->checkState(3) == Qt::Checked;
            vars.insert(vName, vShouldCalc ? (QStringLiteral("CALC:") + vItem->text(2)) : vItem->text(2));
        }
    }
    return vars;
}
