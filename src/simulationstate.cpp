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

#include "simulationstate.h"
#include <QStringList>

SimulationState::SimulationState(QObject *parent)
    : QObject(parent)
{
}

void SimulationState::setValue(const QString &path, const QJsonValue &value)
{
    QStringList keys = path.split(QLatin1Char('.'));
    if (keys.isEmpty()) return;

    // Helper lambda for deep update
    std::function<QJsonObject(QJsonObject, QStringList, const QJsonValue&)> update;
    update = [&](QJsonObject current, QStringList pathKeys, const QJsonValue &val) -> QJsonObject {
        QString key = pathKeys.takeFirst();
        if (pathKeys.isEmpty()) {
            current.insert(key, val);
        } else {
            QJsonObject nextObj = current.value(key).toObject();
            current.insert(key, update(nextObj, pathKeys, val));
        }
        return current;
    };

    m_state = update(m_state, keys, value);
    Q_EMIT stateChanged();
}

QJsonValue SimulationState::value(const QString &path) const
{
    QStringList keys = path.split(QLatin1Char('.'));
    QJsonValue current = m_state;

    for (const QString &key : keys) {
        if (!current.isObject()) return QJsonValue::Undefined;
        current = current.toObject().value(key);
        if (current.isUndefined()) return QJsonValue::Undefined;
    }

    return current;
}

void SimulationState::clear()
{
    m_state = QJsonObject();
    Q_EMIT stateChanged();
}
