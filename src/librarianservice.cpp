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
#include "agentgatekeeper.h"
#include "llmservice.h"
#include "projectmanager.h"
#include "debuglog.h"
#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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
#include <QDateTime>
#include <QSettings>

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
    // Deliberately NOT started here: starting before a project is open
    // means the timer ticks against an empty queue forever and (worse)
    // would have ticked against the previous project after a close. We
    // start it from setProjectPath / resume and stop it from pause.

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &LibrarianService::onFileChanged);

    // Pick up newly-added project files (drag-drop, imports, manual file
    // creation) without waiting for the next setProjectPath() call.
    // Queued so we don't run on whatever thread emitted the tree change.
    connect(&ProjectManager::instance(), &ProjectManager::treeStructureChanged,
            this, &LibrarianService::onProjectTreeChanged, Qt::QueuedConnection);

    connect(&AgentGatekeeper::instance(), &AgentGatekeeper::serviceEnabledChanged,
            this, [this](AgentGatekeeper::Service s, bool enabled) {
        if (s != AgentGatekeeper::Service::Librarian) return;
        if (!enabled) {
            {
                QMutexLocker locker(&m_mutex);
                m_pendingFiles.clear();
            }
            pause();
        } else {
            resume();
        }
    });
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
        // A project is now open; the semantic timer can usefully tick.
        if (!m_paused && !m_semanticTimer->isActive()) m_semanticTimer->start();
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
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Librarian)) {
        qDebug() << "Data Extractor: scanAll skipped — disabled for this project.";
        return;
    }
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
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Librarian)) {
        qDebug() << "Data Extractor: scanFile skipped — disabled for this project.";
        return;
    }
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
    if (path.isEmpty()) return;
    // Determine whether this watcher fire actually changed the bytes on
    // disk. A pure mtime touch (e.g. tar -x preserving content) hashes
    // identically to what we already extracted; skip re-extraction in
    // that case.
    if (!QFileInfo::exists(path)) {
        // File deleted from under us. Forget any hash currently mapped to
        // this path — frees the slot for a future create.
        const QString staleHash = m_db ? m_db->hashForPath(path) : QString();
        if (!staleHash.isEmpty()) m_db->forgetExtraction(staleHash);
        return;
    }
    const QString newHash = computeFileHash(path);
    if (newHash.isEmpty()) return;

    const QString oldHash = m_db ? m_db->hashForPath(path) : QString();
    if (!oldHash.isEmpty() && oldHash == newHash) {
        // Bytes unchanged — nothing to do.
        return;
    }
    if (!oldHash.isEmpty() && oldHash != newHash) {
        // Path had different content before; clear old hash row so it
        // doesn't shadow the new one. The new hash will get its own row
        // when extraction runs.
        m_db->forgetExtraction(oldHash);
    }
    scanFile(path);
}

