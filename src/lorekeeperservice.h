#ifndef LOREKEEPERSERVICE_H
#define LOREKEEPERSERVICE_H

#include <QObject>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QRecursiveMutex>
#include <QJsonObject>
#include <QList>

class LLMService;
class LibrarianService;
class ProjectTreeModel;
struct SearchResult;

/**
 * @brief The LoreKeeperService class automatically maintains world-building synopses
 * (Characters, Races, Locations, etc.) in the LoreKeeper folder by scanning manuscript text.
 */
class LoreKeeperService : public QObject
{
    Q_OBJECT
public:
    static LoreKeeperService& instance();

    void init(LLMService *llm, LibrarianService *librarian);
    void setProjectPath(const QString &path);

    void pause();
    void resume();

    /**
     * @brief Updates the LoreKeeper configuration from the project manager.
     */
    void updateConfig(const QJsonObject &config);

public Q_SLOTS:
    /**
     * @brief Scans the project's manuscript files to discover entities to track.
     */
    void scanManuscript();
    
    /**
     * @brief Triggers an LLM update for a specific entity in a category.
     */
    void updateEntityLore(const QString &category, const QString &entityName, const QString &contextText);

    /**
     * @brief Immediately scans a single document to discover and update lore entities.
     * @param filePath The absolute path to the document.
     */
    void indexDocument(const QString &filePath);

Q_SIGNALS:
    void loreUpdated(const QString &category, const QString &entityName);
    void loreUpdateStarted(const QString &filePath);
    void loreUpdateFinished(const QString &filePath);
    void scanningStarted();
    void scanningFinished();

private:
    explicit LoreKeeperService(QObject *parent = nullptr);
    ~LoreKeeperService() override = default;

    void processPendingUpdates();

    // Step 2 of updateEntityLore(): after RAG retrieval completes, assemble
    // the full prompt (category system prompt + retrieved passages + raw
    // discovery context + existing lore) and dispatch to the LLM. Split
    // out from updateEntityLore() because KnowledgeBase::search is async
    // and the generation must wait for its callback.
    void dispatchLoreGeneration(const QString &categoryName,
                                const QString &entityName,
                                const QString &rawContext,
                                const QList<SearchResult> &ragResults);

    LLMService *m_llm = nullptr;
    LibrarianService *m_librarian = nullptr;
    QString m_projectPath;
    
    QJsonObject m_config;
    bool m_paused = false;
    QTimer *m_scanTimer;
    
    struct PendingLore {
        QString category;
        QString name;
        QString context;
    };
    QList<PendingLore> m_pendingLore;
    mutable QRecursiveMutex m_mutex;
};

#endif // LOREKEEPERSERVICE_H
