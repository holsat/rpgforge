#include "lorekeeperservice.h"
#include "llmservice.h"
#include "librarianservice.h"
#include "ragassistservice.h"
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

    // Find the category-specific system prompt (e.g. the 8-section
    // "Character Dossier" template for Characters, etc.). Falls back to
    // empty string if the project's LoreKeeper config doesn't define one
    // for this category — the service's baseline system prompt still
    // applies.
    QString categoryPrompt;
    {
        QJsonArray categories = m_config.value(QStringLiteral("categories")).toArray();
        for (const QJsonValue &cv : categories) {
            if (cv.toObject().value(QStringLiteral("name")).toString() == categoryName) {
                categoryPrompt = cv.toObject().value(QStringLiteral("prompt")).toString();
                break;
            }
        }
    }

    // Load any existing dossier so the service can feed it back in as
    // priority-0 context. This both preserves the author's edits and
    // gives the LLM the canonical prior state to extend rather than
    // rewrite from scratch.
    const QString relPath = QStringLiteral("lorekeeper/") + categoryName + QDir::separator() + entityName + QStringLiteral(".md");
    const QString absPath = QDir(m_projectPath).absoluteFilePath(relPath);
    QString existingContent;
    {
        QFile file(absPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            existingContent = QString::fromUtf8(file.readAll());
            file.close();
        }
    }

    // Resolve provider + model from settings (falling back through
    // lorekeeper-specific → global defaults). Mirrors the pattern the
    // original pre-service implementation used so user config stays
    // honoured.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const LLMProvider provider = static_cast<LLMProvider>(
        settings.value(QStringLiteral("lorekeeper/lorekeeper_provider"),
                       settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    QString model = settings.value(QStringLiteral("lorekeeper/lorekeeper_model")).toString();
    if (model.isEmpty()) {
        switch (provider) {
            case LLMProvider::OpenAI: model = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
            case LLMProvider::Anthropic: model = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
            case LLMProvider::Ollama: model = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
            case LLMProvider::Grok: model = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
            case LLMProvider::Gemini: model = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
            case LLMProvider::LMStudio: model = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
        }
    }

    // Build the request. The service owns the heavy lifting: KB
    // retrieval, dedup, citation-preserving assembly, multi-hop synthesis
    // (once Comprehensive mode is fully wired up), and output formatting.
    RagAssistRequest req;
    req.provider = provider;
    req.model = model;
    req.serviceName = i18n("LoreKeeper Generator (%1)", categoryName);
    req.settingsKey = QStringLiteral("lorekeeper/lorekeeper_model");
    req.systemPrompt = categoryPrompt +
        QStringLiteral("\n\nReturn ONLY the full updated Markdown content for the dossier file. "
                       "Do not wrap the output in code fences. Preserve existing headings where still accurate.");
    req.userPrompt = i18n("Update the dossier for \"%1\". Integrate anything new from the "
                          "retrieved project passages, keep anything still accurate from the "
                          "existing dossier, and rewrite sections whose factual basis has "
                          "changed. Output the whole updated file.", entityName);
    req.entityName = entityName;

    // Existing dossier takes absolute priority: the author may have hand-
    // edited it. The raw discovery text is lower priority — it's the
    // context the entity was found in and sometimes repeats content
    // already indexed in the KB.
    if (!existingContent.isEmpty()) {
        req.extraSources.append(ContextSource{
            i18n("EXISTING DOSSIER"), existingContent, relPath, /*priority=*/0
        });
    }
    if (!contextText.isEmpty()) {
        req.extraSources.append(ContextSource{
            i18n("ORIGINATING CONTEXT"),
            contextText.left(16000),   // raw discovery text, not citable
            QString(),
            /*priority=*/10
        });
    }

    // Dossier generation is the synthesis use case the Comprehensive
    // multi-hop pipeline is designed for. Until that pipeline lands on
    // this branch, the service treats Comprehensive identically to
    // Quick — setting it now means LoreKeeper gets the benefit for free
    // when multi-hop arrives.
    req.depth = SynthesisDepth::Comprehensive;
    req.maxTokens = 4096;
    req.topK = 30;
    req.finalK = 12;
    req.requireCitations = true;

    QPointer<LoreKeeperService> weakThis(this);
    RagAssistCallbacks callbacks;
    callbacks.onComplete = [weakThis, categoryName, entityName, absPath, relPath]
                           (const QString &, const QString &response) {
        if (!weakThis || response.isEmpty()) return;

        qDebug() << "Lore AI: Writing lore for" << entityName << "to" << absPath;
        QDir().mkpath(QFileInfo(absPath).absolutePath());
        QFile outFile(absPath);
        if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&outFile);
            out << response;
            outFile.close();

            ProjectManager::instance().requestTreeUpdate(categoryName, entityName, relPath);
            Q_EMIT weakThis->loreUpdated(categoryName, entityName);
        }
    };
    callbacks.onError = [entityName](const QString &, const QString &message) {
        qWarning() << "Lore AI:" << entityName << "generation failed:" << message;
    };

    RagAssistService::instance().generate(req, callbacks);
}
