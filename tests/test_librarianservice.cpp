#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>

#include "../src/librarianservice.h"

class TestLibrarianService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QString testDirName = QStringLiteral("test_project_service");
        QDir().mkdir(testDirName);
        m_testPath = QDir::current().absoluteFilePath(testDirName);
        
        // Pass nullptr for LLMService as we only test heuristic extraction
        m_service = new LibrarianService(nullptr, this);
        m_service->setProjectPath(m_testPath);
    }

    void cleanupTestCase()
    {
        delete m_service;
        QDir(m_testPath).removeRecursively();
    }

    void testTableExtraction()
    {
        QString markdown = QStringLiteral(
            "| Name | HP | AC |\n"
            "|------|----|----|\n"
            "| Orc  | 15 | 13 |\n"
            "| Goblin | 7 | 15 |\n");

        QString filePath = m_testPath + QStringLiteral("/tables.md");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(markdown.toUtf8());
        file.close();

        QSignalSpy spy(m_service, &LibrarianService::entityUpdated);
        QSignalSpy scanSpy(m_service, &LibrarianService::scanningFinished);
        m_service->scanFile(filePath);
        
        // Wait for scanning to finish
        QVERIFY(scanSpy.wait(3000));
        QVERIFY(spy.count() >= 2);
    }

    void testListExtraction()
    {
        QString markdown = QStringLiteral(
            "# Character Stats\n"
            "- Strength: 18\n"
            "**Agility**: 12\n"
            "__Intelligence__: 14\n"
            "  Wisdom  : 10\n");

        QString filePath = m_testPath + QStringLiteral("/lists.md");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(markdown.toUtf8());
        file.close();

        QSignalSpy spy(m_service, &LibrarianService::entityUpdated);
        QSignalSpy scanSpy(m_service, &LibrarianService::scanningFinished);
        m_service->scanFile(filePath);
        
        QVERIFY(scanSpy.wait(3000));
        QVERIFY(spy.count() >= 4);
    }

private:
    LibrarianService *m_service;
    QString m_testPath;
};

QTEST_MAIN(TestLibrarianService)
#include "test_librarianservice.moc"
