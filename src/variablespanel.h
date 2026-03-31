#ifndef VARIABLESPANEL_H
#define VARIABLESPANEL_H

#include <QWidget>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

struct Variable {
    QString name;
    QString value;
    QString description;
};

class VariablesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablesPanel(QWidget *parent = nullptr);
    ~VariablesPanel() override = default;

    // Get all defined variables
    QMap<QString, QString> variables() const;

Q_SIGNALS:
    void variablesChanged();

private Q_SLOTS:
    void addVariable();
    void removeVariable();
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_treeWidget;
    QToolButton *m_addButton;
    QToolButton *m_removeButton;

    void setupUi();
    void recalculateAll();
    void saveVariables();
    void loadVariables();
};

#endif // VARIABLESPANEL_H
