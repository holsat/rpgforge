#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
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

    void cleanup()
    {
        ProjectManager::instance().closeProject();
    }

    // ---------------------------------------------------------------
    // Existing tests (preserved)
    // ---------------------------------------------------------------

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
        model.setProjectData(pm.treeData());
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

        // 1. Add file. As of Phase 3, addFile creates the empty file on disk
        // if it doesn't exist — no need to pre-create.
        QString relPath = QStringLiteral("research/notes.md");

        QVERIFY(pm.addFile(QStringLiteral("Notes"), relPath, QStringLiteral("research")));

        // Verify in model via authoritative wrapper AND on disk.
        ProjectTreeItem *item = pm.findItem(relPath);
        QVERIFY(item != nullptr);
        QCOMPARE(item->name, QStringLiteral("Notes"));
        QVERIFY(QFileInfo(QDir(projPath).absoluteFilePath(relPath)).isFile());

        // 2. Rename — Phase 3 renames on disk too and updates the path field.
        QVERIFY(pm.renameItem(relPath, QStringLiteral("Project Notes")));
        QCOMPARE(item->name, QStringLiteral("Project Notes"));

        const QString newRelPath = QStringLiteral("research/Project Notes.md");
        QCOMPARE(item->path, newRelPath);
        QVERIFY(!QFileInfo(QDir(projPath).absoluteFilePath(relPath)).exists());
        QVERIFY(QFileInfo(QDir(projPath).absoluteFilePath(newRelPath)).isFile());

        // 3. Remove via the new path.
        QVERIFY(pm.removeItem(newRelPath));
        QVERIFY(pm.findItem(newRelPath) == nullptr);
        QVERIFY(!QFileInfo(QDir(projPath).absoluteFilePath(newRelPath)).exists());

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

    // ---------------------------------------------------------------
    // Migration: string type values become integers
    // ---------------------------------------------------------------

    void test_migrate_stringTypeToInteger()
    {
        // Write a project file with string "type" values, then open it.
        // Migration should convert them to integers.
        QString projName = QStringLiteral("MigrateStringType");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("notes.md");
        child[QStringLiteral("path")] = QStringLiteral("research/notes.md");
        child[QStringLiteral("type")] = QStringLiteral("file");   // string, needs migration
        child[QStringLiteral("category")] = 2; // Research (int)
        child[QStringLiteral("children")] = QJsonArray();

        tree[QStringLiteral("children")] = QJsonArray{child};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = 2;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        // Phase 6: disk is authoritative; create the backing file.
        QDir(projPath).mkpath(QStringLiteral("research"));
        QFile notesFile(QDir(projPath).absoluteFilePath(QStringLiteral("research/notes.md")));
        QVERIFY(notesFile.open(QIODevice::WriteOnly));
        notesFile.close();

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        // After migration + load, the item should be File type
        ProjectTreeItem *item = pm.findItem(QStringLiteral("research/notes.md"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::File);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // Migration: string category values become integers
    // ---------------------------------------------------------------

    void test_migrate_stringCategoryToInteger()
    {
        QString projName = QStringLiteral("MigrateStringCat");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("LoreKeeper");
        child[QStringLiteral("path")] = QStringLiteral("lorekeeper");
        child[QStringLiteral("type")] = 0;
        child[QStringLiteral("category")] = QStringLiteral("lorekeeper"); // string, needs migration
        child[QStringLiteral("children")] = QJsonArray();

        tree[QStringLiteral("children")] = QJsonArray{child};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = 2;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("lorekeeper"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->category, ProjectTreeItem::LoreKeeper);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // Migration: type=0 (Folder) with file extension -> File
    // ---------------------------------------------------------------

    void test_migrate_type0WithFileExtensionBecomesFile()
    {
        QString projName = QStringLiteral("MigrateType0Ext");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("chapter1.md");
        child[QStringLiteral("path")] = QStringLiteral("manuscript/chapter1.md");
        child[QStringLiteral("type")] = 0;  // Folder (wrong, has .md extension)
        child[QStringLiteral("category")] = 1; // Manuscript
        child[QStringLiteral("children")] = QJsonArray();

        tree[QStringLiteral("children")] = QJsonArray{child};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = 2;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        // Phase 6: materialise the backing file on disk.
        QDir(projPath).mkpath(QStringLiteral("manuscript"));
        QFile chFile(QDir(projPath).absoluteFilePath(QStringLiteral("manuscript/chapter1.md")));
        QVERIFY(chFile.open(QIODevice::WriteOnly));
        chFile.close();

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("manuscript/chapter1.md"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::File);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // validateTree: Folder->File for items with file extensions
    // ---------------------------------------------------------------

    void test_validateTree_folderToFileForExtension()
    {
        // Create a valid project, then manipulate the tree model to have
        // a misclassified node, save, reopen and check validateTree fixed it.
        QString projName = QStringLiteral("ValidateFolderToFile");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(projPath, projName));
        pm.setupDefaultProject(projPath, projName);

        // Inject a misclassified node: type=Folder but path has .md extension
        QJsonObject data = pm.treeData();
        QJsonArray children = data.value(QStringLiteral("children")).toArray();

        QJsonObject badNode;
        badNode[QStringLiteral("name")] = QStringLiteral("misclassified.txt");
        badNode[QStringLiteral("path")] = QStringLiteral("research/misclassified.txt");
        badNode[QStringLiteral("type")] = 0; // Folder (wrong)
        badNode[QStringLiteral("category")] = 2;
        badNode[QStringLiteral("children")] = QJsonArray();

        children.append(badNode);
        data[QStringLiteral("children")] = children;

        // Write the corrupted tree to disk
        QJsonObject projectObj;
        projectObj[QLatin1String(ProjectKeys::Name)] = projName;
        projectObj[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        projectObj[QLatin1String(ProjectKeys::Tree)] = data;

        pm.closeProject();
        writeProjectFile(projPath, projectObj);

        // Phase 6: materialise the real file on disk — buildFromDisk sees
        // it as a File (it's a regular file), which is the correct shape
        // regardless of what the legacy JSON said.
        QFile f(QDir(projPath).absoluteFilePath(QStringLiteral("research/misclassified.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        // Re-open: buildFromDisk sees a real file and creates a File node.
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("research/misclassified.txt"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::File);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // validateTree: File->Folder for items with children
    // ---------------------------------------------------------------

    void test_validateTree_fileToFolderWithChildren()
    {
        QString projName = QStringLiteral("ValidateFileToFolder");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        // Build a tree where a node is type=File but has children
        QJsonObject childFile;
        childFile[QStringLiteral("name")] = QStringLiteral("nested.md");
        childFile[QStringLiteral("path")] = QStringLiteral("bad/nested.md");
        childFile[QStringLiteral("type")] = 1;
        childFile[QStringLiteral("category")] = 0;
        childFile[QStringLiteral("children")] = QJsonArray();

        QJsonObject badParent;
        badParent[QStringLiteral("name")] = QStringLiteral("bad");
        badParent[QStringLiteral("path")] = QStringLiteral("bad");
        badParent[QStringLiteral("type")] = 1; // File (wrong, has children)
        badParent[QStringLiteral("category")] = 0;
        badParent[QStringLiteral("children")] = QJsonArray{childFile};

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{badParent};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        // Phase 6: materialise the folder + child on disk. buildFromDisk
        // creates a Folder for the directory and a File for the child.
        QDir(projPath).mkpath(QStringLiteral("bad"));
        QFile nested(QDir(projPath).absoluteFilePath(QStringLiteral("bad/nested.md")));
        QVERIFY(nested.open(QIODevice::WriteOnly));
        nested.close();

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("bad"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::Folder);
        QCOMPARE(item->children.count(), 1);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // validateTree: empty paths for Research/LoreKeeper/Manuscript
    // ---------------------------------------------------------------

    void test_validateTree_emptyResearchPath()
    {
        QString projName = QStringLiteral("ValidateResearchPath");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject research;
        research[QStringLiteral("name")] = QStringLiteral("Research");
        research[QStringLiteral("path")] = QString(); // empty path
        research[QStringLiteral("type")] = 0; // Folder
        research[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Research);
        research[QStringLiteral("children")] = QJsonArray();

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{research};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("research"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->path, QStringLiteral("research"));

        pm.closeProject();
    }

    void test_validateTree_emptyLoreKeeperPath()
    {
        QString projName = QStringLiteral("ValidateLKPath");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject lorekeeper;
        lorekeeper[QStringLiteral("name")] = QStringLiteral("LoreKeeper");
        lorekeeper[QStringLiteral("path")] = QString();
        lorekeeper[QStringLiteral("type")] = 0;
        lorekeeper[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::LoreKeeper);
        lorekeeper[QStringLiteral("children")] = QJsonArray();

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{lorekeeper};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("lorekeeper"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->path, QStringLiteral("lorekeeper"));

        pm.closeProject();
    }

    void test_validateTree_emptyManuscriptPath()
    {
        QString projName = QStringLiteral("ValidateManuscriptPath");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject manuscript;
        manuscript[QStringLiteral("name")] = QStringLiteral("Manuscript");
        manuscript[QStringLiteral("path")] = QString();
        manuscript[QStringLiteral("type")] = 0;
        manuscript[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Manuscript);
        manuscript[QStringLiteral("children")] = QJsonArray();

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{manuscript};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("manuscript"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->path, QStringLiteral("manuscript"));

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // Migration: recursive child migration
    // ---------------------------------------------------------------

    void test_migrate_recursiveStringFixesInChildren()
    {
        QString projName = QStringLiteral("MigrateRecursive");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject grandchild;
        grandchild[QStringLiteral("name")] = QStringLiteral("scene.md");
        grandchild[QStringLiteral("path")] = QStringLiteral("manuscript/ch1/scene.md");
        grandchild[QStringLiteral("type")] = QStringLiteral("file");
        grandchild[QStringLiteral("category")] = QStringLiteral("scene");
        grandchild[QStringLiteral("children")] = QJsonArray();

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("Chapter 1");
        child[QStringLiteral("path")] = QStringLiteral("manuscript/ch1");
        child[QStringLiteral("type")] = QStringLiteral("folder");
        child[QStringLiteral("category")] = QStringLiteral("chapter");
        child[QStringLiteral("children")] = QJsonArray{grandchild};

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{child};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = 2;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);

        // Phase 6: tree is sourced from disk, so the fixture directories
        // and files that the legacy JSON declared must actually exist for
        // the tree to carry them through. Create the disk structure that
        // matches the JSON above; the legacy-metadata migration then
        // decorates the tree nodes with the Chapter / Scene categories
        // via nodeMetadata.categoryOverride.
        QDir(projPath).mkpath(QStringLiteral("manuscript/ch1"));
        QFile sceneFile(QDir(projPath).absoluteFilePath(QStringLiteral("manuscript/ch1/scene.md")));
        QVERIFY(sceneFile.open(QIODevice::WriteOnly));
        sceneFile.close();

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *ch = pm.findItem(QStringLiteral("manuscript/ch1"));
        QVERIFY(ch != nullptr);
        QCOMPARE(ch->type, ProjectTreeItem::Folder);
        QCOMPARE(ch->category, ProjectTreeItem::Chapter);

        ProjectTreeItem *scene = pm.findItem(QStringLiteral("manuscript/ch1/scene.md"));
        QVERIFY(scene != nullptr);
        QCOMPARE(scene->type, ProjectTreeItem::File);
        QCOMPARE(scene->category, ProjectTreeItem::Scene);

        pm.closeProject();
    }

    // ---------------------------------------------------------------
    // Migration: unknown string category defaults to 0 (None)
    // ---------------------------------------------------------------

    void test_migrate_unknownStringCategoryDefaultsToNone()
    {
        QString projName = QStringLiteral("MigrateUnknownCat");
        QString projPath = m_testDirPath + QStringLiteral("/") + projName;
        QDir().mkpath(projPath);

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("Mystery");
        child[QStringLiteral("path")] = QStringLiteral("mystery");
        child[QStringLiteral("type")] = 0;
        child[QStringLiteral("category")] = QStringLiteral("nonexistent_category");
        child[QStringLiteral("children")] = QJsonArray();

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray{child};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = projName;
        project[QLatin1String(ProjectKeys::Version)] = 2;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectFile(projPath, project);
        QDir(projPath).mkpath(QStringLiteral("mystery"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(projectFilePath(projPath)));

        ProjectTreeItem *item = pm.findItem(QStringLiteral("mystery"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->category, ProjectTreeItem::None);

        pm.closeProject();
    }

private:
    QString m_testDirPath;

    void writeProjectFile(const QString &projPath, const QJsonObject &data)
    {
        QDir().mkpath(projPath);
        QString filePath = QDir(projPath).absoluteFilePath(QStringLiteral("rpgforge.project"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(data).toJson());
        file.close();
    }

    QString projectFilePath(const QString &projPath)
    {
        return QDir(projPath).absoluteFilePath(QStringLiteral("rpgforge.project"));
    }
};

QTEST_MAIN(TestProjectManager)
#include "test_projectmanager.moc"
