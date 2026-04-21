/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"

/**
 * Tests for Phase 3 atomic disk-then-tree mutations on ProjectManager.
 *
 * Each test uses a fresh QTemporaryDir so they don't interfere with each
 * other and leave no residue on disk. Every test exercises both ends of the
 * mutation: the tree (via pathExists / findItem) and the disk (via
 * QFileInfo::exists).
 */
class TestProjectManagerMutations : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_projectRoot;

    QString absPath(const QString &rel) const
    {
        return QDir(m_projectRoot).absoluteFilePath(rel);
    }

    void createFreshProject(const QString &name = QStringLiteral("MutTest"))
    {
        ProjectManager &pm = ProjectManager::instance();
        pm.closeProject();
        delete m_tmp;
        m_tmp = new QTemporaryDir();
        QVERIFY(m_tmp->isValid());
        m_projectRoot = m_tmp->path() + QStringLiteral("/") + name;
        QVERIFY(pm.createProject(m_projectRoot, name));
        pm.setupDefaultProject(m_projectRoot, name);
    }

private Q_SLOTS:
    void initTestCase() {}

    void cleanup()
    {
        ProjectManager::instance().closeProject();
        delete m_tmp;
        m_tmp = nullptr;
    }

    // -----------------------------------------------------------------
    // addFolder: creates the folder on disk and registers it in the tree
    // -----------------------------------------------------------------
    void testAddFolder_createsOnDisk()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Create a top-level folder — PM should mkpath and then register.
        QVERIFY(pm.addFolder(QStringLiteral("Scratch"), QStringLiteral("scratch"), QString()));
        QVERIFY(pm.pathExists(QStringLiteral("scratch")));
        QVERIFY(QFileInfo(absPath(QStringLiteral("scratch"))).isDir());
    }

    void testAddFolder_emptyPathDerivedFromParent()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // UI button path: passes empty relativePath; PM should derive it as
        // parentPath + "/" + name and still create it on disk.
        QVERIFY(pm.addFolder(QStringLiteral("Chapters"), QString(), QStringLiteral("manuscript")));
        QVERIFY(pm.pathExists(QStringLiteral("manuscript/Chapters")));
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/Chapters"))).isDir());
    }

    // -----------------------------------------------------------------
    // addFile: creates an empty file when the path is missing on disk
    // -----------------------------------------------------------------
    void testAddFile_createsEmptyFileWhenMissing()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/new.md");
        QVERIFY(!QFileInfo(absPath(rel)).exists());

        QVERIFY(pm.addFile(QStringLiteral("New"), rel, QStringLiteral("manuscript")));
        QVERIFY(pm.pathExists(rel));
        QVERIFY(QFileInfo(absPath(rel)).isFile());
        QCOMPARE(QFileInfo(absPath(rel)).size(), qint64(0));
    }

    // -----------------------------------------------------------------
    // addFile: if the file pre-exists on disk, register it without
    // touching its contents
    // -----------------------------------------------------------------
    void testAddFile_registersExistingFile()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/prewritten.md");
        const QString abs = absPath(rel);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        {
            QFile f(abs);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Preserved content.\n");
            f.close();
        }

        QVERIFY(pm.addFile(QStringLiteral("Prewritten"), rel, QStringLiteral("manuscript")));
        QVERIFY(pm.pathExists(rel));

        // Content must survive unchanged.
        QFile f(abs);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()).trimmed(), QStringLiteral("Preserved content."));
    }

    // -----------------------------------------------------------------
    // renameItem: renames the backing file on disk
    // -----------------------------------------------------------------
    void testRenameItem_renamesOnDisk()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString oldRel = QStringLiteral("manuscript/alpha.md");
        QVERIFY(pm.addFile(QStringLiteral("Alpha"), oldRel, QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(oldRel)).exists());

        QVERIFY(pm.renameItem(oldRel, QStringLiteral("beta")));

        const QString newRel = QStringLiteral("manuscript/beta.md");
        // Old path gone (both tree and disk)
        QVERIFY(!pm.pathExists(oldRel));
        QVERIFY(!QFileInfo(absPath(oldRel)).exists());
        // New path present
        QVERIFY(pm.pathExists(newRel));
        QVERIFY(QFileInfo(absPath(newRel)).exists());
    }

    // -----------------------------------------------------------------
    // moveItem: moves the backing file on disk
    // -----------------------------------------------------------------
    void testMoveItem_movesOnDisk()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString srcRel = QStringLiteral("manuscript/moved.md");
        QVERIFY(pm.addFile(QStringLiteral("Moved"), srcRel, QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(srcRel)).exists());

        QVERIFY(pm.moveItem(srcRel, QStringLiteral("research")));

        const QString targetRel = QStringLiteral("research/moved.md");
        QVERIFY(!pm.pathExists(srcRel));
        QVERIFY(!QFileInfo(absPath(srcRel)).exists());
        QVERIFY(pm.pathExists(targetRel));
        QVERIFY(QFileInfo(absPath(targetRel)).exists());
    }

    // -----------------------------------------------------------------
    // removeItem: deletes the backing file on disk
    // -----------------------------------------------------------------
    void testRemoveItem_deletesOnDisk()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/to_delete.md");
        QVERIFY(pm.addFile(QStringLiteral("Doomed"), rel, QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(rel)).exists());

        QVERIFY(pm.removeItem(rel));
        QVERIFY(!pm.pathExists(rel));
        QVERIFY(!QFileInfo(absPath(rel)).exists());
    }

    // -----------------------------------------------------------------
    // removeItem: refuses to remove authoritative roots
    // -----------------------------------------------------------------
    void testRemoveItem_refusesAuthoritativeRoot()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // "manuscript" is an authoritative root. PM must refuse.
        QVERIFY(pm.pathExists(QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript"))).isDir());

        QVERIFY(!pm.removeItem(QStringLiteral("manuscript")));

        // Both tree and disk are untouched.
        QVERIFY(pm.pathExists(QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript"))).isDir());
    }

    // -----------------------------------------------------------------
    // moveItem rolls back the disk rename if the tree op fails
    // (exercised indirectly: target-already-exists must fail cleanly)
    // -----------------------------------------------------------------
    void testMoveItem_refusesWhenTargetExists()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        QVERIFY(pm.addFile(QStringLiteral("A"), QStringLiteral("manuscript/a.md"),
                            QStringLiteral("manuscript")));
        // Pre-populate a file at the target so the move must fail.
        QDir().mkpath(absPath(QStringLiteral("research")));
        {
            QFile f(absPath(QStringLiteral("research/a.md")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("existing");
            f.close();
        }

        QVERIFY(!pm.moveItem(QStringLiteral("manuscript/a.md"), QStringLiteral("research")));
        // Source still present
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/a.md"))).exists());
        // Target unchanged
        QVERIFY(QFileInfo(absPath(QStringLiteral("research/a.md"))).exists());
    }
};

QTEST_MAIN(TestProjectManagerMutations)
#include "test_projectmanager_mutations.moc"
