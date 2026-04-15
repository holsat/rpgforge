#include <QtTest>
#include <QSignalSpy>
#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"
#include "../src/agentgatekeeper.h"
#include "../src/librarianservice.h"
#include "../src/llmservice.h"

class TestProjectManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDirPath = QDir::current().filePath(QStringLiteral("PM_Test_Dir"));
        QDir().mkpath(m_testDirPath);
    }

    void cleanupTestCase()
    {
        QDir(m_testDirPath).removeRecursively();
    }

    void testProjectFullLifecycle()
    {
        ProjectManager &pm = ProjectManager::instance();
        QString projName = QStringLiteral("LifecycleTest");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;

        QSignalSpy openSpy(&pm, &ProjectManager::projectOpened);
        QSignalSpy closeSpy(&pm, &ProjectManager::projectClosed);

        // 1. Create Project Shell
        QVERIFY(pm.createProject(projPath, projName));
        
        // 2. Setup Default Structure (This should trigger projectOpened)
        pm.setupDefaultProject(projPath, projName);
        QCOMPARE(openSpy.count(), 1);
        QVERIFY(pm.isProjectOpen());
        QCOMPARE(pm.projectName(), projName);

        // 3. Verify Model Integrity
        ProjectTreeModel model;
        model.setProjectData(pm.tree());
        QVERIFY(model.rowCount(QModelIndex()) > 0);

        // 4. Close Project
        pm.closeProject();
        QCOMPARE(closeSpy.count(), 1);
        QVERIFY(!pm.isProjectOpen());
    }

    void testSchemaMigration()
    {
        ProjectManager &pm = ProjectManager::instance();
        QString projName = QStringLiteral("MigrationTest");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QString projFile = projPath + QStringLiteral("/rpgforge.project");

        QDir().mkpath(projPath);

        // Manually create a v1 project file
        QJsonObject v1Data;
        v1Data[QStringLiteral("name")] = projName;
        v1Data[QStringLiteral("version")] = 1;
        v1Data[QStringLiteral("marginLeft")] = 10.0;
        v1Data[QStringLiteral("marginTop")] = 15.0;
        v1Data[QStringLiteral("marginRight")] = 20.0;
        v1Data[QStringLiteral("marginBottom")] = 25.0;

        QFile file(projFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(v1Data).toJson());
        file.close();

        // Open the v1 project
        QVERIFY(pm.openProject(projFile));
        QVERIFY(pm.isProjectOpen());

        // Verify that margins were migrated to the new nested structure
        QCOMPARE(pm.marginLeft(), 10.0);
        QCOMPARE(pm.marginTop(), 15.0);
        QCOMPARE(pm.marginRight(), 20.0);
        QCOMPARE(pm.marginBottom(), 25.0);

        // Save and verify the version on disk is now 2
        QVERIFY(pm.saveProject());
        pm.closeProject();

        QFile reOpenFile(projFile);
        QVERIFY(reOpenFile.open(QIODevice::ReadOnly));
        QJsonDocument doc = QJsonDocument::fromJson(reOpenFile.readAll());
        QJsonObject data = doc.object();
        QCOMPARE(data.value(QStringLiteral("version")).toInt(), 2);
        QVERIFY(data.contains(QStringLiteral("margins")));
        QVERIFY(!data.contains(QStringLiteral("marginLeft")));

        reOpenFile.close();
    }

    void testStressHarness()
    {
        ProjectManager &pm = ProjectManager::instance();
        
        for (int i = 0; i < 10; ++i) {
            QString name = QStringLiteral("StressProj_%1").arg(i);
            QString path = m_testDirPath + QStringLiteral("/") + name;
            
            qDebug() << "Stress Test Iteration" << i;
            
            QVERIFY(pm.createProject(path, name));
            pm.setupDefaultProject(path, name);
            QVERIFY(pm.isProjectOpen());
            
            // Simulate adding a file
            ProjectTreeModel model;
            model.setProjectData(pm.tree());
            model.addFile(QStringLiteral("StressFile"), QStringLiteral("manuscript/stress.md"), QModelIndex());
            pm.setTree(model.projectData());
            
            pm.closeProject();
            QVERIFY(!pm.isProjectOpen());
        }
    }

private:
    QString m_testDirPath;
};

QTEST_MAIN(TestProjectManager)
#include "test_projectmanager.moc"
