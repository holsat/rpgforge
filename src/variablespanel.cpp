#include "variablespanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QToolButton>
#include <QIcon>
#include <KLocalizedString>

VariablesPanel::VariablesPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void VariablesPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar for Add/Remove
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
    
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &VariablesPanel::onItemChanged);

    layout->addWidget(m_treeWidget);

    // Add some default examples
    auto *item = new QTreeWidgetItem(m_treeWidget);
    item->setText(0, QStringLiteral("hp_base"));
    item->setText(1, QStringLiteral("10"));
    item->setText(2, QStringLiteral("10"));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

void VariablesPanel::addVariable()
{
    auto *item = new QTreeWidgetItem(m_treeWidget);
    item->setText(0, i18n("new_variable"));
    item->setText(1, QStringLiteral("0"));
    item->setText(2, QStringLiteral("0"));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    
    m_treeWidget->editItem(item, 0);
    Q_EMIT variablesChanged();
}

void VariablesPanel::removeVariable()
{
    auto *item = m_treeWidget->currentItem();
    if (item) {
        delete item;
        Q_EMIT variablesChanged();
    }
}

void VariablesPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(item);
    Q_UNUSED(column);
    // When Name or Expression changes, we'll need to re-evaluate
    // For now, just emit the signal
    Q_EMIT variablesChanged();
}

QMap<QString, QString> VariablesPanel::variables() const
{
    QMap<QString, QString> vars;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto *item = m_treeWidget->topLevelItem(i);
        vars.insert(item->text(0), item->text(2)); // Name -> Expression
    }
    return vars;
}
