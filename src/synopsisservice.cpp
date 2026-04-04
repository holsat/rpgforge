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
#include <QTimer>
#include <QDebug>
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
    if (!m_model) return;
    
    ProjectTreeItem *item = m_model->findItem(relativePath);
    if (!item) return;

    if (!force && !item->synopsis.isEmpty()) return;

    if (!m_queue.contains(relativePath) && !m_activeRequests.contains(relativePath)) {
        m_queue.enqueue(relativePath);
    }

    if (!m_isProcessing) {
        m_isProcessing = true;
        QTimer::singleShot(1000, this, &SynopsisService::processNext);
    }
}

void SynopsisService::scanProject()
{
    if (!m_model || !ProjectManager::instance().isProjectOpen()) return;

    std::function<void(ProjectTreeItem*)> scan = [&](ProjectTreeItem *item) {
        if (item->synopsis.isEmpty() && !item->path.isEmpty()) {
            requestUpdate(item->path);
        }
        for (auto *child : item->children) {
            scan(child);
        }
    };

    scan(m_model->itemFromIndex(QModelIndex()));
}

void SynopsisService::processNext()
{
    if (m_queue.isEmpty()) {
        m_isProcessing = false;
        return;
    }

    QString relPath = m_queue.dequeue();
    ProjectTreeItem *item = m_model->findItem(relPath);
    
    if (!item) {
        QTimer::singleShot(100, this, &SynopsisService::processNext);
        return;
    }

    m_activeRequests.insert(relPath);

    if (item->type == ProjectTreeItem::File) {
        QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = VariableManager::stripMetadata(QString::fromUtf8(file.readAll()));
            updateFileSynopsis(item, content);
        } else {
            m_activeRequests.remove(relPath);
            QTimer::singleShot(100, this, &SynopsisService::processNext);
        }
    } else {
        updateFolderSynopsis(item);
    }
}

void SynopsisService::updateFileSynopsis(ProjectTreeItem *item, const QString &content)
{
    LLMRequest req;
    req.provider = LLMProvider::OpenAI; // Use OpenAI by default for background tasks
    req.model = QStringLiteral("gpt-4o-mini"); // Cheap model for background tasks
    req.stream = false;
    
    LLMMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = QStringLiteral("You are a helpful assistant that writes a one-sentence synopsis of a roleplaying game document. Be concise and capture the essence.");
    
    LLMMessage user;
    user.role = QStringLiteral("user");
    user.content = i18n("Write a one-sentence synopsis of the following RPG content:\n\n%1").arg(content);
    
    req.messages << sys << user;

    QString relPath = item->path;
    LLMService::instance().sendNonStreamingRequest(req, [this, relPath](const QString &response) {
        ProjectTreeItem *target = m_model->findItem(relPath);
        if (target) {
            target->synopsis = response.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' '));
            // Trigger model update
            QModelIndex idx = m_model->index(0, 0, QModelIndex()); // Dummy for now, ideally find index
            // Actually ProjectTreePanel listens to dataChanged, but we need the correct index.
            // For now, let's just save the project.
            ProjectManager::instance().setTree(m_model->projectData());
            ProjectManager::instance().saveProject();
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

    if (childSynopses.isEmpty()) {
        m_activeRequests.remove(item->path);
        processNext();
        return;
    }

    LLMRequest req;
    req.provider = LLMProvider::OpenAI;
    req.model = QStringLiteral("gpt-4o-mini");
    req.stream = false;
    
    LLMMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = QStringLiteral("You are a helpful assistant that writes a one-sentence synopsis for a folder containing several RPG documents, based on their individual synopses.");
    
    LLMMessage user;
    user.role = QStringLiteral("user");
    user.content = i18n("Write a one-sentence summary of this folder contents:\n\n%1").arg(childSynopses);
    
    req.messages << sys << user;

    QString relPath = item->path;
    LLMService::instance().sendNonStreamingRequest(req, [this, relPath](const QString &response) {
        ProjectTreeItem *target = m_model->findItem(relPath);
        if (target) {
            target->synopsis = response.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' '));
            ProjectManager::instance().setTree(m_model->projectData());
            ProjectManager::instance().saveProject();
        }
        m_activeRequests.remove(relPath);
        processNext();
    });
}
