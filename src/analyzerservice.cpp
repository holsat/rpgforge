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

#include "analyzerservice.h"
#include "agentgatekeeper.h"
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
    connect(&AgentGatekeeper::instance(), &AgentGatekeeper::serviceEnabledChanged,
            this, [this](AgentGatekeeper::Service s, bool enabled) {
        if (s != AgentGatekeeper::Service::Analyzer) return;
        if (!enabled) {
            m_queue.clear();
            pause();
        } else {
            resume();
        }
    });
}

AnalyzerService::~AnalyzerService() = default;

void AnalyzerService::analyzeDocument(const QString &filePath, const QString &content)
{
    qDebug() << "Analyzer AI: Requested analysis for" << filePath;

    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Analyzer)) {
        qDebug() << "Analyzer AI: Skipping — disabled for this project.";
        return;
    }

    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    if (!activeFiles.contains(filePath)) {
        qWarning() << "Analyzer AI: Skipping analysis - file is NOT in authoritative project tree:" << filePath;
        // Print first 5 active files for debugging
        if (!activeFiles.isEmpty()) {
            qDebug() << "Analyzer AI: First few active files:" << activeFiles.mid(0, 5);
        }
        return;
    }

    if (m_paused) {
        qDebug() << "Analyzer AI: Service is paused. Queuing request.";
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
    int runMode = settings.value(QStringLiteral("analyzer/run_mode"), 0).toInt(); // 0 = Auto, 1 = Manual, 2 = Paused
    qDebug() << "Analyzer AI: Current run mode:" << runMode;
    
    if (runMode == 2) { 
        qDebug() << "Analyzer AI: Run mode is set to 'Paused'. Skipping.";
        return;
    }

    if (m_activeAnalysisFile == filePath) {
        qDebug() << "Analyzer AI: Analysis already in progress for this file. Skipping duplicate request.";
        return;
    }
    
    m_activeAnalysisFile = filePath;
    Q_EMIT analysisStarted(filePath);

    qDebug() << "Analyzer AI: Starting RAG context search...";
    QString queryText = content.left(1000);
    
    QPointer<AnalyzerService> weakThis(this);
    KnowledgeBase::instance().search(queryText, 3, filePath, [weakThis, filePath, content](const QList<SearchResult> &results) {
        if (weakThis) {
            qDebug() << "Analyzer AI: RAG search returned" << results.size() << "chunks.";
            weakThis->onRagSearchCompleted(filePath, content, results);
        }
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

void AnalyzerService::suppressDiagnostic(const QString &message)
{
    if (!m_suppressionList.contains(message)) {
        m_suppressionList.append(message);
    }
}

bool AnalyzerService::isSuppressed(const QString &message) const
{
    return m_suppressionList.contains(message);
}

void AnalyzerService::onRagSearchCompleted(const QString &filePath, const QString &content, const QList<SearchResult> &results)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("analyzer/analyzer_provider"), 
                                                                   settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    
    QString systemPrompt = settings.value(QStringLiteral("analyzer/system_prompt"),
        QStringLiteral("You are an expert RPG game design analyzer.\n"
                       "Analyze the provided document for rule conflicts, ambiguities, and completeness gaps.\n"
                       "Each line of <current_document> is prefixed with its 1-based line number and a pipe, e.g. `42| some text`. "
                       "In every `line` field of your response, report the number you see in that prefix — do not count lines yourself. "
                       "The prefix is a guide for you; it is not part of the document content.\n"
                       "You must output ONLY a valid JSON array of objects. Do not include markdown code blocks or conversational text.\n"
                       "Format: [{\"line\": 1, \"severity\": \"error|warning|info\", \"message\": \"...\", \"references\": [{\"filePath\": \"...\", \"line\": 1}]}]")).toString();

    // Annotate each line with its 1-based number so the model reads the number
    // instead of counting — LLMs are unreliable at line arithmetic on long
    // documents and were reporting offsets of hundreds of lines.
    QString annotated;
    annotated.reserve(content.size() + content.count(QLatin1Char('\n')) * 8);
    int lineNo = 1;
    int start = 0;
    for (int i = 0; i <= content.size(); ++i) {
        if (i == content.size() || content.at(i) == QLatin1Char('\n')) {
            annotated += QString::number(lineNo++);
            annotated += QStringLiteral("| ");
            annotated += QStringView{content}.mid(start, i - start);
            annotated += QLatin1Char('\n');
            start = i + 1;
        }
    }

    QString augmentedContext = QStringLiteral("<current_document path=\"%1\">\n%2</current_document>\n").arg(QDir(ProjectManager::instance().projectPath()).relativeFilePath(filePath), annotated);

    if (!results.isEmpty()) {
        augmentedContext += QStringLiteral("\n<related_context>\n");
        for (const auto &res : results) {
            augmentedContext += QStringLiteral("<context_chunk path=\"%1\" heading=\"%2\">\n%3\n</context_chunk>\n").arg(res.filePath, res.heading, res.content);
        }
        augmentedContext += QStringLiteral("</related_context>\n");
    }

    LLMRequest req;
    req.provider = provider;
    req.serviceName = i18n("Game Analyzer");
    req.settingsKey = QStringLiteral("analyzer/analyzer_model");
    
    req.model = settings.value(QStringLiteral("analyzer/analyzer_model")).toString();
    if (req.model.isEmpty()) {
        // Fallback to provider default if analyzer specific model not set
        switch(provider) {
            case LLMProvider::OpenAI: req.model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
            case LLMProvider::Anthropic: req.model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
            case LLMProvider::Ollama: req.model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
            case LLMProvider::Grok: req.model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
            case LLMProvider::Gemini: req.model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
            case LLMProvider::LMStudio: req.model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
        }
    }
    
    req.temperature = 0.2; 
    
    req.messages.append({QStringLiteral("system"), systemPrompt});
    req.messages.append({QStringLiteral("user"), augmentedContext});
    req.stream = false;

    QPointer<AnalyzerService> weakThis(this);
    LLMService::instance().sendNonStreamingRequestDetailed(req, [weakThis, filePath](const QString &response, const QString &error) {
        if (!weakThis) return;

        qDebug() << "Analyzer AI: Received response from LLM for" << filePath << "(length:" << response.length() << ")";
        weakThis->m_activeAnalysisFile.clear();

        if (!error.isEmpty()) {
            qWarning() << "Analyzer AI: LLM error for" << filePath << ":" << error;
            Q_EMIT weakThis->analysisFailed(filePath, error);
            return;
        }
        if (response.isEmpty()) {
            qWarning() << "Analyzer AI: Received EMPTY response from LLM.";
            Q_EMIT weakThis->analysisFailed(filePath, i18n("Empty response from LLM."));
            return;
        }

        QList<Diagnostic> diagnostics = parseDiagnostics(response);
        qDebug() << "Analyzer AI: Successfully parsed" << diagnostics.size() << "diagnostics.";
        Q_EMIT weakThis->diagnosticsUpdated(filePath, diagnostics);
    });
}

QList<Diagnostic> AnalyzerService::parseDiagnostics(const QString &jsonResponse)
{
    qDebug() << "Analyzer AI: Parsing raw JSON response:" << jsonResponse.left(500);
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
        QString message = obj.value(QStringLiteral("message")).toString();
        
        // Skip if suppressed
        if (instance().isSuppressed(message)) continue;

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
