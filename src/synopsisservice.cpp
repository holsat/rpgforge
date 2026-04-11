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
#include "projecttreemodel.h"
#include "projectmanager.h"
#include "llmservice.h"
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QDebug>
#include <QTimer>
#include <QPointer>

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
    if (relativePath.isEmpty()) return;
    
    if (!force) {
        ProjectTreeItem *item = m_model ? m_model->findItem(relativePath) : nullptr;
        if (item && !item->synopsis.isEmpty()) return;
    }

    if (!m_queue.contains(relativePath) && !m_activeRequests.contains(relativePath)) {
        qDebug() << "SynopsisService: Queuing update for" << relativePath;
        m_queue.enqueue(relativePath);
    }

    if (!m_isProcessing) {
        m_isProcessing = true;
        QTimer::singleShot(0, this, &SynopsisService::processNext);
    }
}

void SynopsisService::cancelRequest(const QString &relativePath)
{
    m_queue.removeAll(relativePath);
    m_activeRequests.remove(relativePath);
}

void SynopsisService::scanProject()
{
    qDebug() << "SynopsisService: Scanning project for missing synopses...";
    if (!m_model) {
        qDebug() << "SynopsisService: Scan aborted - No model set.";
        return;
    }
    if (!ProjectManager::instance().isProjectOpen()) {
        qDebug() << "SynopsisService: Scan aborted - Project not open.";
        return;
    }
    if (m_paused) {
        qDebug() << "SynopsisService: Scan aborted - Service is paused.";
        return;
    }

    ProjectTreeItem *root = m_model->rootItem();
    if (!root || root->children.isEmpty()) {
        qDebug() << "SynopsisService: Scan aborted - Tree is empty.";
        return;
    }

    QList<ProjectTreeItem*> items;
    std::function<void(ProjectTreeItem*)> scan = [&](ProjectTreeItem *item) {
        if (!item) return;
        if (item != root) {
            items.append(item);
        }
        for (auto *child : item->children) {
            scan(child);
        }
    };

    scan(root);

    qDebug() << "SynopsisService: Found" << items.count() << "items to check.";

    // Prioritize files to build leaf-level data first
    for (auto *item : items) {
        if (item->type == ProjectTreeItem::File) {
            if (item->synopsis.isEmpty() && !item->path.isEmpty()) requestUpdate(item->path);
        }
    }
    
    // Then request folders
    for (auto *item : items) {
        if (item->type == ProjectTreeItem::Folder) {
            if (item->synopsis.isEmpty()) requestUpdate(item->path);
        }
    }
}

void SynopsisService::pause()
{
    qDebug() << "SynopsisService: Pausing...";
    m_paused = true;
}

void SynopsisService::resume()
{
    qDebug() << "SynopsisService: Resuming...";
    if (!m_paused) return;
    m_paused = false;
    
    if (!m_queue.isEmpty() && !m_isProcessing) {
        m_isProcessing = true;
        QTimer::singleShot(0, this, &SynopsisService::processNext);
    }
}

void SynopsisService::processNext()
{
    if (m_paused || m_queue.isEmpty()) {
        m_isProcessing = false;
        return;
    }

    QString relPath = m_queue.dequeue();
    ProjectTreeItem *item = m_model ? m_model->findItem(relPath) : nullptr;
    
    if (!item) {
        qDebug() << "SynopsisService: Item no longer in tree, skipping:" << relPath;
        QTimer::singleShot(0, this, &SynopsisService::processNext);
        return;
    }

    m_activeRequests.insert(relPath);

    if (item->type == ProjectTreeItem::File) {
        QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);
        if (!QFile::exists(fullPath)) {
            m_activeRequests.remove(relPath);
            QTimer::singleShot(0, this, &SynopsisService::processNext);
            return;
        }

        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            updateFileSynopsis(item, content);
        } else {
            m_activeRequests.remove(relPath);
            QTimer::singleShot(0, this, &SynopsisService::processNext);
        }
    } else {
        updateFolderSynopsis(item);
    }
}

void SynopsisService::updateFileSynopsis(ProjectTreeItem *item, const QString &content)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    QString model = settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString();
    if (provider == LLMProvider::Anthropic) model = settings.value(QStringLiteral("llm/anthropic/model")).toString();
    else if (provider == LLMProvider::Ollama) model = settings.value(QStringLiteral("llm/ollama/model"), QStringLiteral("llama3")).toString();

    LLMRequest req;
    req.provider = provider;
    req.model = model;
    req.messages << LLMMessage{QStringLiteral("system"), i18n("You are an assistant that summarizes RPG documents concisely.")};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("Summarize this document in one short sentence: %1").arg(content.left(2000))};
    req.stream = false;

    QString relPath = item->path;
    QPointer<SynopsisService> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, relPath](const QString &response) {
        if (!weakThis) return;
        
        ProjectTreeItem *resItem = weakThis->m_model ? weakThis->m_model->findItem(relPath) : nullptr;
        if (resItem) {
            resItem->synopsis = response.trimmed();
            QModelIndex idx = weakThis->m_model->indexForItem(resItem);
            if (idx.isValid()) Q_EMIT weakThis->m_model->dataChanged(idx, idx, {ProjectTreeModel::SynopsisRole});
        }
        
        weakThis->m_activeRequests.remove(relPath);
        QTimer::singleShot(0, weakThis.data(), &SynopsisService::processNext);
    });
}

void SynopsisService::updateFolderSynopsis(ProjectTreeItem *item)
{
    // Folder synopsis is a synthesis of its children's synopses
    QString relPath = item->path;
    QStringList childSynopses;
    for (auto *child : item->children) {
        if (!child->synopsis.isEmpty()) childSynopses << child->synopsis;
    }

    if (childSynopses.isEmpty()) {
        m_activeRequests.remove(relPath);
        QTimer::singleShot(0, this, &SynopsisService::processNext);
        return;
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
    QString model = settings.value(QStringLiteral("llm/openai/model"), QStringLiteral("gpt-4o")).toString();

    LLMRequest req;
    req.provider = provider;
    req.model = model;
    req.messages << LLMMessage{QStringLiteral("system"), i18n("You are an assistant that summarizes RPG project folders.")};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("Summarize this folder based on its contents: %1").arg(childSynopses.join(QLatin1String("\n")))};
    req.stream = false;

    QPointer<SynopsisService> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, relPath](const QString &response) {
        if (!weakThis) return;
        
        ProjectTreeItem *resItem = weakThis->m_model ? weakThis->m_model->findItem(relPath) : nullptr;
        if (resItem) {
            resItem->synopsis = response.trimmed();
            QModelIndex idx = weakThis->m_model->indexForItem(resItem);
            if (idx.isValid()) Q_EMIT weakThis->m_model->dataChanged(idx, idx, {ProjectTreeModel::SynopsisRole});
        }
        
        weakThis->m_activeRequests.remove(relPath);
        QTimer::singleShot(0, weakThis.data(), &SynopsisService::processNext);
    });
}
