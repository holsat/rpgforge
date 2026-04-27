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
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace {
// Connection name for the analyzer's per-project SQLite cache. Kept
// separate from KnowledgeBase's connections so QSqlDatabase doesn't
// reuse a wrong-schema handle.
const QString kAnalyzerDbConnection = QStringLiteral("rpgforge_analyzer_db");
} // namespace

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

    // Background catch-up timer. Drains the pending queue one file at
    // a time with a 4s gap between dispatches so the LLM provider's
    // rate limit isn't a problem and we don't compete with foreground
    // typing-driven analysis. The interval is conservative on purpose
    // — accuracy + restart-resilience matter more than how fast the
    // initial sweep finishes.
    m_bgTimer = new QTimer(this);
    m_bgTimer->setInterval(4000);
    connect(m_bgTimer, &QTimer::timeout, this, [this]() {
        if (m_queue.isEmpty()) {
            m_bgTimer->stop();
            return;
        }
        if (!m_activeAnalysisFile.isEmpty()) return;   // wait for inflight
        if (m_paused) { m_bgTimer->stop(); return; }
        const PendingAnalysis p = m_queue.takeFirst();
        // Re-enter analyzeDocument so the hash skip path runs again
        // — content on disk may have changed since we queued it.
        analyzeDocument(p.filePath, p.content);
    });
}

AnalyzerService::~AnalyzerService() = default;

// ---------------------------------------------------------------------------
// Persistence layer
// ---------------------------------------------------------------------------

void AnalyzerService::initForProject(const QString &projectPath)
{
    if (projectPath == m_projectPath && !m_dbPath.isEmpty()) {
        // Same project; just re-broadcast cached results.
        emitAllCached();
        return;
    }

    closeProject();

    m_projectPath = projectPath;
    if (projectPath.isEmpty()) return;

    m_dbPath = QDir(projectPath).absoluteFilePath(QStringLiteral(".rpgforge-analyzer.db"));

    if (!ensureSchema()) {
        qWarning() << "Analyzer AI: failed to open analyzer cache at" << m_dbPath;
        m_dbPath.clear();
        return;
    }
    qInfo().noquote() << "Analyzer AI: cache ready at" << m_dbPath;
    emitAllCached();
}

void AnalyzerService::closeProject()
{
    if (m_bgTimer) m_bgTimer->stop();
    m_queue.clear();
    m_activeAnalysisFile.clear();
    if (QSqlDatabase::contains(kAnalyzerDbConnection)) {
        {
            QSqlDatabase db = QSqlDatabase::database(kAnalyzerDbConnection);
            if (db.isOpen()) db.close();
        }
        QSqlDatabase::removeDatabase(kAnalyzerDbConnection);
    }
    m_projectPath.clear();
    m_dbPath.clear();
}

bool AnalyzerService::ensureSchema()
{
    if (m_dbPath.isEmpty()) return false;

    QSqlDatabase db;
    if (QSqlDatabase::contains(kAnalyzerDbConnection)) {
        db = QSqlDatabase::database(kAnalyzerDbConnection);
        if (db.databaseName() != m_dbPath) {
            db.close();
            QSqlDatabase::removeDatabase(kAnalyzerDbConnection);
            db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kAnalyzerDbConnection);
            db.setDatabaseName(m_dbPath);
        }
    } else {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kAnalyzerDbConnection);
        db.setDatabaseName(m_dbPath);
    }

    if (!db.isOpen() && !db.open()) {
        qWarning() << "Analyzer AI: open failed:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS analyzer_results ("
            "  file_path TEXT PRIMARY KEY,"
            "  content_hash TEXT NOT NULL,"
            "  analyzed_at INTEGER NOT NULL,"
            "  diagnostics_json TEXT NOT NULL"
            ")"))) {
        qWarning() << "Analyzer AI: schema create failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QString AnalyzerService::hashContent(const QString &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool AnalyzerService::loadCacheRow(const QString &filePath,
                                    QString &outHash,
                                    QList<Diagnostic> &outDiagnostics) const
{
    if (m_dbPath.isEmpty() || !QSqlDatabase::contains(kAnalyzerDbConnection)) return false;
    QSqlDatabase db = QSqlDatabase::database(kAnalyzerDbConnection);
    if (!db.isOpen()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT content_hash, diagnostics_json "
                              "FROM analyzer_results WHERE file_path = :p"));
    q.bindValue(QStringLiteral(":p"), filePath);
    if (!q.exec() || !q.next()) return false;
    outHash = q.value(0).toString();
    outDiagnostics = diagnosticsFromJson(q.value(1).toString());
    return true;
}

