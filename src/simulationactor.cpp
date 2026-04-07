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

#include "simulationactor.h"
#include "llmservice.h"
#include <QJsonDocument>
#include <QSettings>

SimulationActor::SimulationActor(const QString &name, QObject *parent)
    : QObject(parent), m_name(name)
{
}

void SimulationActor::think(const QJsonObject &worldState)
{
    Q_EMIT thinkingStarted();

    QString systemPrompt = QStringLiteral(
        "You are an autonomous agent in a tabletop RPG simulation.\n"
        "Name: %1\n"
        "Motive: %2\n"
        "Your Character Sheet (JSON): %3\n\n"
        "Current World State (JSON): %4\n\n"
        "TASK:\n"
        "Decide on your next action based on your motive and current situation.\n"
        "Output ONLY a valid JSON object with the following fields:\n"
        "- \"action\": A short string describing the action (e.g. \"attack\", \"heal\", \"hide\").\n"
        "- \"target\": The name of the target or object (if applicable).\n"
        "- \"description\": A one-sentence description of what you are doing in character.\n"
        "- \"reasoning\": A brief internal monologue explaining your choice.\n"
        "Do not include any other text."
    ).arg(m_name, m_motive, 
          QString::fromUtf8(QJsonDocument(m_sheet).toJson(QJsonDocument::Compact)),
          QString::fromUtf8(QJsonDocument(worldState).toJson(QJsonDocument::Compact)));

    LLMRequest req;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    
    // Actors use the default chat provider/model for now
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    req.model = (req.provider == LLMProvider::OpenAI) 
        ? settings.value(QStringLiteral("llm/openai/model")).toString()
        : (req.provider == LLMProvider::Anthropic)
            ? settings.value(QStringLiteral("llm/anthropic/model")).toString()
            : settings.value(QStringLiteral("llm/ollama/model")).toString();
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), QStringLiteral("What is your next move?")});
    req.stream = false;

    LLMService::instance().sendNonStreamingRequest(req, [this](const QString &response) {
        if (response.isEmpty()) {
            Q_EMIT errorOccurred(QStringLiteral("Empty response from LLM actor %1").arg(m_name));
            return;
        }

        // Try to parse JSON from response
        QString cleanJson = response.trimmed();
        if (cleanJson.startsWith(QLatin1String("```json"))) {
            cleanJson = cleanJson.mid(7);
            if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
        } else if (cleanJson.startsWith(QLatin1String("```"))) {
            cleanJson = cleanJson.mid(3);
            if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(cleanJson.trimmed().toUtf8(), &error);
        if (doc.isNull() || !doc.isObject()) {
            Q_EMIT errorOccurred(QStringLiteral("Failed to parse intent for %1: %2").arg(m_name, error.errorString()));
            return;
        }

        m_lastIntent = doc.object();
        Q_EMIT intentDecided(m_lastIntent);
    });
}
