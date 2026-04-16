#include "agentgatekeeper.h"
#include "synopsisservice.h"
#include "librarianservice.h"
#include "knowledgebase.h"
#include "analyzerservice.h"
#include "lorekeeperservice.h"
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
