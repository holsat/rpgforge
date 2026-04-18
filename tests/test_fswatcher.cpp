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
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "../src/projectmanager.h"

/**
 * Phase 5 tests for ProjectManager's QFileSystemWatcher integration and
 * external-change window.
 *
 * The watcher's debounce is 250 ms. Tests that assert a reload has happened
 * use a waitForReload() helper that spins the event loop until a fresh
 * treeStructureChanged fires or a hard timeout lapses. Tests that assert
 * the watcher did NOT fire use a counting spy and ensure the count stays
 * flat past the debounce window.
 */
class TestFsWatcher : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_tmp = nullptr;
    QString m_projectRoot;

    QString absPath(const QString &rel) const
    {
        return QDir(m_projectRoot).absoluteFilePath(rel);
    }

    void createFreshProject(const QString &name = QStringLiteral("FsWatchTest"))
    {
        ProjectManager &pm = ProjectManager::instance();
        pm.closeProject();
        delete m_tmp;
        m_tmp = new QTemporaryDir();
        QVERIFY(m_tmp->isValid());
        m_projectRoot = m_tmp->path() + QStringLiteral("/") + name;
        QVERIFY(pm.createProject(m_projectRoot, name));
        pm.setupDefaultProject(m_projectRoot, name);

        // createProject + setupDefaultProject set m_projectFilePath but
        // don't route through openProject, so the watcher never got armed.
        // Trigger a reload so the watcher picks up the authoritative
        // folders for the test.
        QVERIFY(pm.reloadFromDisk());
    }

    // Spin the event loop waiting for \a spy to reach at least \a expected
    // recorded emissions, up to \a timeoutMs. Returns true when the count
    // is met or exceeded, false on timeout. Caller supplies the expected
    // threshold based on the baseline it captured before the action it's
    // waiting on, so signals emitted synchronously before this helper is
    // entered are still counted.
    bool waitForSignalCount(QSignalSpy &spy, int expected, int timeoutMs = 2000)
    {
        QElapsedTimer t; t.start();
        while (spy.count() < expected && t.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(25);
        }
        return spy.count() >= expected;
    }

    // Spin the event loop for the given number of milliseconds, processing
    // events. Used to assert the watcher didn't fire after a period.
    void spin(int ms)
    {
        QElapsedTimer t; t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QTest::qWait(10);
        }
    }

    bool fsWatcherUsable() const
    {
        // Sanity check: QFileSystemWatcher must be usable on the test host.
        // Linux inotify is always available in practice; this guard is
        // defensive for CI sandboxes that forbid it.
        QFileSystemWatcher probe;
        return probe.addPath(QDir::tempPath());
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!fsWatcherUsable()) {
            QSKIP("QFileSystemWatcher not usable on this host.");
        }
    }

    void cleanup()
    {
        ProjectManager::instance().closeProject();
        delete m_tmp;
        m_tmp = nullptr;
    }

    // -----------------------------------------------------------------
    // A file appearing externally under a watched folder causes a reload.
    // -----------------------------------------------------------------
    void testExternalFileAppears()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Wait for the post-mutation quiet window from createFreshProject
        // to elapse so it doesn't mask our external event.
        spin(1100);

        QSignalSpy spy(&pm, &ProjectManager::treeStructureChanged);
        QVERIFY(spy.isValid());
        const int baseline = spy.count();

        // Touch a file under manuscript/ externally (no PM mutation).
        const QString rel = QStringLiteral("manuscript/new.md");
        const QString abs = absPath(rel);
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# New chapter\n");
        f.close();

        QVERIFY2(waitForSignalCount(spy, baseline + 1, 3000),
                 "ProjectManager did not reload after external file creation");

        // The file genuinely exists on disk (sanity).
        QVERIFY(QFileInfo(abs).exists());
    }

    // -----------------------------------------------------------------
    // An external-change window suppresses watcher storm during the
    // window; a reload fires on end.
    // -----------------------------------------------------------------
    void testExternalChangeWindow_suppressesWatcherStorm()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();
        spin(1100); // let quiet window from createFreshProject elapse

        QSignalSpy spy(&pm, &ProjectManager::treeStructureChanged);
        QVERIFY(spy.isValid());
        const int baseline = spy.count();

        pm.beginExternalChange();

        // Burst of external writes under manuscript/.
        for (int i = 0; i < 20; ++i) {
            const QString p = absPath(QStringLiteral("manuscript/bulk_%1.md").arg(i));
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("burst\n");
            f.close();
        }

        // Let any pending debounce run. Count must stay at baseline because
        // the depth-gated handler swallows events.
        spin(400);
        QCOMPARE(spy.count(), baseline);

        // End window: reloadFromDisk fires and emits treeStructureChanged.
        // The reload can proxy more than one (modelReset +
        // row-insertions from validateTree + the explicit emit), so we
        // only assert at-least-one — suppressing the storm is the point.
        pm.endExternalChange();
        QVERIFY(waitForSignalCount(spy, baseline + 1, 3000));

        // Disk writes really happened.
        for (int i = 0; i < 20; ++i) {
            QVERIFY(QFileInfo(absPath(QStringLiteral("manuscript/bulk_%1.md").arg(i))).exists());
        }
    }

    // -----------------------------------------------------------------
    // PM's own mutations must not retrigger the watcher — otherwise every
    // addFile / addFolder would cause a redundant reload.
    // -----------------------------------------------------------------
    void testSelfMutationDoesNotTriggerWatcher()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        // Let the post-createFreshProject quiet window expire, then a
        // small extra spin so any queued fsnotify events settle.
        spin(1100);

        QSignalSpy spy(&pm, &ProjectManager::treeStructureChanged);
        QVERIFY(spy.isValid());

        // PM mutation — SelfMutationScope should prevent the watcher from
        // scheduling a reload for our own write. The tree op itself fires
        // rowsInserted → treeStructureChanged exactly once.
        QVERIFY(pm.addFile(QStringLiteral("note"),
                           QStringLiteral("research/note.md"),
                           QStringLiteral("research")));

        const int afterMutation = spy.count();
        QVERIFY2(afterMutation >= 1,
                 qPrintable(QStringLiteral("Expected >=1 emission from addFile, got %1").arg(afterMutation)));

        // Spin past the 250 ms debounce + post-mutation quiet window. If
        // the watcher mistakenly fired a reload, spy.count() would grow.
        spin(1500);
        QCOMPARE(spy.count(), afterMutation);
    }

    // -----------------------------------------------------------------
    // reloadFromDisk() is idempotent and safe to call repeatedly.
    // -----------------------------------------------------------------
    void testReloadFromDisk_idempotent()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        QVERIFY(pm.reloadFromDisk());
        QVERIFY(pm.reloadFromDisk());
        QVERIFY(pm.reloadFromDisk());

        // Tree remains consistent.
        QVERIFY(pm.pathExists(QStringLiteral("manuscript")));
        QVERIFY(pm.pathExists(QStringLiteral("lorekeeper")));
        QVERIFY(pm.pathExists(QStringLiteral("research")));
    }

    // -----------------------------------------------------------------
    // Nested external-change windows reload once at the outermost end.
    // -----------------------------------------------------------------
    void testExternalChangeWindow_nested()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();
        spin(1100);

        QSignalSpy spy(&pm, &ProjectManager::treeStructureChanged);
        QVERIFY(spy.isValid());
        const int baseline = spy.count();

        pm.beginExternalChange();
        pm.beginExternalChange();

        QFile f(absPath(QStringLiteral("manuscript/nested.md")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x\n");
        f.close();

        // Inner end: must not reload yet.
        pm.endExternalChange();
        spin(400);
        QCOMPARE(spy.count(), baseline);

        // Outer end: reload fires.
        pm.endExternalChange();
        QVERIFY(waitForSignalCount(spy, baseline + 1, 3000));
    }

    // -----------------------------------------------------------------
    // Unbalanced endExternalChange is a no-op (logs a warning).
    // -----------------------------------------------------------------
    void testExternalChangeWindow_unbalanced()
    {
        createFreshProject();
        ProjectManager &pm = ProjectManager::instance();

        QSignalSpy spy(&pm, &ProjectManager::treeStructureChanged);
        QVERIFY(spy.isValid());

        // Must not crash and must not trigger reload.
        pm.endExternalChange();
        spin(200);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestFsWatcher)
#include "test_fswatcher.moc"