void AnalyzerService::storeCacheRow(const QString &filePath,
                                     const QString &hash,
                                     const QList<Diagnostic> &diagnostics)
{
    if (m_dbPath.isEmpty() || !QSqlDatabase::contains(kAnalyzerDbConnection)) return;
    QSqlDatabase db = QSqlDatabase::database(kAnalyzerDbConnection);
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO analyzer_results (file_path, content_hash, analyzed_at, diagnostics_json) "
        "VALUES (:p, :h, :t, :j) "
        "ON CONFLICT(file_path) DO UPDATE SET "
        "  content_hash = excluded.content_hash, "
        "  analyzed_at  = excluded.analyzed_at, "
        "  diagnostics_json = excluded.diagnostics_json"));
    q.bindValue(QStringLiteral(":p"), filePath);
    q.bindValue(QStringLiteral(":h"), hash);
    q.bindValue(QStringLiteral(":t"), QDateTime::currentMSecsSinceEpoch());
    q.bindValue(QStringLiteral(":j"), diagnosticsToJson(diagnostics));
    if (!q.exec()) {
        qWarning() << "Analyzer AI: store failed:" << q.lastError().text();
    }
}

void AnalyzerService::emitAllCached()
{
    if (m_dbPath.isEmpty() || !QSqlDatabase::contains(kAnalyzerDbConnection)) return;
    QSqlDatabase db = QSqlDatabase::database(kAnalyzerDbConnection);
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT file_path, diagnostics_json FROM analyzer_results"))) {
        qWarning() << "Analyzer AI: emitAllCached read failed:" << q.lastError().text();
        return;
    }
    int n = 0;
    while (q.next()) {
        const QString filePath = q.value(0).toString();
        const QList<Diagnostic> diags = diagnosticsFromJson(q.value(1).toString());
        Q_EMIT diagnosticsUpdated(filePath, diags);
        ++n;
    }
    qInfo().noquote() << "Analyzer AI: replayed" << n << "cached file(s) into UI";
}

