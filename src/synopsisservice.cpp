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

void SynopsisService::requestUpdate(const QString &relativePath, bool force)
{
    if (relativePath.isEmpty()) return;

    {
        QMutexLocker locker(&m_mutex);
        if (!force) {
            // Only skip if a synopsis is already present. We look this up
            // via the snapshot API so the service stays decoupled from the
            // live model.
            const auto snap = ProjectManager::instance().nodeSnapshot(relativePath);
            if (snap && !snap->synopsis.isEmpty()) return;
        }

        if (!m_queue.contains(relativePath) && !m_activeRequests.contains(relativePath)) {
            qDebug() << "Synopsis AI: Queuing update for" << relativePath;
            m_queue.enqueue(relativePath);
        }

        if (m_isProcessing) return;
        m_isProcessing = true;
    }

    QTimer::singleShot(0, this, &SynopsisService::processNext);
}

void SynopsisService::cancelRequest(const QString &relativePath)
{
    QMutexLocker locker(&m_mutex);
    m_queue.removeAll(relativePath);
    m_activeRequests.remove(relativePath);
}

void SynopsisService::scanProject()
{
    qDebug() << "Synopsis AI: Scanning project for missing synopses...";
    if (!ProjectManager::instance().isProjectOpen()) {
        qDebug() << "Synopsis AI: Scan aborted - Project not open.";
        return;
    }

    const TreeNodeSnapshot root = ProjectManager::instance().treeSnapshot();
    if (root.children.isEmpty()) return;

    scanSnapshot(root, /*inManuscript=*/false);
}

void SynopsisService::scanSnapshot(const TreeNodeSnapshot &node, bool inManuscript)
{
    const bool manuscript = inManuscript
        || (node.category == static_cast<int>(ProjectTreeItem::Manuscript));

    // Prioritize leaf files under Manuscript — folder synopses depend on
    // child data being present first. Non-manuscript folders are also
    // enqueued (below) but only after the manuscript pass has primed the
    // queue.
    if (!node.path.isEmpty() && !node.isTransient) {
        if (node.type == static_cast<int>(ProjectTreeItem::File)
            && manuscript
            && node.synopsis.isEmpty()) {
            requestUpdate(node.path);
        }
    }

    for (const auto &child : node.children) {
        scanSnapshot(child, manuscript);
    }

    // Folder-level enqueue at the tail of recursion so children are
    // queued first — the processor is FIFO so files end up before folders.
    if (!node.path.isEmpty() && !node.isTransient) {
        if (node.type == static_cast<int>(ProjectTreeItem::Folder)
            && node.synopsis.isEmpty()) {
            requestUpdate(node.path);
        }
    }
}

void SynopsisService::pause()
{
    qDebug() << "Synopsis AI: Pausing...";
    QMutexLocker locker(&m_mutex);
    m_paused = true;
}

void SynopsisService::resume()
{
    qDebug() << "Synopsis AI: Resuming...";
    bool startProcessing = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_paused) return;
        m_paused = false;

        if (!m_queue.isEmpty() && !m_isProcessing) {
            m_isProcessing = true;
            startProcessing = true;
        }
    }

    if (startProcessing) {
        QTimer::singleShot(0, this, &SynopsisService::processNext);
    }
}

void SynopsisService::processNext()
{
    QString relPath;
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_queue.isEmpty()) {
            m_isProcessing = false;
            return;
        }
        relPath = m_queue.dequeue();
        m_activeRequests.insert(relPath);
    }

    const auto snap = ProjectManager::instance().nodeSnapshot(relPath);
    if (!snap) {
        qDebug() << "Synopsis AI: Item no longer in tree, skipping:" << relPath;
        {
            QMutexLocker locker(&m_mutex);
            m_activeRequests.remove(relPath);
        }
        QTimer::singleShot(0, this, &SynopsisService::processNext);
        return;
    }

    if (snap->type == static_cast<int>(ProjectTreeItem::File)) {
        const QString fullPath = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(relPath);
        if (!QFile::exists(fullPath)) {
            {
                QMutexLocker locker(&m_mutex);
                m_activeRequests.remove(relPath);
            }
            QTimer::singleShot(0, this, &SynopsisService::processNext);
            return;
        }

        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QString content = QString::fromUtf8(file.readAll());
            updateFileSynopsis(relPath, content);
        } else {
            {
                QMutexLocker locker(&m_mutex);
                m_activeRequests.remove(relPath);
            }
            QTimer::singleShot(0, this, &SynopsisService::processNext);
        }
    } else {
        QStringList childSynopses;
        for (const auto &child : snap->children) {
            if (!child.synopsis.isEmpty()) childSynopses << child.synopsis;
        }

        if (childSynopses.isEmpty()) {
            {
                QMutexLocker locker(&m_mutex);
                m_activeRequests.remove(relPath);
            }
            QTimer::singleShot(0, this, &SynopsisService::processNext);
            return;
        }

        updateFolderSynopsis(relPath, childSynopses);
    }
}

