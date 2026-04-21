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

#include "librarianservice.h"
#include "llmservice.h"
#include "projectmanager.h"
#include "debuglog.h"
#include <KLocalizedString>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

LibrarianService::LibrarianService(LLMService *llmService, QObject *parent)
    : QObject(parent), m_llmService(llmService)
{
    m_db = new LibrarianDatabase(this);
    m_watcher = new QFileSystemWatcher(this);
    
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    m_processTimer->setInterval(500); // 500ms debounce
    connect(m_processTimer, &QTimer::timeout, this, &LibrarianService::processQueue);

    m_semanticTimer = new QTimer(this);
    m_semanticTimer->setInterval(30000); // 30s batch cycle
    connect(m_semanticTimer, &QTimer::timeout, this, &LibrarianService::runSemanticBatch);
    m_semanticTimer->start();

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &LibrarianService::onFileChanged);
}

LibrarianService::~LibrarianService()
{
    m_db->close();
}

void LibrarianService::setProjectPath(const QString &path)
{
    if (path.isEmpty()) return;
    qDebug() << "Data Extractor: Setting project path:" << path;
    QMutexLocker locker(&m_mutex);
    m_projectPath = path;
    m_pendingFiles.clear(); 
    
    QStringList watched = m_watcher->files();
    if (!watched.isEmpty()) {
        m_watcher->removePaths(watched);
    }
    
    QString dbPath = QDir(path).filePath(QStringLiteral(".rpgforge.db"));
    m_dbPath = dbPath;
    if (m_db->open(dbPath)) {
        RPGFORGE_DLOG("VARS") << "LibrarianService: DB opened at" << dbPath;
        scanAll();
    } else {
        qWarning().noquote() << "LibrarianService: FAILED to open DB at" << dbPath
                              << "—" << m_db->lastError()
                              << "— variable extraction is DISABLED";
        Q_EMIT errorOccurred(m_db->lastError());
    }
}

void LibrarianService::pause()
{
    qDebug() << "Data Extractor: Pausing...";
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    m_processTimer->stop();
    m_semanticTimer->stop();
}

void LibrarianService::resume()
{
    qDebug() << "Data Extractor: Resuming...";
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    m_processTimer->start();
    m_semanticTimer->start();
}

void LibrarianService::scanAll()
{
    if (m_paused || m_projectPath.isEmpty()) {
        RPGFORGE_DLOG("VARS") << "LibrarianService::scanAll: skipping — paused="
                              << m_paused << "projectPath=" << m_projectPath;
        return;
    }

    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    RPGFORGE_DLOG("VARS") << "LibrarianService::scanAll: considering"
                          << activeFiles.size() << "active project files";

    int queued = 0;
    QMutexLocker locker(&m_mutex);
    for (const QString &file : activeFiles) {
        QString suffix = QFileInfo(file).suffix().toLower();
        if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
            if (!m_pendingFiles.contains(file)) {
                m_pendingFiles.append(file);
                ++queued;
            }
            m_watcher->addPath(file);
        }
    }
    RPGFORGE_DLOG("VARS") << "LibrarianService::scanAll: queued" << queued
                          << "markdown file(s), starting debounce timer";
    m_processTimer->start();
}

void LibrarianService::scanFile(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    if (!m_pendingFiles.contains(filePath)) {
        m_pendingFiles.append(filePath);
    }
    if (!m_watcher->files().contains(filePath)) {
        m_watcher->addPath(filePath);
    }
    m_processTimer->start();
}

void LibrarianService::onFileChanged(const QString &path)
{
    scanFile(path);
}

