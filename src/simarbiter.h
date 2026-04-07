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

#ifndef SIMARBITER_H
#define SIMARBITER_H

#include <QObject>
#include <QJsonObject>
#include "simulationstate.h"

/**
 * @brief The rule-enforcement agent that validates intents and updates the world state.
 */
class SimulationArbiter : public QObject {
    Q_OBJECT

public:
    explicit SimulationArbiter(QObject *parent = nullptr);

    /**
     * @brief Processes an actor's intent and generates a state change.
     * Uses RAG to find rules and DiceEngine for rolls.
     */
    void processIntent(const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState);

Q_SIGNALS:
    /**
     * @brief Emitted when the arbiter has decided on the outcome.
     * @param patch A JSON object describing the changes (e.g. {"actors.target.hp": 15}).
     * @param log A dry, rules-based log of what happened (e.g. "Rolled 18 vs AC 15. Hit for 5 dmg.").
     */
    void outcomeDecided(const QJsonObject &patch, const QString &log);
    void processingStarted();
    void errorOccurred(const QString &message);

private:
    void performRuleLookup(const QString &query, const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState);
    void finalizeOutcome(const QString &rulesContext, const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState);
};

#endif // SIMARBITER_H
