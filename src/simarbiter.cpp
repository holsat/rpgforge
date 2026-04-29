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

#include "simarbiter.h"
#include "llmservice.h"
#include "textutils.h"
#include "knowledgebase.h"
#include "diceengine.h"
#include "simulationmanager.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QDebug>
#include <QPointer>

SimulationArbiter::SimulationArbiter(QObject *parent)
    : QObject(parent)
{
}

void SimulationArbiter::processIntent(const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState)
{
    Q_EMIT processingStarted();

    // Step 1: Identify what rules to lookup based on the intent
    QString action = intent.value(QStringLiteral("action")).toString();
    performRuleLookup(action, actorName, actorSheet, intent, worldState);
}

void SimulationArbiter::performRuleLookup(const QString &query, const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState)
{
    KnowledgeBase::instance().search(query, 5, QString(), [this, actorName, actorSheet, intent, worldState](const QList<SearchResult> &results) {
        QString rulesContext;
        for (const auto &res : results) {
            rulesContext += QStringLiteral("--- Rule: %1 ---\n%2\n\n").arg(res.heading, res.content);
        }
        
        finalizeOutcome(rulesContext, actorName, actorSheet, intent, worldState);
    });
}

void SimulationArbiter::finalizeOutcome(const QString &rulesContext, const QString &actorName, const QJsonObject &actorSheet, const QJsonObject &intent, const QJsonObject &worldState)
{
    QString scenario = SimulationManager::instance().scenario();
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString systemPrompt = settings.value(QStringLiteral("simulation/arbiter_prompt"),
        QStringLiteral("You are the Arbiter of a tabletop RPG simulation. Your job is to enforce the rules and update the world state.\n\n"
                       "SCENARIO CONTEXT:\n%1\n\n"
                       "RELEVANT RULES:\n%2\n\n"
                       "CURRENT SITUATION:\n"
                       "- Actor: %3\n"
                       "- Actor Sheet: %4\n"
                       "- Intent: %5\n"
                       "- World State: %6\n\n"
                       "TASK:\n"
                       "1. Determine the outcome of the intent based on the rules provided.\n"
                       "2. If a dice roll is required, you must simulate it (but denote it clearly).\n"
                       "3. Output a valid JSON object with:\n"
                       "   - \"patch\": A JSON object containing dot-separated paths and their NEW values (e.g. {\"actors.fighter.hp\": 12}). Only include changed values.\n"
                       "   - \"log\": A concise, dry summary of the mechanics applied (e.g. \"Rolled 18 (1d20+5) vs AC 15. Target takes 6 damage.\").\n"
                       "   - \"narrative_hints\": Brief keywords for the storyteller (e.g. \"near miss\", \"bloody wound\").\n"
                       "Output ONLY the JSON object.")).toString()
        .arg(scenario, rulesContext, actorName,
          QString::fromUtf8(QJsonDocument(actorSheet).toJson(QJsonDocument::Compact)),
          QString::fromUtf8(QJsonDocument(intent).toJson(QJsonDocument::Compact)),
          QString::fromUtf8(QJsonDocument(worldState).toJson(QJsonDocument::Compact)));

    LLMRequest req;
    req.serviceName = i18n("Simulation Arbiter");
    req.settingsKey = QStringLiteral("simulation/sim_arbiter_model");
    
    // 1. Try agent-specific setting
    // 2. Try legacy analyzer setting
    // 3. Fallback to global default
    // analyzer/analyzer_provider is the canonical key written by the
    // Settings dialog AI Services tab; analyzer/provider is the legacy
    // pre-tab key kept only as a defensive fallback.
    int defaultProv = settings.value(QStringLiteral("analyzer/analyzer_provider"),
                                      settings.value(QStringLiteral("analyzer/provider"),
                                                     settings.value(QStringLiteral("llm/provider"), 0))).toInt();
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("simulation/sim_arbiter_provider"), defaultProv).toInt());

    req.model = settings.value(QStringLiteral("simulation/sim_arbiter_model")).toString();
    if (req.model.isEmpty()) {
        req.model = settings.value(QStringLiteral("analyzer/analyzer_model"),
                                    settings.value(QStringLiteral("analyzer/model"))).toString();
        if (req.model.isEmpty()) {
            switch(req.provider) {
                case LLMProvider::OpenAI: req.model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
                case LLMProvider::Anthropic: req.model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
                case LLMProvider::Ollama: req.model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
                case LLMProvider::Grok: req.model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
                case LLMProvider::Gemini: req.model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
                case LLMProvider::LMStudio: req.model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
            }
        }
    }
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), QStringLiteral("Apply the rules and generate the outcome JSON.")});
    req.stream = false;
    req.temperature = 0.1; // Maximum precision

    QPointer<SimulationArbiter> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis](const QString &response) {
        if (!weakThis) return;

        if (response.isEmpty()) {
            Q_EMIT weakThis->errorOccurred(i18n("Empty response from Arbiter."));
            return;
        }

        // Parse JSON
        QString cleanJson = stripMarkdownFences(response);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(cleanJson.toUtf8(), &error);
        if (doc.isNull() || !doc.isObject()) {
            Q_EMIT weakThis->errorOccurred(i18n("Failed to parse Arbiter outcome: %1", error.errorString()));
            return;
        }

        QJsonObject obj = doc.object();
        QJsonObject patch = obj.value(QStringLiteral("patch")).toObject();
        QString log = obj.value(QStringLiteral("log")).toString();

        Q_EMIT weakThis->outcomeDecided(patch, log);
    });
}