void LibrarianService::processQueue()
{
    QMutexLocker locker(&m_mutex);
    if (m_paused || m_pendingFiles.isEmpty() || !m_db->database().isOpen()) {
        RPGFORGE_DLOG("VARS") << "LibrarianService::processQueue: skipping — paused="
                              << m_paused << "pending=" << m_pendingFiles.size()
                              << "dbOpen=" << m_db->database().isOpen();
        return;
    }

    Q_EMIT scanningStarted();

    QStringList filesToProcess = m_pendingFiles;
    m_pendingFiles.clear();
    RPGFORGE_DLOG("VARS") << "LibrarianService::processQueue: processing"
                          << filesToProcess.size() << "file(s)";
    
    QPointer<LibrarianService> weakThis(this);
    // Fire-and-forget: finalization happens via QueuedConnection back to
    // the main thread at the end of the task, so we don't need the QFuture.
    QThreadPool::globalInstance()->start([weakThis, filesToProcess]() {
        if (!weakThis) return;

        for (const QString &filePath : filesToProcess) {
            if (!weakThis || weakThis->isPaused()) break;
            weakThis->extractHeuristic(filePath);
        }

        // Finalize back on main thread
        QMetaObject::invokeMethod(weakThis.data(), [weakThis]() {
            if (!weakThis) return;

            QMap<QString, QString> libVars;
            QSqlDatabase db = weakThis->database()->database();
            if (db.isOpen()) {
                QSqlQuery query(db);
                query.exec(QStringLiteral("SELECT e.type, e.name, a.key, a.value FROM entities e "
                                        "JOIN attributes a ON e.id = a.entity_id"));
                // Normaliser: drop every character that can't appear in
                // a {{var.path}} identifier. VariableManager::resolve's
                // regex allows [A-Za-z0-9_\.] only; anything else (":",
                // ",", parens, commas, apostrophes, emoji, …) must be
                // stripped or the variable is unresolvable. The DB still
                // has the pretty form for display.
                static const QRegularExpression nonIdent(
                    QStringLiteral("[^A-Za-z0-9_\\.]"));
                auto sanitise = [](const QString &raw) {
                    QString cleaned = raw;
                    cleaned.replace(nonIdent, QString());
                    return cleaned;
                };
                while (query.next()) {
                    const QString type = sanitise(query.value(0).toString());
                    const QString name = sanitise(query.value(1).toString());
                    const QString key  = sanitise(query.value(2).toString());
                    const QString val  = query.value(3).toString();
                    if (type.isEmpty() || name.isEmpty() || key.isEmpty()) continue;
                    libVars.insert(QStringLiteral("%1.%2.%3").arg(type, name, key), val);
                }
            }

            RPGFORGE_DLOG("VARS") << "LibrarianService: emitting libraryVariablesChanged with"
                                   << libVars.size() << "entries";
            for (auto it = libVars.constBegin(); it != libVars.constEnd(); ++it) {
                RPGFORGE_DLOG("VARS") << "  " << it.key() << "=" << it.value();
            }
            // Sanity: every variable key must match VariableManager's
            // resolver regex ([A-Za-z0-9_\.]+) or it can't be invoked
            // via {{name}} at all. Log anything that won't resolve so
            // we can see it rather than puzzling over silent misses.
            static const QRegularExpression validKey(
                QStringLiteral("^[A-Za-z0-9_\\.]+$"));
            for (auto it = libVars.constBegin(); it != libVars.constEnd(); ++it) {
                if (!validKey.match(it.key()).hasMatch()) {
                    qWarning().noquote() << "LibrarianService: variable key"
                        << it.key() << "contains characters that cannot resolve in {{...}}";
                }
            }

            Q_EMIT weakThis->libraryVariablesChanged(libVars);
            Q_EMIT weakThis->scanningFinished();
        }, Qt::QueuedConnection);
    });
}

void LibrarianService::extractHeuristic(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        RPGFORGE_DLOG("VARS") << "extractHeuristic: cannot open" << filePath;
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    RPGFORGE_DLOG("VARS") << "extractHeuristic:" << filePath << "(" << content.size() << "chars)";
    m_db->beginTransaction();
    parseMarkdownTables(content, filePath);
    parseMarkdownLists(content, filePath);
    m_db->commit();
}

