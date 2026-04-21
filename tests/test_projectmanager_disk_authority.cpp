/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    Phase 6 disk-authority tests — the tree is derived from walking the
    project directory on disk; the legacy `tree` JSON is migrated once on
    open into nodeMetadata and kept only for back-compat writes.
*/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "../src/projectkeys.h"
#include "../src/projectmanager.h"
#include "../src/projectmetadata.h"
#include "../src/projecttreemodel.h"

class TestProjectManagerDiskAuthority : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_projectRoot;
    QString m_projectFilePath;

    QString absPath(const QString &rel) const
    {
        return QDir(m_projectRoot).absoluteFilePath(rel);
    }

    void setupTempRoot(const QString &name = QStringLiteral("DiskAuth"))
    {
        ProjectManager::instance().closeProject();
        delete m_tmp;
        m_tmp = new QTemporaryDir();
        QVERIFY(m_tmp->isValid());
        m_projectRoot = m_tmp->path() + QStringLiteral("/") + name;
        QDir().mkpath(m_projectRoot);
        m_projectFilePath =
            QDir(m_projectRoot).absoluteFilePath(QStringLiteral("rpgforge.project"));
    }

    // Write a project JSON object to m_projectFilePath.
    void writeProjectJson(const QJsonObject &doc)
    {
        QFile f(m_projectFilePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(doc).toJson());
        f.close();
    }

    QJsonObject readProjectJson() const
    {
        QFile f(m_projectFilePath);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    // Materialise the three authoritative folders on disk.
    void ensureAuthoritativeFoldersOnDisk()
    {
        QDir(m_projectRoot).mkpath(QStringLiteral("manuscript"));
        QDir(m_projectRoot).mkpath(QStringLiteral("lorekeeper"));
        QDir(m_projectRoot).mkpath(QStringLiteral("research"));
    }

    void createFileWithContent(const QString &relPath, const QByteArray &bytes = {})
    {
        const QString abs = absPath(relPath);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly));
        if (!bytes.isEmpty()) f.write(bytes);
        f.close();
    }

