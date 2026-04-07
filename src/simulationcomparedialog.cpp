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

#include "simulationcomparedialog.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

SimulationCompareDialog::SimulationCompareDialog(const QJsonObject &stateA, const QJsonObject &stateB, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Compare Simulation Results"));
    setupUi();
    compare(stateA, stateB);
}

SimulationCompareDialog::~SimulationCompareDialog() = default;

void SimulationCompareDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({i18n("Path"), i18n("Result A"), i18n("Result B")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    
    layout->addWidget(m_table);
    resize(800, 600);
}

void SimulationCompareDialog::compare(const QJsonObject &objA, const QJsonObject &objB, const QString &parentPath)
{
    QStringList keysA = objA.keys();
    QStringList keysB = objB.keys();
    QSet<QString> allKeys(keysA.begin(), keysA.end());
    allKeys.unite(QSet<QString>(keysB.begin(), keysB.end()));

    for (const QString &key : allKeys) {
        QString currentPath = parentPath.isEmpty() ? key : parentPath + QStringLiteral(".") + key;
        QJsonValue valA = objA.value(key);
        QJsonValue valB = objB.value(key);

        if (valA.isObject() || valB.isObject()) {
            compare(valA.toObject(), valB.toObject(), currentPath);
        } else if (valA != valB) {
            QString strA = valA.isUndefined() ? i18n("(Missing)") : QString::fromUtf8(QJsonDocument(QJsonArray{valA}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
            QString strB = valB.isUndefined() ? i18n("(Missing)") : QString::fromUtf8(QJsonDocument(QJsonArray{valB}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
            addRow(currentPath, strA, strB);
        }
    }
}

void SimulationCompareDialog::addRow(const QString &path, const QString &valA, const QString &valB)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *pathItem = new QTableWidgetItem(path);
    auto *itemA = new QTableWidgetItem(valA);
    auto *itemB = new QTableWidgetItem(valB);

    // Color code
    if (valA == i18n("(Missing)")) {
        itemB->setBackground(Qt::green); // Added in B
    } else if (valB == i18n("(Missing)")) {
        itemA->setBackground(Qt::red); // Removed in B
    } else {
        itemA->setBackground(Qt::yellow);
        itemB->setBackground(Qt::yellow); // Changed
    }

    m_table->setItem(row, 0, pathItem);
    m_table->setItem(row, 1, itemA);
    m_table->setItem(row, 2, itemB);
}