namespace {

// Strip markdown inline formatting from a header cell or entity name so
// the derived variable identifier is clean. Handles:
//   **bold** / __bold__     -> bold
//   *italic* / _italic_     -> italic
//   `code`                  -> code
//   \#  \|  \* (etc.)       -> #  |  *  (backslash-escapes)
//   leading/trailing space  -> removed
// NOT used on cell VALUES — we want to preserve the author's formatting
// there so callers rendering the value can re-style it.
QString stripMarkdownFormatting(const QString &in)
{
    QString out = in.trimmed();

    // Unescape backslash-escaped markdown punctuation first so we don't
    // mistake \* for a bold marker.
    static const QRegularExpression escapes(QStringLiteral("\\\\([\\\\`*_{}\\[\\]()#+\\-.!|])"));
    out.replace(escapes, QStringLiteral("\\1"));

    // Strip paired bold/italic/code markers. Non-greedy to handle multiple
    // spans on one line. Order matters: bold first (longer sequences),
    // then italic, then inline code.
    static const QRegularExpression bold(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    static const QRegularExpression boldUnderscore(QStringLiteral("__([^_]+)__"));
    static const QRegularExpression italic(QStringLiteral("\\*([^*]+)\\*"));
    static const QRegularExpression italicUnderscore(QStringLiteral("_([^_]+)_"));
    static const QRegularExpression code(QStringLiteral("`([^`]+)`"));
    out.replace(bold, QStringLiteral("\\1"));
    out.replace(boldUnderscore, QStringLiteral("\\1"));
    out.replace(italic, QStringLiteral("\\1"));
    out.replace(italicUnderscore, QStringLiteral("\\1"));
    out.replace(code, QStringLiteral("\\1"));

    return out.trimmed();
}

// Derive a variable-safe identifier from a header/entity name: strip
// formatting, lowercase, collapse non-alphanumeric runs (spaces, hashes,
// etc.) into nothing. Keeps letters, digits, and dots. Empty input ->
// empty string (caller should skip).
QString identifierFromMarkdown(const QString &in)
{
    QString stripped = stripMarkdownFormatting(in).toLower();
    static const QRegularExpression nonIdent(QStringLiteral("[^a-z0-9.]"));
    stripped.replace(nonIdent, QString());
    return stripped;
}

} // namespace

void LibrarianService::parseMarkdownTables(const QString &content, const QString &sourceFile)
{
    QRegularExpression separatorRegex(QStringLiteral("^\\|([:\\s-]+\\|)+\\s*$"), QRegularExpression::MultilineOption);

    QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1Char('|'))) {
            QStringList headers = lines[i].split(QLatin1Char('|'), Qt::SkipEmptyParts);
            if (!headers.isEmpty() && i + 1 < lines.size() && separatorRegex.match(lines[i+1]).hasMatch()) {
                // Table name comes from the first header cell. Strip
                // markdown formatting and collapse to a clean identifier
                // so "**Difficulty**" becomes "difficulty", not
                // "**difficulty**".
                const QString tableName = identifierFromMarkdown(headers[0]);
                if (tableName.isEmpty()) continue;

                QStringList cleanedHeaders;
                for (const QString &h : headers) cleanedHeaders << identifierFromMarkdown(h);
                RPGFORGE_DLOG("VARS") << "parseMarkdownTables: found table at line" << i
                                       << "in" << sourceFile
                                       << "tableName=" << tableName
                                       << "headers=" << cleanedHeaders;

                for (int j = i + 2; j < lines.size() && lines[j].startsWith(QLatin1Char('|')); ++j) {
                    QStringList cells = lines[j].split(QLatin1Char('|'), Qt::SkipEmptyParts);
                    if (cells.size() >= 2) {
                        // Entity name can stay close to the author's
                        // literal value — numeric indexes (0, 1, 2) are
                        // common and must be preserved exactly. Strip
                        // formatting but don't over-normalise.
                        const QString entityName = stripMarkdownFormatting(cells[0]);
                        if (entityName.isEmpty()) continue;

                        qint64 entityId = m_db->findEntity(entityName, tableName, sourceFile);
                        if (entityId == -1) {
                            entityId = m_db->addEntity(entityName, tableName, sourceFile);
                        }

                        for (int k = 1; k < cells.size() && k < headers.size(); ++k) {
                            // Attribute keys also get the identifier
                            // treatment — "**Target \#**" becomes "target".
                            const QString key = identifierFromMarkdown(headers[k]);
                            if (key.isEmpty()) continue;

                            // Strip stray markdown markers from the
                            // value — paired bold/italic spans get
                            // preserved by stripMarkdownFormatting, but
                            // we still want to kill trailing "**" that
                            // a row with a single escaped asterisk (e.g.
                            // "\*aptitude checks.\*") accidentally
                            // produces.
                            QString val = stripMarkdownFormatting(cells[k]);
                            if (val.isEmpty()) continue;
                            m_db->setAttribute(entityId, key, val);
                            RPGFORGE_DLOG("VARS") << "  attr" << tableName << "/" << entityName
                                                   << "." << key << "=" << val;
                        }
                        Q_EMIT entityUpdated(entityId);
                    }
                }
            }
        }
    }
}

