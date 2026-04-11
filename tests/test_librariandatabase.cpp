#include <QtTest>
#include <QSignalSpy>
#include "../src/librariandatabase.h"

class TestLibrarianDatabase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_db = new LibrarianDatabase(this);
        m_tempPath = QStringLiteral("test_librarian.db");
    }

    void cleanupTestCase()
    {
        m_db->close();
        QFile::remove(m_tempPath);
    }

    void testOpenAndSchema()
    {
        QVERIFY(m_db->open(m_tempPath));
        QVERIFY(QFile::exists(m_tempPath));
    }

    void testEntityManagement()
    {
        qint64 id = m_db->addEntity(QStringLiteral("Dragon"), QStringLiteral("Monster"), QStringLiteral("bestiary.md"));
        QVERIFY(id != -1);

        QVERIFY(m_db->updateEntity(id, QStringLiteral("Red Dragon"), QStringLiteral("Monster")));
        
        QList<qint64> ids = m_db->findEntitiesByType(QStringLiteral("Monster"));
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), id);
    }

    void testAttributeManagement()
    {
        qint64 id = m_db->addEntity(QStringLiteral("Slave"), QStringLiteral("SocialClass"), QStringLiteral("classes.md"));
        
        QVERIFY(m_db->setAttribute(id, QStringLiteral("resilience"), -2));
        QVERIFY(m_db->setAttribute(id, QStringLiteral("whisper"), 3));
        QVERIFY(m_db->setAttribute(id, QStringLiteral("description"), QStringLiteral("Indentured servant")));

        QCOMPARE(m_db->getAttribute(id, QStringLiteral("resilience")).toInt(), -2);
        QCOMPARE(m_db->getAttribute(id, QStringLiteral("whisper")).toInt(), 3);
        QCOMPARE(m_db->getAttribute(id, QStringLiteral("description")).toString(), QStringLiteral("Indentured servant"));

        QVariantMap attrs = m_db->getAttributes(id);
        QCOMPARE(attrs.count(), 3);
        QCOMPARE(attrs.value(QStringLiteral("resilience")).toInt(), -2);
    }

    void testDependencyGraph()
    {
        qint64 id = m_db->addEntity(QStringLiteral("Fireball"), QStringLiteral("Spell"), QStringLiteral("spells.md"));
        QVERIFY(m_db->addReference(id, QStringLiteral("chapter1.md")));
        QVERIFY(m_db->addReference(id, QStringLiteral("chapter2.md")));

        QStringList refs = m_db->getReferences(id);
        QCOMPARE(refs.size(), 2);
        QVERIFY(refs.contains(QStringLiteral("chapter1.md")));
    }

private:
    LibrarianDatabase *m_db;
    QString m_tempPath;
};

QTEST_MAIN(TestLibrarianDatabase)
#include "test_librariandatabase.moc"
