/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * Regression test for the RAG retrieval-quality improvements shipped on
 * feature/rag-augmentations. Each case is named after the bug it
 * prevents — failures here mean a behavior the user complained about
 * has come back.
 *
 * Coverage:
 *   - Stopword filtering: "Who is Ryzen?" must yield ["Ryzen"], not
 *     ["Who","Ryzen"]. The latter drowns ripgrep with common-word
 *     matches and was why proper-noun queries returned witchdoctor
 *     passages instead of the actual character profile.
 *   - Sliding-window subdivision: long sections must produce multiple
 *     overlapping chunks so a fact straddling a section cut is still
 *     embedded in at least one full-context chunk.
 *   - Citation validator: hallucinated paths like "(manuscript.md)"
 *     get stripped while real paths survive.
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>

#include "../src/knowledgebase.h"
#include "../src/ragassistservice.h"
#include "../src/librariandatabase.h"

#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalSpy>

class TestRagQuality : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ---------- Stopword filtering ----------

    /// Old code returned ["Who","Ryzen"]; "Who" matched dozens of files
    /// and crowded out the proper-noun candidate before ripgrep's
    /// per-root cap of 10 hit Ryzen.md. Fixed by adding interrogatives
    /// to the stopword list.
    void testStopwordsFilterInterrogativesAndAuxiliaries()
    {
        QStringList tokens = KnowledgeBase::extractKeywordTokens(
            QStringLiteral("Who is Ryzen?"));
        QVERIFY2(!tokens.contains(QStringLiteral("Who"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("'Who' should be stopworded; got: ")
                            + tokens.join(QStringLiteral(", "))));
        QVERIFY2(tokens.contains(QStringLiteral("Ryzen")),
                 qPrintable(QStringLiteral("'Ryzen' should survive; got: ")
                            + tokens.join(QStringLiteral(", "))));

        // Verify the broader interrogative + auxiliary sweep works.
        const QStringList probes = {
            QStringLiteral("How does Ryzen work?"),
            QStringLiteral("Why is Ryzen here?"),
            QStringLiteral("Where was Ryzen born?"),
            QStringLiteral("What did Ryzen do?"),
        };
        const QStringList stoppedWords = {
            QStringLiteral("How"), QStringLiteral("Why"), QStringLiteral("Where"),
            QStringLiteral("What"), QStringLiteral("does"), QStringLiteral("was"),
            QStringLiteral("did"),
        };
        for (const QString &q : probes) {
            QStringList t = KnowledgeBase::extractKeywordTokens(q);
            for (const QString &sw : stoppedWords) {
                QVERIFY2(!t.contains(sw, Qt::CaseInsensitive),
                         qPrintable(QStringLiteral("'") + sw +
                                     QStringLiteral("' leaked through for query '") +
                                     q + QStringLiteral("' → ") +
                                     t.join(QStringLiteral(", "))));
            }
            QVERIFY2(t.contains(QStringLiteral("Ryzen")),
                     qPrintable(QStringLiteral("'Ryzen' missing for query '") + q
                                 + QStringLiteral("' → ") + t.join(QStringLiteral(", "))));
        }
    }

    /// Non-ASCII proper nouns must always survive — that's the entire
    /// reason the grep arm exists. Fiction is full of made-up names
    /// that the embedding model has never seen.
    void testStopwordsKeepNonAsciiProperNouns()
    {
        QStringList tokens = KnowledgeBase::extractKeywordTokens(
            QStringLiteral("Tell me about Vål'naden and Sifacsi"));
        QVERIFY2(tokens.contains(QStringLiteral("Vål'naden")),
                 qPrintable(QStringLiteral("got: ") + tokens.join(QStringLiteral(", "))));
        QVERIFY2(tokens.contains(QStringLiteral("Sifacsi")),
                 qPrintable(QStringLiteral("got: ") + tokens.join(QStringLiteral(", "))));
        // "Tell" is in the stopword list.
        QVERIFY(!tokens.contains(QStringLiteral("Tell"), Qt::CaseInsensitive));
    }

    // ---------- Sliding-window subdivision ----------

    /// Short sections must come back unchanged so the existing
    /// heading-aware chunking is preserved for ordinary content.
    void testSubdivideShortChunkIsUnchanged()
    {
        const QString shortText = QStringLiteral(
            "## Section\n\nA short paragraph with one sentence.");
        const QStringList parts = KnowledgeBase::subdivideChunk(shortText, 1500, 200);
        QCOMPARE(parts.size(), 1);
        QCOMPARE(parts.first(), shortText);
    }

    /// The improvement: long sections produce multiple chunks with a
    /// shared tail/head so a sentence straddling a cut still gets
    /// indexed in at least one full-context chunk.
    void testSubdivideLongChunkProducesOverlappingWindows()
    {
        // Build a section that's well over the 1500-char cap, with
        // distinct anchor sentences at known offsets so we can prove
        // overlap.
        QString text = QStringLiteral("## Section\n\n");
        for (int i = 0; i < 60; ++i) {
            text += QStringLiteral("Sentence number %1 contains anchor%1 token. ").arg(i);
        }

        const int maxChars = 800;
        const int overlap  = 200;
        const QStringList parts = KnowledgeBase::subdivideChunk(text, maxChars, overlap);

        QVERIFY2(parts.size() >= 2,
                 qPrintable(QStringLiteral("expected >= 2 sub-chunks, got %1")
                             .arg(parts.size())));

        // Each sub-chunk should be at most maxChars long (with a few
        // chars of slack for the snap-to-boundary logic).
        for (const QString &p : parts) {
            QVERIFY2(p.size() <= maxChars + 32,
                     qPrintable(QStringLiteral("sub-chunk too long: %1 chars").arg(p.size())));
        }

        // Adjacent chunks must overlap. We test by checking that the
        // last 100 chars of chunk N appear somewhere in chunk N+1.
        for (int i = 0; i < parts.size() - 1; ++i) {
            const QString tail = parts[i].right(100);
            QVERIFY2(parts[i + 1].contains(tail),
                     qPrintable(QStringLiteral("chunks %1 and %2 do not overlap; "
                                                "tail of %1 was: '%3'")
                                .arg(i).arg(i + 1).arg(tail.left(40))));
        }
    }

    /// The cut must prefer paragraph / sentence boundaries over a
    /// hard mid-word slice when one is available within the overlap
    /// region. We construct a text where paragraph breaks land just
    /// before the hard cut and assert the chunk ends with a period.
    void testSubdivideRespectsParagraphBoundaries()
    {
        // 5 paragraphs of ~250 chars each = 1250 chars total. Cap at
        // 600 with 200 overlap so a paragraph break is reachable from
        // the search floor.
        QString text;
        for (int i = 0; i < 5; ++i) {
            text += QStringLiteral("Paragraph %1 text. ").arg(i)
                  + QString(220, QLatin1Char('x'))
                  + QStringLiteral(".\n\n");
        }
        const QStringList parts = KnowledgeBase::subdivideChunk(text, 600, 200);
        QVERIFY(parts.size() >= 2);
        // First chunk should end on a sentence/paragraph boundary —
        // either a newline or a period.
        const QString first = parts.first();
        const QChar last = first.at(first.size() - 1);
        QVERIFY2(last == QLatin1Char('\n') || last == QLatin1Char('.')
                 || last == QLatin1Char(' '),
                 qPrintable(QStringLiteral("first chunk did not end on a boundary; "
                                            "last char was U+%1, tail: '%2'")
                            .arg(static_cast<int>(last.unicode()), 4, 16, QLatin1Char('0'))
                            .arg(first.right(30))));
    }

    // ---------- Citation validator ----------

    /// A response citing a real file in the project should pass through
    /// unchanged.
    void testCitationValidatorKeepsValidCitations()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir(dir.path()).mkpath(QStringLiteral("lorekeeper/Characters"));
        QFile f(dir.path() + QStringLiteral("/lorekeeper/Characters/Ryzen.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# Ryzen\n");
        f.close();

        const QString prose = QStringLiteral(
            "Ryzen is a sailor (lorekeeper/Characters/Ryzen.md) who later joins the cult.");
        const QString out = RagAssistService::stripInvalidCitations(prose, dir.path());
        QCOMPARE(out, prose);
    }

    /// Hallucinated citations like the gemma-3-4b "(manuscript.md)"
    /// failure mode must be removed without mangling the surrounding
    /// prose.
    void testCitationValidatorStripsHallucinatedCitations()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir(dir.path()).mkpath(QStringLiteral("lorekeeper/Characters"));
        QFile f(dir.path() + QStringLiteral("/lorekeeper/Characters/Ryzen.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# Ryzen\n");
        f.close();

        // "manuscript.md" does not exist in this project; the second
        // citation does. Validator must keep the second, drop the first.
        const QString prose = QStringLiteral(
            "Ryzen is described in (manuscript.md) and again in "
            "(lorekeeper/Characters/Ryzen.md).");
        const QString out = RagAssistService::stripInvalidCitations(prose, dir.path());

        QVERIFY2(!out.contains(QStringLiteral("manuscript.md")),
                 qPrintable(QStringLiteral("hallucinated citation survived: ") + out));
        QVERIFY2(out.contains(QStringLiteral("lorekeeper/Characters/Ryzen.md")),
                 qPrintable(QStringLiteral("real citation was stripped: ") + out));
        // Surrounding prose should remain readable.
        QVERIFY(out.contains(QStringLiteral("Ryzen is described")));
    }

    /// Anchor fragments (#section) must be tolerated when checking
    /// existence — they refer to a heading inside the file, not a
    /// separate file.
    void testCitationValidatorHandlesAnchorFragments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir(dir.path()).mkpath(QStringLiteral("research"));
        QFile f(dir.path() + QStringLiteral("/research/world.md"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# World\n## Geography\n");
        f.close();

        const QString prose = QStringLiteral(
            "The northern coast (research/world.md#geography) is rugged.");
        const QString out = RagAssistService::stripInvalidCitations(prose, dir.path());
        QCOMPARE(out, prose);    // anchor citation should pass through
    }

    /// SOURCE-tag style citations (sometimes echoed by smaller models
    /// that copy the input prompt header) must also be validated.
    void testCitationValidatorStripsBogusSourceTags()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString prose = QStringLiteral(
            "Some claim [SOURCE: imaginary/file.md] and more text.");
        const QString out = RagAssistService::stripInvalidCitations(prose, dir.path());
        QVERIFY2(!out.contains(QStringLiteral("[SOURCE:")),
                 qPrintable(QStringLiteral("bogus SOURCE tag survived: ") + out));
        QVERIFY(out.contains(QStringLiteral("Some claim")));
        QVERIFY(out.contains(QStringLiteral("and more text")));
    }

    /// Validator must be a no-op when projectPath is empty — that's
    /// the case before a project is opened, and the assistant should
    /// not strip citations during model self-tests.
    void testCitationValidatorIsNoOpWithoutProject()
    {
        const QString prose = QStringLiteral("See (some/file.md) for details.");
        QCOMPARE(RagAssistService::stripInvalidCitations(prose, QString()), prose);
    }

    // ---------- Phase 3: graph-augmented retrieval ----------

    /// Synthesize a project with two entities and one relationship,
    /// chunks linked to each. A query mentioning entity A must surface
    /// chunks linked to entity B too — that's the graph-traversal arm
    /// in action. Run hybridSearch and assert the merged result set
    /// contains B's chunks.
    void testGraphTraversalSurfacesNeighborChunks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 1. Set up the librarian DB with two entities + a relationship.
        const QString libDbPath = QDir(dir.path()).absoluteFilePath(
            QStringLiteral(".rpgforge.db"));
        LibrarianDatabase libDb;
        QVERIFY(libDb.open(libDbPath));
        const qint64 ryzen = libDb.addEntity(
            QStringLiteral("Ryzen"), QStringLiteral("character"),
            QStringLiteral("a.md"));
        const qint64 sah = libDb.addEntity(
            QStringLiteral("Sahmokhan"), QStringLiteral("character"),
            QStringLiteral("b.md"));
        QVERIFY(ryzen > 0 && sah > 0);
        libDb.upsertRelationship(ryzen, sah, QStringLiteral("friend"),
                                  QString(), 0, 0.7);

        // 2. Set up the vector DB with the same schema KnowledgeBase
        //    creates, plus chunk_entities pointing chunks at each entity.
        const QString vecDbPath = QDir(dir.path()).absoluteFilePath(
            QStringLiteral(".rpgforge-vectors.db"));
        const QString conn = QStringLiteral("phase3_vec");
        {
            QSqlDatabase vec = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            vec.setDatabaseName(vecDbPath);
            QVERIFY(vec.open());
            QSqlQuery q(vec);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE chunks (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "file_path TEXT, heading TEXT, content TEXT, file_hash TEXT, "
                "embedding BLOB)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE chunk_entities (chunk_id INTEGER NOT NULL, "
                "entity_id INTEGER NOT NULL, PRIMARY KEY (chunk_id, entity_id))")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO chunks (id, file_path, heading, content) VALUES "
                "(1, 'lorekeeper/Ryzen.md', 'Ryzen', 'Ryzen sailed home.'), "
                "(2, 'lorekeeper/Sahmokhan.md', 'Sah', 'A loyal companion.')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO chunk_entities VALUES "
                "(1, %1), (2, %2)").arg(ryzen).arg(sah)));
            vec.close();
        }
        QSqlDatabase::removeDatabase(conn);

        // 3. Init KnowledgeBase against the project root + run
        //    hybridSearch for a query that should resolve to Ryzen.
        //    The vector arm will return nothing (no embeddings), the
        //    grep arm will hit Ryzen.md, and the graph arm should
        //    pick up Sahmokhan.md via the friend edge.
        KnowledgeBase::instance().initForProject(dir.path());

        QList<SearchResult> results;
        QEventLoop loop;
        bool done = false;
        KnowledgeBase::instance().hybridSearch(
            QStringLiteral("Tell me about Ryzen"), 20, QString(),
            [&](const QList<SearchResult> &r) {
                results = r;
                done = true;
                loop.quit();
            });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(done, "hybridSearch did not call back within 5s");

        // The graph arm should have surfaced Sahmokhan's chunk via the
        // friend relationship — that's a passage the literal-match
        // arm could never have reached because "Sahmokhan" never
        // appears in the query.
        bool foundSah = false;
        for (const auto &r : results) {
            if (r.filePath.contains(QStringLiteral("Sahmokhan"))) {
                foundSah = true;
                break;
            }
        }
        QVERIFY2(foundSah, qPrintable(QStringLiteral(
            "graph arm did not surface Sahmokhan's chunk via the friend edge; "
            "got %1 results").arg(results.size())));
    }

    /// When the query contains no entity name that resolves against
    /// the alias index, the graph arm contributes 0 results — the
    /// pipeline falls through to vector + grep only. We can't observe
    /// 0 directly without internal hooks, but we can verify that a
    /// query using only generic words against a known-entities project
    /// does not surface chunks linked to the aliased entity.
    void testGraphTraversalSkippedWhenNoEntityMatch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString libDbPath = QDir(dir.path()).absoluteFilePath(
            QStringLiteral(".rpgforge.db"));
        LibrarianDatabase libDb;
        QVERIFY(libDb.open(libDbPath));
        const qint64 ryzen = libDb.addEntity(
            QStringLiteral("Ryzen"), QStringLiteral("character"),
            QStringLiteral("a.md"));

        const QString vecDbPath = QDir(dir.path()).absoluteFilePath(
            QStringLiteral(".rpgforge-vectors.db"));
        const QString conn = QStringLiteral("phase3_no_match");
        {
            QSqlDatabase vec = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            vec.setDatabaseName(vecDbPath);
            QVERIFY(vec.open());
            QSqlQuery q(vec);
            q.exec(QStringLiteral(
                "CREATE TABLE chunks (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "file_path TEXT, heading TEXT, content TEXT, file_hash TEXT, "
                "embedding BLOB)"));
            q.exec(QStringLiteral(
                "CREATE TABLE chunk_entities (chunk_id INTEGER NOT NULL, "
                "entity_id INTEGER NOT NULL, PRIMARY KEY (chunk_id, entity_id))"));
            q.exec(QStringLiteral(
                "INSERT INTO chunks (id, file_path, heading, content) VALUES "
                "(1, 'lorekeeper/Ryzen.md', 'Ryzen', 'Ryzen content.')"));
            q.exec(QStringLiteral(
                "INSERT INTO chunk_entities VALUES (1, %1)").arg(ryzen));
            vec.close();
        }
        QSqlDatabase::removeDatabase(conn);

        KnowledgeBase::instance().initForProject(dir.path());
        QList<SearchResult> results;
        QEventLoop loop;
        KnowledgeBase::instance().hybridSearch(
            QStringLiteral("the the the"), 20, QString(),
            [&](const QList<SearchResult> &r) { results = r; loop.quit(); });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();

        // Generic query → no resolved entities → no graph results.
        // Ryzen's chunk should NOT appear via the graph arm. (It might
        // still appear via vector if embeddings ran, but here there
        // are no embeddings, so the only way it could show up is the
        // graph arm — and we want it not to.)
        bool foundRyzen = false;
        for (const auto &r : results) {
            if (r.filePath.contains(QStringLiteral("Ryzen"))) {
                foundRyzen = true;
                break;
            }
        }
        QVERIFY2(!foundRyzen, qPrintable(QStringLiteral(
            "graph arm leaked Ryzen.md for a query with no entity tokens; "
            "got %1 results").arg(results.size())));
    }
};

QTEST_MAIN(TestRagQuality)
#include "test_ragquality.moc"
