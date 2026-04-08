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

#include "simgriot.h"
#include "llmservice.h"
#include <QJsonDocument>
#include <QSettings>

SimulationGriot::SimulationGriot(QObject *parent)
    : QObject(parent)
{
}

void SimulationGriot::narrate(const QString &actorName, const QJsonObject &intent, const QString &arbiterLog, const QJsonObject &patch, const QJsonObject &worldState)
{
    Q_EMIT narrationStarted();

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString systemPrompt = settings.value(QStringLiteral("simulation/griot_prompt"),
        QStringLiteral("You are the Griot, an immersive storyteller for an RPG simulation.\n"
                       "Your task is to take dry mechanical results and turn them into cinematic prose.\n\n"
                       "INPUTS:\n"
                       "- Actor: %1\n"
                       "- Intent: %2\n"
                       "- Rules Applied: %3\n"
                       "- State Changes: %4\n"
                       "- World Context: %5\n\n"
                       "TASK:\n"
                       "Write 1-3 sentences of atmospheric narration describing what happens. "
                       "Be strictly accurate to the rules applied (e.g. if it was a near miss, describe it as such). "
                       "Do not include any mechanical terms like 'HP' or 'd20' in your prose.")).toString()
        .arg(actorName, 
          QString::fromUtf8(QJsonDocument(intent).toJson(QJsonDocument::Compact)),
          arbiterLog,
          QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact)),
          QString::fromUtf8(QJsonDocument(worldState).toJson(QJsonDocument::Compact)));

    LLMRequest req;
    
    // Griot uses the creative model (default chat)
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    req.model = (req.provider == LLMProvider::OpenAI) 
        ? settings.value(QStringLiteral("llm/openai/model")).toString()
        : (req.provider == LLMProvider::Anthropic)
            ? settings.value(QStringLiteral("llm/anthropic/model")).toString()
            : settings.value(QStringLiteral("llm/ollama/model")).toString();
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), QStringLiteral("Narrate the event.")});
    req.stream = false;
    req.temperature = 0.8; // High creativity

    LLMService::instance().sendNonStreamingRequest(req, [this](const QString &response) {
        if (response.isEmpty()) {
            Q_EMIT errorOccurred(QStringLiteral("Empty response from Griot."));
            return;
        }
        Q_EMIT narrativeDecided(response.trimmed());
    });
}