QString AnalyzerService::diagnosticsToJson(const QList<Diagnostic> &diagnostics)
{
    QJsonArray arr;
    for (const Diagnostic &d : diagnostics) {
        QJsonObject o;
        o.insert(QStringLiteral("line"), d.line);
        o.insert(QStringLiteral("message"), d.message);
        o.insert(QStringLiteral("filePath"), d.filePath);
        QString sev;
        switch (d.severity) {
            case DiagnosticSeverity::Error:   sev = QStringLiteral("error"); break;
            case DiagnosticSeverity::Warning: sev = QStringLiteral("warning"); break;
            case DiagnosticSeverity::Info:    sev = QStringLiteral("info"); break;
        }
        o.insert(QStringLiteral("severity"), sev);
        QJsonArray refs;
        for (const DiagnosticReference &r : d.references) {
            QJsonObject ro;
            ro.insert(QStringLiteral("filePath"), r.filePath);
            ro.insert(QStringLiteral("line"), r.line);
            refs.append(ro);
        }
        o.insert(QStringLiteral("references"), refs);
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<Diagnostic> AnalyzerService::diagnosticsFromJson(const QString &json)
{
    QList<Diagnostic> out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (doc.isNull() || !doc.isArray()) return out;
    for (const QJsonValue &v : doc.array()) {
        QJsonObject o = v.toObject();
        Diagnostic d;
        d.line = o.value(QStringLiteral("line")).toInt();
        d.message = o.value(QStringLiteral("message")).toString();
        d.filePath = o.value(QStringLiteral("filePath")).toString();
        const QString sev = o.value(QStringLiteral("severity")).toString().toLower();
        if (sev == QStringLiteral("error")) d.severity = DiagnosticSeverity::Error;
        else if (sev == QStringLiteral("warning")) d.severity = DiagnosticSeverity::Warning;
        else d.severity = DiagnosticSeverity::Info;
        const QJsonArray refs = o.value(QStringLiteral("references")).toArray();
        for (const QJsonValue &rv : refs) {
            const QJsonObject ro = rv.toObject();
            d.references.append({
                ro.value(QStringLiteral("filePath")).toString(),
                ro.value(QStringLiteral("line")).toInt(),
            });
        }
        out.append(d);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Background catch-up
// ---------------------------------------------------------------------------

void AnalyzerService::kickBackgroundScan()
{
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Analyzer)) {
        qDebug() << "Analyzer AI: background scan skipped — analyzer disabled.";
        return;
    }
    if (m_projectPath.isEmpty()) return;

    const QStringList active = ProjectManager::instance().getActiveFiles();
    int queued = 0;
    int skippedFresh = 0;
    for (const QString &path : active) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix != QStringLiteral("md") && suffix != QStringLiteral("markdown")) continue;

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString content = QString::fromUtf8(f.readAll());
        f.close();

        const QString currentHash = hashContent(content);
        QString cachedHash;
        QList<Diagnostic> cachedDiags;
        if (loadCacheRow(path, cachedHash, cachedDiags) && cachedHash == currentHash) {
            // Already analyzed at this exact content — nothing to do.
            // The diagnostics were already broadcast by emitAllCached().
            ++skippedFresh;
            continue;
        }

        // Queue with the disk content so the timer-driven drain can
        // re-check when it actually runs (file may have changed).
        const auto it = std::find_if(m_queue.begin(), m_queue.end(),
            [&](const PendingAnalysis &p) { return p.filePath == path; });
        if (it != m_queue.end()) {
            it->content = content;
        } else {
            m_queue.append({path, content});
        }
        ++queued;
    }
    qInfo().noquote() << "Analyzer AI: background scan queued" << queued
                       << "file(s);" << skippedFresh << "already fresh";
    if (queued > 0 && !m_paused && m_bgTimer && !m_bgTimer->isActive()) {
        m_bgTimer->start();
    }
}

// ---------------------------------------------------------------------------
// Foreground analysis (typing-driven)
// ---------------------------------------------------------------------------

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
        if (!activeFiles.isEmpty()) {
            qDebug() << "Analyzer AI: First few active files:" << activeFiles.mid(0, 5);
        }
        return;
    }

    // Hash check: if we have a cache row whose hash matches the
    // supplied content, the LLM round-trip is unnecessary. Re-emit
    // the cached diagnostics so the UI is current and return.
    const QString currentHash = hashContent(content);
    {
        QString cachedHash;
        QList<Diagnostic> cachedDiags;
        if (loadCacheRow(filePath, cachedHash, cachedDiags) && cachedHash == currentHash) {
            qDebug() << "Analyzer AI: cache hit — skipping LLM call for" << filePath;
            Q_EMIT diagnosticsUpdated(filePath, cachedDiags);
            return;
        }
    }

    if (m_paused) {
        qDebug() << "Analyzer AI: Service is paused. Queuing request.";
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
    KnowledgeBase::instance().hybridSearch(queryText, 3, filePath, [weakThis, filePath, content](const QList<SearchResult> &results) {
        if (weakThis) {
            qDebug() << "Analyzer AI: RAG search returned" << results.size() << "chunks.";
            weakThis->onRagSearchCompleted(filePath, content, results);
        }
    });
}

void AnalyzerService::pause()
{
    m_paused = true;
    if (m_bgTimer) m_bgTimer->stop();
}

void AnalyzerService::resume()
{
    if (!m_paused) return;
    m_paused = false;

    // Process queue (FIFO). Foreground requests take priority — the
    // background timer is restarted only after the synchronous drain.
    while (!m_queue.isEmpty()) {
        PendingAnalysis p = m_queue.takeFirst();
        analyzeDocument(p.filePath, p.content);
    }
    if (!m_queue.isEmpty() && m_bgTimer && !m_bgTimer->isActive()) {
        m_bgTimer->start();
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
    LLMService::instance().sendNonStreamingRequestDetailed(req, [weakThis, filePath, content](const QString &response, const QString &error) {
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

        // Persist successful results so the next session sees them
        // immediately and we skip the LLM call for unchanged content.
        const QString hash = hashContent(content);
        weakThis->storeCacheRow(filePath, hash, diagnostics);

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
