/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

    Signal-emission tests for UnsavedChangesDialog.
*/

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QList>

#include "../src/unsavedchangesdialog.h"

namespace {

// The dialog builds its UI with anonymous QPushButtons; find them by their
// visible text.  i18n() is active but defaults to English when no translation
// is loaded for the test runner.
QPushButton *findButton(QDialog *dialog, const QStringList &needles)
{
    const auto buttons = dialog->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        const QString t = b->text();
        for (const QString &needle : needles) {
            if (t.contains(needle, Qt::CaseInsensitive)) return b;
        }
    }
    return nullptr;
}

}

class TestUnsavedChangesDialog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSaveRequestedSignalEmitted()
    {
        UnsavedChangesDialog dialog(QStringLiteral("main"), QStringLiteral("feature"));
        QSignalSpy spy(&dialog, &UnsavedChangesDialog::saveRequested);

        QPushButton *saveBtn = findButton(&dialog, {QStringLiteral("Save")});
        QVERIFY2(saveBtn, "Save & Switch button not found");

        saveBtn->click();

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 1);
        QVERIFY(!args.at(0).toString().isEmpty());
    }

    void testParkRequestedSignalEmitted()
    {
        UnsavedChangesDialog dialog(QStringLiteral("main"), QStringLiteral("feature"));
        QSignalSpy spy(&dialog, &UnsavedChangesDialog::parkRequested);

        QPushButton *parkBtn = findButton(&dialog, {QStringLiteral("Park")});
        QVERIFY2(parkBtn, "Park & Switch button not found");

        parkBtn->click();

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 1);
        const QString msg = args.at(0).toString();
        // Message format: "Parked on 'main' — <date>"
        QVERIFY2(msg.contains(QStringLiteral("main")),
                 qPrintable(QStringLiteral("Expected branch name in message, got: %1").arg(msg)));
        QVERIFY(msg.contains(QStringLiteral("Parked on")));
    }

    void testCancelRejects()
    {
        UnsavedChangesDialog dialog(QStringLiteral("main"), QStringLiteral("feature"));

        QPushButton *cancelBtn = findButton(&dialog, {QStringLiteral("Cancel")});
        QVERIFY2(cancelBtn, "Cancel button not found");

        QSignalSpy saveSpy(&dialog, &UnsavedChangesDialog::saveRequested);
        QSignalSpy parkSpy(&dialog, &UnsavedChangesDialog::parkRequested);

        cancelBtn->click();

        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
        QCOMPARE(saveSpy.count(), 0);
        QCOMPARE(parkSpy.count(), 0);
    }
};

QTEST_MAIN(TestUnsavedChangesDialog)
#include "test_unsavedchangesdialog.moc"
