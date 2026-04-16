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

#ifndef LIBRARIANSERVICE_H
#define LIBRARIANSERVICE_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QFileSystemWatcher>
#include "librariandatabase.h"

class LLMService;

/**
 * @brief The LibrarianService class handles background project scanning, 
 * data extraction (heuristic and semantic), and database synchronization.
 */
class LibrarianService : public QObject
{
    Q_OBJECT
public:
    explicit LibrarianService(LLMService *llmService, QObject *parent = nullptr);
    ~LibrarianService();

    void setProjectPath(const QString &path);
    bool isPaused() const { return m_paused; }

    LibrarianDatabase* database() const { return m_db; }

public Q_SLOTS:
    void pause();
    void resume();
    void scanAll();
    void scanFile(const QString &filePath);
    void triggerSemanticReindex();

Q_SIGNALS:
    void entityUpdated(qint64 entityId);
    void libraryVariablesChanged(const QMap<QString, QString> &vars);
    void scanningStarted();
    void scanningFinished();
    void errorOccurred(const QString &message);

private Q_SLOTS:
    void onFileChanged(const QString &path);
    void processQueue();
    void runSemanticBatch();

private:
    void extractHeuristic(const QString &filePath);
    void extractSemantic(const QString &filePath);
    
    // Heuristic helpers
    void parseMarkdownTables(const QString &content, const QString &sourceFile);
    void parseMarkdownLists(const QString &content, const QString &sourceFile);

    LLMService *m_llmService;
    LibrarianDatabase *m_db;
    QFileSystemWatcher *m_watcher;
    QString m_projectPath;
    QString m_dbPath;
    
    mutable QRecursiveMutex m_mutex;
    bool m_paused = false;
    QStringList m_pendingFiles;
    
    QTimer *m_processTimer;
    QTimer *m_semanticTimer;
    
    bool m_isScanning = false;
};

#endif // LIBRARIANSERVICE_H
