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

#include "librarianservice.h"
#include "llmservice.h"
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
    qDebug() << "LibrarianService: Setting project path:" << path;
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
        scanAll();
    } else {
        Q_EMIT errorOccurred(m_db->lastError());
    }
}

QSqlDatabase LibrarianService::getDatabase() const
{
    QString connectionName = QStringLiteral("librarian_thread_") + QString::number(size_t(QThread::currentThreadId()));
    if (QSqlDatabase::contains(connectionName)) {
        return QSqlDatabase::database(connectionName);
    }
    
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_dbPath);
    db.open();
    return db;
}

void LibrarianService::pause()
{
    qDebug() << "LibrarianService: Pausing...";
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    m_processTimer->stop();
    m_semanticTimer->stop();
}

void LibrarianService::resume()
{
    qDebug() << "LibrarianService: Resuming...";
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    m_processTimer->start();
    m_semanticTimer->start();
}

void LibrarianService::scanAll()
{
    if (m_paused || m_projectPath.isEmpty()) return;
    
    QDir dir(m_projectPath);
    QStringList filters;
    filters << QStringLiteral("*.md") << QStringLiteral("*.markdown");
    
    QDirIterator it(m_projectPath, filters, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    QMutexLocker locker(&m_mutex);
    while (it.hasNext()) {
        QString file = it.next();
        if (!m_pendingFiles.contains(file)) {
            m_pendingFiles.append(file);
        }
        m_watcher->addPath(file);
    }
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
    if (m_paused || m_pendingFiles.isEmpty() || !m_db->database().isOpen()) return;

    Q_EMIT scanningStarted();
    
    QStringList filesToProcess = m_pendingFiles;
    m_pendingFiles.clear();
    
    QPointer<LibrarianService> weakThis(this);
    QtConcurrent::run([weakThis, filesToProcess]() {
        if (!weakThis) return;

        for (const QString &filePath : filesToProcess) {
            if (!weakThis || weakThis->isPaused()) break;
            weakThis->extractHeuristic(filePath);
        }

        // Finalize back on main thread
        QMetaObject::invokeMethod(weakThis.data(), [weakThis]() {
            if (!weakThis) return;
            
            QMap<QString, QString> libVars;
            QSqlDatabase db = weakThis->getDatabase();
            if (db.isOpen()) {
                QSqlQuery query(db);
                query.exec(QStringLiteral("SELECT e.type, e.name, a.key, a.value FROM entities e "
                                        "JOIN attributes a ON e.id = a.entity_id"));
                while (query.next()) {
                    QString type = query.value(0).toString().replace(QStringLiteral(" "), QString());
                    QString name = query.value(1).toString().replace(QStringLiteral(" "), QString());
                    QString key = query.value(2).toString().replace(QStringLiteral(" "), QString());
                    QString val = query.value(3).toString();
                    libVars.insert(QStringLiteral("%1.%2.%3").arg(type, name, key), val);
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
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    m_db->beginTransaction();
    parseMarkdownTables(content, filePath);
    parseMarkdownLists(content, filePath);
    m_db->commit();
}

void LibrarianService::parseMarkdownTables(const QString &content, const QString &sourceFile)
{
    QRegularExpression separatorRegex(QStringLiteral("^\\|([:\\s-]+\\|)+\\s*$"), QRegularExpression::MultilineOption);
    
    QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1Char('|'))) {
            QStringList headers = lines[i].split(QLatin1Char('|'), Qt::SkipEmptyParts);
            if (!headers.isEmpty() && i + 1 < lines.size() && separatorRegex.match(lines[i+1]).hasMatch()) {
                QString tableName = headers[0].trimmed().toLower().replace(QStringLiteral(" "), QString());
                for (int j = i + 2; j < lines.size() && lines[j].startsWith(QLatin1Char('|')); ++j) {
                    QStringList cells = lines[j].split(QLatin1Char('|'), Qt::SkipEmptyParts);
                    if (cells.size() >= 2) {
                        QString entityName = cells[0].trimmed();
                        qint64 entityId = m_db->addEntity(entityName, tableName, sourceFile);
                        for (int k = 1; k < cells.size() && k < headers.size(); ++k) {
                            QString key = headers[k].trimmed().toLower();
                            QString val = cells[k].trimmed();
                            m_db->setAttribute(entityId, key, val);
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
    QRegularExpression kvRegex(QStringLiteral("^\\s*[-*+]?\\s*([\\w\\s]+):\\s*(.+)$"), QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = kvRegex.globalMatch(content);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1).trimmed();
        QString value = match.captured(2).trimmed();
        
        qint64 globalId = m_db->addEntity(QStringLiteral("Global"), QStringLiteral("property"), sourceFile);
        m_db->setAttribute(globalId, key.toLower(), value);
        Q_EMIT entityUpdated(globalId);
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
