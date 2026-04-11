#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>

// MOCK LibrarianService that doesn't depend on LLMService
#include "../src/librarianservice.h"
#include "../src/llmservice.h"

// Provide implementation for missing LLMService methods in test context
void LLMService::sendNonStreamingRequest(const LLMRequest&, std::function<void(const QString&)>) {}
LLMService& LLMService::instance() { static auto* inst = reinterpret_cast<LLMService*>(new QObject()); return *inst; }

class TestLibrarianService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDir.mkdir(QStringLiteral("test_project_service"));
        m_service = new LibrarianService(&LLMService::instance(), this);
        m_service->setProjectPath(QDir::current().filePath(QStringLiteral("test_project_service")));
    }

    void cleanupTestCase()
    {
        delete m_service;
        QDir(QStringLiteral("test_project_service")).removeRecursively();
    }

    void testTableExtraction()
    {
        QString markdown = QStringLiteral(
            "| Roll (1d10) | Class Description | Resilience | Whisper |\n"
            "|:-----------:|-------------------|:----------:|:-------:|\n"
            "|     1-6     | Slave             |     -2     |   +3    |\n"
            "|     7-8     | Indentured        |     -1     |   +1    |\n");

        QString filePath = QDir::current().filePath(QStringLiteral("test_project_service/tables.md"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(markdown.toUtf8());
        file.close();

        QSignalSpy spy(m_service, &LibrarianService::entityUpdated);
        m_service->scanFile(filePath);
        
        // Wait for signals
        bool found = false;
        for(int i=0; i<30; ++i) {
            QTest::qWait(100);
            if (spy.count() >= 2) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void testListExtraction()
    {
        QString markdown = QStringLiteral(
            "- Strength: 18\n"
            "- Agility: 12\n");

        QString filePath = QDir::current().filePath(QStringLiteral("test_project_service/lists.md"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(markdown.toUtf8());
        file.close();

        QSignalSpy spy(m_service, &LibrarianService::entityUpdated);
        m_service->scanFile(filePath);
        
        bool found = false;
        for(int i=0; i<30; ++i) {
            QTest::qWait(100);
            if (spy.count() >= 2) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

private:
    LibrarianService *m_service;
    QDir m_testDir;
};

QTEST_MAIN(TestLibrarianService)
#include "test_librarianservice.moc"