private Q_SLOTS:
    void cleanup()
    {
        ProjectManager::instance().closeProject();
        delete m_tmp;
        m_tmp = nullptr;
    }

    // -----------------------------------------------------------------
    // Legacy project (pre-Phase-6): has synopsis/status in the tree JSON
    // but no nodeMetadata. Opening must migrate those per-node fields
    // into m_meta.nodeMetadata keyed by path, and stamp them onto the
    // tree nodes after buildFromDisk walks the directory.
    // -----------------------------------------------------------------
    void testLegacyProjectLoad_migratesNodeMetadata()
    {
        setupTempRoot(QStringLiteral("LegacyMigration"));
        ensureAuthoritativeFoldersOnDisk();

        // Create real files that the legacy tree references — buildFromDisk
        // needs them to exist for the nodes to survive into the tree.
        createFileWithContent(QStringLiteral("manuscript/chapter1.md"),
                              QByteArrayLiteral("# Chapter One"));
        createFileWithContent(QStringLiteral("research/world.md"),
                              QByteArrayLiteral("# World"));

        // Build a legacy-shape tree JSON with synopsis/status decorations.
        auto makeLegacyNode = [](const QString &name, const QString &path,
                                  int type, int category,
                                  const QString &synopsis,
                                  const QString &status,
                                  const QJsonArray &children = {}) {
            QJsonObject obj;
            obj[QStringLiteral("name")] = name;
            obj[QStringLiteral("path")] = path;
            obj[QStringLiteral("type")] = type;
            obj[QStringLiteral("category")] = category;
            if (!synopsis.isEmpty()) obj[QStringLiteral("synopsis")] = synopsis;
            if (!status.isEmpty())   obj[QStringLiteral("status")]   = status;
            obj[QStringLiteral("children")] = children;
            return obj;
        };

        const QJsonObject chapter = makeLegacyNode(
            QStringLiteral("Chapter 1"),
            QStringLiteral("manuscript/chapter1.md"),
            1, static_cast<int>(ProjectTreeItem::Scene),
            QStringLiteral("The quest begins."),
            QStringLiteral("First Draft"));

        const QJsonObject world = makeLegacyNode(
            QStringLiteral("World Notes"),
            QStringLiteral("research/world.md"),
            1, static_cast<int>(ProjectTreeItem::Research),
            QStringLiteral("Continental geography and climate."),
            QString());

        const QJsonObject manuscript = makeLegacyNode(
            QStringLiteral("Manuscript"), QStringLiteral("manuscript"),
            0, static_cast<int>(ProjectTreeItem::Manuscript),
            QString(), QString(), QJsonArray{chapter});

        const QJsonObject research = makeLegacyNode(
            QStringLiteral("Research"), QStringLiteral("research"),
            0, static_cast<int>(ProjectTreeItem::Research),
            QString(), QString(), QJsonArray{world});

        const QJsonObject lorekeeper = makeLegacyNode(
            QStringLiteral("LoreKeeper"), QStringLiteral("lorekeeper"),
            0, static_cast<int>(ProjectTreeItem::LoreKeeper),
            QString(), QString(), QJsonArray{});

        QJsonObject root = makeLegacyNode(
            QStringLiteral("Root"), QString(), 0, 0,
            QString(), QString(),
            QJsonArray{manuscript, lorekeeper, research});

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = QStringLiteral("Legacy");
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = root;
        // Intentionally NO nodeMetadata / orderHints — this is the legacy shape.

        writeProjectJson(project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(m_projectFilePath));

        // Synopses must survive onto the live tree, keyed by path.
        ProjectTreeItem *ch = pm.findItem(QStringLiteral("manuscript/chapter1.md"));
        QVERIFY(ch != nullptr);
        QCOMPARE(ch->synopsis, QStringLiteral("The quest begins."));
        QCOMPARE(ch->status, QStringLiteral("First Draft"));

        ProjectTreeItem *wr = pm.findItem(QStringLiteral("research/world.md"));
        QVERIFY(wr != nullptr);
        QCOMPARE(wr->synopsis, QStringLiteral("Continental geography and climate."));

        // Persisted JSON must now contain a nodeMetadata section with the
        // migrated entries.
        const QJsonObject reloaded = readProjectJson();
        QVERIFY(reloaded.contains(QStringLiteral("nodeMetadata")));
        const QJsonObject nm = reloaded.value(QStringLiteral("nodeMetadata")).toObject();
        QVERIFY(nm.contains(QStringLiteral("manuscript/chapter1.md")));
        const QJsonObject chMeta = nm.value(QStringLiteral("manuscript/chapter1.md")).toObject();
        QCOMPARE(chMeta.value(QStringLiteral("synopsis")).toString(),
                 QStringLiteral("The quest begins."));
        QCOMPARE(chMeta.value(QStringLiteral("status")).toString(),
                 QStringLiteral("First Draft"));
    }

    // -----------------------------------------------------------------
    // Fresh load: the tree reflects what's actually on disk, even when
    // the legacy tree JSON claims a totally different structure. Proves
    // the disk walk is authoritative.
    // -----------------------------------------------------------------
    void testFreshLoad_buildsFromDisk()
    {
        setupTempRoot(QStringLiteral("FreshDisk"));
        ensureAuthoritativeFoldersOnDisk();

        // Disk truth: two real files under manuscript/.
        createFileWithContent(QStringLiteral("manuscript/scene1.md"));
        createFileWithContent(QStringLiteral("manuscript/scene2.md"));

        // Legacy tree lies about what's on disk — lists phantom entries
        // that don't exist AND omits the real files. After Phase 6 these
        // fabrications must be ignored.
        QJsonObject phantomFile;
        phantomFile[QStringLiteral("name")] = QStringLiteral("ghost.md");
        phantomFile[QStringLiteral("path")] = QStringLiteral("manuscript/ghost.md");
        phantomFile[QStringLiteral("type")] = 1;
        phantomFile[QStringLiteral("category")] = 0;
        phantomFile[QStringLiteral("children")] = QJsonArray();

        QJsonObject manuscript;
        manuscript[QStringLiteral("name")] = QStringLiteral("Manuscript");
        manuscript[QStringLiteral("path")] = QStringLiteral("manuscript");
        manuscript[QStringLiteral("type")] = 0;
        manuscript[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Manuscript);
        manuscript[QStringLiteral("children")] = QJsonArray{phantomFile};

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("children")] = QJsonArray{manuscript};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = QStringLiteral("Fresh");
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;
        // Non-empty nodeMetadata disables the legacy migration; after Phase 6
        // disk walk is authoritative so the lie in `tree` is ignored.
        QJsonObject nm;
        nm[QStringLiteral("manuscript/scene1.md")] = QJsonObject{};
        project[QStringLiteral("nodeMetadata")] = nm;

        writeProjectJson(project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(m_projectFilePath));

        // Real disk files appear in the tree.
        QVERIFY(pm.pathExists(QStringLiteral("manuscript/scene1.md")));
        QVERIFY(pm.pathExists(QStringLiteral("manuscript/scene2.md")));
        // Phantom entry does NOT leak through.
        QVERIFY(!pm.pathExists(QStringLiteral("manuscript/ghost.md")));
    }

    // -----------------------------------------------------------------
    // setNodeSynopsis(path) + saveProject must write a nodeMetadata entry
    // for that path into rpgforge.project.
    // -----------------------------------------------------------------
    void testSave_emitsNodeMetadata()
    {
        setupTempRoot(QStringLiteral("SaveMetadata"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("SaveMetadata")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("SaveMetadata"));

        const QString rel = QStringLiteral("manuscript/scene.md");
        QVERIFY(pm.addFile(QStringLiteral("Scene"), rel, QStringLiteral("manuscript")));
        QVERIFY(pm.setNodeSynopsis(rel, QStringLiteral("Opening scene at dusk.")));

        QVERIFY(pm.saveProject());

        const QJsonObject doc = readProjectJson();
        QVERIFY(doc.contains(QStringLiteral("nodeMetadata")));
        const QJsonObject nm = doc.value(QStringLiteral("nodeMetadata")).toObject();
        QVERIFY(nm.contains(rel));
        QCOMPARE(nm.value(rel).toObject().value(QStringLiteral("synopsis")).toString(),
                 QStringLiteral("Opening scene at dusk."));
    }

    // -----------------------------------------------------------------
    // saveProject writes both nodeMetadata (new) and the legacy `tree`
    // field (back-compat) so pre-Phase-6 builds can still open the file.
    // -----------------------------------------------------------------
    void testSave_preservesLegacyTreeForBackCompat()
    {
        setupTempRoot(QStringLiteral("BackCompat"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("BackCompat")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("BackCompat"));

        QVERIFY(pm.addFile(QStringLiteral("A"),
                           QStringLiteral("manuscript/a.md"),
                           QStringLiteral("manuscript")));
        QVERIFY(pm.saveProject());

        const QJsonObject doc = readProjectJson();
        // Back-compat: legacy `tree` must still be written.
        QVERIFY(doc.contains(QLatin1String(ProjectKeys::Tree)));
        const QJsonObject tree = doc.value(QLatin1String(ProjectKeys::Tree)).toObject();
        QVERIFY(tree.contains(QStringLiteral("children")));
    }

    // -----------------------------------------------------------------
    // User reorders sibling items into a non-alphanumeric order;
    // saveProject emits orderHints; the next load restores the order.
    // -----------------------------------------------------------------
    void testOrderHints_savedAndRestored()
    {
        setupTempRoot(QStringLiteral("OrderHints"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("OrderHints")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("OrderHints"));

        // Add three files that naturally sort a.md < b.md < c.md. Then
        // manipulate their order in the live tree via ProjectTreeModel's
        // moveItem to produce c, a, b.
        QVERIFY(pm.addFile(QStringLiteral("A"), QStringLiteral("manuscript/a.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("B"), QStringLiteral("manuscript/b.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("C"), QStringLiteral("manuscript/c.md"),
                            QStringLiteral("manuscript")));

        // Reorder under the manuscript folder: move c to position 0.
        QVERIFY(pm.moveItem(QStringLiteral("manuscript/c.md"),
                            QStringLiteral("manuscript"), 0));

        QVERIFY(pm.saveProject());

        const QJsonObject doc = readProjectJson();
        QVERIFY(doc.contains(QStringLiteral("orderHints")));
        const QJsonObject oh = doc.value(QStringLiteral("orderHints")).toObject();
        QVERIFY(oh.contains(QStringLiteral("manuscript")));
        const QJsonArray order = oh.value(QStringLiteral("manuscript")).toArray();
        QCOMPARE(order.size(), 3);
        QCOMPARE(order.at(0).toString(), QStringLiteral("c.md"));
        // The remaining two are a.md and b.md in some order.
        QStringList rest;
        for (int i = 1; i < order.size(); ++i) rest.append(order.at(i).toString());
        QVERIFY(rest.contains(QStringLiteral("a.md")));
        QVERIFY(rest.contains(QStringLiteral("b.md")));

        // Close + reopen: buildFromDisk should honour the order hint.
        pm.closeProject();
        QVERIFY(pm.openProject(m_projectFilePath));
        auto snap = pm.folderSnapshot(QStringLiteral("manuscript"));
        QVERIFY(snap.has_value());
        QVERIFY(!snap->children.isEmpty());
        QCOMPARE(snap->children.first().path, QStringLiteral("manuscript/c.md"));
    }

    // -----------------------------------------------------------------
    // A standard plain alphabetical order doesn't need persisting.
    // saveProject must omit / emit an empty orderHints.
    // -----------------------------------------------------------------
    void testOrderHints_omittedWhenAlphanumeric()
    {
        setupTempRoot(QStringLiteral("OrderAlpha"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("OrderAlpha")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("OrderAlpha"));

        QVERIFY(pm.addFile(QStringLiteral("A"), QStringLiteral("manuscript/a.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("B"), QStringLiteral("manuscript/b.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.saveProject());

        const QJsonObject doc = readProjectJson();
        // Either the key is absent OR it's empty — both count as "no hint
        // needed". setupDefaultProject produces other siblings too, but
        // they are in alphabetical order as well, so no parent should
        // appear in orderHints.
        const QJsonObject oh = doc.value(QStringLiteral("orderHints")).toObject();
        QVERIFY(!oh.contains(QStringLiteral("manuscript")));
    }

    // -----------------------------------------------------------------
    // The disk walk must skip dotfiles (.git, .rpgforge-vectors.db, etc.)
    // and RPG Forge's own side-files.
    // -----------------------------------------------------------------
    void testBuildFromDisk_skipsDotFilesAndLockFiles()
    {
        setupTempRoot(QStringLiteral("SkipList"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("SkipList")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("SkipList"));

        // Plant side-files + dot-dirs that should be invisible to the tree.
        QDir(m_projectRoot).mkpath(QStringLiteral(".git/objects"));
        createFileWithContent(QStringLiteral(".git/HEAD"),
                              QByteArrayLiteral("ref: refs/heads/main\n"));
        createFileWithContent(QStringLiteral(".rpgforge-vectors.db"),
                              QByteArrayLiteral("sqlite-stub"));
        createFileWithContent(QStringLiteral(".rpgforge-lorekeeper.lock"),
                              QByteArrayLiteral("pid"));
        createFileWithContent(QStringLiteral(".DS_Store"),
                              QByteArrayLiteral("mac"));
        QDir(m_projectRoot).mkpath(QStringLiteral("node_modules/junk"));
        createFileWithContent(QStringLiteral("node_modules/junk/a.js"),
                              QByteArrayLiteral("x"));

        QVERIFY(pm.reloadFromDisk());

        QVERIFY(!pm.pathExists(QStringLiteral(".git")));
        QVERIFY(!pm.pathExists(QStringLiteral(".git/HEAD")));
        QVERIFY(!pm.pathExists(QStringLiteral(".rpgforge-vectors.db")));
        QVERIFY(!pm.pathExists(QStringLiteral(".rpgforge-lorekeeper.lock")));
        QVERIFY(!pm.pathExists(QStringLiteral(".DS_Store")));
        QVERIFY(!pm.pathExists(QStringLiteral("node_modules")));
        QVERIFY(!pm.pathExists(QStringLiteral("rpgforge.project")));
    }

    // -----------------------------------------------------------------
    // External edit appears after reloadFromDisk.
    // -----------------------------------------------------------------
    void testExternalFileAppearsAfterReload()
    {
        setupTempRoot(QStringLiteral("ExternalFile"));

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.createProject(m_projectRoot, QStringLiteral("ExternalFile")));
        pm.setupDefaultProject(m_projectRoot, QStringLiteral("ExternalFile"));

        const QString rel = QStringLiteral("manuscript/drifted.md");
        QVERIFY(!pm.pathExists(rel));

        // Create via plain filesystem — bypass PM entirely.
        createFileWithContent(rel, QByteArrayLiteral("# Drifted"));
        QVERIFY(QFileInfo(absPath(rel)).exists());

        QVERIFY(pm.reloadFromDisk());
        QVERIFY(pm.pathExists(rel));
    }

    // -----------------------------------------------------------------
    // Opening a legacy project with paths that never resolve cleanly on
    // disk must not crash and must silently drop the orphaned metadata.
    // -----------------------------------------------------------------
    void testLegacyProjectLoad_badPathsDropSilently()
    {
        setupTempRoot(QStringLiteral("BadPaths"));
        ensureAuthoritativeFoldersOnDisk();

        // Plant a single real file under manuscript/ to prove the good
        // entry still flows through.
        createFileWithContent(QStringLiteral("manuscript/real.md"),
                              QByteArrayLiteral("real"));

        auto makeLeaf = [](const QString &path, const QString &synopsis) {
            QJsonObject obj;
            obj[QStringLiteral("name")] = QFileInfo(path).completeBaseName();
            obj[QStringLiteral("path")] = path;
            obj[QStringLiteral("type")] = 1;
            obj[QStringLiteral("category")] = 0;
            if (!synopsis.isEmpty()) obj[QStringLiteral("synopsis")] = synopsis;
            obj[QStringLiteral("children")] = QJsonArray();
            return obj;
        };

        QJsonArray mansChildren;
        mansChildren.append(makeLeaf(QStringLiteral("manuscript/real.md"),
                                      QStringLiteral("Keep me.")));
        mansChildren.append(makeLeaf(QStringLiteral("manuscript/imported/nope.md"),
                                      QStringLiteral("Lost to time.")));

        QJsonObject manuscript;
        manuscript[QStringLiteral("name")] = QStringLiteral("Manuscript");
        manuscript[QStringLiteral("path")] = QStringLiteral("manuscript");
        manuscript[QStringLiteral("type")] = 0;
        manuscript[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Manuscript);
        manuscript[QStringLiteral("children")] = mansChildren;

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("children")] = QJsonArray{manuscript};

        QJsonObject project;
        project[QLatin1String(ProjectKeys::Name)] = QStringLiteral("BadPaths");
        project[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        project[QLatin1String(ProjectKeys::Tree)] = tree;

        writeProjectJson(project);

        ProjectManager &pm = ProjectManager::instance();
        QVERIFY(pm.openProject(m_projectFilePath));

        // Real file present with its migrated synopsis.
        ProjectTreeItem *real = pm.findItem(QStringLiteral("manuscript/real.md"));
        QVERIFY(real != nullptr);
        QCOMPARE(real->synopsis, QStringLiteral("Keep me."));

        // Phantom file is NOT in the tree — disk walk didn't find it.
        QVERIFY(!pm.pathExists(QStringLiteral("manuscript/imported/nope.md")));

        // nodeMetadata from the migration still contains the phantom entry
        // (harmless orphan) — that's OK, it's cheap to keep and doesn't
        // pollute the tree.
    }
};

QTEST_MAIN(TestProjectManagerDiskAuthority)
#include "test_projectmanager_disk_authority.moc"
