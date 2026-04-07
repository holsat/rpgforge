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

#include "simulationmanager.h"
#include <QDebug>
#include <QTimer>

SimulationManager& SimulationManager::instance()
{
    static SimulationManager s_instance;
    return s_instance;
}

SimulationManager::SimulationManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_arbiter, &SimulationArbiter::outcomeDecided, this, &SimulationManager::applyOutcome);
    connect(&m_arbiter, &SimulationArbiter::errorOccurred, this, [this](const QString &msg) {
        Q_EMIT logMessage(QStringLiteral("Arbiter Error: %1").arg(msg));
        stop();
    });

    connect(&m_griot, &SimulationGriot::narrativeDecided, this, &SimulationManager::finalizeTurn);
    connect(&m_griot, &SimulationGriot::errorOccurred, this, [this](const QString &msg) {
        Q_EMIT logMessage(QStringLiteral("Griot Error: %1").arg(msg));
        stop();
    });
}

SimulationManager::~SimulationManager() = default;

void SimulationManager::addActor(SimulationActor *actor)
{
    if (!m_actors.contains(actor)) {
        m_actors.append(actor);
        connect(actor, &SimulationActor::intentDecided, this, [this, actor](const QJsonObject &intent) {
            processActorIntent(actor, intent);
        });
        connect(actor, &SimulationActor::errorOccurred, this, [this](const QString &msg) {
            Q_EMIT logMessage(QStringLiteral("Actor Error: %1").arg(msg));
            stop();
        });
    }
}

void SimulationManager::removeActor(SimulationActor *actor)
{
    m_actors.removeAll(actor);
}

void SimulationManager::clearActors()
{
    m_actors.clear();
    m_currentActorIndex = -1;
}

void SimulationManager::start()
{
    if (m_isRunning || m_actors.isEmpty()) return;
    
    m_isRunning = true;
    m_currentActorIndex = -1;
    m_currentRunIndex = 0;
    m_turnCounter = 0;
    m_currentBatch = BatchResult();
    m_currentBatch.totalRuns = m_runCount;
    
    Q_EMIT simulationStarted();
    Q_EMIT logMessage(QStringLiteral("Simulation started. Batch Mode: %1").arg(m_isBatchMode ? QStringLiteral("Yes") : QStringLiteral("No")));
    
    nextTurn();
}

void SimulationManager::stop()
{
    m_isRunning = false;
    Q_EMIT simulationStopped();
    Q_EMIT logMessage(QStringLiteral("Simulation stopped."));
}

void SimulationManager::nextTurn()
{
    if (!m_isRunning || m_actors.isEmpty()) return;

    m_turnCounter++;
    if (m_turnCounter > m_maxTurns) {
        checkSimulationEnd();
        return;
    }

    m_currentActorIndex = (m_currentActorIndex + 1) % m_actors.size();
    SimulationActor *current = m_actors.at(m_currentActorIndex);

    Q_EMIT turnStarted(current->name());
    Q_EMIT logMessage(QStringLiteral("--- Turn %1: %2 ---").arg(QString::number(m_turnCounter), current->name()));
    
    current->think(m_state.state());
}

void SimulationManager::processActorIntent(SimulationActor *actor, const QJsonObject &intent)
{
    Q_EMIT turnFinished(actor->name(), intent);
    
    QString desc = intent.value(QStringLiteral("description")).toString();
    QString reasoning = intent.value(QStringLiteral("reasoning")).toString();
    
    Q_EMIT logMessage(QStringLiteral("%1 decides to: %2").arg(actor->name(), desc));
    
    if (!m_isBatchMode) {
        Q_EMIT logMessage(QStringLiteral("(Reasoning: %1)").arg(reasoning));
    }

    // Arbiter processes the intent
    m_arbiter.processIntent(actor->name(), actor->sheet(), intent, m_state.state());
}

void SimulationManager::applyOutcome(const QJsonObject &patch, const QString &log)
{
    if (!m_isBatchMode) {
        Q_EMIT logMessage(QStringLiteral("Arbiter: %1").arg(log));
    }

    // Apply JSON patch to state
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        m_state.setValue(it.key(), it.value());
    }

    if (m_isBatchMode) {
        // In batch mode, we skip the Griot to save time/tokens
        checkSimulationEnd();
    } else {
        // Step 4: Let the Griot narrate the result
        SimulationActor *actor = m_actors.at(m_currentActorIndex);
        m_griot.narrate(actor->name(), actor->lastIntent(), log, patch, m_state.state());
    }
}

void SimulationManager::finalizeTurn(const QString &prose)
{
    Q_EMIT narrativeGenerated(prose);
    Q_EMIT logMessage(QStringLiteral("Griot: %1").arg(prose));
    m_currentRun.narrative.append(prose);

    // Auto-continue if running
    if (m_isRunning) {
        QTimer::singleShot(1000, this, &SimulationManager::nextTurn);
    }
}

void SimulationManager::checkSimulationEnd()
{
    // Check if current run is done
    if (m_turnCounter > m_maxTurns) {
        m_currentRun.turns = m_turnCounter - 1;
        m_currentRun.finalState = m_state.state();
        m_currentBatch.runs.append(m_currentRun);
        m_currentRun = RunResult();

        m_currentRunIndex++;
        if (m_currentRunIndex < m_runCount) {
            Q_EMIT logMessage(QStringLiteral("Run %1 finished. Starting next run...").arg(QString::number(m_currentRunIndex)));
            m_state.clear();
            m_turnCounter = 0;
            m_currentActorIndex = -1;
            nextTurn();
        } else {
            Q_EMIT logMessage(QStringLiteral("Batch finished. Total runs: %1").arg(QString::number(m_runCount)));
            m_isRunning = false;
            Q_EMIT batchFinished(m_currentBatch);
            stop();
        }
    } else if (m_isBatchMode) {
        // In batch mode, continue immediately
        nextTurn();
    }
}
