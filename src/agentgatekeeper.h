#ifndef AGENTGATEKEEPER_H
#define AGENTGATEKEEPER_H

#include <QObject>

class LibrarianService;

/**
 * @brief Singleton that coordinates pausing/resuming all background scanners and agents.
 *
 * Also carries the per-service AI kill-switch state: each toggle is stored
 * in the open project's ProjectMetadata (under "aiFeatures") and gates the
 * matching service at its primary entry points. When no project is open
 * the gatekeeper reports every service as enabled so app-wide code paths
 * (settings UI, idle init) are not blocked.
 */
class AgentGatekeeper : public QObject
{
    Q_OBJECT
public:
    enum class Service {
        Analyzer,
        LoreKeeper,
        Synopsis,
        Librarian,
        RagAssist,
    };
    Q_ENUM(Service)

    static AgentGatekeeper& instance();

    void setLibrarianService(LibrarianService *service);

    /// Returns true when no project is open so code that runs before a
    /// project is loaded is not gated off by a missing metadata struct.
    bool isEnabled(Service s) const;

    /// Persist a toggle change: updates ProjectMetadata, saves the project,
    /// then emits serviceEnabledChanged. No-op when no project is open.
    void setEnabled(Service s, bool enabled);

Q_SIGNALS:
    void serviceEnabledChanged(Service s, bool enabled);

public Q_SLOTS:
    void pauseAll();
    void resumeAll();

private:
    explicit AgentGatekeeper(QObject *parent = nullptr);

    LibrarianService *m_librarianService = nullptr;
};

#endif // AGENTGATEKEEPER_H
