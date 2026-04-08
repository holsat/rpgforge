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

#include "synopsisservice.h"
#include "projectmanager.h"
#include "projecttreemodel.h"
#include "llmservice.h"
#include "variablemanager.h"

#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <KLocalizedString>

SynopsisService& SynopsisService::instance()
{
    static SynopsisService inst;
    return inst;
}

SynopsisService::SynopsisService(QObject *parent)
    : QObject(parent)
{
}

SynopsisService::~SynopsisService() = default;

void SynopsisService::setModel(ProjectTreeModel *model)
{
    m_model = model;
}

void SynopsisService::requestUpdate(const QString &relativePath, bool force)
{
    if (!m_model || relativePath.isEmpty()) return;
    
    ProjectTreeItem *item = m_model->findItem(relativePath);
    if (!item) return;

    if (!force && !item->synopsis.isEmpty()) return;

    if (!m_queue.contains(relativePath) && !m_activeRequests.contains(relativePath)) {
        m_queue.enqueue(relativePath);
    }

    if (!m_isProcessing && !m_paused) {
        m_isProcessing = true;
        QTimer::singleShot(1000, this, &SynopsisService::processNext);
    }
}

void SynopsisService::cancelRequest(const QString &relativePath)
{
    m_queue.removeAll(relativePath);
    m_activeRequests.remove(relativePath);
}

void SynopsisService::scanProject()
{
    if (!m_model || !ProjectManager::instance().isProjectOpen() || m_paused) return;

    QList<ProjectTreeItem*> items;
    std::function<void(ProjectTreeItem*)> scan = [&](ProjectTreeItem *item) {
        if (item != m_model->itemFromIndex(QModelIndex())) {
            items.append(item);
        }
        for (auto *child : item->children) {
            scan(child);
        }
    };

    scan(m_model->itemFromIndex(QModelIndex()));

    // Prioritize files to build leaf-level data first
    for (auto *item : items) {
        if (item->type == ProjectTreeItem::File) {
            if (item->synopsis.isEmpty() && !item->path.isEmpty()) requestUpdate(item->path);
        }
    }
    
    // Then request folders (they will wait for children in updateFolderSynopsis)
    for (auto *item : items) {
        if (item->type == ProjectTreeItem::Folder) {
            if (item->synopsis.isEmpty()) requestUpdate(item->path);
        }
    }
}

void SynopsisService::pause()
{
    m_paused = true;
}

void SynopsisService::resume()
{
    if (!m_paused) return;
    m_paused = false;
    if (!m_isProcessing && !m_queue.isEmpty()) {
        m_isProcessing = true;
        processNext();
    }
}

void SynopsisService::processNext()
{
    if (m_paused) {
        m_isProcessing = false;
        return;
    }

    if (m_queue.isEmpty()) {
        m_isProcessing = false;
        return;
    }

    QString relPath = m_queue.dequeue();
    ProjectTreeItem *item = m_model->findItem(relPath);
    
    if (!item) {
        // Item was deleted while in queue, skip to next immediately
        processNext();
        return;
    }

    m_activeRequests.insert(relPath);

    if (item->type == ProjectTreeItem::File) {
        QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);
        if (!QFile::exists(fullPath)) {
            m_activeRequests.remove(relPath);
            processNext();
            return;
        }

        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = VariableManager::stripMetadata(QString::fromUtf8(file.readAll()));
            updateFileSynopsis(item, content);
        } else {
            m_activeRequests.remove(relPath);
            processNext();
        }
    } else {
        updateFolderSynopsis(item);
    }
}

void SynopsisService::updateFileSynopsis(ProjectTreeItem *item, const QString &content)
{
    LLMRequest req;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("synopsis/synopsis_file_provider"), 
                                                           settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    req.model = settings.value(QStringLiteral("synopsis/synopsis_file_model")).toString();
    req.stream = false;
    
    LLMMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = settings.value(QStringLiteral("synopsis/file_prompt"),
        QStringLiteral("You are a senior RPG editor. Write a one-sentence hook/synopsis for this scene or document. Be atmospheric and concise.")).toString();
    
    LLMMessage user;
    user.role = QStringLiteral("user");
    user.content = i18n("Document Content:\n\n%1\n\nTask: Write a one-sentence synopsis.", content);
    
    req.messages << sys << user;

    QString relPath = item->path;
    LLMService::instance().sendNonStreamingRequest(req, [this, relPath](const QString &response) {
        ProjectTreeItem *target = m_model->findItem(relPath);
        if (target) {
            QModelIndex idx = m_model->indexForItem(target);
            if (idx.isValid()) {
                m_model->setData(idx, response.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')), ProjectTreeModel::SynopsisRole);
                ProjectManager::instance().saveProject();
                
                // Trigger parent update immediately
                if (target->parent && target->parent != m_model->itemFromIndex(QModelIndex())) {
                    requestUpdate(target->parent->path, true);
                }
            }
        }
        m_activeRequests.remove(relPath);
        processNext();
    });
}

void SynopsisService::updateFolderSynopsis(ProjectTreeItem *item)
{
    QString childSynopses;
    for (auto *child : item->children) {
        if (!child->synopsis.isEmpty()) {
            childSynopses += QStringLiteral("- %1: %2\n").arg(child->name, child->synopsis);
        }
    }

    if (childSynopses.isEmpty() && !item->children.isEmpty()) {
        m_activeRequests.remove(item->path);
        processNext();
        return;
    }

    LLMRequest req;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("synopsis/synopsis_folder_provider"), 
                                                           settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    req.model = settings.value(QStringLiteral("synopsis/synopsis_folder_model")).toString();
    req.stream = false;
    
    LLMMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = settings.value(QStringLiteral("synopsis/folder_prompt"),
        QStringLiteral("You are an RPG project manager. Write a one-sentence summary for this folder (e.g. 'A collection of character backgrounds' or 'The core mechanics of combat').")).toString();
    
    LLMMessage user;
    user.role = QStringLiteral("user");
    user.content = i18n("Folder: %1\nContents:\n%2\n\nTask: Write a one-sentence summary.", item->name, childSynopses);
    
    req.messages << sys << user;

    QString relPath = item->path;
    LLMService::instance().sendNonStreamingRequest(req, [this, relPath](const QString &response) {
        ProjectTreeItem *target = m_model->findItem(relPath);
        if (target) {
            QModelIndex idx = m_model->indexForItem(target);
            if (idx.isValid()) {
                m_model->setData(idx, response.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')), ProjectTreeModel::SynopsisRole);
                ProjectManager::instance().saveProject();
                
                // Bubble up to grandparent
                if (target->parent && target->parent != m_model->itemFromIndex(QModelIndex())) {
                    requestUpdate(target->parent->path, true);
                }
            }
        }
        m_activeRequests.remove(relPath);
        processNext();
    });
}
