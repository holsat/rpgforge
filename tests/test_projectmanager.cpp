#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"
#include "../src/projectkeys.h"

class TestProjectManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDirPath = QDir::current().absoluteFilePath(QStringLiteral("PM_Test_Dir"));
        QDir().mkpath(m_testDirPath);
        ProjectManager::instance().closeProject();
    }

    void cleanupTestCase()
    {
        ProjectManager::instance().closeProject();
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
        model.setProjectData(pm.model()->projectData());
        QVERIFY(model.rowCount(QModelIndex()) > 0);

        // 4. Close Project
        pm.closeProject();
        QCOMPARE(closeSpy.count(), 1);
        QVERIFY(!pm.isProjectOpen());
    }

    void testProjectPersistence()
    {
        ProjectManager &pm = ProjectManager::instance();
        QString projName = QStringLiteral("PersistenceTest");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;

        // Create and modify
        QVERIFY(pm.createProject(projPath, projName));
        pm.setupDefaultProject(projPath, projName);
        
        QString newAuthor = QStringLiteral("Test Author");
        pm.setAuthor(newAuthor);
        QVERIFY(pm.saveProject());
        pm.closeProject();

        // Re-open and verify
        QString projectFile = QDir(projPath).absoluteFilePath(QStringLiteral("rpgforge.project"));
        QVERIFY(pm.openProject(projectFile));
        QCOMPARE(pm.author(), newAuthor);
        QCOMPARE(pm.projectName(), projName);
        
        pm.closeProject();
    }

    void testActiveFilesListing()
    {
        ProjectManager &pm = ProjectManager::instance();
        QString projName = QStringLiteral("FilesTest");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;

        QVERIFY(pm.createProject(projPath, projName));
        pm.setupDefaultProject(projPath, projName);

        // Relative path registered in setupDefaultProject is "research/README.md"
        QStringList activeFiles = pm.getActiveFiles();
        
        bool foundReadme = false;
        for (const QString &absPath : activeFiles) {
            if (absPath.endsWith(QStringLiteral("research/README.md"))) {
                foundReadme = true;
                break;
            }
        }
        QVERIFY(foundReadme);
        
        pm.closeProject();
    }

    void testAuthoritativeMutation()
    {
        ProjectManager &pm = ProjectManager::instance();
        QString projName = QStringLiteral("MutationTest");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;

        QVERIFY(pm.createProject(projPath, projName));
        pm.setupDefaultProject(projPath, projName);
        
        // 1. Add file (Must exist physically)
        QString relPath = QStringLiteral("research/notes.md");
        QString absPath = QDir(projPath).absoluteFilePath(relPath);
        QFile file(absPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Authoritative content");
        file.close();

        QVERIFY(pm.addFile(QStringLiteral("Notes"), relPath, QStringLiteral("research")));
        
        // Verify in model via authoritative wrapper
        ProjectTreeItem *item = pm.findItem(relPath);
        QVERIFY(item != nullptr);
        QCOMPARE(item->name, QStringLiteral("Notes"));
        
        // 2. Rename
        QVERIFY(pm.renameItem(relPath, QStringLiteral("Project Notes")));
        QCOMPARE(item->name, QStringLiteral("Project Notes"));
        
        // 3. Remove
        QVERIFY(pm.removeItem(relPath));
        QVERIFY(pm.model()->findItem(relPath) == nullptr);
        
        pm.closeProject();
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
            
            // Simulate adding a file via authoritative API
            QString relPath = QStringLiteral("manuscript/stress.md");
            QDir(path).mkpath(QStringLiteral("manuscript"));
            QFile dummy(QDir(path).absoluteFilePath(relPath));
            if (dummy.open(QIODevice::WriteOnly)) {
                dummy.close();
            }

            QVERIFY(pm.addFile(QStringLiteral("StressFile"), relPath));

            pm.closeProject();
            QVERIFY(!pm.isProjectOpen());
        }
    }

private:
    QString m_testDirPath;
};

QTEST_MAIN(TestProjectManager)
#include "test_projectmanager.moc"
