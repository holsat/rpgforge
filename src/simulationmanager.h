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

#ifndef SIMULATIONMANAGER_H
#define SIMULATIONMANAGER_H

#include <QObject>
#include <QList>
#include <QJsonArray>
#include "simulationstate.h"
#include "simulationactor.h"
#include "simarbiter.h"
#include "simgriot.h"

struct RunResult {
    int turns = 0;
    QJsonObject finalState;
    QStringList narrative;
};

struct BatchResult {
    int totalRuns = 0;
    QList<RunResult> runs;
};

/**
 * @brief Orchestrates simulation runs, turn order, and agent communication.
 */
class SimulationManager : public QObject {
    Q_OBJECT

public:
    static SimulationManager& instance();

    void addActor(SimulationActor *actor);
    void removeActor(SimulationActor *actor);
    void clearActors();

    SimulationState* state() { return &m_state; }

    void setScenario(const QString &text) { m_scenario = text; }
    QString scenario() const { return m_scenario; }

    void setBatchMode(bool enabled) { m_isBatchMode = enabled; }
    void setMaxTurns(int turns) { m_maxTurns = turns; }
    void setRunCount(int count) { m_runCount = count; }
    void setTacticalAggression(int level) { m_tacticalAggression = level; }
    int tacticalAggression() const { return m_tacticalAggression; }

    /**
     * @brief Starts the simulation loop.
     */
    void start();

    /**
     * @brief Stops the simulation loop.
     */
    void stop();

    /**
     * @brief Ticks the simulation forward by one turn.
     */
    void nextTurn();

Q_SIGNALS:
    void simulationStarted();
    void simulationStopped();
    void batchFinished(const BatchResult &results);
    void turnStarted(const QString &actorName);
    void turnFinished(const QString &actorName, const QJsonObject &intent);
    void narrativeGenerated(const QString &prose);
    void logMessage(const QString &message);

private:
    explicit SimulationManager(QObject *parent = nullptr);
    ~SimulationManager() override;

    SimulationManager(const SimulationManager&) = delete;
    SimulationManager& operator=(const SimulationManager&) = delete;

    void processActorIntent(SimulationActor *actor, const QJsonObject &intent);
    void applyOutcome(const QJsonObject &patch, const QString &log);
    void finalizeTurn(const QString &prose);
    void checkSimulationEnd();

    SimulationState m_state;
    SimulationArbiter m_arbiter;
    SimulationGriot m_griot;
    QList<SimulationActor*> m_actors;
    QJsonArray m_recentIntents;
    QString m_scenario;
    int m_currentActorIndex = -1;
    bool m_isRunning = false;

    // Batch settings
    bool m_isBatchMode = false;
    int m_maxTurns = 50;
    int m_runCount = 1;
    int m_tacticalAggression = 3; // 1-5
    int m_currentRunIndex = 0;
    int m_turnCounter = 0;
    
    BatchResult m_currentBatch;
    RunResult m_currentRun;
};

#endif // SIMULATIONMANAGER_H
