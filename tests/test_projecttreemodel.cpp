#include <QtTest>
#include "../src/projecttreemodel.h"
#include "../src/llmservice.h"

// Self-contained mock for LLMService to satisfy SynopsisService dependency
void LLMService::sendNonStreamingRequest(const LLMRequest&, std::function<void(const QString&)>) {}
LLMService& LLMService::instance() { static auto* inst = reinterpret_cast<LLMService*>(new QObject()); return *inst; }

class TestProjectTreeModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    void testRecursiveLocking()
    {
        ProjectTreeModel model;
        QModelIndex root = QModelIndex();
        
        QModelIndex folderIdx = model.addFolder(QStringLiteral("Manuscript"), QStringLiteral("manuscript"), root);
        QVERIFY(folderIdx.isValid());
        
        QModelIndex fileIdx = model.addFile(QStringLiteral("Scene 1"), QStringLiteral("manuscript/scene1.md"), folderIdx);
        QVERIFY(fileIdx.isValid());
        
        ProjectTreeItem *item = model.findItem(QStringLiteral("manuscript/scene1.md"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->name, QStringLiteral("Scene 1"));
    }

    void testItemRemoval()
    {
        ProjectTreeModel model;
        QModelIndex folderIdx = model.addFolder(QStringLiteral("Research"), QStringLiteral("research"), QModelIndex());
        model.addFile(QStringLiteral("Notes"), QStringLiteral("research/notes.md"), folderIdx);
        
        QCOMPARE(model.rowCount(folderIdx), 1);
        
        model.removeItem(model.index(0, 0, folderIdx));
        QCOMPARE(model.rowCount(folderIdx), 0);
    }

    void testModelDataExport()
    {
        ProjectTreeModel model;
        model.addFolder(QStringLiteral("Test"), QStringLiteral("test"), QModelIndex());
        
        QJsonObject data = model.projectData();
        QVERIFY(!data.isEmpty());
        QCOMPARE(data.value(QStringLiteral("name")).toString(), QStringLiteral("Root"));
        
        QJsonArray children = data.value(QStringLiteral("children")).toArray();
        QCOMPARE(children.count(), 1);
    }
};

QTEST_MAIN(TestProjectTreeModel)
#include "test_projecttreemodel.moc"
