/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    Integration tests for GitService against a real temporary libgit2 repo.
*/

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>

#include <git2.h>

#include "../src/gitservice.h"

class TestGitService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Ensure makeSignature() can fall back when git config has no identity.
        QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        settings.setValue(QStringLiteral("git/author_name"), QStringLiteral("TestUser"));
        settings.setValue(QStringLiteral("git/author_email"), QStringLiteral("test@rpgforge.local"));
        settings.sync();
    }

    void init()
    {
        m_tmpDir = new QTemporaryDir();
        QVERIFY(m_tmpDir->isValid());
        m_repoPath = m_tmpDir->path();
    }

    void cleanup()
    {
        delete m_tmpDir;
        m_tmpDir = nullptr;
    }

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    void writeFile(const QString &relPath, const QString &content)
    {
        const QString abs = QDir(m_repoPath).filePath(relPath);
        QFileInfo(abs).absoluteDir().mkpath(QStringLiteral("."));
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content.toUtf8());
        f.close();
    }

    bool initAndSeed(const QString &relPath = QStringLiteral("manuscript/chapter1.md"),
                     const QString &body = QStringLiteral("Hello world. Initial content."),
                     const QString &msg = QStringLiteral("Initial commit"))
    {
        auto &git = GitService::instance();
        if (!git.initRepo(m_repoPath)) return false;
        writeFile(relPath, body);
        auto f = git.commitAll(m_repoPath, msg);
        f.waitForFinished();
        return f.result();
    }

    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    void testInitRepoAndCommitAll()
    {
        auto &git = GitService::instance();
        QVERIFY(git.initRepo(m_repoPath));
        QVERIFY(git.isRepo(m_repoPath));

        writeFile(QStringLiteral("manuscript/chapter1.md"),
                  QStringLiteral("Once upon a time."));

        // Dirty tree before commit.
        QVERIFY(git.hasUncommittedChanges(m_repoPath));

        auto f = git.commitAll(m_repoPath, QStringLiteral("Initial commit"));
        f.waitForFinished();
        QVERIFY(f.result());

        // Clean tree after commit.
        QVERIFY(!git.hasUncommittedChanges(m_repoPath));
    }

    void testGetHistorySingleFile()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");

        QVERIFY(initAndSeed(rel, QStringLiteral("v1"), QStringLiteral("C1")));

        // Git commit timestamps have 1-second resolution; space commits out
        // so the revwalk ordering is stable.
        QTest::qSleep(1100);
        writeFile(rel, QStringLiteral("v1\nv2"));
        auto f2 = git.commitAll(m_repoPath, QStringLiteral("C2"));
        f2.waitForFinished();
        QVERIFY(f2.result());

        QTest::qSleep(1100);
        writeFile(rel, QStringLiteral("v1\nv2\nv3"));
        auto f3 = git.commitAll(m_repoPath, QStringLiteral("C3"));
        f3.waitForFinished();
        QVERIFY(f3.result());

        const QString absPath = QDir(m_repoPath).filePath(rel);
        auto hf = git.getHistory(absPath);
        hf.waitForFinished();
        const QList<VersionInfo> history = hf.result();

        QCOMPARE(history.size(), 3);
        // Newest first.
        QVERIFY(history[0].message.contains(QLatin1String("C3")));
        QVERIFY(history[1].message.contains(QLatin1String("C2")));
        QVERIFY(history[2].message.contains(QLatin1String("C1")));
    }

    void testExtractBlob()
    {
        auto &git = GitService::instance();
        const QString content = QStringLiteral("The quick brown fox jumps.");
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, content, QStringLiteral("seed")));

        // Walk the HEAD tree to recover the blob OID for chapter1.md.
        git_repository *repo = nullptr;
        QVERIFY(git_repository_open(&repo, m_repoPath.toUtf8().constData()) == 0);

        git_oid head_oid;
        QVERIFY(git_reference_name_to_id(&head_oid, repo, "HEAD") == 0);

        git_commit *commit = nullptr;
        QVERIFY(git_commit_lookup(&commit, repo, &head_oid) == 0);

        git_tree *tree = nullptr;
        QVERIFY(git_commit_tree(&tree, commit) == 0);

        git_tree_entry *entry = nullptr;
        QVERIFY(git_tree_entry_bypath(&entry, tree, "manuscript/chapter1.md") == 0);

        char oid_str[GIT_OID_HEXSZ + 1];
        git_oid_tostr(oid_str, sizeof(oid_str), git_tree_entry_id(entry));
        const QString blobOid = QString::fromLatin1(oid_str);

        git_tree_entry_free(entry);
        git_tree_free(tree);
        git_commit_free(commit);
        git_repository_free(repo);

        const QString outPath = QDir(m_tmpDir->path()).filePath(QStringLiteral("extracted.md"));
        auto fx = git.extractBlob(m_repoPath, blobOid, outPath);
        fx.waitForFinished();
        QVERIFY(fx.result());

        QFile f(outPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), content);
    }

    void testStashLifecycle()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, QStringLiteral("clean"), QStringLiteral("seed")));

        // Dirty the tree.
        writeFile(rel, QStringLiteral("DIRTY"));
        QVERIFY(git.hasUncommittedChanges(m_repoPath));

        auto stashFut = git.stashChanges(m_repoPath, QStringLiteral("testing park"));
        stashFut.waitForFinished();
        QVERIFY(stashFut.result());

        QVERIFY(!git.hasUncommittedChanges(m_repoPath));
        QCOMPARE(git.listStashes(m_repoPath).size(), 1);

        auto applyFut = git.applyStash(m_repoPath, 0);
        applyFut.waitForFinished();
        QVERIFY(applyFut.result());

        QCOMPARE(git.listStashes(m_repoPath).size(), 0);

        // Working tree should be back to the dirty state.
        QFile f(QDir(m_repoPath).filePath(rel));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("DIRTY"));
    }

    void testDropStash()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, QStringLiteral("clean"), QStringLiteral("seed")));

        writeFile(rel, QStringLiteral("DIRTY"));
        QVERIFY(git.hasUncommittedChanges(m_repoPath));

        auto stashFut = git.stashChanges(m_repoPath, QStringLiteral("drop-me"));
        stashFut.waitForFinished();
        QVERIFY(stashFut.result());
        QCOMPARE(git.listStashes(m_repoPath).size(), 1);

        auto dropFut = git.dropStash(m_repoPath, 0);
        dropFut.waitForFinished();
        QVERIFY(dropFut.result());

        QCOMPARE(git.listStashes(m_repoPath).size(), 0);
        // Drop does NOT restore — working tree must remain clean.
        QVERIFY(!git.hasUncommittedChanges(m_repoPath));
    }

    void testCreateAndSwitchExploration()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, QStringLiteral("base"), QStringLiteral("root")));

        const QString baseBranch = git.currentBranch(m_repoPath);
        QVERIFY(!baseBranch.isEmpty());

        QVERIFY(git.createExploration(m_repoPath, QStringLiteral("feature/test")));
        QCOMPARE(git.currentBranch(m_repoPath), QStringLiteral("feature/test"));

        // Commit on the feature branch.
        writeFile(rel, QStringLiteral("feature content"));
        auto cf = git.commitAll(m_repoPath, QStringLiteral("feature commit"));
        cf.waitForFinished();
        QVERIFY(cf.result());

        QVERIFY(git.checkoutBranch(m_repoPath, baseBranch));
        QCOMPARE(git.currentBranch(m_repoPath), baseBranch);
    }

    void testGetExplorationGraph()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, QStringLiteral("c1"), QStringLiteral("main-c1")));

        // Second commit on main.
        writeFile(rel, QStringLiteral("c1\nc2"));
        auto f2 = git.commitAll(m_repoPath, QStringLiteral("main-c2"));
        f2.waitForFinished();
        QVERIFY(f2.result());

        const QString mainBranch = git.currentBranch(m_repoPath);

        // Branch off and add 2 commits on feature.
        QVERIFY(git.createExploration(m_repoPath, QStringLiteral("feature")));
        writeFile(rel, QStringLiteral("c1\nc2\nf1"));
        auto f3 = git.commitAll(m_repoPath, QStringLiteral("feat-c1"));
        f3.waitForFinished();
        QVERIFY(f3.result());

        writeFile(rel, QStringLiteral("c1\nc2\nf1\nf2"));
        auto f4 = git.commitAll(m_repoPath, QStringLiteral("feat-c2"));
        f4.waitForFinished();
        QVERIFY(f4.result());

        // Back to main.
        QVERIFY(git.checkoutBranch(m_repoPath, mainBranch));

        auto gf = git.getExplorationGraph(m_repoPath);
        gf.waitForFinished();
        const QList<ExplorationNode> nodes = gf.result();

        // 2 on main + 2 additional on feature = 4 unique commits.
        QCOMPARE(nodes.size(), 4);

        QSet<QString> branchNames;
        for (const auto &n : nodes) {
            if (!n.branchName.isEmpty()) branchNames.insert(n.branchName);
        }
        QVERIFY2(branchNames.size() >= 2,
                 qPrintable(QStringLiteral("Expected >=2 branch names, got %1").arg(branchNames.size())));
    }

    void testWordCountCachePersistence()
    {
        auto &git = GitService::instance();
        QVariantMap seed;
        seed[QStringLiteral("abc123")] = 42;
        git.loadWordCountCache(seed);

        const QVariantMap saved = git.saveWordCountCache();
        QVERIFY(saved.contains(QStringLiteral("abc123")));
        QCOMPARE(saved.value(QStringLiteral("abc123")).toInt(), 42);
    }

    void testIntegrateExplorationFastForward()
    {
        auto &git = GitService::instance();
        const QString rel = QStringLiteral("manuscript/chapter1.md");
        QVERIFY(initAndSeed(rel, QStringLiteral("base"), QStringLiteral("main c1")));

        const QString mainBranch = git.currentBranch(m_repoPath);

        // feature with 2 commits past main.
        QVERIFY(git.createExploration(m_repoPath, QStringLiteral("feature")));
        writeFile(rel, QStringLiteral("base\nf1"));
        auto f2 = git.commitAll(m_repoPath, QStringLiteral("feat c1"));
        f2.waitForFinished();
        QVERIFY(f2.result());

        writeFile(rel, QStringLiteral("base\nf1\nf2"));
        auto f3 = git.commitAll(m_repoPath, QStringLiteral("feat c2"));
        f3.waitForFinished();
        QVERIFY(f3.result());

        // Feature tip OID.
        git_repository *repo = nullptr;
        QVERIFY(git_repository_open(&repo, m_repoPath.toUtf8().constData()) == 0);
        git_oid featTip;
        QVERIFY(git_reference_name_to_id(&featTip, repo, "HEAD") == 0);
        char featStr[GIT_OID_HEXSZ + 1];
        git_oid_tostr(featStr, sizeof(featStr), &featTip);
        const QString featHash = QString::fromLatin1(featStr);
        git_repository_free(repo);

        QVERIFY(git.checkoutBranch(m_repoPath, mainBranch));

        auto mf = git.integrateExploration(m_repoPath, QStringLiteral("feature"));
        mf.waitForFinished();
        QVERIFY(mf.result());

        // HEAD on main should now point to feature's tip (fast-forward).
        QVERIFY(git_repository_open(&repo, m_repoPath.toUtf8().constData()) == 0);
        git_oid newHead;
        QVERIFY(git_reference_name_to_id(&newHead, repo, "HEAD") == 0);
        char newHeadStr[GIT_OID_HEXSZ + 1];
        git_oid_tostr(newHeadStr, sizeof(newHeadStr), &newHead);
        const QString newHeadHash = QString::fromLatin1(newHeadStr);
        git_repository_free(repo);

        QCOMPARE(newHeadHash, featHash);
    }

    void testGetConflictingFilesEmpty()
    {
        auto &git = GitService::instance();
        QVERIFY(initAndSeed());

        auto cf = git.getConflictingFiles(m_repoPath);
        cf.waitForFinished();
        QVERIFY(cf.result().isEmpty());
    }

private:
    QTemporaryDir *m_tmpDir = nullptr;
    QString m_repoPath;
};

QTEST_MAIN(TestGitService)
#include "test_gitservice.moc"
