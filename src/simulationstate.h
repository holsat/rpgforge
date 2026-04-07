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

#ifndef SIMULATIONSTATE_H
#define SIMULATIONSTATE_H

#include <QObject>
#include <QJsonObject>
#include <QJsonValue>

/**
 * @brief Manages the dynamic, schema-less world state for a simulation.
 */
class SimulationState : public QObject {
    Q_OBJECT

public:
    explicit SimulationState(QObject *parent = nullptr);

    /**
     * @brief Sets a value at a specific dot-separated path (e.g. "actors.fighter.hp").
     */
    void setValue(const QString &path, const QJsonValue &value);

    /**
     * @brief Gets a value from a path. Returns QJsonValue::Undefined if not found.
     */
    QJsonValue value(const QString &path) const;

    /**
     * @brief Returns the entire state as a JSON object.
     */
    QJsonObject state() const { return m_state; }

    /**
     * @brief Resets the state.
     */
    void clear();

Q_SIGNALS:
    void stateChanged();

private:
    QJsonObject m_state;
};

#endif // SIMULATIONSTATE_H