void LibrarianService::onProjectTreeChanged()
{
    if (m_paused || m_projectPath.isEmpty() || !m_db) return;
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Librarian)) return;

    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    int queuedNew = 0;
    int reheldSemantic = 0;
    {
        QMutexLocker locker(&m_mutex);
        for (const QString &file : activeFiles) {
            const QString suffix = QFileInfo(file).suffix().toLower();
            if (suffix != QStringLiteral("md") && suffix != QStringLiteral("markdown")) continue;
            if (!m_watcher->files().contains(file)) m_watcher->addPath(file);

            // Path-only check — DO NOT hash on the GUI thread. If the
            // path is already mapped to a file_extractions row, the
            // worker has nothing new to do for this file. Hashing every
            // .md on every treeStructureChanged emission was the source
            // of the GUI-thread amplification observed live: 92 SHA-256
            // computations and a full DB re-emit per drag/drop or
            // import.
            const QString existingHash = m_db->hashForPath(file);
            if (existingHash.isEmpty()) {
                // Genuinely new path (new file, content-edited file
                // whose hash row was forgotten by onFileChanged, or a
                // moved file we have not yet remapped). Queue for the
                // worker; it will hash, dedup against file_extractions,
                // and route into heuristic + semantic as appropriate.
                if (!m_pendingFiles.contains(file)) {
                    m_pendingFiles.append(file);
                    ++queuedNew;
                }
            } else {
                // Path is known. If the row is heuristic-done but
                // semantic-pending, just make sure the file is in the
                // semantic queue. Do not enter m_pendingFiles — the
                // heuristic already ran for this content hash and the
                // worker would otherwise needlessly hash + path-update.
                bool h = false, s = false;
                if (m_db->getExtractionState(existingHash, &h, &s) && !s) {
                    if (!m_pendingSemantic.contains(file)) {
                        m_pendingSemantic.append(file);
                        ++reheldSemantic;
                    }
                }
            }
        }
    }
    if (queuedNew > 0 || reheldSemantic > 0) {
        RPGFORGE_DLOG("VARS") << "LibrarianService::onProjectTreeChanged:"
                              << "new=" << queuedNew
                              << "rehold-semantic=" << reheldSemantic;
        if (queuedNew > 0) m_processTimer->start();
    }
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

        // Track whether any file in this batch actually had heuristic work
        // done. Pure path-update / both-flags-set passes are no-ops as far
        // as the {{var}} table is concerned, so the post-batch
        // entities-x-attributes re-query and libraryVariablesChanged emit
        // can be skipped — that emit was visibly stalling the GUI on
        // large projects each time onProjectTreeChanged fired.
        int filesActuallyExtracted = 0;

        for (const QString &filePath : filesToProcess) {
            if (!weakThis || weakThis->isPaused()) break;

            // Capture the hash ONCE before extraction. We recompute it
            // AFTER extraction to detect a mid-flight save: if the bytes
            // changed under us, the watcher already / will re-queue,
            // and writing the heuristic-done row keyed on the OLD hash
            // would resurrect a now-stale entry.
            const QString hashBefore = LibrarianService::computeFileHash(filePath);
            if (hashBefore.isEmpty()) continue;

            bool h = false, s = false;
            LibrarianDatabase *db = weakThis->m_db;
            const bool known = db->getExtractionState(hashBefore, &h, &s);

            if (known && h && s) {
                // Both passes already done for this content — just
                // refresh the path mapping (handles a move/rename of an
                // unchanged file).
                db->updateExtractionPath(hashBefore, filePath);
                continue;
            }

            if (!h) {
                weakThis->extractHeuristic(filePath);
                // Mid-flight save guard: if the file changed during
                // extractHeuristic, skip the write — the watcher's
                // onFileChanged has already / will re-queue under the
                // new hash, and writing the old hash would resurrect a
                // stale row.
                const QString hashAfter = LibrarianService::computeFileHash(filePath);
                if (hashAfter != hashBefore) {
                    RPGFORGE_DLOG("VARS")
                        << "LibrarianService::processQueue: file changed mid-extraction;"
                        << "discarding heuristic result for" << filePath;
                    continue;
                }
                db->recordExtractionState(hashBefore, filePath, /*heuristicDone=*/true, s);
                ++filesActuallyExtracted;
            } else {
                // Heuristic already done for this hash — but path may
                // have moved.
                db->updateExtractionPath(hashBefore, filePath);
            }

            // Hand off to the semantic pipeline. The LLM tick rate is
            // controlled by m_semanticTimer.
            if (!s) {
                QMutexLocker locker(&weakThis->m_mutex);
                if (!weakThis->m_pendingSemantic.contains(filePath)) {
                    weakThis->m_pendingSemantic.append(filePath);
                }
            }
        }

        // Release this worker's DB connection on the owning thread before
        // returning to the pool — otherwise main thread tears it down at
        // shutdown and emits a thread-mismatch warning.
        if (weakThis && weakThis->m_db) {
            weakThis->m_db->closeCurrentThreadConnection();
        }

        // Finalize back on main thread
        QMetaObject::invokeMethod(weakThis.data(), [weakThis, filesActuallyExtracted]() {
            if (!weakThis) return;

            // No-op pass: every file in the batch was already
            // (h=1, s=0/1) for its content hash, so nothing in the
            // entities/attributes tables changed. Skip the full re-query
            // and emit — but DO fire scanningFinished so any progress UI
            // dismisses.
            //
            // Exception: the very first emit per service-lifetime is
            // mandatory so freshly-constructed consumers (VariablesPanel
            // when a project is reopened, tests with a fresh QSignalSpy)
            // see the current DB contents. Tracked via m_hasEmittedVarsOnce.
            if (filesActuallyExtracted == 0 && weakThis->m_hasEmittedVarsOnce) {
                Q_EMIT weakThis->scanningFinished();
                return;
            }

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
            weakThis->m_hasEmittedVarsOnce = true;
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
    if (m_paused || !m_llmService) return;
    if (!AgentGatekeeper::instance().isEnabled(AgentGatekeeper::Service::Librarian)) return;
    if (!m_db || !m_db->database().isOpen()) return;

    QString next;
    QString nextHash;
    {
        QMutexLocker locker(&m_mutex);
        // Backpressure: if the previous extractSemantic hasn't completed
        // yet, leave the queue alone. The LLM is async and the timer
        // ticks at a fixed cadence; without this guard, a slow model
        // would have N concurrent in-flight calls all racing each
        // other's DB writes.
        if (m_semanticInFlight) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        // Files we skipped because they're in backoff. Re-append after the
        // pop loop so they retain their place in the queue (just behind
        // anything we left untouched) and get re-considered on the next
        // tick. Without this they'd silently fall out of the queue.
        QStringList deferred;
        while (!m_pendingSemantic.isEmpty()) {
            const QString candidate = m_pendingSemantic.takeFirst();
            if (!QFileInfo::exists(candidate)) continue;
            const QString hash = computeFileHash(candidate);
            if (hash.isEmpty()) continue;
            bool h = false, s = false;
            m_db->getExtractionState(hash, &h, &s);
            if (s) {
                // Already done semantically — just keep the path mapping
                // up-to-date (handles a file that was moved while it sat
                // in the queue).
                m_db->updateExtractionPath(hash, candidate);
                continue;
            }
            const qint64 scheduled = m_semanticNextAttemptAt.value(hash, 0);
            if (now < scheduled) {
                // In backoff after a recent strike — leave it in the queue
                // and try the next candidate. The 30s tick will revisit.
                deferred.append(candidate);
                continue;
            }
            next = candidate;
            nextHash = hash;
            break;
        }
        // Restore deferred (backed-off) candidates to the front of the
        // queue so they're seen first on the next tick once their backoff
        // window elapses. Anything we successfully chose has already been
        // taken; everything we couldn't choose-because-done was discarded.
        if (!deferred.isEmpty()) {
            m_pendingSemantic = deferred + m_pendingSemantic;
        }
        // Edge case: every file in the queue is currently backed off.
        // `next` is empty; we'll fall through and return below without
        // dispatching, and the next tick will retry.
    }
    if (next.isEmpty()) return;
    // Mark the call in-flight BEFORE dispatching. extractSemantic's
    // completion lambda clears the flag on both branches (parse-fail and
    // success) so the next tick can proceed.
    {
        QMutexLocker locker(&m_mutex);
        m_semanticInFlight = true;
    }
    // Pass the hash captured at submission time. extractSemantic must
    // not recompute it inside the LLM completion callback — see the
    // function comment for why.
    extractSemantic(next, nextHash);
}

void LibrarianService::extractSemantic(const QString &filePath, const QString &submissionHash)
{
    if (!m_llmService) return;
    if (submissionHash.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Best-effort cap on document size. Models with smaller context windows
    // (e.g. local LM Studio loadouts) reject oversized prompts deterministically;
    // retrying just burns LLM budget. v1: skip oversized files and retire them
    // from the semantic queue. Future work will chunk + merge.
    const int maxChars = QSettings(QStringLiteral("RPGForge"),
                                    QStringLiteral("RPGForge"))
        .value(QStringLiteral("librarian/semantic_max_chars"), 30000).toInt();
    if (content.size() > maxChars) {
        qWarning().noquote()
            << "LibrarianService: skipping oversized file for semantic extraction:"
            << filePath << "(" << content.size() << "chars >"
            << maxChars << "limit). Marking semantic_done; configure"
            << "librarian/semantic_max_chars to override.";
        if (m_db) {
            m_db->recordExtractionState(submissionHash, filePath,
                                         /*heuristicDone=*/true,
                                         /*semanticDone=*/true);
        }
        {
            QMutexLocker locker(&m_mutex);
            m_semanticInFlight = false;
            m_semanticNextAttemptAt.remove(submissionHash);
        }
        return;
    }

    // Extended prompt for the relationship-graph work. The model is asked to
    // emit a richer payload than the legacy {entity, type, attributes}: it
    // also provides a one-line summary, common aliases, tags, an optional
    // containment parent, an optional era, and typed relationships to other
    // named entities. The librarian then does a three-pass write so that
    // relationship targets resolve correctly across documents that introduce
    // entities in different orders.
    //
    // The "type" vocabulary is the user-approved scope:
    //   character, place, time, concept, rule, myth, magic
    // (legacy categories like Monster/Item/Class/Spell are still accepted
    // and stored verbatim — graph filters use whatever the LLM emits).
    QString prompt = QStringLiteral(
        "You are the Librarian Agent for RPG Forge. Extract a relationship graph "
        "from the supplied text. Output ONLY a JSON array — no markdown fences, "
        "no commentary.\n\n"
        "Each array element describes one named entity:\n"
        "  - entity         : the canonical name (string, required).\n"
        "  - type           : one of: character, place, time, concept, rule, myth, "
        "magic. Required.\n"
        "  - summary        : ONE short sentence (≤ 25 words) describing this entity. Required.\n"
        "  - aliases        : array of nicknames, titles, alternate spellings (e.g. "
        "[\"Ryz\", \"Captain Ryzen\"]). Optional; empty array if none.\n"
        "  - tags           : array of secondary descriptors (e.g. [\"protagonist\", "
        "\"sailor\"]). Optional.\n"
        "  - parent         : name of the containing entity (a Place inside a Region, "
        "a Sub-rule inside a Rule). Use null when no containment applies.\n"
        "  - era            : the in-world era / period this entity belongs to. Use "
        "null when irrelevant.\n"
        "  - attributes     : object of free-form key→value descriptive properties.\n"
        "  - relationships  : array of {\"target\": \"Name\", \"type\": \"verb_phrase\"} "
        "objects. Use existing entity names as targets. Examples: \"friend\", "
        "\"member_of\", \"located_in\", \"rules\", \"manifests\", \"opposes\".\n\n"
        "Schema example:\n"
        "[{\n"
        "  \"entity\": \"Ryzen\", \"type\": \"character\",\n"
        "  \"summary\": \"A young sailor whose journey leads him into the Phoenix Cult's orbit.\",\n"
        "  \"aliases\": [\"Ryz\"], \"tags\": [\"protagonist\", \"sailor\"],\n"
        "  \"parent\": null, \"era\": null,\n"
        "  \"attributes\": {\"role\": \"sailor\"},\n"
        "  \"relationships\": [\n"
        "    {\"target\": \"Sah'mokhan\", \"type\": \"friend\"},\n"
        "    {\"target\": \"Phoenix Cult\", \"type\": \"member_of\"}\n"
        "  ]\n"
        "}]\n\n"
        "Rules:\n"
        " - Do not invent entities not implied by the text.\n"
        " - Reuse the same canonical name for repeat mentions; list nicknames as aliases.\n"
        " - When a relationship's target is not also an entity in your output, still "
        "include it — it may resolve against an entity from a previous document.\n\n"
        "Text to analyze:\n"
    ) + content;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    // Settings dialog (AI Services tab) writes the librarian provider
    // and model under librarian/librarian_provider and
    // librarian/librarian_model — that is the keyPrefix/id_property
    // pattern used by every agent row. Reading librarian/provider /
    // librarian/model (legacy keys) silently ignored every change the
    // user made in Settings. Fall back to the legacy keys ONLY if the
    // new keys are absent so existing user values aren't lost.
    const int provIdx = settings.value(
        QStringLiteral("librarian/librarian_provider"),
        settings.value(QStringLiteral("librarian/provider"),
                       settings.value(QStringLiteral("llm/provider"), 0))).toInt();
    LLMProvider provider = static_cast<LLMProvider>(provIdx);
    QString model = settings.value(
        QStringLiteral("librarian/librarian_model"),
        settings.value(QStringLiteral("librarian/model"),
                       provider == LLMProvider::Ollama
                           ? QStringLiteral("llama3")
                           : QString())).toString();

    LLMRequest request;
    request.provider = provider;
    request.model = model;
    request.serviceName = i18n("Librarian Agent");
    request.settingsKey = QStringLiteral("librarian/librarian_model");
    request.messages << LLMMessage{QStringLiteral("system"), QStringLiteral("You extract structured RPG data as JSON.")};
    request.messages << LLMMessage{QStringLiteral("user"), prompt};
    request.stream = false;

    QPointer<LibrarianService> weakThis(this);
    // Capture submissionHash by value so every write below — strike
    // bump, completion record, retry — keys off the SAME content the
    // LLM was shown, even if the user saves the file before the
    // response arrives. Recomputing the hash inside this callback would
    // mis-attribute the analysis of the OLD content to the NEW content,
    // and the new content would never be re-extracted.
    m_llmService->sendNonStreamingRequest(request,
        [weakThis, filePath, submissionHash](const QString &response) {
        if (!weakThis) return;
        // Backpressure: the call we set m_semanticInFlight=true for is
        // resolving NOW. Clear the flag on every exit branch (parse-fail
        // give-up, parse-fail strike-bump, success) so the next 30s
        // semantic-timer tick can pop the queue. If `weakThis` is gone
        // (service destroyed mid-flight) the flag dies with the service,
        // so no leak.
        auto clearInFlight = [weakThis]() {
            if (!weakThis) return;
            QMutexLocker locker(&weakThis->m_mutex);
            weakThis->m_semanticInFlight = false;
        };
        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
        if (!doc.isArray()) {
            int start = response.indexOf(QLatin1Char('['));
            int end = response.lastIndexOf(QLatin1Char(']'));
            if (start != -1 && end != -1) {
                doc = QJsonDocument::fromJson(response.mid(start, end - start + 1).toUtf8());
            }
        }
        if (!doc.isArray()) {
            // Parse failure. Bump the per-hash strike counter on the
            // file_extractions row; after 3 strikes mark semantic_done
            // so this file stops monopolising the LLM queue. Persisted
            // (not in-memory) so a permanently-broken file can't burn
            // 3 LLM calls per restart.
            if (weakThis->m_db) {
                weakThis->m_db->incrementSemanticAttempts(submissionHash);
                const int strikes = weakThis->m_db->getSemanticAttempts(submissionHash);
                if (strikes >= 3) {
                    qWarning().noquote()
                        << "LibrarianService: giving up on semantic extraction for"
                        << filePath << "after 3 LLM-response parse failures";
                    weakThis->m_db->recordExtractionState(
                        submissionHash, filePath, /*heuristicDone=*/true, /*semanticDone=*/true);
                    weakThis->m_db->resetSemanticAttempts(submissionHash);
                    QMutexLocker locker(&weakThis->m_mutex);
                    weakThis->m_semanticNextAttemptAt.remove(submissionHash);
                } else {
                    qWarning().noquote()
                        << "LibrarianService: LLM response not parseable for"
                        << filePath << "(strike" << strikes << "/ 3)";
                    QMutexLocker locker(&weakThis->m_mutex);
                    // 5-minute backoff before this hash is eligible to be
                    // popped again. Without it the 30s tick would re-pop
                    // this same broken file every cycle and burn LLM budget.
                    weakThis->m_semanticNextAttemptAt[submissionHash] =
                        QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000;
                    if (!weakThis->m_pendingSemantic.contains(filePath)) {
                        weakThis->m_pendingSemantic.append(filePath);
                    }
                }
            }
            clearInFlight();
            return;
        }

        const QJsonArray entities = doc.array();
        LibrarianDatabase *db = weakThis->m_db;

        // Pass 1: upsert each entity (reuse existing by canonical name when
        // present so repeat mentions across files don't fragment), set
        // summary / era / attributes, register the canonical name + aliases.
        // We collect per-element ids so passes 2 and 3 don't have to re-resolve.
        QVector<qint64> elementIds(entities.size(), -1);

        for (int i = 0; i < entities.size(); ++i) {
            const QJsonObject obj = entities[i].toObject();
            const QString name = obj.value(QStringLiteral("entity")).toString().trimmed();
            const QString type = obj.value(QStringLiteral("type")).toString().trimmed();
            if (name.isEmpty()) continue;

            // Dedupe by name across documents. The first document to mention
            // an entity creates the row; subsequent mentions reuse it. The
            // alias index (which addEntity populates with the canonical name)
            // makes this a single index hit.
            qint64 id = db->resolveEntityByName(name);
            if (id < 0) {
                id = db->addEntity(name, type, filePath);
                if (id < 0) continue;
            } else if (!type.isEmpty() && db->getEntityType(id).isEmpty()) {
                // Patch in the type if the original creator left it blank.
                db->updateEntity(id, name, type);
            }
            elementIds[i] = id;

            const QString summary = obj.value(QStringLiteral("summary")).toString().trimmed();
            if (!summary.isEmpty()) db->setEntitySummary(id, summary);
            const QString era = obj.value(QStringLiteral("era")).toString().trimmed();
            if (!era.isEmpty()) db->setEntityEra(id, era);

            const QJsonObject attrs = obj.value(QStringLiteral("attributes")).toObject();
            for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                db->setAttribute(id, it.key(), it.value().toVariant());
            }
        }

        // Pass 2: aliases + tags. Done after pass 1 so that an entity
        // referenced as a relationship target by its alias can still be
        // resolved during pass 3.
        for (int i = 0; i < entities.size(); ++i) {
            const qint64 id = elementIds[i];
            if (id < 0) continue;
            const QJsonObject obj = entities[i].toObject();

            const QJsonArray aliases = obj.value(QStringLiteral("aliases")).toArray();
            for (const QJsonValue &av : aliases) {
                const QString a = av.toString().trimmed();
                if (!a.isEmpty()) db->addAlias(id, a, /*isPrimary=*/false);
            }

            const QJsonArray tags = obj.value(QStringLiteral("tags")).toArray();
            for (const QJsonValue &tv : tags) {
                const QString t = tv.toString().trimmed();
                if (!t.isEmpty()) db->addTag(id, t);
            }
        }

        // Pass 3: containment + peer relationships. Both go through the
        // alias index. Targets that don't resolve get logged + skipped —
        // they'll typically resolve when the document that introduces
        // them gets processed.
        for (int i = 0; i < entities.size(); ++i) {
            const qint64 id = elementIds[i];
            if (id < 0) continue;
            const QJsonObject obj = entities[i].toObject();

            const QString parentName = obj.value(QStringLiteral("parent")).toString().trimmed();
            if (!parentName.isEmpty()) {
                const qint64 parentId = db->resolveEntityByName(parentName);
                if (parentId > 0) {
                    db->setEntityParent(id, parentId);
                } else {
                    // Defer: queue containment with the synthetic
                    // "is_part_of" relationship so resolvePendingRelationships
                    // can apply setEntityParent once the parent document
                    // gets ingested. Same deferral pattern as peer edges
                    // below — silently dropping these was the reason
                    // cross-document containment never appeared in the
                    // graph.
                    db->queuePendingRelationship(id, parentName,
                                                  QStringLiteral("is_part_of"),
                                                  filePath, /*strength=*/1.0);
                }
            }

            const QJsonArray rels = obj.value(QStringLiteral("relationships")).toArray();
            for (const QJsonValue &rv : rels) {
                const QJsonObject ro = rv.toObject();
                const QString targetName = ro.value(QStringLiteral("target")).toString().trimmed();
                const QString relType = ro.value(QStringLiteral("type")).toString().trimmed();
                if (targetName.isEmpty() || relType.isEmpty()) continue;

                qint64 targetId = db->resolveEntityByName(targetName);
                if (targetId < 0) {
                    // Defer: queue the unresolved edge so it lands as
                    // soon as the document that introduces the target
                    // gets ingested. The naive log-and-drop this
                    // replaces was the reason cross-document edges
                    // never appeared in the graph.
                    db->queuePendingRelationship(id, targetName, relType,
                                                  filePath, /*strength=*/0.7);
                    continue;
                }
                if (targetId == id) continue;     // self-loop — drop
                db->upsertRelationship(id, targetId, relType, filePath, /*line=*/0, /*strength=*/0.7);
            }

            Q_EMIT weakThis->entityUpdated(id);
        }

        // Sweep the deferred-relationship queue: this batch may have
        // introduced entities that earlier batches couldn't resolve.
        const int resolved = db->resolvePendingRelationships();
        if (resolved > 0) {
            RPGFORGE_DLOG("VARS")
                << "LibrarianService: resolved" << resolved
                << "pending cross-document relationship(s) after extracting"
                << filePath;
        }

        // Mark this file's semantic pass complete keyed by the
        // SUBMISSION hash — the content the LLM was actually shown. If
        // the file has since changed on disk, the watcher's
        // onFileChanged has already / will queue the new hash; writing
        // the new hash here would mis-attribute this old-content
        // analysis to the new content.
        db->recordExtractionState(submissionHash, filePath,
                                   /*heuristicDone=*/true,
                                   /*semanticDone=*/true);
        db->resetSemanticAttempts(submissionHash);
        {
            QMutexLocker locker(&weakThis->m_mutex);
            weakThis->m_semanticNextAttemptAt.remove(submissionHash);
        }
        clearInFlight();
    });
}

void LibrarianService::triggerSemanticReindex()
{
    qDebug() << "Manually triggered full semantic reindex.";
    if (!m_db) return;

    // Force every file back through the LLM pass:
    //   1. drop file_extractions rows for files that no longer exist
    //      (otherwise they'd sit in the queue forever once we clear
    //      semantic_done);
    //   2. clear semantic_done across the remaining rows so they re-run;
    //   3. clear the persisted strike counter so previously-given-up
    //      files get another chance;
    //   4. repopulate the in-memory queue from the active project files;
    //   5. kick the timer.
    const int pruned = m_db->garbageCollectMissingFiles();
    if (pruned > 0) {
        RPGFORGE_DLOG("VARS")
            << "LibrarianService::triggerSemanticReindex: pruned"
            << pruned << "missing-file extraction row(s)";
    }
    m_db->clearAllSemanticDone();
    m_db->clearAllSemanticAttempts();

    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    {
        QMutexLocker locker(&m_mutex);
        // Clear in-memory backoff floors — full reindex means every file
        // gets a fresh chance at the LLM, regardless of when the last
        // strike happened.
        m_semanticNextAttemptAt.clear();
        for (const QString &file : activeFiles) {
            const QString suffix = QFileInfo(file).suffix().toLower();
            if (suffix != QStringLiteral("md") && suffix != QStringLiteral("markdown")) continue;
            if (!m_pendingSemantic.contains(file)) m_pendingSemantic.append(file);
        }
    }
    if (!m_paused && !m_semanticTimer->isActive()) m_semanticTimer->start();
    runSemanticBatch();
}

QString LibrarianService::computeFileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "LibrarianService::computeFileHash: cannot open"
                             << path << ":" << file.errorString();
        return QString();
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    constexpr qint64 kChunk = 64 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunk);
        if (chunk.isEmpty()) break;
        hasher.addData(chunk);
    }
    file.close();
    return QString::fromLatin1(hasher.result().toHex());
}
