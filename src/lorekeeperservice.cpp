#include "lorekeeperservice.h"
#include "llmservice.h"
#include "librarianservice.h"
#include "projectmanager.h"
#include "projecttreemodel.h"
#include "projectkeys.h"
#include <QDir>
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
    
    // 1. Get manuscript text from active files only
    QStringList activeFiles = ProjectManager::instance().getActiveFiles();
    QString combinedText;

    for (const QString &filePath : activeFiles) {
        if (filePath.contains(QStringLiteral("/manuscript/"), Qt::CaseInsensitive)) {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                combinedText += QString::fromUtf8(file.readAll()) + QStringLiteral("\n\n");
            }
        }
    }

    if (combinedText.isEmpty()) {
        Q_EMIT scanningFinished();
        return;
    }

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

    req.messages << LLMMessage{QStringLiteral("system"), genPromptBase.arg(categoryPrompt)};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("ENTITY: %1\\nEXISTING LORE:\\n%2\\n\\nCONTEXT:\\n%3\\n\\nUpdate the lore now.", entityName, existingContent.isEmpty() ? i18n("[New Entity]") : existingContent, contextText.left(4000))};
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
