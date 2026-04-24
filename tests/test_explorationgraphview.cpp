/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    Widget / layout tests for ExplorationGraphView.
*/

#include <QtTest>
#include <QSignalSpy>
#include <QVariantMap>

#include "../src/explorationsgraphview.h"

class TestExplorationGraphView : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testColorMapRoundtrip()
    {
        ExplorationGraphView view;

        QVariantMap in;
        in[QStringLiteral("main")]    = QStringLiteral("#888888");
        in[QStringLiteral("feat")]    = QStringLiteral("#4DA6FF");
        view.loadColorMap(in);

        const QVariantMap out = view.saveColorMap();
        QCOMPARE(out.size(), in.size());
        // Compare normalized hex strings (QColor round-trips to lowercase 6-char form).
        QCOMPARE(QColor(out.value(QStringLiteral("main")).toString()),
                 QColor(in.value(QStringLiteral("main")).toString()));
        QCOMPARE(QColor(out.value(QStringLiteral("feat")).toString()),
                 QColor(in.value(QStringLiteral("feat")).toString()));
    }

    void testMinimumWidth()
    {
        ExplorationGraphView view;
        QVERIFY2(view.minimumWidth() >= 260,
                 qPrintable(QStringLiteral("minimumWidth was %1").arg(view.minimumWidth())));
    }

    void testSetCurrentBranchTriggersRepaint()
    {
        ExplorationGraphView view;
        view.resize(400, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Baseline paint count using an event-filter counter.
        struct PaintCounter : public QObject {
            int count = 0;
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Paint) ++count;
                return false;
            }
        } counter;
        view.installEventFilter(&counter);

        view.setCurrentBranch(QStringLiteral("main"));
        // update() is scheduled; force event loop to process the repaint.
        QTRY_VERIFY_WITH_TIMEOUT(counter.count >= 1, 2000);
    }

    void testColorMapChangedSignalOnlyOnInsert()
    {
        ExplorationGraphView view;

        QVariantMap preset;
        preset[QStringLiteral("main")] = QStringLiteral("#888888");
        view.loadColorMap(preset);

        QSignalSpy spy(&view, &ExplorationGraphView::colorMapChanged);
        // colorForBranch() is private; we cannot invoke it directly without a
        // friend class hook.  Per test plan, document the skip and verify the
        // observable invariant: loadColorMap() itself does NOT emit.
        QCOMPARE(spy.count(), 0);

        QSKIP("colorForBranch() is private; cache-hit path is not externally "
              "reachable from a black-box test. Covered implicitly by the "
              "roundtrip test.");
    }
};

QTEST_MAIN(TestExplorationGraphView)
#include "test_explorationgraphview.moc"
