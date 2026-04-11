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
#include "llmservice.h"
#include "projectmanager.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <QPointer>

AnalyzerService& AnalyzerService::instance()
{
    static AnalyzerService inst;
    return inst;
}

AnalyzerService::AnalyzerService(QObject *parent)
    : QObject(parent)
{
}

AnalyzerService::~AnalyzerService() = default;

void AnalyzerService::analyzeDocument(const QString &filePath, const QString &content)
{
    if (m_paused) {
        // Keep only the latest content for each file in the queue
        auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const PendingAnalysis &p) {
            return p.filePath == filePath;
        });
        if (it != m_queue.end()) {
            it->content = content;
        } else {
            m_queue.append({filePath, content});
        }
        return;
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    // Default to "Paused" if not set
    int runMode = settings.value(QStringLiteral("analyzer/run_mode"), 2).toInt(); 
    if (runMode == 2) { // 2 = Paused
        return;
    }

    if (m_activeAnalysisFile == filePath) return;
    m_activeAnalysisFile = filePath;

    Q_EMIT analysisStarted(filePath);

    // Step 1: Query KnowledgeBase for RAG context
    QString queryText = content.left(1000);
    
    QPointer<AnalyzerService> weakThis(this);
    KnowledgeBase::instance().search(queryText, 3, filePath, [weakThis, filePath, content](const QList<SearchResult> &results) {
        if (weakThis) weakThis->onRagSearchCompleted(filePath, content, results);
    });
}

void AnalyzerService::pause()
{
    m_paused = true;
}

void AnalyzerService::resume()
{
    if (!m_paused) return;
    m_paused = false;
    
    // Process queue (FIFO)
    while (!m_queue.isEmpty()) {
        PendingAnalysis p = m_queue.takeFirst();
        analyzeDocument(p.filePath, p.content);
    }
}

void AnalyzerService::onRagSearchCompleted(const QString &filePath, const QString &content, const QList<SearchResult> &results)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("analyzer/analyzer_provider"), 
                                                                   settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    
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
    
    req.model = settings.value(QStringLiteral("analyzer/analyzer_model"), 
                               settings.value(QStringLiteral("analyzer/model"))).toString();
    req.temperature = 0.2; 
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), augmentedContext});
    req.stream = false;

    QPointer<AnalyzerService> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, filePath](const QString &response) {
        if (!weakThis) return;
        
        weakThis->m_activeAnalysisFile.clear();
        
        if (response.isEmpty()) {
            Q_EMIT weakThis->analysisFailed(filePath, i18n("Empty response from LLM."));
            return;
        }

        QList<Diagnostic> diagnostics = parseDiagnostics(response);
        Q_EMIT weakThis->diagnosticsUpdated(filePath, diagnostics);
    });
}

QList<Diagnostic> AnalyzerService::parseDiagnostics(const QString &jsonResponse)
{
    QList<Diagnostic> results;
    
    // Simple JSON cleanup if LLM included fences
    QString cleanJson = jsonResponse.trimmed();
    if (cleanJson.startsWith(QLatin1String("```json"))) {
        cleanJson = cleanJson.mid(7);
        if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(cleanJson.trimmed().toUtf8(), &error);
    if (doc.isNull() || !doc.isArray()) {
        qWarning() << "Failed to parse Analyzer JSON:" << error.errorString();
        return results;
    }

    QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        QJsonObject obj = v.toObject();
        Diagnostic d;
        d.line = obj.value(QStringLiteral("line")).toInt();
        QString sev = obj.value(QStringLiteral("severity")).toString().toLower();
        if (sev == QStringLiteral("error")) d.severity = DiagnosticSeverity::Error;
        else if (sev == QStringLiteral("warning")) d.severity = DiagnosticSeverity::Warning;
        else d.severity = DiagnosticSeverity::Info;
        
        d.message = obj.value(QStringLiteral("message")).toString();
        
        QJsonArray refs = obj.value(QStringLiteral("references")).toArray();
        for (const auto &rv : refs) {
            QJsonObject robj = rv.toObject();
            d.references.append({robj.value(QStringLiteral("filePath")).toString(), robj.value(QStringLiteral("line")).toInt()});
        }
        
        results.append(d);
    }

    return results;
}
