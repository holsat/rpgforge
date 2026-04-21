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

#ifndef SIMULATIONACTOR_H
#define SIMULATIONACTOR_H

#include <QObject>
#include <QString>
#include <QJsonObject>

/**
 * @brief Represents an autonomous agent (PC or NPC) in the simulation.
 */
class SimulationActor : public QObject {
    Q_OBJECT

public:
    explicit SimulationActor(const QString &name, QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    QString motive() const { return m_motive; }
    void setMotive(const QString &motive) { m_motive = motive; }

    QJsonObject sheet() const { return m_sheet; }
    void setSheet(const QJsonObject &sheet) { m_sheet = sheet; }

    QJsonObject lastIntent() const { return m_lastIntent; }

    QStringList memory() const { return m_memory; }
    QStringList plan() const { return m_plan; }

    /**
     * @brief Adds a specific observation to the actor's memory.
     */
    void addMemory(const QString &observation);

    /**
     * @brief Asks the actor to decide on their next intent based on the world state and recent actions.
     */
    virtual void think(const QJsonObject &worldState, const QJsonArray &recentIntents, int tacticalAggression);

Q_SIGNALS:
    /**
     * @brief Emitted when the actor has decided on an action.
     * @param intent A JSON object describing the intended action.
     */
    void intentDecided(const QJsonObject &intent);
    void thinkingStarted();
    void errorOccurred(const QString &message);

private:
    QString m_name;
    QString m_motive;
    QJsonObject m_sheet;
    QJsonObject m_lastIntent;
    QStringList m_memory;
    QStringList m_plan;
};

#endif // SIMULATIONACTOR_H
