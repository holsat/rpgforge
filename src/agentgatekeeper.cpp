#include "agentgatekeeper.h"
#include "synopsisservice.h"
#include "librarianservice.h"
#include "knowledgebase.h"
#include "analyzerservice.h"
#include "lorekeeperservice.h"
#include "projectmanager.h"
#include <QDebug>

AgentGatekeeper& AgentGatekeeper::instance()
{
    static AgentGatekeeper inst;
    return inst;
}

AgentGatekeeper::AgentGatekeeper(QObject *parent)
    : QObject(parent)
{
}

void AgentGatekeeper::setLibrarianService(LibrarianService *service)
{
    m_librarianService = service;
}

bool AgentGatekeeper::isEnabled(Service s) const
{
    auto &pm = ProjectManager::instance();
    if (!pm.isProjectOpen()) return true;
    switch (s) {
    case Service::Analyzer:   return pm.aiAnalyzerEnabled();
    case Service::LoreKeeper: return pm.aiLoreKeeperEnabled();
    case Service::Synopsis:   return pm.aiSynopsisEnabled();
    case Service::Librarian:  return pm.aiLibrarianEnabled();
    case Service::RagAssist:  return pm.aiRagAssistEnabled();
    }
    return true;
}

void AgentGatekeeper::setEnabled(Service s, bool enabled)
{
    auto &pm = ProjectManager::instance();
    if (!pm.isProjectOpen()) return;

    bool current = isEnabled(s);
    if (current == enabled) return;

    switch (s) {
    case Service::Analyzer:   pm.setAiAnalyzerEnabled(enabled);   break;
    case Service::LoreKeeper: pm.setAiLoreKeeperEnabled(enabled); break;
    case Service::Synopsis:   pm.setAiSynopsisEnabled(enabled);   break;
    case Service::Librarian:  pm.setAiLibrarianEnabled(enabled);  break;
    case Service::RagAssist:  pm.setAiRagAssistEnabled(enabled);  break;
    }
    pm.saveProject();
    Q_EMIT serviceEnabledChanged(s, enabled);
}

void AgentGatekeeper::pauseAll()
{
    qDebug() << "AgentGatekeeper: Pausing all background agents...";
    SynopsisService::instance().pause();
    KnowledgeBase::instance().pause();
    AnalyzerService::instance().pause();
    LoreKeeperService::instance().pause();
    if (m_librarianService) {
        qDebug() << "AgentGatekeeper: Pausing Data Extractor.";
        m_librarianService->pause();
    }
}

void AgentGatekeeper::resumeAll()
{
    qDebug() << "AgentGatekeeper: Resuming all background agents...";
    SynopsisService::instance().resume();
    KnowledgeBase::instance().resume();
    AnalyzerService::instance().resume(); // Fixed: was pause()
    LoreKeeperService::instance().resume();
    if (m_librarianService) {
        qDebug() << "AgentGatekeeper: Resuming Data Extractor.";
        m_librarianService->resume();
    }
}
