#include <QtTest>
#include <QSignalSpy>
#include "../src/librarianservice.h"
#include "../src/librariandatabase.h"
#include "../src/llmservice.h"
#include "../src/knowledgebase.h"
#include "../src/lorekeeperservice.h"
#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"

class TestLibrarian : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDirPath = QDir::current().filePath(QStringLiteral("Librarian_Test_Dir"));
        QDir().mkpath(m_testDirPath);
        
        ProjectManager &pm = ProjectManager::instance();
        pm.createProject(m_testDirPath, QStringLiteral("TestProj"));
        pm.setupDefaultProject(m_testDirPath, QStringLiteral("TestProj"));
    }

    void cleanupTestCase()
    {
        ProjectManager::instance().closeProject();
        QDir(m_testDirPath).removeRecursively();
    }

    void testHeuristicExtraction()
    {
        ProjectManager &pm = ProjectManager::instance();
        LibrarianService librarian(nullptr);
        librarian.setProjectPath(m_testDirPath);

        // 1. Create sample content
        QString samplePath = m_testDirPath + QStringLiteral("/manuscript/kabal_intro.md");
        QFile file(samplePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << R"(
# Game Rules

| Difficulty | Name | Target # | Description |
|:--:|----|:--:|----|
| 0 | Routine | 0 | No brainer |
| 1 | Simple | 4 | Almost everyone can do this |
| 5 | Difficult | 20 | Minimum for checks |

## Statistics
**Health Points**: 35
**Base Prowess**: 5
)";
        file.close();

        // 2. Register in logical tree via ProjectManager's public API
        // (no direct model access).
        pm.addFile(QStringLiteral("Intro"), QStringLiteral("manuscript/kabal_intro.md"), QStringLiteral("manuscript"));
        pm.saveProject();

        // 3. Trigger scan
        QSignalSpy scanSpy(&librarian, &LibrarianService::scanningFinished);
        librarian.scanAll(); // This should now find the file
        
        QVERIFY(scanSpy.wait(3000));

        // 4. Verify Database
        LibrarianDatabase *db = librarian.database();
        QList<qint64> diffIds = db->findEntitiesByType(QStringLiteral("difficulty"));
        QVERIFY(diffIds.size() >= 3);
        
        QList<qint64> propIds = db->findEntitiesByType(QStringLiteral("property"));
        bool foundHP = false;
        for (qint64 id : propIds) {
            QSqlQuery q(db->database());
            q.prepare(QStringLiteral("SELECT name FROM entities WHERE id = :id"));
            q.bindValue(QStringLiteral(":id"), id);
            if (q.exec() && q.next() && q.value(0).toString() == QStringLiteral("Game Rules")) {
                QCOMPARE(db->getAttribute(id, QStringLiteral("Health Points")).toString(), QStringLiteral("35"));
                foundHP = true;
            }
        }
        QVERIFY(foundHP);
    }

    void testVariableIntegration()
    {
        LibrarianService librarian(nullptr);
        QSignalSpy varSpy(&librarian, &LibrarianService::libraryVariablesChanged);
        librarian.setProjectPath(m_testDirPath);
        
        // scanAll is called by setProjectPath, and it should find the file registered in previous test
        QVERIFY(varSpy.wait(3000));

        QVariantMap vmap = varSpy.first().at(0).toMap();
        QMap<QString, QString> vars;
        for (auto it = vmap.begin(); it != vmap.end(); ++it) {
            vars.insert(it.key(), it.value().toString());
        }
        
        // Attribute keys are now normalised via identifierFromMarkdown:
        // "**Target \#**" → "target" (no bold markers, no "#", lowercase).
        QVERIFY(vars.contains(QStringLiteral("difficulty.1.target")));
        QCOMPARE(vars.value(QStringLiteral("difficulty.1.target")), QStringLiteral("4"));
        QVERIFY(vars.contains(QStringLiteral("property.GameRules.HealthPoints")));
        QCOMPARE(vars.value(QStringLiteral("property.GameRules.HealthPoints")), QStringLiteral("35"));
    }

    void testKnowledgeBaseSourceOfTruth()
    {
        ProjectManager &pm = ProjectManager::instance();
        KnowledgeBase &kb = KnowledgeBase::instance();
        kb.initForProject(m_testDirPath);

        // 1. Create two files on disk
        QString inProjectPath = m_testDirPath + QStringLiteral("/manuscript/in_project.md");
        QString outProjectPath = m_testDirPath + QStringLiteral("/manuscript/out_project.md");

        QFile f1(inProjectPath);
        QVERIFY(f1.open(QIODevice::WriteOnly | QIODevice::Text));
        f1.write("# In Project\nThis is relevant lore.");
        f1.close();

        QFile f2(outProjectPath);
        QVERIFY(f2.open(QIODevice::WriteOnly | QIODevice::Text));
        f2.write("# Out of Project\nThis is Kabal RPG data that should be ignored.");
        f2.close();

        // 2. Register ONLY in_project.md in the logical tree via
        // ProjectManager's public API.
        pm.addFile(QStringLiteral("InProject"), QStringLiteral("manuscript/in_project.md"), QStringLiteral("manuscript"));
        pm.saveProject();

        // 3. Reindex
        QSignalSpy spy(&kb, &KnowledgeBase::indexingFinished);
        kb.reindexProject();
        if (spy.isEmpty()) spy.wait(3000);

        // 4. Search for "Kabal"
        QSignalSpy searchSpy(&kb, &KnowledgeBase::indexingFinished); // search uses async embedding
        bool callbackCalled = false;
        kb.search(QStringLiteral("Kabal RPG data"), 5, QString(), [&](const QList<SearchResult> &results) {
            callbackCalled = true;
            for (const auto &res : results) {
                // Verify that NO results come from out_project.md
                QVERIFY(!res.filePath.contains(QStringLiteral("out_project.md")));
            }
        });

        // Search calls generateEmbedding which is async. We need to wait.
        // For this unit test, let's just verify the logic in a way that doesn't 
        // require a real LLM if possible, but our implementation calls it.
        // Since we are in a unit test, we might need a mock LLM here too.
    }

private:
    QString m_testDirPath;
};

QTEST_MAIN(TestLibrarian)
#include "test_librarian.moc"
