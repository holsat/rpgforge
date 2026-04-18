#include "lorekeeperservice.h"
#include "llmservice.h"
#include "librarianservice.h"
#include "knowledgebase.h"
#include "projectmanager.h"
#include "projecttreemodel.h"
#include "projectkeys.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>
#include <KLocalizedString>
#include <QSettings>
#include <QPointer>

LoreKeeperService& LoreKeeperService::instance()
{
    static LoreKeeperService inst;
    return inst;
}

LoreKeeperService::LoreKeeperService(QObject *parent)
    : QObject(parent)
{
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(300000); // Scan every 5 minutes by default
    m_scanTimer->setSingleShot(false);
    connect(m_scanTimer, &QTimer::timeout, this, &LoreKeeperService::scanManuscript);
}

void LoreKeeperService::init(LLMService *llm, LibrarianService *librarian)
{
    m_llm = llm;
    m_librarian = librarian;
}


void LoreKeeperService::setProjectPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_projectPath = path;
    if (!path.isEmpty()) {
        m_config = ProjectManager::instance().loreKeeperConfig();
        // Initial scan after 15s
        QTimer::singleShot(15000, this, &LoreKeeperService::scanManuscript);
        m_scanTimer->start();
    } else {
        m_scanTimer->stop();
    }
}

void LoreKeeperService::updateConfig(const QJsonObject &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    qDebug() << "Lore AI: Configuration updated.";
}

void LoreKeeperService::pause()
{
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    m_scanTimer->stop();
}

void LoreKeeperService::resume()
{
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    if (!m_projectPath.isEmpty()) {
        m_scanTimer->start();
    }
}

void LoreKeeperService::scanManuscript()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_projectPath.isEmpty() || !m_llm) return;
    }

    Q_EMIT scanningStarted();
    qDebug() << "Lore AI: Starting manuscript scan...";

    // 1. Get manuscript text by walking the manuscript/ directory on disk.
    //    Previously we relied on ProjectManager::getActiveFiles(), but that
    //    only returns files explicitly registered in the project tree —
    //    projects whose manuscript .md files were never linked into the
    //    tree (a common case for imported / Scrivener / out-of-band edits)
    //    would yield an empty active list, causing the scan to silently
    //    early-return at the combinedText.isEmpty() check below and never
    //    drive any LLM calls or tree updates downstream.
    const QDir manuscriptDir(QDir(m_projectPath).absoluteFilePath(QStringLiteral("manuscript")));
    QString combinedText;
    if (manuscriptDir.exists()) {
        QDirIterator it(manuscriptDir.absolutePath(),
                        QStringList{QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                        QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                combinedText += QString::fromUtf8(file.readAll()) + QStringLiteral("\n\n");
            }
        }
    }

    if (combinedText.isEmpty()) {
        qDebug() << "Lore AI: No manuscript text found under" << manuscriptDir.absolutePath()
                 << "— skipping scan.";
        Q_EMIT scanningFinished();
        return;
    }
    qDebug() << "Lore AI: Collected" << combinedText.size() << "chars of manuscript text from"
             << manuscriptDir.absolutePath();

    // 2. Process each category in the config
    QJsonArray categories = m_config.value(QStringLiteral("categories")).toArray();
    
    for (const QJsonValue &catVal : categories) {
        QJsonObject category = catVal.toObject();
        QString catName = category.value(QStringLiteral("name")).toString();
        
        QDir catDir(QDir(m_projectPath).absoluteFilePath(QStringLiteral("lorekeeper/") + catName));
        QStringList existing = catDir.entryList({QStringLiteral("*.md")}, QDir::Files);
        for (QString &s : existing) s.chop(3);

        LLMRequest req;
        req.serviceName = i18n("LoreKeeper Entity Discovery (%1)", catName);
        req.settingsKey = QStringLiteral("lorekeeper/lorekeeper_model");
        
        QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("lorekeeper/lorekeeper_provider"), 
                                                                settings.value(QStringLiteral("llm/provider"), 0)).toInt());
        
        req.model = settings.value(QStringLiteral("lorekeeper/lorekeeper_model")).toString();
        if (req.model.isEmpty()) {
            switch(req.provider) {
                case LLMProvider::OpenAI: req.model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
                case LLMProvider::Anthropic: req.model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
                case LLMProvider::Ollama: req.model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
                case LLMProvider::Grok: req.model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
                case LLMProvider::Gemini: req.model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
                case LLMProvider::LMStudio: req.model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
            }
        }
        
        QString discoveryPrompt = settings.value(QStringLiteral("lorekeeper/discovery_prompt"),
            QStringLiteral("You are a world-building assistant. Extract a list of entities of type '%1' from the provided text.")).toString();

        req.messages << LLMMessage{QStringLiteral("system"), discoveryPrompt.arg(catName)};
        req.messages << LLMMessage{QStringLiteral("user"), i18n("List ONLY the names of entities belonging to the category '%1' found in this text, as a JSON array of strings. Known entities: %2\\n\\nTEXT:\\n%3", catName, existing.join(QLatin1String(", ")), combinedText.left(4000))};
        req.stream = false;

        QPointer<LoreKeeperService> weakThis(this);
        m_llm->sendNonStreamingRequest(req, [weakThis, catName, combinedText](const QString &response) {
            if (!weakThis) return;
            
            QString cleanJson = response.trimmed();
            if (cleanJson.startsWith(QLatin1String("```json"))) {
                cleanJson = cleanJson.mid(7);
                if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
            }

            QJsonDocument doc = QJsonDocument::fromJson(cleanJson.toUtf8());
            if (doc.isArray()) {
                qDebug() << "Lore AI: Discovery found entities:" << cleanJson;
                QJsonArray names = doc.array();
                for (const auto &v : names) {
                    QString name = v.toString().trimmed();
                    if (!name.isEmpty()) {
                        weakThis->updateEntityLore(catName, name, combinedText);
                    }
                }
            }
        });
    }
    
    Q_EMIT scanningFinished();
}

