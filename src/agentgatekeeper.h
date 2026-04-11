#ifndef AGENTGATEKEEPER_H
#define AGENTGATEKEEPER_H

#include <QObject>

class LibrarianService;

/**
 * @brief Singleton that coordinates pausing/resuming all background scanners and agents.
 */
class AgentGatekeeper : public QObject
{
    Q_OBJECT
public:
    static AgentGatekeeper& instance();

    void setLibrarianService(LibrarianService *service);

public Q_SLOTS:
    void pauseAll();
    void resumeAll();

private:
    explicit AgentGatekeeper(QObject *parent = nullptr);
    
    LibrarianService *m_librarianService = nullptr;
};

#endif // AGENTGATEKEEPER_H
