#include "characterdossierservice.h"
#include "llmservice.h"
#include "librarianservice.h"
#include "projectmanager.h"
#include "projecttreemodel.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>
#include <KLocalizedString>

CharacterDossierService& CharacterDossierService::instance()
{
    static CharacterDossierService inst;
    return inst;
}

CharacterDossierService::CharacterDossierService(QObject *parent)
    : QObject(parent)
{
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(60000); // Scan every minute if not paused
    m_scanTimer->setSingleShot(false);
    connect(m_scanTimer, &QTimer::timeout, this, &CharacterDossierService::scanManuscript);
}

void CharacterDossierService::init(LLMService *llm, LibrarianService *librarian)
{
    m_llm = llm;
    m_librarian = librarian;
}

void CharacterDossierService::setProjectPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_projectPath = path;
    if (!path.isEmpty()) {
        // Initial delay to avoid scanning during project setup
        QTimer::singleShot(10000, this, &CharacterDossierService::scanManuscript);
        m_scanTimer->start();
    } else {
        m_scanTimer->stop();
    }
}

void CharacterDossierService::pause()
{
    QMutexLocker locker(&m_mutex);
    m_paused = true;
    m_scanTimer->stop();
}

void CharacterDossierService::resume()
{
    QMutexLocker locker(&m_mutex);
    m_paused = false;
    if (!m_projectPath.isEmpty()) {
        m_scanTimer->start();
    }
}

void CharacterDossierService::scanManuscript()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_projectPath.isEmpty() || !m_llm) return;
    }

    qDebug() << "CharacterDossierService: Scanning manuscript for characters...";
    
    // Find all manuscript files
    QDir msDir(QDir(m_projectPath).absoluteFilePath(QStringLiteral("manuscript")));
    if (!msDir.exists()) return;

    QStringList filters = {QStringLiteral("*.md"), QStringLiteral("*.markdown")};
    QDirIterator it(msDir.path(), filters, QDir::Files, QDirIterator::Subdirectories);
    
    QString combinedText;
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            combinedText += QString::fromUtf8(file.readAll()) + QStringLiteral("\n\n");
        }
    }

    if (combinedText.isEmpty()) return;

    // List existing sketches to tell the AI what we already know
    QDir sketchDir(QDir(m_projectPath).absoluteFilePath(QStringLiteral("library/Character Sketches")));
    QStringList existingSketches = sketchDir.entryList({QStringLiteral("*.md")}, QDir::Files);
    for (QString &s : existingSketches) s.chop(3); // Remove .md

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("dossier/provider"), settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    QString model = settings.value(QStringLiteral("dossier/model"), (provider == LLMProvider::Ollama ? QStringLiteral("llama3") : QString())).toString();

    // Ask LLM to list characters found in the text
    LLMRequest req;
    req.serviceName = i18n("Character Dossier Scanner");
    req.provider = provider;
    req.model = model;
    req.settingsKey = QStringLiteral("dossier/model");
    req.messages << LLMMessage{QStringLiteral("system"), i18n("You are a literary analyst. Extract a list of character names mentioned in the following RPG manuscript.")};
    req.messages << LLMMessage{QStringLiteral("user"), i18n("List ONLY the names of significant characters found in this text, as a JSON array of strings. Existing sketches: %1\n\nMANUSCRIPT:\n%2", existingSketches.join(QLatin1String(", ")), combinedText.left(5000))};
    req.stream = false;

    QPointer<CharacterDossierService> weakThis(this);
    m_llm->sendNonStreamingRequest(req, [weakThis, combinedText](const QString &response) {
        if (!weakThis) return;
        
        QString cleanJson = response.trimmed();
        if (cleanJson.startsWith(QLatin1String("```json"))) {
            cleanJson = cleanJson.mid(7);
            if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
        }

        QJsonDocument doc = QJsonDocument::fromJson(cleanJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray names = doc.array();
            for (const auto &v : names) {
                QString name = v.toString().trimmed();
                if (!name.isEmpty()) {
                    weakThis->updateDossier(name, combinedText);
                }
            }
        }
    });
}

void CharacterDossierService::updateDossier(const QString &characterName, const QString &contextText)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_paused || m_projectPath.isEmpty() || !m_llm) return;
    }

    QString dossierPath = QDir(m_projectPath).absoluteFilePath(QStringLiteral("library/Character Sketches/") + characterName + QStringLiteral(".md"));
    QString existingContent;
    QFile file(dossierPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        existingContent = QString::fromUtf8(file.readAll());
        file.close();
    }

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(settings.value(QStringLiteral("dossier/provider"), settings.value(QStringLiteral("llm/provider"), 0)).toInt());
    QString model = settings.value(QStringLiteral("dossier/model"), (provider == LLMProvider::Ollama ? QStringLiteral("llama3") : QString())).toString();

    LLMRequest req;
    req.serviceName = i18n("Character Dossier Generator");
    req.provider = provider;
    req.model = model;
    req.settingsKey = QStringLiteral("dossier/model");
    
    QString systemPrompt = i18n(
        "You are an expert RPG writer. Maintain a comprehensive Character Dossier for '%1'.\n"
        "Your task is to synthesize new information from the provided manuscript context into the existing dossier.\n\n"
        "DOSSIER STRUCTURE:\n"
        "1. Basic Identity\n"
        "2. Physical Description\n"
        "3. Personality\n"
        "4. Habits/Mannerisms\n"
        "5. General Description\n"
        "6. Character Background & Origin Story (Before & After story begins)\n"
        "7. Goals\n"
        "8. Internal & External Conflicts\n"
        "9. Key Moments (Significant traits/changes/actions)\n\n"
        "If information is missing for a section, mark it as [Unknown/Pending].\n"
        "Keep the prose atmospheric and consistent with the project's tone.\n"
        "Return ONLY the updated Markdown content.",
        characterName);

    QString userPrompt = i18n(
        "EXISTING DOSSIER:\n%1\n\nNEW MANUSCRIPT CONTEXT:\n%2\n\nUpdate the dossier now.",
        existingContent.isEmpty() ? i18n("[New Character]") : existingContent, 
        contextText.left(4000));

    req.messages << LLMMessage{QStringLiteral("system"), systemPrompt};
    req.messages << LLMMessage{QStringLiteral("user"), userPrompt};
    req.stream = false;

    QPointer<CharacterDossierService> weakThis(this);
    m_llm->sendNonStreamingRequest(req, [weakThis, characterName, dossierPath](const QString &response) {
        if (!weakThis) return;
        if (response.isEmpty()) return;

        QtConcurrent::run([weakThis, characterName, dossierPath, response]() {
            QFile outFile(dossierPath);
            QDir().mkpath(QFileInfo(dossierPath).absolutePath());
            if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&outFile);
                out << response;
                outFile.close();
                
                QMetaObject::invokeMethod(weakThis.data(), [weakThis, characterName]() {
                    if (weakThis) {
                        Q_EMIT weakThis->dossierUpdated(characterName);
                        // Also trigger librarian to refresh tree if needed
                        // (Usually project tree watches filesystem so it might be automatic)
                    }
                }, Qt::QueuedConnection);
            }
        });
    });
}
