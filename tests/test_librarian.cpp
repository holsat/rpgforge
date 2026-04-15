#include <QtTest>
#include <QSignalSpy>
#include "../src/librarianservice.h"
#include "../src/librariandatabase.h"
#include "../src/llmservice.h"
#include "../src/characterdossierservice.h"
#include "../src/projectmanager.h"

class TestLibrarian : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDirPath = QDir::current().filePath(QStringLiteral("Librarian_Test_Dir"));
        QDir().mkpath(m_testDirPath);
        QDir().mkpath(m_testDirPath + QStringLiteral("/manuscript"));
        QDir().mkpath(m_testDirPath + QStringLiteral("/library/Character Sketches"));
    }

    void cleanupTestCase()
    {
        QDir(m_testDirPath).removeRecursively();
    }

    void testHeuristicExtraction()
    {
        LibrarianService librarian(nullptr);
        librarian.setProjectPath(m_testDirPath);

        // Copy sample content to a file in manuscript
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

        // Trigger scan
        QSignalSpy scanSpy(&librarian, &LibrarianService::scanningFinished);
        librarian.scanFile(samplePath);
        
        // Wait for debounce and processing (max 2s)
        QVERIFY(scanSpy.wait(2000));

        // Verify Database
        LibrarianDatabase *db = librarian.database();
        
        // Check table extraction
        QList<qint64> diffIds = db->findEntitiesByType(QStringLiteral("difficulty"));
        QCOMPARE(diffIds.size(), 3);
        
        // Check "1" difficulty
        qint64 id1 = -1;
        for (qint64 id : diffIds) {
            if (db->getAttribute(id, QStringLiteral("name")).toString() == QStringLiteral("Simple")) {
                id1 = id;
                break;
            }
        }
        QVERIFY(id1 != -1);
        QCOMPARE(db->getAttribute(id1, QStringLiteral("target#")).toString(), QStringLiteral("4"));

        // Check list extraction (section-aware)
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
        
        // Wait for variables to be emitted after scan
        QVERIFY(varSpy.wait(2000));

        QVariantMap vmap = varSpy.first().at(0).toMap();
        QMap<QString, QString> vars;
        for (auto it = vmap.begin(); it != vmap.end(); ++it) {
            vars.insert(it.key(), it.value().toString());
        }
        
        // Verify formatted keys: type.name.key
        // difficulty -> name "1" -> key "target#"
        QVERIFY(vars.contains(QStringLiteral("difficulty.1.target#")));
        QCOMPARE(vars.value(QStringLiteral("difficulty.1.target#")), QStringLiteral("4"));
        
        // property -> name "GameRules" -> key "HealthPoints"
        QVERIFY(vars.contains(QStringLiteral("property.GameRules.HealthPoints")));
        QCOMPARE(vars.value(QStringLiteral("property.GameRules.HealthPoints")), QStringLiteral("35"));
    }

    void testKabalExtraction()
    {
        LibrarianService librarian(nullptr);
        librarian.setProjectPath(m_testDirPath);

        QString samplePath = m_testDirPath + QStringLiteral("/manuscript/kabal_full.md");
        QFile file(samplePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        // Use a subset of the Kabal content that has mechanics
        out << R"(
# The Game
| Difficulty | Name | Target # | Description |
|:--:|----|:--:|----|
| 0 | Routine | 0 | No brainer |
| 1 | Simple | 4 | Almost everyone |
| 2 | Standard | 8 | Concentration |

### Arakasha
##### STATISTICS
**Health Points**: 35
**Notable characteristics**: Resistant to heat.

### Humans
##### STATISTICS
**Health Points**: 24
)";
        file.close();

        QSignalSpy scanSpy(&librarian, &LibrarianService::scanningFinished);
        librarian.scanFile(samplePath);
        QVERIFY(scanSpy.wait(3000));

        QSignalSpy varSpy(&librarian, &LibrarianService::libraryVariablesChanged);
        // Trigger a fake queue process to get variables
        librarian.scanFile(samplePath); 
        QVERIFY(varSpy.wait(3000));

        QVariantMap vmap = varSpy.last().at(0).toMap();
        QMap<QString, QString> vars;
        for (auto it = vmap.begin(); it != vmap.end(); ++it) {
            vars.insert(it.key(), it.value().toString());
        }

        // Verify Difficulty Table
        QCOMPARE(vars.value(QStringLiteral("difficulty.1.target#")), QStringLiteral("4"));
        QCOMPARE(vars.value(QStringLiteral("difficulty.2.target#")), QStringLiteral("8"));

        // Verify Arakasha Stats
        QVERIFY(vars.contains(QStringLiteral("property.Arakasha.HealthPoints")));
        QCOMPARE(vars.value(QStringLiteral("property.Arakasha.HealthPoints")), QStringLiteral("35"));

        // Verify Human Stats
        QVERIFY(vars.contains(QStringLiteral("property.Humans.HealthPoints")));
        QCOMPARE(vars.value(QStringLiteral("property.Humans.HealthPoints")), QStringLiteral("24"));
    }

private:
    QString m_testDirPath;
};

QTEST_MAIN(TestLibrarian)
#include "test_librarian.moc"
