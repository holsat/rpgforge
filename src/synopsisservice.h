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

#ifndef SYNOPSISSERVICE_H
#define SYNOPSISSERVICE_H

#include <QObject>
#include <QString>
#include <QQueue>
#include <QSet>
#include <QMutex>

#include "treenodesnapshot.h"

/**
 * @brief Background AI Agent that automatically generates synopses for files and folders.
 *
 * Consumes the project tree via ProjectManager snapshots only — the service
 * never holds a ProjectTreeModel pointer and is no longer friended on
 * ProjectManager. Write-back to the tree is routed through
 * ProjectManager::setNodeSynopsis() so dataChanged signals fire and the
 * project is persisted automatically.
 */
class SynopsisService : public QObject
{
    Q_OBJECT

public:
    static SynopsisService& instance();

    /**
     * @brief Requests a synopsis update for a specific file path.
     * @param relativePath Path relative to project root.
     * @param force If true, regenerates even if synopsis exists.
     */
    void requestUpdate(const QString &relativePath, bool force = false);

    /**
     * @brief Cancels any pending or active request for the given path.
     */
    void cancelRequest(const QString &relativePath);

    /**
     * @brief Scans the entire project and requests updates for all items with missing synopses.
     */
    void scanProject();

    /**
     * @brief Pauses processing of the synopsis queue.
     */
    void pause();

    /**
     * @brief Resumes processing of the synopsis queue.
     */
    void resume();

private Q_SLOTS:
    void processNext();

private:
    explicit SynopsisService(QObject *parent = nullptr);
    ~SynopsisService() override;

    void scanSnapshot(const TreeNodeSnapshot &node, bool inManuscript);
    void updateFileSynopsis(const QString &relativePath, const QString &content);
    void updateFolderSynopsis(const QString &relativePath,
                              const QStringList &childSynopses);

    QQueue<QString> m_queue;
    QSet<QString> m_activeRequests;
    bool m_isProcessing = false;
    bool m_paused = false;
    QMutex m_mutex;
};

#endif // SYNOPSISSERVICE_H
