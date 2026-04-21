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

#include "simulationactor.h"
#include "llmservice.h"
#include "textutils.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QPointer>

SimulationActor::SimulationActor(const QString &name, QObject *parent)
    : QObject(parent), m_name(name)
{
}

void SimulationActor::addMemory(const QString &observation)
{
    m_memory.append(observation);
    if (m_memory.size() > 20) {
        m_memory.removeFirst();
    }
}

void SimulationActor::think(const QJsonObject &worldState, const QJsonArray &recentIntents, int tacticalAggression)
{
    Q_EMIT thinkingStarted();

    QString aggressionNote;
    switch (tacticalAggression) {
        case 1: aggressionNote = QStringLiteral("Focus on narrative and character flavor. Don't worry about winning; focus on what makes a good story."); break;
        case 2: aggressionNote = QStringLiteral("Be reasonable, but favor cinematic choices over optimal ones."); break;
        case 3: aggressionNote = QStringLiteral("Balance story and strategy. Play the game fairly and effectively."); break;
        case 4: aggressionNote = QStringLiteral("Be highly tactical. Use your abilities to their full potential to win."); break;
        case 5: aggressionNote = QStringLiteral("MUNCHKIN MODE: Find every loophole, maximize every bonus, and use the most exploitative tactics possible to ensure total victory."); break;
        default: aggressionNote = QStringLiteral("Play standard strategy."); break;
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString systemPrompt = settings.value(QStringLiteral("simulation/actor_prompt"),
        QStringLiteral("You are an autonomous agent in a tabletop RPG simulation.\n"
                       "Name: %1\n"
                       "Motive: %2\n"
                       "Your Character Sheet (JSON): %3\n\n"
                       "TACTICAL AGGRESSION (Level %4): %5\n\n"
                       "--- YOUR MEMORY (Last few turns) ---\n"
                       "%6\n\n"
                       "--- YOUR CURRENT PLAN ---\n"
                       "%7\n\n"
                       "--- RECENT ACTIONS BY OTHERS ---\n"
                       "%8\n\n"
                       "Current World State (JSON): %9\n\n"
                       "TASK:\n"
                       "1. Observe the world state, your memories, and what others just did.\n"
                       "2. Evaluate if your current plan is still valid given your TACTICAL AGGRESSION level.\n"
                       "3. Decide on your next action and update your plan.\n\n"
                       "Output ONLY a valid JSON object with the following fields:\n"
                       "- \"action\": A short string describing the action.\n"
                       "- \"target\": The name of the target (if applicable).\n"
                       "- \"description\": A one-sentence description of what you are doing in character.\n"
                       "- \"reasoning\": A brief internal monologue.\n"
                       "- \"current_plan\": A list of 1-3 strings describing your immediate next steps.\n"
                       "Do not include any other text.")).toString()
        .arg(m_name, m_motive, 
          QString::fromUtf8(QJsonDocument(m_sheet).toJson(QJsonDocument::Compact)),
          QString::number(tacticalAggression), aggressionNote,
          m_memory.isEmpty() ? QStringLiteral("No memories yet.") : m_memory.join(QLatin1String("\n")),
          m_plan.isEmpty() ? QStringLiteral("No plan yet.") : m_plan.join(QLatin1String(" -> ")),
          QString::fromUtf8(QJsonDocument(recentIntents).toJson(QJsonDocument::Compact)),
          QString::fromUtf8(QJsonDocument(worldState).toJson(QJsonDocument::Compact)));

    LLMRequest req;
    req.serviceName = i18n("Simulation Actor");
    req.settingsKey = QStringLiteral("simulation/sim_actor_model");

    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("simulation/sim_actor_provider"), 
                                                           settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    req.model = settings.value(QStringLiteral("simulation/sim_actor_model")).toString();
    // Leave req.model empty if setting is empty so LLMService handles default
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), QStringLiteral("What is your next move?")});
    req.stream = false;

    QPointer<SimulationActor> weakThis(this);
    LLMService::instance().sendNonStreamingRequestDetailed(req, [weakThis](const QString &response, const QString &llmError) {
        if (!weakThis) return;

        if (!llmError.isEmpty()) {
            Q_EMIT weakThis->errorOccurred(QStringLiteral("Actor %1: %2").arg(weakThis->m_name, llmError));
            return;
        }
        if (response.isEmpty()) {
            Q_EMIT weakThis->errorOccurred(QStringLiteral("Empty response from LLM actor %1").arg(weakThis->m_name));
            return;
        }

        QString cleanJson = stripMarkdownFences(response);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(cleanJson.trimmed().toUtf8(), &error);
        if (doc.isNull() || !doc.isObject()) {
            Q_EMIT weakThis->errorOccurred(QStringLiteral("Failed to parse intent for %1: %2").arg(weakThis->m_name, error.errorString()));
            return;
        }

        weakThis->m_lastIntent = doc.object();
        
        // Update the plan from the LLM's decision
        weakThis->m_plan.clear();
        QJsonArray planArray = weakThis->m_lastIntent.value(QStringLiteral("current_plan")).toArray();
        for (const QJsonValue &v : planArray) {
            weakThis->m_plan.append(v.toString());
        }

        Q_EMIT weakThis->intentDecided(weakThis->m_lastIntent);
    });
}
