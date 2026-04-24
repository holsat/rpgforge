/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    Unit tests for the ProjectMetadata value type (Phase 1 of the
    ProjectManager / tree-refactor plan).
*/

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "../src/projectmanager.h"
#include "../src/projectmetadata.h"
#include "../src/projectkeys.h"
#include "../src/projecttreemodel.h"

class TestProjectMetadata : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --------------------------------------------------------------
    // Pure data round-trips — no filesystem involved.
    // --------------------------------------------------------------

    void testRoundTrip_defaultConstructed()
    {
        const ProjectMetadata original;

        const QJsonObject serialised = original.toJson();
        const ProjectMetadata recovered = ProjectMetadata::fromJson(serialised);

        QCOMPARE(recovered.name, original.name);
        QCOMPARE(recovered.author, original.author);
        QCOMPARE(recovered.pageSize, original.pageSize);
        QCOMPARE(recovered.margins.left, original.margins.left);
        QCOMPARE(recovered.margins.right, original.margins.right);
        QCOMPARE(recovered.margins.top, original.margins.top);
        QCOMPARE(recovered.margins.bottom, original.margins.bottom);
        QCOMPARE(recovered.showPageNumbers, original.showPageNumbers);
        QCOMPARE(recovered.stylesheetPath, original.stylesheetPath);
        QCOMPARE(recovered.autoSync, original.autoSync);
        QCOMPARE(recovered.version, ProjectKeys::CurrentVersion);
        QCOMPARE(recovered.loreKeeperConfig, original.loreKeeperConfig);
        QCOMPARE(recovered.wordCountCache, original.wordCountCache);
        QCOMPARE(recovered.explorationColors, original.explorationColors);
    }

    void testRoundTrip_allFieldsSet()
    {
        ProjectMetadata original;
        original.name = QStringLiteral("Kabal");
        original.author = QStringLiteral("Sheldon Lee Wen");
        original.pageSize = QStringLiteral("Letter");
        original.margins.left = 15.5;
        original.margins.right = 16.5;
        original.margins.top = 17.5;
        original.margins.bottom = 18.5;
        original.showPageNumbers = false;
        original.stylesheetPath = QStringLiteral("stylesheets/dark.css");
        original.autoSync = false;

        QJsonObject lk;
        QJsonArray cats;
        QJsonObject catChars;
        catChars[QStringLiteral("name")] = QStringLiteral("Characters");
        catChars[QStringLiteral("prompt")] = QStringLiteral("Character dossier prompt");
        cats.append(catChars);
        lk[QStringLiteral("categories")] = cats;
        lk[QStringLiteral("customField")] = QStringLiteral("passthrough");
        original.loreKeeperConfig = lk;

        original.wordCountCache.insert(QStringLiteral("abc123"), 1234);
        original.wordCountCache.insert(QStringLiteral("def456"), 5678);
        original.explorationColors.insert(QStringLiteral("main"), QStringLiteral("#a0c4ff"));
        original.explorationColors.insert(QStringLiteral("draft"), QStringLiteral("#ffadad"));

        const QJsonObject serialised = original.toJson();
        const ProjectMetadata recovered = ProjectMetadata::fromJson(serialised);

        QCOMPARE(recovered.name, original.name);
        QCOMPARE(recovered.author, original.author);
        QCOMPARE(recovered.pageSize, original.pageSize);
        QCOMPARE(recovered.margins.left, original.margins.left);
        QCOMPARE(recovered.margins.right, original.margins.right);
        QCOMPARE(recovered.margins.top, original.margins.top);
        QCOMPARE(recovered.margins.bottom, original.margins.bottom);
        QCOMPARE(recovered.showPageNumbers, original.showPageNumbers);
        QCOMPARE(recovered.stylesheetPath, original.stylesheetPath);
        QCOMPARE(recovered.autoSync, original.autoSync);
        QCOMPARE(recovered.loreKeeperConfig, original.loreKeeperConfig);
        QCOMPARE(recovered.wordCountCache, original.wordCountCache);
        QCOMPARE(recovered.explorationColors, original.explorationColors);
    }

    void testMigration_v1FlatMargins()
    {
        // A v1 project stored flat marginLeft/.../marginBottom keys at
        // the top level. ProjectMetadata::fromJson must fold them into
        // the nested margins struct and toJson must emit the v3 layout.
        QJsonObject v1;
        v1[QLatin1String(ProjectKeys::Name)] = QStringLiteral("LegacyProject");
        v1[QLatin1String(ProjectKeys::Author)] = QStringLiteral("Author");
        v1[QLatin1String(ProjectKeys::Version)] = 1;
        v1[QLatin1String(ProjectKeys::MarginLeft)] = 11.0;
        v1[QLatin1String(ProjectKeys::MarginRight)] = 12.0;
        v1[QLatin1String(ProjectKeys::MarginTop)] = 13.0;
        v1[QLatin1String(ProjectKeys::MarginBottom)] = 14.0;

        const ProjectMetadata meta = ProjectMetadata::fromJson(v1);
        QCOMPARE(meta.margins.left, 11.0);
        QCOMPARE(meta.margins.right, 12.0);
        QCOMPARE(meta.margins.top, 13.0);
        QCOMPARE(meta.margins.bottom, 14.0);

        // Saved form must emit the nested margins object (v3 shape).
        const QJsonObject saved = meta.toJson();
        QVERIFY(saved.contains(QLatin1String(ProjectKeys::Margins)));
        const QJsonObject marginsObj =
            saved.value(QLatin1String(ProjectKeys::Margins)).toObject();
        QCOMPARE(marginsObj.value(QStringLiteral("left")).toDouble(),   11.0);
        QCOMPARE(marginsObj.value(QStringLiteral("right")).toDouble(),  12.0);
        QCOMPARE(marginsObj.value(QStringLiteral("top")).toDouble(),    13.0);
        QCOMPARE(marginsObj.value(QStringLiteral("bottom")).toDouble(), 14.0);
        QCOMPARE(saved.value(QLatin1String(ProjectKeys::Version)).toInt(),
                 ProjectKeys::CurrentVersion);
    }

    void testLoreKeeperConfigPassthrough()
    {
        // loreKeeperConfig is opaque; arbitrary nested JSON must round-trip
        // verbatim — the user defines the schema of categories.
        QJsonObject custom;
        custom[QStringLiteral("ui.version")] = 7;
        QJsonArray tags;
        tags.append(QStringLiteral("drama"));
        tags.append(QStringLiteral("mystery"));
        custom[QStringLiteral("tags")] = tags;

        QJsonObject categories;
        categories[QStringLiteral("characters")] = QStringLiteral("Prompt A");
        categories[QStringLiteral("places")] = QStringLiteral("Prompt B");
        custom[QStringLiteral("categoriesByName")] = categories;

        ProjectMetadata meta;
        meta.loreKeeperConfig = custom;

        const ProjectMetadata recovered =
            ProjectMetadata::fromJson(meta.toJson());
        QCOMPARE(recovered.loreKeeperConfig, custom);
    }

    void testWordCountCacheRoundTrip()
    {
        ProjectMetadata meta;
        meta.wordCountCache.insert(QStringLiteral("hash-one"), 42);
        meta.wordCountCache.insert(QStringLiteral("hash-two"), 1337);
        meta.wordCountCache.insert(QStringLiteral("hash-three"), 0);

        const ProjectMetadata recovered =
            ProjectMetadata::fromJson(meta.toJson());
        QCOMPARE(recovered.wordCountCache.size(), 3);
        QCOMPARE(recovered.wordCountCache.value(QStringLiteral("hash-one")).toInt(), 42);
        QCOMPARE(recovered.wordCountCache.value(QStringLiteral("hash-two")).toInt(), 1337);
        QCOMPARE(recovered.wordCountCache.value(QStringLiteral("hash-three")).toInt(), 0);
    }

    // --------------------------------------------------------------
    // ProjectManager integration — unknown keys survive open+save via
    // m_extraJson, and byte-for-byte-compatible (modulo key ordering)
    // round-trip of an existing project file.
    // --------------------------------------------------------------

    void testRoundTrip_preservesExtraFields()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Build a minimal but complete v3 project file with an unknown
        // top-level key that ProjectMetadata does NOT understand.
        QJsonObject margins;
        margins[QStringLiteral("left")] = 20.0;
        margins[QStringLiteral("right")] = 20.0;
        margins[QStringLiteral("top")] = 20.0;
        margins[QStringLiteral("bottom")] = 20.0;

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray();

        QJsonObject original;
        original[QLatin1String(ProjectKeys::Name)] = QStringLiteral("ExtraFieldsProject");
        original[QLatin1String(ProjectKeys::Author)] = QStringLiteral("Author");
        original[QLatin1String(ProjectKeys::PageSize)] = QStringLiteral("A4");
        original[QLatin1String(ProjectKeys::Margins)] = margins;
        original[QLatin1String(ProjectKeys::ShowPageNumbers)] = true;
        original[QLatin1String(ProjectKeys::AutoSync)] = true;
        original[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        original[QLatin1String(ProjectKeys::Tree)] = tree;

        // Future-compat: a key from a newer build that we don't recognise.
        QJsonObject futureFeature;
        futureFeature[QStringLiteral("enabled")] = true;
        futureFeature[QStringLiteral("value")] = 99;
        original[QStringLiteral("futureFeature")] = futureFeature;
        original[QStringLiteral("anotherUnknownField")] = QStringLiteral("preserve me");

        const QString projectDir = tmp.path();
        const QString projectFile = QDir(projectDir).absoluteFilePath(QStringLiteral("rpgforge.project"));
        {
            QFile f(projectFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(original).toJson());
        }

        ProjectManager &pm = ProjectManager::instance();
        pm.closeProject();
        QVERIFY(pm.openProject(projectFile));
        QVERIFY(pm.saveProject());
        pm.closeProject();

        // Re-read from disk: unknown keys must still be present.
        QFile f(projectFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject reloaded = QJsonDocument::fromJson(f.readAll()).object();

        QVERIFY(reloaded.contains(QStringLiteral("futureFeature")));
        QCOMPARE(reloaded.value(QStringLiteral("futureFeature")).toObject(), futureFeature);
        QCOMPARE(reloaded.value(QStringLiteral("anotherUnknownField")).toString(),
                 QStringLiteral("preserve me"));

        // Typed fields still intact.
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Name)).toString(),
                 QStringLiteral("ExtraFieldsProject"));
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Version)).toInt(),
                 ProjectKeys::CurrentVersion);
    }

    void testProjectManager_v1MarginsMigrationOnOpen()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // v1 project file — flat margin keys, no nested margins object.
        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();
        tree[QStringLiteral("children")] = QJsonArray();

        QJsonObject v1;
        v1[QLatin1String(ProjectKeys::Name)] = QStringLiteral("V1MarginsProject");
        v1[QLatin1String(ProjectKeys::Author)] = QStringLiteral("Legacy");
        v1[QLatin1String(ProjectKeys::Version)] = 1;
        v1[QLatin1String(ProjectKeys::MarginLeft)] = 5.0;
        v1[QLatin1String(ProjectKeys::MarginRight)] = 6.0;
        v1[QLatin1String(ProjectKeys::MarginTop)] = 7.0;
        v1[QLatin1String(ProjectKeys::MarginBottom)] = 8.0;
        v1[QLatin1String(ProjectKeys::Tree)] = tree;

        const QString projectFile =
            QDir(tmp.path()).absoluteFilePath(QStringLiteral("rpgforge.project"));
        {
            QFile f(projectFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(v1).toJson());
        }

        ProjectManager &pm = ProjectManager::instance();
        pm.closeProject();
        QVERIFY(pm.openProject(projectFile));

        QCOMPARE(pm.marginLeft(),   5.0);
        QCOMPARE(pm.marginRight(),  6.0);
        QCOMPARE(pm.marginTop(),    7.0);
        QCOMPARE(pm.marginBottom(), 8.0);

        QVERIFY(pm.saveProject());
        pm.closeProject();

        // Post-save: file is in v3 shape, margins live under the nested
        // object and the old flat keys are no longer emitted.
        QFile f(projectFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject reloaded = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(reloaded.contains(QLatin1String(ProjectKeys::Margins)));
        const QJsonObject margins =
            reloaded.value(QLatin1String(ProjectKeys::Margins)).toObject();
        QCOMPARE(margins.value(QStringLiteral("left")).toDouble(),   5.0);
        QCOMPARE(margins.value(QStringLiteral("right")).toDouble(),  6.0);
        QCOMPARE(margins.value(QStringLiteral("top")).toDouble(),    7.0);
        QCOMPARE(margins.value(QStringLiteral("bottom")).toDouble(), 8.0);
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Version)).toInt(),
                 ProjectKeys::CurrentVersion);
    }

    void testProjectManager_roundTripSemanticallyEquivalent()
    {
        // Strong acceptance criterion: openProject + saveProject on an
        // existing v3 project must produce a file that is semantically
        // identical (every typed field + tree + extras preserved).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject margins;
        margins[QStringLiteral("left")] = 12.5;
        margins[QStringLiteral("right")] = 13.5;
        margins[QStringLiteral("top")] = 14.5;
        margins[QStringLiteral("bottom")] = 15.5;

        QJsonObject lk;
        QJsonArray cats;
        QJsonObject cat;
        cat[QStringLiteral("name")] = QStringLiteral("Characters");
        cat[QStringLiteral("prompt")] = QStringLiteral("Dossier please.");
        cats.append(cat);
        lk[QStringLiteral("categories")] = cats;

        QJsonObject tree;
        tree[QStringLiteral("name")] = QStringLiteral("Root");
        tree[QStringLiteral("type")] = 0;
        tree[QStringLiteral("category")] = 0;
        tree[QStringLiteral("path")] = QString();

        QJsonObject manuscript;
        manuscript[QStringLiteral("name")] = QStringLiteral("Manuscript");
        manuscript[QStringLiteral("path")] = QStringLiteral("manuscript");
        manuscript[QStringLiteral("type")] = 0;
        manuscript[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Manuscript);
        manuscript[QStringLiteral("children")] = QJsonArray();

        QJsonObject lorekeeper;
        lorekeeper[QStringLiteral("name")] = QStringLiteral("LoreKeeper");
        lorekeeper[QStringLiteral("path")] = QStringLiteral("lorekeeper");
        lorekeeper[QStringLiteral("type")] = 0;
        lorekeeper[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::LoreKeeper);
        lorekeeper[QStringLiteral("children")] = QJsonArray();

        QJsonObject research;
        research[QStringLiteral("name")] = QStringLiteral("Research");
        research[QStringLiteral("path")] = QStringLiteral("research");
        research[QStringLiteral("type")] = 0;
        research[QStringLiteral("category")] = static_cast<int>(ProjectTreeItem::Research);
        research[QStringLiteral("children")] = QJsonArray();

        tree[QStringLiteral("children")] = QJsonArray{manuscript, lorekeeper, research};

        QJsonObject wordCache;
        wordCache[QStringLiteral("abc")] = 100;
        wordCache[QStringLiteral("def")] = 200;
        QJsonObject colors;
        colors[QStringLiteral("main")] = QStringLiteral("#a0c4ff");

        QJsonObject original;
        original[QLatin1String(ProjectKeys::Name)] = QStringLiteral("RoundTripProject");
        original[QLatin1String(ProjectKeys::Author)] = QStringLiteral("Tester");
        original[QLatin1String(ProjectKeys::PageSize)] = QStringLiteral("A4");
        original[QLatin1String(ProjectKeys::Margins)] = margins;
        original[QLatin1String(ProjectKeys::ShowPageNumbers)] = false;
        original[QLatin1String(ProjectKeys::StylesheetPath)] = QStringLiteral("stylesheets/default.css");
        original[QLatin1String(ProjectKeys::AutoSync)] = false;
        original[QLatin1String(ProjectKeys::Version)] = ProjectKeys::CurrentVersion;
        original[QLatin1String(ProjectKeys::LoreKeeperConfig)] = lk;
        original[QLatin1String(ProjectKeys::Tree)] = tree;
        original[QStringLiteral("wordCountCache")] = wordCache;
        original[QStringLiteral("explorationColors")] = colors;
        original[QStringLiteral("vendorExtension")] =
            QJsonObject{{QStringLiteral("flavor"), QStringLiteral("vanilla")}};

        const QString projectDir = tmp.path();
        QDir(projectDir).mkpath(QStringLiteral("manuscript"));
        QDir(projectDir).mkpath(QStringLiteral("lorekeeper"));
        QDir(projectDir).mkpath(QStringLiteral("research"));

        const QString projectFile =
            QDir(projectDir).absoluteFilePath(QStringLiteral("rpgforge.project"));
        {
            QFile f(projectFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(original).toJson());
        }

        ProjectManager &pm = ProjectManager::instance();
        pm.closeProject();
        QVERIFY(pm.openProject(projectFile));
        QVERIFY(pm.saveProject());
        pm.closeProject();

        QFile f(projectFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject reloaded = QJsonDocument::fromJson(f.readAll()).object();

        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Name)).toString(),
                 original.value(QLatin1String(ProjectKeys::Name)).toString());
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Author)).toString(),
                 original.value(QLatin1String(ProjectKeys::Author)).toString());
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::PageSize)).toString(),
                 original.value(QLatin1String(ProjectKeys::PageSize)).toString());
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Margins)).toObject(), margins);
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::ShowPageNumbers)).toBool(), false);
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::StylesheetPath)).toString(),
                 QStringLiteral("stylesheets/default.css"));
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::AutoSync)).toBool(), false);
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::Version)).toInt(),
                 ProjectKeys::CurrentVersion);
        QCOMPARE(reloaded.value(QLatin1String(ProjectKeys::LoreKeeperConfig)).toObject(), lk);
        QCOMPARE(reloaded.value(QStringLiteral("wordCountCache")).toObject(), wordCache);
        QCOMPARE(reloaded.value(QStringLiteral("explorationColors")).toObject(), colors);
        QCOMPARE(reloaded.value(QStringLiteral("vendorExtension")).toObject(),
                 original.value(QStringLiteral("vendorExtension")).toObject());

        // The tree key must still exist; the structural fields of each
        // authoritative folder must match.
        QVERIFY(reloaded.contains(QLatin1String(ProjectKeys::Tree)));
    }
};

QTEST_MAIN(TestProjectMetadata)
#include "test_projectmetadata.moc"
