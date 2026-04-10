#include "librarianservice.h"
#include "llmservice.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
#include <QMutexLocker>

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
    QMutexLocker locker(&m_mutex);
    m_projectPath = path;
    
    // Clear old watches
    QStringList watched = m_watcher->files();
    if (!watched.isEmpty()) {
        m_watcher->removePaths(watched);
    }
    
    QString dbPath = QDir(path).filePath(QStringLiteral(".rpgforge.db"));
    if (m_db->open(dbPath)) {
        scanAll();
    } else {
        Q_EMIT errorOccurred(m_db->lastError());
    }
}

void LibrarianService::pause()
{
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    m_processTimer->stop();
    m_semanticTimer->stop();
    qDebug() << "Librarian Service Paused.";
}

void LibrarianService::resume()
{
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    m_processTimer->start();
    m_semanticTimer->start();
    qDebug() << "Librarian Service Resumed.";
}

void LibrarianService::scanAll()
{
    if (m_paused) return;
    
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
    if (m_paused || m_pendingFiles.isEmpty()) return;

    Q_EMIT scanningStarted();
    
    while (!m_pendingFiles.isEmpty()) {
        QString filePath = m_pendingFiles.takeFirst();
        locker.unlock();
        extractHeuristic(filePath);
        locker.relock();
    }
    
    // Collect all variables from DB for the VariableManager
    QMap<QString, QString> libVars;
    QSqlQuery query(m_db->database());
    query.exec(QStringLiteral("SELECT e.type, e.name, a.key, a.value FROM entities e "
                              "JOIN attributes a ON e.id = a.entity_id"));
    while (query.next()) {
        QString type = query.value(0).toString().replace(QStringLiteral(" "), QString());
        QString name = query.value(1).toString().replace(QStringLiteral(" "), QString());
        QString key = query.value(2).toString().replace(QStringLiteral(" "), QString());
        QString val = query.value(3).toString();
        
        // Format: type.name.key
        libVars.insert(QStringLiteral("%1.%2.%3").arg(type, name, key), val);
    }
    Q_EMIT libraryVariablesChanged(libVars);

    Q_EMIT scanningFinished();
}

void LibrarianService::extractHeuristic(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Heuristic extraction
    parseMarkdownTables(content, filePath);
    parseMarkdownLists(content, filePath);
}

void LibrarianService::parseMarkdownTables(const QString &content, const QString &sourceFile)
{
    QRegularExpression separatorRegex(QStringLiteral("^\\|([:\\s-]+\\|)+\\s*$"), QRegularExpression::MultilineOption);
    
    QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1Char('|'))) {
            // Found a potential table
            QStringList headers = lines[i].split(QLatin1Char('|'), Qt::SkipEmptyParts);
            if (i + 1 < lines.size() && separatorRegex.match(lines[i+1]).hasMatch()) {
                // It's a table with headers
                QString tableName = headers[0].trimmed().toLower().replace(QStringLiteral(" "), QString());
                
                // Extract rows
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
    if (m_paused) return;
    qDebug() << "Running Async Semantic Batch...";
}

void LibrarianService::triggerSemanticReindex()
{
    qDebug() << "Manually triggered full semantic reindex.";
    runSemanticBatch();
}
void LibrarianService::extractSemantic(const QString &filePath) { (void)filePath; }
