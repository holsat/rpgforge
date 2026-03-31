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
    m_addButton->setToolTip(i18n("Add Variable"));
    connect(m_addButton, &QToolButton::clicked, this, &VariablesPanel::addVariable);
    
    m_removeButton = new QToolButton(toolbar);
    m_removeButton->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    m_removeButton->setToolTip(i18n("Remove Selected Variable"));
    connect(m_removeButton, &QToolButton::clicked, this, &VariablesPanel::removeVariable);

    toolbarLayout->addWidget(m_addButton);
    toolbarLayout->addWidget(m_removeButton);
    toolbarLayout->addStretch();
    
    layout->addWidget(toolbar);

    // Tree widget
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setColumnCount(3);
    m_treeWidget->setHeaderLabels({i18n("Name"), i18n("Computed"), i18n("Expression")});
    m_treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_treeWidget->setRootIsDecorated(false);
    
    // Column 1 (Computed) should not be directly editable by the user
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &VariablesPanel::onItemChanged);

    layout->addWidget(m_treeWidget);
}

void VariablesPanel::addVariable()
{
    auto *item = new QTreeWidgetItem(m_treeWidget);
    item->setText(0, i18n("new_variable"));
    item->setText(1, QStringLiteral("0"));
    item->setText(2, QStringLiteral("0"));
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    
    // Disable editing for the "Computed" column
    // We handle this by checking column in onItemChanged or via delegates, 
    // but a simple way is to just revert changes to col 1.
    
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
    if (column == 1) return; // Ignore changes to computed column to avoid loops

    // Sync variables to manager so recalculateAll has latest data
    VariableManager::instance().setPanelVariables(variables());
    
    recalculateAll();
    saveVariables();
    Q_EMIT variablesChanged();
}

void VariablesPanel::recalculateAll()
{
    m_treeWidget->blockSignals(true);
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto *item = m_treeWidget->topLevelItem(i);
        QString expression = item->text(2);
        QString result = VariableManager::instance().resolve(expression);
        item->setText(1, result);
    }
    m_treeWidget->blockSignals(false);
}

void VariablesPanel::saveVariables()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("Variables"));
    settings.beginGroup(QStringLiteral("CustomVariables"));
    settings.remove(QString()); // Clear old
    
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto *item = m_treeWidget->topLevelItem(i);
        settings.setValue(item->text(0), item->text(2));
    }
    settings.endGroup();
}

void VariablesPanel::loadVariables()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("Variables"));
    settings.beginGroup(QStringLiteral("CustomVariables"));
    QStringList keys = settings.allKeys();
    
    m_treeWidget->blockSignals(true);
    if (keys.isEmpty()) {
        // Default example
        auto *item = new QTreeWidgetItem(m_treeWidget);
        item->setText(0, QStringLiteral("hp_base"));
        item->setText(1, QStringLiteral("10"));
        item->setText(2, QStringLiteral("10"));
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    } else {
        for (const QString &key : keys) {
            auto *item = new QTreeWidgetItem(m_treeWidget);
            item->setText(0, key);
            item->setText(2, settings.value(key).toString());
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        }
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
        vars.insert(item->text(0), item->text(2));
    }
    return vars;
}
