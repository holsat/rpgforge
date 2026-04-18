/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

    Unit tests for TreeNodeSnapshot and ProjectManager's snapshot-based
    read API (Phase 2 of the tree-refactor plan).
*/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"
#include "../src/treenodesnapshot.h"

class TestTreeSnapshot : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ----------------------------------------------------------------
    // TreeNodeSnapshot::find() — pure value-semantic unit tests.
    // ----------------------------------------------------------------

    void testFind_self()
    {
        TreeNodeSnapshot root;
        root.name = QStringLiteral("Root");
        root.path = QString(); // empty path

        TreeNodeSnapshot a;
        a.name = QStringLiteral("A");
        a.path = QStringLiteral("a");

        TreeNodeSnapshot b;
        b.name = QStringLiteral("B");
        b.path = QStringLiteral("a/b");
        a.children.append(b);
        root.children.append(a);

        const TreeNodeSnapshot *hit = root.find(QString());
        QVERIFY(hit != nullptr);
        QCOMPARE(hit->name, root.name);
        QCOMPARE(hit->children.size(), 1);
    }

    void testFind_byPath()
    {
        TreeNodeSnapshot root;
        root.path = QString();

        TreeNodeSnapshot a;
        a.name = QStringLiteral("A");
        a.path = QStringLiteral("a");

        TreeNodeSnapshot b;
        b.name = QStringLiteral("B");
        b.path = QStringLiteral("a/b");
        a.children.append(b);

        TreeNodeSnapshot c;
        c.name = QStringLiteral("C");
        c.path = QStringLiteral("a/b/c");
        a.children[0].children.append(c);

        root.children.append(a);

        const TreeNodeSnapshot *hit = root.find(QStringLiteral("a/b"));
        QVERIFY(hit != nullptr);
        QCOMPARE(hit->name, QStringLiteral("B"));

        const TreeNodeSnapshot *deep = root.find(QStringLiteral("a/b/c"));
        QVERIFY(deep != nullptr);
        QCOMPARE(deep->name, QStringLiteral("C"));
    }

    void testFind_notFound()
    {
        TreeNodeSnapshot root;
        root.path = QString();

        TreeNodeSnapshot a;
        a.path = QStringLiteral("a");
        root.children.append(a);

        QVERIFY(root.find(QStringLiteral("does/not/exist")) == nullptr);
    }

    void testDeepCopy()
    {
        // Build a model, snapshot it, mutate the model, verify the snapshot
        // is unaffected (by-value semantics check).
        ProjectTreeModel model;

        QModelIndex rootIdx = model.addFolder(QStringLiteral("Manuscript"),
                                              QStringLiteral("manuscript"));
        QVERIFY(rootIdx.isValid());
        model.setData(rootIdx, static_cast<int>(ProjectTreeItem::Manuscript),
                      ProjectTreeModel::CategoryRole);

        QModelIndex chapterIdx = model.addFolder(QStringLiteral("Chapter 1"),
                                                 QStringLiteral("manuscript/ch1"),
                                                 rootIdx);
        QVERIFY(chapterIdx.isValid());

        TreeNodeSnapshot snap;
        model.executeUnderLock([&] {
            snap = model.snapshotFrom(model.rootItem());
        });

        // Snapshot holds two levels.
        QCOMPARE(snap.children.size(), 1);
        QCOMPARE(snap.children.first().children.size(), 1);

        // Mutate the live model: rename the chapter.
        model.setData(chapterIdx, QStringLiteral("Renamed"), Qt::EditRole);

        // Snapshot's copy is independent.
        QCOMPARE(snap.children.first().children.first().name,
                 QStringLiteral("Chapter 1"));
    }

    // ----------------------------------------------------------------
    // ProjectManager snapshot-based read API — uses real project setup.
    // ----------------------------------------------------------------

    void initTestCase()
    {
        // Make sure each test gets a clean manager state.
        ProjectManager::instance().closeProject();
    }

    void cleanup()
    {
        ProjectManager::instance().closeProject();
    }

    void testProjectManager_treeSnapshot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(dir.path(), QStringLiteral("SnapshotTest")));
        pm.setupDefaultProject(dir.path(), QStringLiteral("SnapshotTest"));

        const TreeNodeSnapshot root = pm.treeSnapshot();
        // setupDefaultProject adds the project root folder then four authoritative
        // top-level items (manuscript/research/lorekeeper/media). The snapshot
        // mirrors the tree model; rowCount under the QAbstractItemModel root is
        // 1 (the project-name folder), which in turn contains the four children.
        QVERIFY(!root.children.isEmpty());

        bool foundManuscript = false;
        std::function<void(const TreeNodeSnapshot&)> walk = [&](const TreeNodeSnapshot &node) {
            if (node.path == QStringLiteral("manuscript")) foundManuscript = true;
            for (const auto &c : node.children) walk(c);
        };
        walk(root);
        QVERIFY(foundManuscript);
    }

    void testProjectManager_folderSnapshot_rejectsFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(dir.path(), QStringLiteral("FolderRejectTest")));
        pm.setupDefaultProject(dir.path(), QStringLiteral("FolderRejectTest"));

        // setupDefaultProject writes research/README.md via pm.addFile, which
        // in turn calls m_treeModel->addFile — the file is a File in the tree.
        const QString filePath = QStringLiteral("research/README.md");
        QVERIFY(pm.pathExists(filePath));

        QVERIFY(pm.nodeSnapshot(filePath).has_value());
        QVERIFY(!pm.folderSnapshot(filePath).has_value());

        // Folders still return a snapshot.
        QVERIFY(pm.folderSnapshot(QStringLiteral("research")).has_value());
    }

    void testProjectManager_allFilePaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(dir.path(), QStringLiteral("AllFilesTest")));
        pm.setupDefaultProject(dir.path(), QStringLiteral("AllFilesTest"));

        // Add three physical files at different depths. addFile requires
        // the file to exist on disk, so touch each one first.
        const QStringList paths = {
            QStringLiteral("research/a.md"),
            QStringLiteral("manuscript/b.md"),
            QStringLiteral("manuscript/ch1/c.md")
        };
        QDir(dir.path()).mkpath(QStringLiteral("manuscript/ch1"));

        for (const QString &rel : paths) {
            const QString abs = QDir(dir.path()).absoluteFilePath(rel);
            QDir().mkpath(QFileInfo(abs).absolutePath());
            QFile f(abs);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.close();
        }

        QVERIFY(pm.addFile(QStringLiteral("a.md"), QStringLiteral("research/a.md"),
                           QStringLiteral("research")));
        QVERIFY(pm.addFile(QStringLiteral("b.md"), QStringLiteral("manuscript/b.md"),
                           QStringLiteral("manuscript")));
        QVERIFY(pm.addFolder(QStringLiteral("ch1"), QStringLiteral("manuscript/ch1"),
                             QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("c.md"), QStringLiteral("manuscript/ch1/c.md"),
                           QStringLiteral("manuscript/ch1")));

        const QStringList all = pm.allFilePaths();
        for (const QString &p : paths) {
            QVERIFY2(all.contains(p),
                     qPrintable(QStringLiteral("missing path: %1").arg(p)));
        }
    }

    void testProjectManager_pathExists()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(dir.path(), QStringLiteral("PathExistsTest")));
        pm.setupDefaultProject(dir.path(), QStringLiteral("PathExistsTest"));

        QVERIFY(pm.pathExists(QStringLiteral("research")));
        QVERIFY(pm.pathExists(QStringLiteral("manuscript")));
        QVERIFY(pm.pathExists(QStringLiteral("lorekeeper")));
        QVERIFY(pm.pathExists(QStringLiteral("research/README.md")));

        QVERIFY(!pm.pathExists(QStringLiteral("does/not/exist")));
        QVERIFY(!pm.pathExists(QStringLiteral("research/missing.md")));
    }
};

QTEST_GUILESS_MAIN(TestTreeSnapshot)
#include "test_treesnapshot.moc"
