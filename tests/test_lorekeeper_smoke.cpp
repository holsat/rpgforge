#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <functional>
#include "../src/lorekeeperservice.h"
#include "../src/llmservice.h"
#include "../src/projectmanager.h"
#include "../src/projecttreemodel.h"

// Mock LLM to capture prompts sent by LoreKeeperService
class PromptCaptureLLM : public LLMService {
public:
    QString lastSystemPrompt;
    QString lastUserPrompt;

    void sendNonStreamingRequest(const LLMRequest &request, std::function<void(const QString&)> callback) override {
        if (!request.messages.isEmpty()) {
            lastSystemPrompt = request.messages.first().content;
            lastUserPrompt = request.messages.last().content;
        }
        
        if (request.serviceName.contains(QStringLiteral("Discovery"))) {
            callback(QStringLiteral("[\"OmoWale\"]"));
        } else {
            callback(QStringLiteral("# OmoWale\nOmoWale is a character from the test."));
        }
    }
};

class TestLoreKeeperSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_testDirPath = QDir::current().absoluteFilePath(QStringLiteral("LoreKeeper_Smoke_Dir"));
        QDir().mkpath(m_testDirPath);
        
        ProjectManager &pm = ProjectManager::instance();
        qDebug() << "ProjectManager: Creating project shell at" << m_testDirPath;
        QVERIFY(pm.createProject(m_testDirPath, QStringLiteral("SmokeTest")));
        pm.setupDefaultProject(m_testDirPath, QStringLiteral("SmokeTest"));
    }

    void cleanupTestCase()
    {
        ProjectManager::instance().closeProject();
        QDir(m_testDirPath).removeRecursively();
    }

    void testManuscriptScanPrompt()
    {
        ProjectManager &pm = ProjectManager::instance();
        
        // 1. Add Kabal content
        QString relPath = QStringLiteral("manuscript/Chapter 1/Scene 1.md");
        QString samplePath = QDir(m_testDirPath).absoluteFilePath(relPath);
        QDir(m_testDirPath).mkpath(QFileInfo(samplePath).absolutePath());

        QFile file(samplePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << R"(
# The Dark Visitor
Dis a true story, no lie....
- OmoWale
I wake up, eyes closed, heart racing. 
)";
        file.close();

        // 2. Register in tree via authoritative API
        ProjectTreeModel *model = pm.model();
        QModelIndex msIdx;
        for (auto *c : model->rootItem()->children) {
            if (c->category == ProjectTreeItem::Manuscript) {
                msIdx = model->indexForItem(c);
                break;
            }
        }
        
        // Add the Chapter 1 folder
        QString folderRelPath = QStringLiteral("manuscript/Chapter 1");
        pm.addFolder(QStringLiteral("Chapter 1"), folderRelPath, QString()); // Add to root for simplicity in test
        
        // Now add the file using the folder's relative path as parent
        pm.addFile(QStringLiteral("Scene 1"), relPath, folderRelPath);

        // 3. Setup services
        PromptCaptureLLM mockLlm;
        LoreKeeperService::instance().init(&mockLlm, nullptr);
        LoreKeeperService::instance().setProjectPath(m_testDirPath);
        
        // 4. Trigger Scan
        QSignalSpy loreSpy(&LoreKeeperService::instance(), &LoreKeeperService::loreUpdated);
        QSignalSpy treeSpy(&pm, &ProjectManager::treeChanged);
        
        LoreKeeperService::instance().scanManuscript();

        // 5. Verify Prompts and resulting files
        if (loreSpy.isEmpty()) QVERIFY(loreSpy.wait(5000));
        
        // Wait for the asynchronous tree update from ProjectManager
        if (treeSpy.isEmpty()) QVERIFY(treeSpy.wait(5000));
        
        QVERIFY(!mockLlm.lastSystemPrompt.isEmpty());
        QVERIFY(mockLlm.lastUserPrompt.contains(QStringLiteral("OmoWale")));
        
        // Verify file was written to disk
        QString charPath = m_testDirPath + QStringLiteral("/lorekeeper/Characters/OmoWale.md");
        QVERIFY(QFile::exists(charPath));
        
        // Verify it was added to the tree
        bool foundInTree = false;
        std::function<void(ProjectTreeItem*)> search = [&](ProjectTreeItem *item) {
            if (!item) return;
            if (item->path == QLatin1String("lorekeeper/Characters/OmoWale.md")) foundInTree = true;
            for (auto *c : item->children) {
                if (foundInTree) break;
                search(c);
            }
        };
        search(model->rootItem());
        QVERIFY(foundInTree);
    }

private:
    QString m_testDirPath;
};

QTEST_MAIN(TestLoreKeeperSmoke)
#include "test_lorekeeper_smoke.moc"
