#include <QtTest>
#include <QSignalSpy>
#include <QMimeData>
#include <QDataStream>
#include "../src/projecttreemodel.h"

class TestProjectTreeModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    // ---------------------------------------------------------------
    // Existing tests (preserved)
    // ---------------------------------------------------------------

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

    // ---------------------------------------------------------------
    // loadItem: integer type/category (existing behavior)
    // ---------------------------------------------------------------

    void test_loadItem_integerTypeFolderPreserved()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0; // Folder
        root[QStringLiteral("category")] = 0; // None

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("Manuscript");
        child[QStringLiteral("path")] = QStringLiteral("manuscript");
        child[QStringLiteral("type")] = 0; // Folder
        child[QStringLiteral("category")] = 1; // Manuscript

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        QCOMPARE(model.rowCount(QModelIndex()), 1);
        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::Folder);
        QCOMPARE(item->category, ProjectTreeItem::Manuscript);
    }

    void test_loadItem_integerTypeFilePreserved()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("scene1.md");
        child[QStringLiteral("path")] = QStringLiteral("manuscript/scene1.md");
        child[QStringLiteral("type")] = 1; // File
        child[QStringLiteral("category")] = 6; // Scene

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QCOMPARE(item->type, ProjectTreeItem::File);
        QCOMPARE(item->category, ProjectTreeItem::Scene);
    }

    // ---------------------------------------------------------------
    // loadItem: string type/category
    // ---------------------------------------------------------------

    void test_loadItem_stringTypeFile()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("notes.md");
        child[QStringLiteral("path")] = QStringLiteral("research/notes.md");
        child[QStringLiteral("type")] = QStringLiteral("file");
        child[QStringLiteral("category")] = QStringLiteral("research");

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QVERIFY(item != nullptr);
        QCOMPARE(item->type, ProjectTreeItem::File);
        QCOMPARE(item->category, ProjectTreeItem::Research);
    }

    void test_loadItem_stringTypeFolder()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("Manuscript");
        child[QStringLiteral("path")] = QStringLiteral("manuscript");
        child[QStringLiteral("type")] = QStringLiteral("folder");
        child[QStringLiteral("category")] = QStringLiteral("manuscript");

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QCOMPARE(item->type, ProjectTreeItem::Folder);
        QCOMPARE(item->category, ProjectTreeItem::Manuscript);
    }

    void test_loadItem_stringTypeCaseInsensitive()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("test.md");
        child[QStringLiteral("path")] = QStringLiteral("test.md");
        child[QStringLiteral("type")] = QStringLiteral("FILE");
        child[QStringLiteral("category")] = QStringLiteral("LoreKeeper");

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QCOMPARE(item->type, ProjectTreeItem::File);
        QCOMPARE(item->category, ProjectTreeItem::LoreKeeper);
    }

    void test_loadItem_stringCategoryAllValues()
    {
        // Test all known string categories map correctly
        struct CatCase {
            const char *str;
            ProjectTreeItem::Category expected;
        };
        CatCase cases[] = {
            {"none", ProjectTreeItem::None},
            {"manuscript", ProjectTreeItem::Manuscript},
            {"research", ProjectTreeItem::Research},
            {"lorekeeper", ProjectTreeItem::LoreKeeper},
            {"media", ProjectTreeItem::Media},
            {"chapter", ProjectTreeItem::Chapter},
            {"scene", ProjectTreeItem::Scene},
            {"characters", ProjectTreeItem::Characters},
            {"places", ProjectTreeItem::Places},
            {"cultures", ProjectTreeItem::Cultures},
            {"stylesheet", ProjectTreeItem::Stylesheet},
            {"notes", ProjectTreeItem::Notes},
        };

        for (const auto &tc : cases) {
            ProjectTreeModel model;
            QJsonObject root;
            root[QStringLiteral("name")] = QStringLiteral("Root");
            root[QStringLiteral("type")] = 0;
            root[QStringLiteral("category")] = 0;

            QJsonObject child;
            child[QStringLiteral("name")] = QStringLiteral("item");
            child[QStringLiteral("path")] = QStringLiteral("item");
            child[QStringLiteral("type")] = 0;
            child[QStringLiteral("category")] = QString::fromLatin1(tc.str);

            root[QStringLiteral("children")] = QJsonArray{child};
            model.setProjectData(root);

            ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
            QVERIFY2(item != nullptr, qPrintable(QStringLiteral("Failed for category: %1").arg(QLatin1String(tc.str))));
            QCOMPARE(item->category, tc.expected);
        }
    }

    void test_loadItem_unknownStringCategoryDefaultsToNone()
    {
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("unknown");
        child[QStringLiteral("path")] = QStringLiteral("unknown");
        child[QStringLiteral("type")] = QStringLiteral("folder");
        child[QStringLiteral("category")] = QStringLiteral("totally_unknown_category");

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QCOMPARE(item->category, ProjectTreeItem::None);
    }

    void test_loadItem_nonFileStringTypeDefaultsToFolder()
    {
        // Any string that is not "file" (case-insensitive) should become Folder
        ProjectTreeModel model;
        QJsonObject root;
        root[QStringLiteral("name")] = QStringLiteral("Root");
        root[QStringLiteral("type")] = 0;
        root[QStringLiteral("category")] = 0;

        QJsonObject child;
        child[QStringLiteral("name")] = QStringLiteral("thing");
        child[QStringLiteral("path")] = QStringLiteral("thing");
        child[QStringLiteral("type")] = QStringLiteral("directory");
        child[QStringLiteral("category")] = 0;

        root[QStringLiteral("children")] = QJsonArray{child};
        model.setProjectData(root);

        ProjectTreeItem *item = model.itemFromIndex(model.index(0, 0, QModelIndex()));
        QCOMPARE(item->type, ProjectTreeItem::Folder);
    }

    // ---------------------------------------------------------------
    // moveItem: circular move prevention
    // ---------------------------------------------------------------

    void test_moveItem_rejectsSelfMove()
    {
        ProjectTreeModel model;
        QModelIndex folderIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        ProjectTreeItem *folder = model.itemFromIndex(folderIdx);

        QVERIFY(!model.moveItem(folder, folder, 0));
    }

    void test_moveItem_rejectsCircularMoveToChild()
    {
        // Build tree: Root > A > B > C
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("a/b"), aIdx);
        QModelIndex cIdx = model.addFolder(QStringLiteral("C"), QStringLiteral("a/b/c"), bIdx);

        ProjectTreeItem *a = model.itemFromIndex(aIdx);
        ProjectTreeItem *c = model.itemFromIndex(cIdx);

        // Try to move A into C (its descendant) - must fail
        QVERIFY(!model.moveItem(a, c, 0));
        // Verify A is still under root
        QCOMPARE(model.rowCount(QModelIndex()), 1);
    }

    void test_moveItem_rejectsCircularMoveToGrandchild()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("a/b"), aIdx);
        model.addFolder(QStringLiteral("C"), QStringLiteral("a/b/c"), bIdx);
        QModelIndex dIdx = model.addFolder(QStringLiteral("D"), QStringLiteral("a/b/c/d"),
                                           model.index(0, 0, bIdx));

        ProjectTreeItem *a = model.itemFromIndex(aIdx);
        ProjectTreeItem *d = model.itemFromIndex(dIdx);

        QVERIFY(!model.moveItem(a, d, 0));
    }

    void test_moveItem_allowsValidMove()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), QModelIndex());

        ProjectTreeItem *a = model.itemFromIndex(aIdx);
        ProjectTreeItem *b = model.itemFromIndex(bIdx);

        // Move A into B - valid, they are siblings
        QVERIFY(model.moveItem(a, b, 0));
        QCOMPARE(model.rowCount(QModelIndex()), 1); // only B at root
        QCOMPARE(b->children.count(), 1);
        QCOMPARE(b->children.first()->name, QStringLiteral("A"));
    }

    void test_moveItem_rejectsNullItem()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        ProjectTreeItem *a = model.itemFromIndex(aIdx);

        QVERIFY(!model.moveItem(nullptr, a, 0));
        QVERIFY(!model.moveItem(a, nullptr, 0));
    }

    // ---------------------------------------------------------------
    // Drag-and-drop: supportedDropActions, mimeTypes
    // ---------------------------------------------------------------

    void test_supportedDropActions()
    {
        ProjectTreeModel model;
        QCOMPARE(model.supportedDropActions(), Qt::MoveAction);
    }

    void test_mimeTypes()
    {
        ProjectTreeModel model;
        QStringList types = model.mimeTypes();
        QCOMPARE(types.size(), 1);
        QCOMPARE(types.first(), QStringLiteral("application/x-rpgforge-treeitem"));
    }

    // ---------------------------------------------------------------
    // Drag-and-drop: mimeData round-trip encoding/decoding
    // ---------------------------------------------------------------

    void test_mimeData_roundTrip()
    {
        ProjectTreeModel model;
        QModelIndex folderIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        model.addFile(QStringLiteral("file.md"), QStringLiteral("a/file.md"), folderIdx);

        QModelIndex fileIdx = model.index(0, 0, folderIdx);
        QVERIFY(fileIdx.isValid());

        QModelIndexList indexes;
        indexes << fileIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-rpgforge-treeitem")));

        // The model encodes name + path as QString pairs
        QByteArray encoded = mime->data(QStringLiteral("application/x-rpgforge-treeitem"));
        QDataStream stream(&encoded, QIODevice::ReadOnly);
        QString name, path;
        stream >> name >> path;
        QCOMPARE(name, QStringLiteral("file.md"));
        QCOMPARE(path, QStringLiteral("a/file.md"));

        delete mime;
    }

    void test_mimeData_emptyIndexesReturnsNull()
    {
        ProjectTreeModel model;
        QMimeData *mime = model.mimeData(QModelIndexList());
        QVERIFY(mime == nullptr);
    }

    // ---------------------------------------------------------------
    // canDropMimeData: rejection cases
    // ---------------------------------------------------------------

    void test_canDropMimeData_rejectsDropOntoFileItem()
    {
        ProjectTreeModel model;
        QModelIndex folderIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex fileIdx = model.addFile(QStringLiteral("target.md"), QStringLiteral("a/target.md"), folderIdx);

        // Create another file to drag
        QModelIndex srcIdx = model.addFile(QStringLiteral("src.md"), QStringLiteral("a/src.md"), folderIdx);

        QModelIndexList indexes;
        indexes << srcIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        // Try to drop onto the file item - should be rejected
        QVERIFY(!model.canDropMimeData(mime, Qt::MoveAction, -1, 0, fileIdx));

        delete mime;
    }

    void test_canDropMimeData_rejectsDropOntoDescendant()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("a/b"), aIdx);

        // Drag A, try to drop onto B (child of A) - must reject
        QModelIndexList indexes;
        indexes << aIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        QVERIFY(!model.canDropMimeData(mime, Qt::MoveAction, -1, 0, bIdx));

        delete mime;
    }

    void test_canDropMimeData_rejectsDropOntoSelf()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());

        QModelIndexList indexes;
        indexes << aIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        QVERIFY(!model.canDropMimeData(mime, Qt::MoveAction, -1, 0, aIdx));

        delete mime;
    }

    void test_canDropMimeData_acceptsDropOntoFolder()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), QModelIndex());

        // Drag A, drop onto B - valid
        QModelIndexList indexes;
        indexes << aIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        QVERIFY(model.canDropMimeData(mime, Qt::MoveAction, -1, 0, bIdx));

        delete mime;
    }

    void test_canDropMimeData_rejectsNonMoveAction()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), QModelIndex());

        QModelIndexList indexes;
        indexes << aIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        QVERIFY(!model.canDropMimeData(mime, Qt::CopyAction, -1, 0, bIdx));

        delete mime;
    }

    void test_canDropMimeData_rejectsNullData()
    {
        ProjectTreeModel model;
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), QModelIndex());
        QVERIFY(!model.canDropMimeData(nullptr, Qt::MoveAction, -1, 0, bIdx));
    }

    void test_canDropMimeData_rejectsWrongMimeFormat()
    {
        ProjectTreeModel model;
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), QModelIndex());

        auto *mime = new QMimeData();
        mime->setData(QStringLiteral("text/plain"), QByteArray("stuff"));

        QVERIFY(!model.canDropMimeData(mime, Qt::MoveAction, -1, 0, bIdx));

        delete mime;
    }

    void test_canDropMimeData_acceptsDropOntoRoot()
    {
        ProjectTreeModel model;
        QModelIndex aIdx = model.addFolder(QStringLiteral("A"), QStringLiteral("a"), QModelIndex());
        QModelIndex bIdx = model.addFolder(QStringLiteral("B"), QStringLiteral("b"), aIdx);

        // Drag B, drop onto root (invalid QModelIndex resolves to rootItem)
        QModelIndexList indexes;
        indexes << bIdx;
        QMimeData *mime = model.mimeData(indexes);
        QVERIFY(mime != nullptr);

        QVERIFY(model.canDropMimeData(mime, Qt::MoveAction, -1, 0, QModelIndex()));

        delete mime;
    }
};

QTEST_MAIN(TestProjectTreeModel)
#include "test_projecttreemodel.moc"
