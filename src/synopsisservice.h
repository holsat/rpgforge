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

class ProjectTreeModel;
struct ProjectTreeItem;

/**
 * @brief Background AI Agent that automatically generates synopses for files and folders.
 */
class SynopsisService : public QObject
{
    Q_OBJECT

public:
    static SynopsisService& instance();

    /**
     * @brief Sets the project tree model to use for finding and updating items.
     */
    void setModel(ProjectTreeModel *model);

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

    void updateFolderSynopsis(ProjectTreeItem *folder);
    void updateFileSynopsis(ProjectTreeItem *file, const QString &content);

    ProjectTreeModel *m_model = nullptr;
    QQueue<QString> m_queue;
    QSet<QString> m_activeRequests;
    bool m_isProcessing = false;
    bool m_paused = false;
};

#endif // SYNOPSISSERVICE_H
