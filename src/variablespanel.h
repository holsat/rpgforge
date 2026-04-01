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
    void addVariant();
    void removeVariable();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onCustomContextMenu(const QPoint &pos);

private:
    QTreeWidget *m_treeWidget;
    QToolButton *m_addButton;
    QToolButton *m_addVariantButton;
    QToolButton *m_removeButton;

    void setupUi();
    void recalculateAll();
    void saveVariables();
    void loadVariables();
};

#endif // VARIABLESPANEL_H
