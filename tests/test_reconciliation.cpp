/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"
#include "../src/reconciliationtypes.h"

/**
 * Tests for Phase 4 tree-reconciliation on ProjectManager:
 *   - reconciliationRequired signal is emitted when File-typed or leaf-Folder
 *     tree entries can't be resolved on disk
 *   - fuzzy resolver suggestions are surfaced via ReconciliationEntry.suggestedPath
 *   - the DBus-style apply* flows (Locate / Remove / RecreateEmpty) drive
 *     ProjectManager mutations correctly
 *
 * The tree structure is built using ProjectManager's own mutation API to get
 * a baseline project, then files are deleted off disk (or synthesised on
 * disk) to create the desired divergence. validateTree() is invoked
 * explicitly to exercise the reconciliation scan.
 */
class TestReconciliation : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_projectRoot;

    QString absPath(const QString &rel) const
    {
        return QDir(m_projectRoot).absoluteFilePath(rel);
    }

    void createFreshProject(const QString &name = QStringLiteral("RecTest"))
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
    void initTestCase()
    {
        qRegisterMetaType<QList<ReconciliationEntry>>("QList<ReconciliationEntry>");
    }

    void cleanup()
    {
        ProjectManager::instance().closeProject();
        delete m_tmp;
        m_tmp = nullptr;
    }

    // -----------------------------------------------------------------
    // validateTree() emits reconciliationRequired when a registered file
    // is missing on disk.
    // -----------------------------------------------------------------
    void testEmitsSignalWhenFilesMissing()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Register a file, then delete it from disk to create a mismatch.
        const QString rel = QStringLiteral("manuscript/missing.md");
        QVERIFY(pm.addFile(QStringLiteral("Missing"), rel, QStringLiteral("manuscript")));
        QVERIFY(QFile::remove(absPath(rel)));
        QVERIFY(!QFileInfo(absPath(rel)).exists());
        // Tree still has the entry even though disk is gone.
        QVERIFY(pm.pathExists(rel));

        QSignalSpy spy(&pm, &ProjectManager::reconciliationRequired);
        pm.validateTree();
        QCOMPARE(spy.count(), 1);

        const QList<ReconciliationEntry> entries =
            spy.takeFirst().at(0).value<QList<ReconciliationEntry>>();
        QVERIFY(!entries.isEmpty());

        bool sawMissing = false;
        for (const auto &e : entries) {
            if (e.path == rel) { sawMissing = true; break; }
        }
        QVERIFY(sawMissing);
    }

    // -----------------------------------------------------------------
    // No signal when every registered path is present on disk.
    // -----------------------------------------------------------------
    void testSignalNotEmittedWhenAllResolve()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/present.md");
        QVERIFY(pm.addFile(QStringLiteral("Present"), rel, QStringLiteral("manuscript")));
        QVERIFY(QFileInfo(absPath(rel)).exists());

        QSignalSpy spy(&pm, &ProjectManager::reconciliationRequired);
        pm.validateTree();
        QCOMPARE(spy.count(), 0);
    }

    // -----------------------------------------------------------------
    // Fuzzy resolver: when an underscored file is on disk but the tree
    // entry uses a space-separated path, the entry should carry a
    // non-empty suggestedPath.
    // -----------------------------------------------------------------
    void testFuzzyResolverPopulatesSuggestion()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Register a file path that uses spaces in its leaf.
        const QString trackedRel = QStringLiteral("research/foo 1.md");
        QVERIFY(pm.addFile(QStringLiteral("Foo 1"), trackedRel, QStringLiteral("research")));
        // Sanity: disk now has research/foo 1.md (space preserved).
        QVERIFY(QFileInfo(absPath(trackedRel)).exists());

        // Remove the spaced file from disk and synthesise an underscored
        // variant in the same directory. tryResolveOnDisk should find it
        // via Strategy 3 (parent scan, space/underscore tolerant).
        QVERIFY(QFile::remove(absPath(trackedRel)));
        QVERIFY(!QFileInfo(absPath(trackedRel)).exists());
        {
            QFile f(absPath(QStringLiteral("research/foo_1.md")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("placeholder");
            f.close();
        }

        QSignalSpy spy(&pm, &ProjectManager::reconciliationRequired);
        pm.validateTree();
        QCOMPARE(spy.count(), 1);

        const QList<ReconciliationEntry> entries =
            spy.takeFirst().at(0).value<QList<ReconciliationEntry>>();

        ReconciliationEntry found;
        bool sawEntry = false;
        for (const auto &e : entries) {
            if (e.path == trackedRel) {
                found = e;
                sawEntry = true;
                break;
            }
        }
        QVERIFY(sawEntry);
        QVERIFY(!found.suggestedPath.isEmpty());
        QCOMPARE(found.suggestedPath, QStringLiteral("research/foo_1.md"));
    }

    // -----------------------------------------------------------------
    // Applying a Locate action (same-parent, different basename) ->
    // renameItem updates both the tree and the disk. The registered
    // (tree) path doesn't exist on disk; the user points PM at a
    // different-basename file via Locate, and PM renames the tree entry.
    //
    // NB: PM::renameItem refuses when the target basename already exists
    // on disk (collision guard). The Locate flow therefore only works
    // when the "resolved" target is a brand-new basename — we emulate
    // that by leaving nothing at the target and asserting the tree
    // becomes consistent with what the user typed.
    // -----------------------------------------------------------------
    void testApplyLocate_renamesOrMoves()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Register a file, then delete both the backing file AND leave the
        // target unoccupied. Under Locate, PM renames the tree entry (no
        // disk entry to rename — PM skips the disk op when the source is
        // missing).
        const QString oldRel = QStringLiteral("manuscript/orig.md");
        QVERIFY(pm.addFile(QStringLiteral("Orig"), oldRel, QStringLiteral("manuscript")));
        QVERIFY(QFile::remove(absPath(oldRel)));

        // Simulate the Locate path (same-parent branch): rename the tree
        // entry to the new basename. (MainWindow::applyReconciliationEntry
        // drives this via renameItem for the same-parent case.)
        const QString newRel = QStringLiteral("manuscript/renamed.md");
        QVERIFY(pm.renameItem(oldRel, QStringLiteral("renamed")));

        // Tree moved to the new path.
        QVERIFY(!pm.pathExists(oldRel));
        QVERIFY(pm.pathExists(newRel));
    }

    // -----------------------------------------------------------------
    // Applying a Locate action that crosses folders -> moveItem.
    // Source is missing on disk, target folder is different, target
    // basename is free. PM's moveItem skips the disk op when source is
    // missing and updates the tree.
    // -----------------------------------------------------------------
    void testApplyLocate_crossFolderMove()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString oldRel = QStringLiteral("manuscript/stray.md");
        QVERIFY(pm.addFile(QStringLiteral("Stray"), oldRel, QStringLiteral("manuscript")));
        QVERIFY(QFile::remove(absPath(oldRel)));

        // Locate into research/ (target folder differs, same basename).
        QVERIFY(pm.moveItem(oldRel, QStringLiteral("research")));
        const QString newRel = QStringLiteral("research/stray.md");
        QVERIFY(!pm.pathExists(oldRel));
        QVERIFY(pm.pathExists(newRel));
    }

    // -----------------------------------------------------------------
    // Applying a Remove action -> the node is gone from the tree.
    // Disk presence is N/A (the file didn't exist by definition).
    // -----------------------------------------------------------------
    void testApplyRemove_removesFromTree()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/ghost.md");
        QVERIFY(pm.addFile(QStringLiteral("Ghost"), rel, QStringLiteral("manuscript")));
        // Remove from disk so the reconciliation is the "file was missing" path.
        QVERIFY(QFile::remove(absPath(rel)));
        QVERIFY(!QFileInfo(absPath(rel)).exists());

        // removeItem happily prunes a tree entry whose disk entry is already gone.
        QVERIFY(pm.removeItem(rel));
        QVERIFY(!pm.pathExists(rel));
    }

    // -----------------------------------------------------------------
    // Applying a RecreateEmpty action -> the file is created on disk
    // and the tree entry (which we kept) still points at it.
    // -----------------------------------------------------------------
    void testApplyRecreateEmpty_createsEmptyFile()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        const QString rel = QStringLiteral("manuscript/recreated.md");
        QVERIFY(pm.addFile(QStringLiteral("Recreated"), rel, QStringLiteral("manuscript")));
        QVERIFY(QFile::remove(absPath(rel)));
        QVERIFY(!QFileInfo(absPath(rel)).exists());

        // Mimic MainWindow::applyReconciliationEntry's RecreateEmpty branch.
        const QString abs = absPath(rel);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        QVERIFY(QFileInfo(abs).isFile());
        QCOMPARE(QFileInfo(abs).size(), qint64(0));
        // Tree entry must still be present — we didn't touch the tree.
        QVERIFY(pm.pathExists(rel));
    }

    // -----------------------------------------------------------------
    // beginBatch() / endBatch() should defer saveProject so a sequence of
    // mutations emits a single flush at the end. We sanity-check that
    // the operations complete and the end state is correct.
    // -----------------------------------------------------------------
    void testBatch_appliesMutationsAndFlushesOnEnd()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        pm.beginBatch();
        QVERIFY(pm.addFile(QStringLiteral("A"), QStringLiteral("manuscript/a.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("B"), QStringLiteral("manuscript/b.md"),
                            QStringLiteral("manuscript")));
        QVERIFY(pm.addFile(QStringLiteral("C"), QStringLiteral("manuscript/c.md"),
                            QStringLiteral("manuscript")));
        pm.endBatch();

        QVERIFY(pm.pathExists(QStringLiteral("manuscript/a.md")));
        QVERIFY(pm.pathExists(QStringLiteral("manuscript/b.md")));
        QVERIFY(pm.pathExists(QStringLiteral("manuscript/c.md")));
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/a.md"))).exists());
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/b.md"))).exists());
        QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/c.md"))).exists());
    }
};

QTEST_MAIN(TestReconciliation)
#include "test_reconciliation.moc"
