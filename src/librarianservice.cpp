#include "librarianservice.h"
#include "llmservice.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>

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
    
    QString dbPath = QDir(path).filePath(".rpgforge.db");
    if (m_db->open(dbPath)) {
        scanAll();
    } else {
        emit errorOccurred(m_db->lastError());
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
    filters << "*.md" << "*.markdown";
    
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

    emit scanningStarted();
    
    while (!m_pendingFiles.isEmpty()) {
        QString filePath = m_pendingFiles.takeFirst();
        locker.unlock();
        extractHeuristic(filePath);
        locker.relock();
    }
    
    emit scanningFinished();
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
    // For Phase 1, we use basic regex to find Markdown tables.
    // In Phase 2, we should use the cmark-gfm AST for better robustness.
    QRegularExpression tableRegex("^\\|(.+)\\|\\s*$", QRegularExpression::MultilineOption);
    QRegularExpression separatorRegex("^\\|([:\\s-]+\\|)+\\s*$", QRegularExpression::MultilineOption);
    
    QStringList lines = content.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith('|')) {
            // Found a potential table
            QStringList headers = lines[i].split('|', Qt::SkipEmptyParts);
            if (i + 1 < lines.size() && separatorRegex.match(lines[i+1]).hasMatch()) {
                // It's a table with headers
                QString tableName = headers[0].trimmed().toLower().replace(" ", "");
                
                // Extract rows
                for (int j = i + 2; j < lines.size() && lines[j].startsWith('|'); ++j) {
                    QStringList cells = lines[j].split('|', Qt::SkipEmptyParts);
                    if (cells.size() >= 2) {
                        QString entityName = cells[0].trimmed();
                        qint64 entityId = m_db->addEntity(entityName, tableName, sourceFile);
                        
                        for (int k = 1; k < cells.size() && k < headers.size(); ++k) {
                            QString key = headers[k].trimmed().toLower();
                            QString val = cells[k].trimmed();
                            m_db->setAttribute(entityId, key, val);
                        }
                        emit entityUpdated(entityId);
                    }
                }
            }
        }
    }
}

void LibrarianService::parseMarkdownLists(const QString &content, const QString &sourceFile)
{
    // Basic key:value list extraction (e.g., "- Strength: 15")
    QRegularExpression kvRegex("^\\s*[-*+]?\\s*([\\w\\s]+):\\s*(.+)$", QRegularExpression::MultilineOption);
    QRegularExpressionIterator it = kvRegex.globalMatch(content);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1).trimmed();
        QString value = match.captured(2).trimmed();
        
        // Simple heuristic: if key is short, it's an attribute
        // In Phase 2, we link this to the nearest heading as an Entity.
        // For now, we store them as generic 'Global' entities.
        qint64 globalId = m_db->addEntity("Global", "property", sourceFile);
        m_db->setAttribute(globalId, key.toLower(), value);
        emit entityUpdated(globalId);
    }
}

void LibrarianService::runSemanticBatch()
{
    if (m_paused) return;
    
    // In Phase 13, this would trigger LLMService to deep-scan a random file 
    // or a recently modified one that heuristic failed to fully capture.
    qDebug() << "Running Async Semantic Batch...";
}

void LibrarianService::triggerSemanticReindex()
{
    qDebug() << "Manually triggered full semantic reindex.";
    runSemanticBatch();
}
