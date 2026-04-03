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

#include "analyzerservice.h"
#include "knowledgebase.h"
#include "llmservice.h"
#include "projectmanager.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QDir>

AnalyzerService& AnalyzerService::instance()
{
    static AnalyzerService s_instance;
    return s_instance;
}

AnalyzerService::AnalyzerService(QObject *parent)
    : QObject(parent)
{
}

AnalyzerService::~AnalyzerService() = default;

void AnalyzerService::analyzeDocument(const QString &filePath, const QString &content)
{
    // Basic checks
    if (content.trimmed().isEmpty()) return;
    
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    // Default to "Paused" if not set
    int runMode = settings.value(QStringLiteral("analyzer/run_mode"), 2).toInt(); 
    if (runMode == 2) { // 2 = Paused
        return;
    }

    m_activeAnalysisFile = filePath;
    Q_EMIT analysisStarted(filePath);

    // Step 1: Query KnowledgeBase for RAG context
    // We use the content itself or a summary of it as the query.
    // For simplicity, we just use the first few hundred characters as the query.
    QString queryText = content.left(1000);
    
    KnowledgeBase::instance().search(queryText, 3, filePath, [this, filePath, content](const QList<SearchResult> &results) {
        onRagSearchCompleted(filePath, content, results);
    });
}

void AnalyzerService::onRagSearchCompleted(const QString &filePath, const QString &content, const QList<SearchResult> &results)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("analyzer/provider"), settings.value(QStringLiteral("llm/provider"), 0).toInt()).toInt());
    
    QString systemPrompt = settings.value(QStringLiteral("analyzer/system_prompt"), 
        QStringLiteral("You are an expert RPG game design analyzer.\n"
                       "Analyze the provided document for rule conflicts, ambiguities, and completeness gaps.\n"
                       "You must output ONLY a valid JSON array of objects. Do not include markdown code blocks or conversational text.\n"
                       "Format: [{\"line\": 0, \"severity\": \"error|warning|info\", \"message\": \"...\", \"references\": [{\"filePath\": \"...\", \"line\": 0}]}]")).toString();

    QString augmentedContext = QStringLiteral("<current_document path=\"%1\">\n%2\n</current_document>\n").arg(QDir(ProjectManager::instance().projectPath()).relativeFilePath(filePath), content);

    if (!results.isEmpty()) {
        augmentedContext += QStringLiteral("\n<related_context>\n");
        for (const auto &res : results) {
            augmentedContext += QStringLiteral("<context_chunk path=\"%1\" heading=\"%2\">\n%3\n</context_chunk>\n").arg(res.filePath, res.heading, res.content);
        }
        augmentedContext += QStringLiteral("</related_context>\n");
    }

    LLMRequest req;
    req.provider = provider;
    req.model = settings.value(QStringLiteral("analyzer/model"), settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o"))).toString();
    req.temperature = 0.2; // Low temp for analytical tasks
    req.stream = false;
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), augmentedContext});

    LLMService::instance().sendNonStreamingRequest(req, [this, filePath](const QString &response) {
        if (response.isEmpty()) {
            Q_EMIT analysisFailed(filePath, QStringLiteral("Empty response from LLM"));
            return;
        }
        
        QList<Diagnostic> diagnostics = parseDiagnostics(response);
        Q_EMIT diagnosticsUpdated(filePath, diagnostics);
    });
}

QList<Diagnostic> AnalyzerService::parseDiagnostics(const QString &jsonResponse)
{
    QList<Diagnostic> results;
    
    // Clean up response if it has markdown formatting
    QString cleanJson = jsonResponse.trimmed();
    if (cleanJson.startsWith(QLatin1String("```json"))) {
        cleanJson = cleanJson.mid(7);
        if (cleanJson.endsWith(QLatin1String("```"))) {
            cleanJson.chop(3);
        }
    } else if (cleanJson.startsWith(QLatin1String("```"))) {
        cleanJson = cleanJson.mid(3);
        if (cleanJson.endsWith(QLatin1String("```"))) {
            cleanJson.chop(3);
        }
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(cleanJson.toUtf8(), &error);
    
    if (doc.isNull() || !doc.isArray()) {
        qWarning() << "Failed to parse analyzer response as JSON array:" << error.errorString();
        return results;
    }
    
    QJsonArray arr = doc.array();
    for (const QJsonValue &val : arr) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        
        Diagnostic d;
        d.line = obj.value(QStringLiteral("line")).toInt(0);
        d.severity = obj.value(QStringLiteral("severity")).toString(QStringLiteral("info")).toLower();
        d.message = obj.value(QStringLiteral("message")).toString();
        
        QJsonArray refs = obj.value(QStringLiteral("references")).toArray();
        for (const QJsonValue &refVal : refs) {
            if (refVal.isObject()) {
                QJsonObject refObj = refVal.toObject();
                DiagnosticReference r;
                r.filePath = refObj.value(QStringLiteral("filePath")).toString();
                r.line = refObj.value(QStringLiteral("line")).toInt(0);
                d.references.append(r);
            }
        }
        
        results.append(d);
    }
    
    return results;
}