void LibrarianService::parseMarkdownLists(const QString &content, const QString &sourceFile)
{
    QStringList lines = content.split(QLatin1Char('\n'));
    
    QRegularExpression headerRegex(QStringLiteral("^(#+)\\s+(.+)$"));
    QRegularExpression kvRegex(QStringLiteral("^\\s*[-*+]?\\s*(?:\\*\\*|__)?([\\w\\s\\.]+)(?:\\*\\*|__)?\\s*:\\s*(.+)$"));

    struct Section {
        int level;
        QString name;
    };
    QList<Section> sectionStack;
    sectionStack.append({0, QStringLiteral("Global")});

    for (const QString &line : lines) {
        auto headerMatch = headerRegex.match(line);
        if (headerMatch.hasMatch()) {
            int level = headerMatch.captured(1).length();
            QString name = headerMatch.captured(2).trimmed();
            // Clean up name (e.g. remove trailing tags like {#èwò})
            name = name.remove(QRegularExpression(QStringLiteral("\\{.+\\}$"))).trimmed();

            while (sectionStack.size() > 1 && sectionStack.last().level >= level) {
                sectionStack.removeLast();
            }
            sectionStack.append({level, name});
            continue;
        }

        auto kvMatch = kvRegex.match(line);
        if (kvMatch.hasMatch()) {
            QString key = kvMatch.captured(1).trimmed();
            QString value = kvMatch.captured(2).trimmed();

            // Strip stray markdown markers the regex's optional-bold groups
            // didn't consume (e.g. "**Chapters:** Folders..." leaves the
            // closing "**" at the start of the value). Repeatedly strip
            // leading/trailing bold/italic/code markers until none remain.
            auto stripStrays = [](QString &s) {
                static const QRegularExpression stray(QStringLiteral(
                    "^\\s*(?:\\*\\*|__|\\*|_|`)+\\s*|\\s*(?:\\*\\*|__|\\*|_|`)+\\s*$"));
                QString prev;
                int guard = 4;
                do {
                    prev = s;
                    s.replace(stray, QString());
                    s = s.trimmed();
                } while (s != prev && guard--);
            };
            stripStrays(key);
            stripStrays(value);

            // Reject empty/useless extractions that the old path happily
            // produced. Most common sources:
            //   - Character-sketch templates with "Archetype: **" empty
            //     placeholders (value=="" after strip).
            //   - Template fields like "title:" / "status:" in front matter.
            if (key.isEmpty() || value.isEmpty()) continue;
            if (key.toLower() == QStringLiteral("title") || key.toLower() == QStringLiteral("status")) continue;

            // Reject numbered-list rule steps masquerading as key/value
            // pairs: "1. To check an aptitude: add the aptitude's pool to
            // a roll". The "entity" is the section heading, the "key"
            // captured by the regex is "1.  To check an aptitude" — not
            // a stat, it's a numbered instruction.
            static const QRegularExpression numberedStep(
                QStringLiteral("^\\d+[.)]\\s"));
            if (numberedStep.match(key).hasMatch()) continue;

            // Reject prose values. Game stats are short ("35", "+2",
            // "1d10", "None"). Rule-prose tends to be a sentence. Keep
            // the 200-char ceiling loose so descriptive values
            // ("Resistant to the extremes of heat and cold.") survive
            // but full-paragraph quotations do not.
            if (value.length() > 200) continue;

            if (key.isEmpty() || value.isEmpty()) continue;  // re-check after strips

            // Build entity name from the stack, but skip generic ones like "STATISTICS"
            // if we have a parent.
            QString entityName;
            if (sectionStack.size() > 1) {
                QString last = sectionStack.last().name;
                if ((last.toUpper() == QStringLiteral("STATISTICS") || last.toUpper() == QStringLiteral("ABILITIES")) && sectionStack.size() > 2) {
                    entityName = sectionStack[sectionStack.size() - 2].name;
                } else {
                    entityName = last;
                }
            } else {
                entityName = sectionStack.last().name;
            }

            // Strip stray markers + numbered-list prefixes from the
            // entity name. "1. The Manuscript" → "The Manuscript";
            // "✍️ Writing Tips" is kept as-is for now (valid Unicode
            // text; users may want entities named after characters with
            // emoji prefixes).
            stripStrays(entityName);
            entityName.replace(numberedStep, QString());
            entityName = entityName.trimmed();

            // Reject the global/fallback bucket entirely — anything
            // caught here with entity=="Global" is a list item outside
            // any heading, which in practice is always plot outlines,
            // sample output, or template scaffolding. Never a real stat.
            if (entityName.isEmpty()
                || entityName.compare(QStringLiteral("Global"), Qt::CaseInsensitive) == 0) {
                continue;
            }

            qint64 entityId = m_db->findEntity(entityName, QStringLiteral("property"), sourceFile);
            if (entityId == -1) {
                entityId = m_db->addEntity(entityName, QStringLiteral("property"), sourceFile);
            }
            m_db->setAttribute(entityId, key, value);

            RPGFORGE_DLOG("VARS") << "parseMarkdownLists: entity" << entityName
                                   << "key=" << key << "val=" << value
                                   << "src=" << sourceFile;
            Q_EMIT entityUpdated(entityId);
        }
    }
}

