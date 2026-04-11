#ifndef CHARACTERDOSSIERSERVICE_H
#define CHARACTERDOSSIERSERVICE_H

#include <QObject>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QMutex>

class LLMService;
class LibrarianService;

/**
 * @brief The CharacterDossierService class automatically maintains character synopses
 * in the Library/Characters folder by scanning manuscript text.
 */
class CharacterDossierService : public QObject
{
    Q_OBJECT
public:
    static CharacterDossierService& instance();

    void init(LLMService *llm, LibrarianService *librarian);
    void setProjectPath(const QString &path);

    void pause();
    void resume();

public Q_SLOTS:
    void scanManuscript();
    void updateDossier(const QString &characterName, const QString &contextText);

Q_SIGNALS:
    void dossierUpdated(const QString &characterName);
    void scanningStarted();
    void scanningFinished();

private:
    explicit CharacterDossierService(QObject *parent = nullptr);
    ~CharacterDossierService() override = default;

    LLMService *m_llm = nullptr;
    LibrarianService *m_librarian = nullptr;
    QString m_projectPath;
    
    bool m_paused = false;
    QTimer *m_scanTimer;
    QStringList m_pendingCharacters;
    QMutex m_mutex;
};

#endif // CHARACTERDOSSIERSERVICE_H