void LoreKeeperService::indexDocument(const QString &filePath)
{
    if (m_paused || filePath.isEmpty() || !m_llm) return;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    
    qDebug() << "Lore AI: Force scanning document:" << filePath;
    Q_EMIT loreUpdateStarted(filePath);

    // Process each category
    QJsonArray categories = m_config.value(QStringLiteral("categories")).toArray();
    if (categories.isEmpty()) {
        Q_EMIT loreUpdateFinished(filePath);
        return;
    }
    
    QAtomicInt *pendingCategories = new QAtomicInt(categories.size());

    for (const QJsonValue &catVal : categories) {
        QJsonObject category = catVal.toObject();
        QString catName = category.value(QStringLiteral("name")).toString();
        
        LLMRequest req;
        req.serviceName = i18n("LoreKeeper Entity Discovery (%1)", catName);
        req.settingsKey = QStringLiteral("lorekeeper/lorekeeper_model");
        
        QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("lorekeeper/lorekeeper_provider"), 
                                                                settings.value(QStringLiteral("llm/provider"), 0)).toInt());
        
        req.model = settings.value(QStringLiteral("lorekeeper/lorekeeper_model")).toString();
        if (req.model.isEmpty()) {
            switch(req.provider) {
                case LLMProvider::OpenAI: req.model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
                case LLMProvider::Anthropic: req.model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
                case LLMProvider::Ollama: req.model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
                case LLMProvider::Grok: req.model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
                case LLMProvider::Gemini: req.model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
                case LLMProvider::LMStudio: req.model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
            }
        }
        
        QString discoveryPrompt = settings.value(QStringLiteral("lorekeeper/discovery_prompt"),
            QStringLiteral("You are a world-building assistant. Extract a list of entities of type '%1' from the provided text.")).toString();

        req.messages << LLMMessage{QStringLiteral("system"), discoveryPrompt.arg(catName)};
        req.messages << LLMMessage{QStringLiteral("user"), i18n("List ONLY the names of entities belonging to the category '%1' found in this text, as a JSON array of strings. TEXT:\\n%2", catName, content.left(4000))};
        req.stream = false;

        QPointer<LoreKeeperService> weakThis(this);
        m_llm->sendNonStreamingRequest(req, [weakThis, catName, content, filePath, pendingCategories](const QString &response) {
            if (!weakThis) {
                if (pendingCategories->fetchAndSubRelaxed(1) == 1) delete pendingCategories;
                return;
            }
            
            QString cleanJson = response.trimmed();
            if (cleanJson.startsWith(QLatin1String("```json"))) {
                cleanJson = cleanJson.mid(7);
                if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
            }

            QJsonDocument doc = QJsonDocument::fromJson(cleanJson.toUtf8());
            if (doc.isArray()) {
                qDebug() << "Lore AI: Discovery found entities:" << cleanJson;
                QJsonArray names = doc.array();
                for (const auto &v : names) {
                    QString name = v.toString().trimmed();
                    if (!name.isEmpty()) {
                        weakThis->updateEntityLore(catName, name, content);
                    }
                }
            }
            
            if (pendingCategories->fetchAndSubRelaxed(1) == 1) {
                Q_EMIT weakThis->loreUpdateFinished(filePath);
                delete pendingCategories;
            }
        });
    }
}