void LibrarianService::runSemanticBatch()
{
    if (m_paused || m_projectPath.isEmpty() || !m_llmService) return;

    QDir dir(m_projectPath);
    QStringList filters;
    filters << QStringLiteral("*.md");
    QDirIterator it(m_projectPath, filters, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    if (it.hasNext()) {
        QString filePath = it.next();
        extractSemantic(filePath);
    }
}

void LibrarianService::extractSemantic(const QString &filePath)
{
    if (!m_llmService) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    QString prompt = QStringLiteral(
        "You are the Librarian Agent for RPG Forge. Your task is to extract structured game design data from the following text.\n"
        "Identify Entities (Monsters, Items, Classes, Spells, etc.) and their Attributes (Stats, costs, descriptions).\n"
        "Return the data in a strict JSON array of objects format: \n"
        "[{\"entity\": \"Name\", \"type\": \"Category\", \"attributes\": {\"key\": \"value\"}}]\n\n"
        "Text to analyze:\n"
    ) + content;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("librarian/provider"), settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    QString model = settings.value(QStringLiteral("librarian/model"), (provider == LLMProvider::Ollama ? QStringLiteral("llama3") : QString())).toString();

    LLMRequest request;
    request.provider = provider;
    request.model = model;
    request.serviceName = i18n("Librarian Agent");
    request.settingsKey = QStringLiteral("librarian/model");
    request.messages << LLMMessage{QStringLiteral("system"), QStringLiteral("You extract structured RPG data as JSON.")};
    request.messages << LLMMessage{QStringLiteral("user"), prompt};
    request.stream = false;

    QPointer<LibrarianService> weakThis(this);
    m_llmService->sendNonStreamingRequest(request, [weakThis, filePath](const QString &response) {
        if (!weakThis) return;
        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
        if (!doc.isArray()) {
            int start = response.indexOf(QLatin1Char('['));
            int end = response.lastIndexOf(QLatin1Char(']'));
            if (start != -1 && end != -1) {
                doc = QJsonDocument::fromJson(response.mid(start, end - start + 1).toUtf8());
            }
        }

        if (doc.isArray()) {
            QJsonArray entities = doc.array();
            for (const QJsonValue &val : entities) {
                QJsonObject obj = val.toObject();
                QString name = obj.value(QStringLiteral("entity")).toString();
                QString type = obj.value(QStringLiteral("type")).toString();
                QJsonObject attrs = obj.value(QStringLiteral("attributes")).toObject();

                if (!name.isEmpty()) {
                    qint64 id = weakThis->m_db->addEntity(name, type, filePath);
                    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                        weakThis->m_db->setAttribute(id, it.key(), it.value().toVariant());
                    }
                    Q_EMIT weakThis->entityUpdated(id);
                }
            }
            weakThis->scanFile(filePath); 
        }
    });
}

void LibrarianService::triggerSemanticReindex()
{
    qDebug() << "Manually triggered full semantic reindex.";
    runSemanticBatch();
}