void SynopsisService::updateFileSynopsis(const QString &relativePath, const QString &content)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    // Use agent-specific settings first, then fall back to global
    int providerIdx = settings.value(QStringLiteral("synopsis/synopsis_file_provider"),
                                     settings.value(QStringLiteral("llm/provider"), 0)).toInt();
    LLMProvider provider = static_cast<LLMProvider>(providerIdx);

    QString model = settings.value(QStringLiteral("synopsis/synopsis_file_model")).toString();
    if (model.isEmpty()) {
        // Fallback to provider default model
        QString sk = LLMService::providerSettingsKey(provider);
        model = settings.value(sk + QStringLiteral("/model")).toString();
    }

    QString systemPrompt = settings.value(QStringLiteral("synopsis/file_prompt"),
        i18n("You are a senior RPG editor. Write a one-sentence hook/synopsis for this scene or document. Be atmospheric and concise.")).toString();

    LLMRequest req;
    req.provider = provider;
    req.model = model;
    req.serviceName = i18n("File Synopsis");
    req.settingsKey = QStringLiteral("synopsis/synopsis_file_model");
    req.messages << LLMMessage{QStringLiteral("system"), systemPrompt};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("Summarize this document in one short sentence: %1", content.left(2000))};
    req.stream = false;

    QPointer<SynopsisService> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, relativePath](const QString &response) {
        if (!weakThis) return;

        // Only overwrite the existing synopsis when the LLM actually
        // produced text. An empty response indicates the request failed
        // (4xx error, missing API key, wrong model, etc.) — clobbering
        // the existing synopsis with "" would silently erase either the
        // previous AI-generated synopsis or a hand-written frontmatter
        // one. Leave prior content in place so the corkboard keeps
        // showing something useful.
        const QString trimmed = response.trimmed();
        if (!trimmed.isEmpty()) {
            ProjectManager::instance().setNodeSynopsis(relativePath, trimmed);
        } else {
            qWarning() << "Synopsis AI: empty response for" << relativePath
                       << "— keeping prior synopsis";
        }

        {
            QMutexLocker locker(&weakThis->m_mutex);
            weakThis->m_activeRequests.remove(relativePath);
        }
        QTimer::singleShot(0, weakThis.data(), &SynopsisService::processNext);
    });
}

void SynopsisService::updateFolderSynopsis(const QString &relativePath,
                                            const QStringList &childSynopses)
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));

    int providerIdx = settings.value(QStringLiteral("synopsis/synopsis_folder_provider"),
                                     settings.value(QStringLiteral("llm/provider"), 0)).toInt();
    LLMProvider provider = static_cast<LLMProvider>(providerIdx);

    QString model = settings.value(QStringLiteral("synopsis/synopsis_folder_model")).toString();
    if (model.isEmpty()) {
        QString sk = LLMService::providerSettingsKey(provider);
        model = settings.value(sk + QStringLiteral("/model")).toString();
    }

    QString systemPrompt = settings.value(QStringLiteral("synopsis/folder_prompt"),
        i18n("You are an RPG project manager. Write a one-sentence summary for this folder (e.g. 'A collection of character backgrounds' or 'The core mechanics of combat').")).toString();

    LLMRequest req;
    req.provider = provider;
    req.model = model;
    req.serviceName = i18n("Folder Synopsis");
    req.settingsKey = QStringLiteral("synopsis/synopsis_folder_model");
    req.messages << LLMMessage{QStringLiteral("system"), systemPrompt};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("Summarize this folder based on its contents: %1", childSynopses.join(QLatin1String("\n")))};
    req.stream = false;

    QPointer<SynopsisService> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, relativePath](const QString &response) {
        if (!weakThis) return;

        // Same guard as updateFileSynopsis: don't erase an existing
        // synopsis on a failed LLM call.
        const QString trimmed = response.trimmed();
        if (!trimmed.isEmpty()) {
            ProjectManager::instance().setNodeSynopsis(relativePath, trimmed);
        } else {
            qWarning() << "Synopsis AI: empty response for folder" << relativePath
                       << "— keeping prior synopsis";
        }

        {
            QMutexLocker locker(&weakThis->m_mutex);
            weakThis->m_activeRequests.remove(relativePath);
        }
        QTimer::singleShot(0, weakThis.data(), &SynopsisService::processNext);
    });
}