void LoreKeeperService::updateEntityLore(const QString &categoryName, const QString &entityName, const QString &contextText)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_projectPath.isEmpty() || !m_llm) return;
    }

    // Pull entity-specific passages from the project's KnowledgeBase
    // (semantic search over the full project). This is the RAG layer the
    // Writing Assistant uses — feeding LoreKeeper the same retrieval
    // depth brings dossier quality in line with interactive chat output.
    // See chatpanel.cpp:421 for the equivalent call on the chat side.
    const QString ragQuery = QStringLiteral("%1 %2").arg(entityName, categoryName);
    QPointer<LoreKeeperService> weakThis(this);
    KnowledgeBase::instance().search(ragQuery, 10, QString(),
        [weakThis, categoryName, entityName, contextText](const QList<SearchResult> &ragResults) {
            if (!weakThis) return;
            weakThis->dispatchLoreGeneration(categoryName, entityName, contextText, ragResults);
        });
}

void LoreKeeperService::dispatchLoreGeneration(const QString &categoryName,
                                                const QString &entityName,
                                                const QString &rawContext,
                                                const QList<SearchResult> &ragResults)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_projectPath.isEmpty() || !m_llm) return;
    }

    // Determine category prompt
    QString categoryPrompt;
    QJsonArray categories = m_config.value(QStringLiteral("categories")).toArray();
    for (const QJsonValue &cv : categories) {
        if (cv.toObject().value(QStringLiteral("name")).toString() == categoryName) {
            categoryPrompt = cv.toObject().value(QStringLiteral("prompt")).toString();
            break;
        }
    }

    QString relPath = QStringLiteral("lorekeeper/") + categoryName + QDir::separator() + entityName + QStringLiteral(".md");
    QString absPath = QDir(m_projectPath).absoluteFilePath(relPath);

    QString existingContent;
    QFile file(absPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        existingContent = QString::fromUtf8(file.readAll());
        file.close();
    }

    LLMRequest req;
    req.serviceName = i18n("LoreKeeper Generator (%1)", categoryName);
    req.settingsKey = QStringLiteral("lorekeeper/lorekeeper_model");
    // Rich dossiers span multiple sections (Identity / Description /
    // Personality / Habits / Background / Goals / Conflicts / Key Moments
    // per the default prompt) and routinely exceed the 1024-token default.
    // 4096 matches the Anthropic-branch fallback in llmservice.cpp.
    req.maxTokens = 4096;

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("lorekeeper/lorekeeper_provider"),
                                                            settings.value(QStringLiteral("llm/provider"), 0)).toInt());

    req.model = settings.value(QStringLiteral("lorekeeper/lorekeeper_model")).toString();
    if (req.model.isEmpty()) {
        switch(req.provider) {
            case LLMProvider::OpenAI: req.model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
            case LLMProvider::Anthropic: req.model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
            case LLMProvider::Ollama: req.model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
            case LLMProvider::Grok: req.model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
            case LLMProvider::Gemini: req.model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
            case LLMProvider::LMStudio: req.model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
        }
    }

    QString genPromptBase = settings.value(QStringLiteral("lorekeeper/gen_prompt"),
        QStringLiteral("You are an expert world-builder. %1\\n\\nReturn ONLY the updated Markdown content.")).toString();

    // Assemble RAG-retrieved passages, clearly labelled per source so the
    // model can attribute details to specific files / headings.
    QString ragContext;
    if (!ragResults.isEmpty()) {
        for (const SearchResult &res : ragResults) {
            ragContext += QStringLiteral("--- Source: %1 (Heading: %2) ---\n%3\n\n")
                              .arg(res.filePath, res.heading, res.content);
        }
    }

    // The raw discovery context is supplementary — mostly the raw text the
    // entity was discovered in. Cap it at 16 KiB to leave headroom for the
    // RAG passages and the generation output itself.
    const QString trimmedRaw = rawContext.left(16000);

    req.messages << LLMMessage{QStringLiteral("system"), genPromptBase.arg(categoryPrompt)};
    req.messages << LLMMessage{QStringLiteral("user"),
        i18n("ENTITY: %1\\n"
             "EXISTING LORE:\\n%2\\n\\n"
             "RELEVANT PROJECT PASSAGES (retrieved by semantic search):\\n%3\\n"
             "ORIGINATING CONTEXT:\\n%4\\n\\n"
             "Update the lore now.",
             entityName,
             existingContent.isEmpty() ? i18n("[New Entity]") : existingContent,
             ragContext.isEmpty() ? i18n("[No retrieved passages]") : ragContext,
             trimmedRaw)};
    req.stream = false;

    QPointer<LoreKeeperService> weakThis(this);
    m_llm->sendNonStreamingRequest(req, [weakThis, categoryName, entityName, absPath, relPath](const QString &response) {
        if (!weakThis || response.isEmpty()) return;

        // Write the file to disk
        qDebug() << "Lore AI: Writing lore for" << entityName << "to" << absPath;
        QDir().mkpath(QFileInfo(absPath).absolutePath());
        QFile outFile(absPath);
        if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&outFile);
            out << response;
            outFile.close();

            // Authoritative request to update the tree structure
            ProjectManager::instance().requestTreeUpdate(categoryName, entityName, relPath);

            Q_EMIT weakThis->loreUpdated(categoryName, entityName);
        }
    });
}
