/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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

#include "projectmanager.h"
#include "projecttreemodel.h"
#include "variablemanager.h"

#include <KLocalizedString>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

ProjectManager& ProjectManager::instance()
{
    static ProjectManager s_instance;
    return s_instance;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
    loadDefaults();
}

void ProjectManager::loadDefaults()
{
    m_data = QJsonObject();
    m_data[QStringLiteral("name")] = i18n("Untitled Project");
    m_data[QStringLiteral("author")] = i18n("Unknown Author");
    m_data[QStringLiteral("pageSize")] = QStringLiteral("A4");
    m_data[QStringLiteral("marginLeft")] = 20.0;
    m_data[QStringLiteral("marginRight")] = 20.0;
    m_data[QStringLiteral("marginTop")] = 20.0;
    m_data[QStringLiteral("marginBottom")] = 20.0;
    m_data[QStringLiteral("showPageNumbers")] = true;
    m_data[QStringLiteral("autoSync")] = true;
}

QString ProjectManager::projectName() const
{
    return m_data.value(QStringLiteral("name")).toString();
}

void ProjectManager::setProjectName(const QString &name)
{
    if (projectName() != name) {
        m_data[QStringLiteral("name")] = name;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::author() const
{
    return m_data.value(QStringLiteral("author")).toString();
}

void ProjectManager::setAuthor(const QString &author)
{
    if (this->author() != author) {
        m_data[QStringLiteral("author")] = author;
        Q_EMIT projectSettingsChanged();
    }
}

QString ProjectManager::pageSize() const { return m_data.value(QStringLiteral("pageSize")).toString(); }
void ProjectManager::setPageSize(const QString &size) { m_data[QStringLiteral("pageSize")] = size; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginLeft() const { return m_data.value(QStringLiteral("marginLeft")).toDouble(); }
void ProjectManager::setMarginLeft(double margin) { m_data[QStringLiteral("marginLeft")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginRight() const { return m_data.value(QStringLiteral("marginRight")).toDouble(); }
void ProjectManager::setMarginRight(double margin) { m_data[QStringLiteral("marginRight")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginTop() const { return m_data.value(QStringLiteral("marginTop")).toDouble(); }
void ProjectManager::setMarginTop(double margin) { m_data[QStringLiteral("marginTop")] = margin; Q_EMIT projectSettingsChanged(); }

double ProjectManager::marginBottom() const { return m_data.value(QStringLiteral("marginBottom")).toDouble(); }
void ProjectManager::setMarginBottom(double margin) { m_data[QStringLiteral("marginBottom")] = margin; Q_EMIT projectSettingsChanged(); }

bool ProjectManager::showPageNumbers() const { return m_data.value(QStringLiteral("showPageNumbers")).toBool(); }
void ProjectManager::setShowPageNumbers(bool show) { m_data[QStringLiteral("showPageNumbers")] = show; Q_EMIT projectSettingsChanged(); }

QString ProjectManager::stylesheetPath() const { return m_data.value(QStringLiteral("stylesheetPath")).toString(); }
void ProjectManager::setStylesheetPath(const QString &path) { m_data[QStringLiteral("stylesheetPath")] = path; Q_EMIT projectSettingsChanged(); }

bool ProjectManager::autoSync() const { return m_data.value(QStringLiteral("autoSync")).toBool(); }
void ProjectManager::setAutoSync(bool enabled)
{
    if (autoSync() != enabled) {
        m_data[QStringLiteral("autoSync")] = enabled;
        Q_EMIT projectSettingsChanged();
    }
}

QJsonObject ProjectManager::tree() const
{
    return m_data.value(QStringLiteral("tree")).toObject();
}

void ProjectManager::setTree(const QJsonObject &tree)
{
    m_data[QStringLiteral("tree")] = tree;
    Q_EMIT treeChanged();
}

QString ProjectManager::projectPath() const
{
    if (m_projectFilePath.isEmpty()) return QString();
    return QFileInfo(m_projectFilePath).absolutePath();
}

bool ProjectManager::openProject(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    m_projectFilePath = filePath;
    m_data = doc.object();
    
    Q_EMIT projectOpened();
    return true;
}

bool ProjectManager::createProject(const QString &dirPath, const QString &projectName)
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    m_projectFilePath = dir.absoluteFilePath(QStringLiteral("rpgforge.project"));
    loadDefaults();
    setProjectName(projectName);
    
    // Create stylesheets/ directory and default style.css
    dir.mkdir(QStringLiteral("stylesheets"));
    QString stylePath = dir.absoluteFilePath(QStringLiteral("stylesheets/style.css"));
    if (!QFile::exists(stylePath)) {
        QFile styleFile(stylePath);
        if (styleFile.open(QIODevice::WriteOnly)) {
            styleFile.write(
                "/* RPG Forge Project Stylesheet */\n"
                "body { font-family: serif; line-height: 1.6; }\n"
            );
            styleFile.close();
        }
    }

    bool saved = saveProject();
    if (saved) {
        Q_EMIT projectOpened();
    }
    return saved;
}

void ProjectManager::setupDefaultProject(const QString &dir, const QString &name)
{
    ProjectTreeModel model;
    QDir projectDir(dir);
    
    // Add top level folder with project name
    QModelIndex rootIdx = model.addFolder(name, QString());

    // 1. Manuscript
    projectDir.mkpath(QStringLiteral("manuscript"));
    QModelIndex manuscriptIdx = model.addFolder(i18n("Manuscript"), QStringLiteral("manuscript"), rootIdx);
    model.setData(manuscriptIdx, ProjectTreeItem::Manuscript, ProjectTreeModel::CategoryRole);
    
    QModelIndex chapterIdx = model.addFolder(i18n("Chapter"), QStringLiteral("manuscript/Chapter"), manuscriptIdx);
    model.setData(chapterIdx, ProjectTreeItem::Chapter, ProjectTreeModel::CategoryRole);
    
    QString scenePath = QStringLiteral("manuscript/scene1.md");
    QFile sceneFile(projectDir.absoluteFilePath(scenePath));
    if (sceneFile.open(QIODevice::WriteOnly)) {
        sceneFile.write("# New Scene\n\nWrite your story here...");
        sceneFile.close();
    }
    QModelIndex sceneIdx = model.addFile(i18n("Scene 1"), scenePath, chapterIdx);
    model.setData(sceneIdx, ProjectTreeItem::Scene, ProjectTreeModel::CategoryRole);

    // 2. Research
    projectDir.mkpath(QStringLiteral("research"));
    QModelIndex researchIdx = model.addFolder(i18n("Research"), QStringLiteral("research"), rootIdx);
    model.setData(researchIdx, ProjectTreeItem::Research, ProjectTreeModel::CategoryRole);
    
    auto addResearchSubFolder = [&](const QString &subName, const QString &rel, ProjectTreeItem::Category cat) {
        QModelIndex idx = model.addFolder(subName, rel, researchIdx);
        model.setData(idx, cat, ProjectTreeModel::CategoryRole);
    };
    addResearchSubFolder(i18n("Characters"), QStringLiteral("research/Characters"), ProjectTreeItem::Characters);
    addResearchSubFolder(i18n("Places"), QStringLiteral("research/Places"), ProjectTreeItem::Places);
    addResearchSubFolder(i18n("Cultures"), QStringLiteral("research/Cultures"), ProjectTreeItem::Cultures);

    // Add README.md to Research
    QString readmePath = QStringLiteral("research/README.md");
    QFile readmeFile(projectDir.absoluteFilePath(readmePath));
    if (readmeFile.open(QIODevice::WriteOnly)) {
        const char* readmeContent = R"markdown(# Welcome to your RPG Forge Project

This README provides a quick overview of how to use RPG Forge to create your masterpiece.

## 🚀 Key Concepts

### 1. The Manuscript
All your story and rule files should live under the **Manuscript** folder. RPG Forge automatically assembles these files into your final document. 
*   **Chapters:** Folders categorized as "Chapter" are automatically numbered during export.
*   **Scenes:** Individual files within chapters that make up your narrative flow.

### 2. Research & Lore
The **Research** folder is for your worldbuilding notes. Files here are *not* compiled into the final manuscript, but they are always available for reference while you write.

### 3. Versions & Explorations
Every save is a checkpoint. Use the **Exploration** menu in the Project Tree to create "branches" where you can try out different narrative directions without breaking your main story.

## ✍️ Writing Tips

*   **Focus Mode:** Press `Ctrl+Shift+F` to hide the UI and focus on your words.
*   **Linking:** Drag a file from the project tree into your document to create a link.
*   **Variables:** Define stats in your document header (YAML) and use them like `{{hp_base}}` in your text.

## 🛠 Compilation

Click the **Compile** button in the toolbar to generate a professional PDF of your manuscript.

## 🤖 AI & Simulation

### 1. AI Writing Assistant
Open the sidebar and select **AI Writing Assistant**. It automatically sees the context of your current document. You can also right-click text in the editor to Expand, Rewrite, or Summarize it.

### 2. Game Analyzer
The **Problems** panel at the bottom continuously checks your rules for conflicts using RAG (Retrieval-Augmented Generation). It "remembers" your entire project!

### 3. Rule Simulation
Go to the **Rule Simulation** panel to test your mechanics. 
*   **Participants:** Drag character files into the list.
*   **Scenario:** Drag a markdown encounter file to set the scene.
*   **Arbiter:** The AI acts as a neutral judge, enforcing your specific rules.
*   **Griot:** The AI narrates the mechanical results into a story.
)markdown";
        readmeFile.write(readmeContent);
        readmeFile.close();
    }
    QModelIndex readmeIdx = model.addFile(i18n("Project Guide"), readmePath, researchIdx);
    model.setData(readmeIdx, ProjectTreeItem::Notes, ProjectTreeModel::CategoryRole);

    // 3. Media
    projectDir.mkpath(QStringLiteral("media"));
    model.addFolder(i18n("Media"), QStringLiteral("media"), rootIdx);

    // 4. Stylesheets
    projectDir.mkpath(QStringLiteral("stylesheets"));
    QModelIndex stylesheetsIdx = model.addFolder(i18n("Stylesheets"), QStringLiteral("stylesheets"), rootIdx);
    model.setData(stylesheetsIdx, ProjectTreeItem::Stylesheet, ProjectTreeModel::CategoryRole);
    
    QString stylePath = QStringLiteral("stylesheets/style.css");
    QFile styleFile(projectDir.absoluteFilePath(stylePath));
    if (styleFile.open(QIODevice::WriteOnly)) {
        styleFile.write(
            "/* RPG Forge Project Stylesheet */\n"
            "body { font-family: serif; line-height: 1.6; }\n"
        );
        styleFile.close();
    }
    model.addFile(QStringLiteral("style.css"), stylePath, stylesheetsIdx);

    // Add .gitignore
    QFile gitignore(projectDir.absoluteFilePath(QStringLiteral(".gitignore")));
    if (gitignore.open(QIODevice::WriteOnly)) {
        gitignore.write(
            "# RPG Forge Git Ignore\n"
            "/*\n"
            "!.gitignore\n"
            "!rpgforge.project\n"
            "!manuscript/\n"
            "!stylesheets/\n"
            "!media/\n"
            "!simulations/\n"
            ".rpgforge-vectors.db*\n"
            "*.db-shm\n"
            "*.db-wal\n"
        );
        gitignore.close();
    }

    // 5. Simulations
    projectDir.mkpath(QStringLiteral("simulations"));
    QModelIndex simIdx = model.addFolder(i18n("Simulations"), QStringLiteral("simulations"), rootIdx);
    
    // Add Sample Encounter
    QString encPath = QStringLiteral("simulations/sample_encounter.md");
    QFile encFile(projectDir.absoluteFilePath(encPath));
    if (encFile.open(QIODevice::WriteOnly)) {
        const char* encContent = R"markdown(# Sample Encounter: The Bridge Ambush

**Starting Situation:**
The party is crossing a narrow stone bridge over a rushing river. Suddenly, three goblins jump out from behind the boulders on the far side.

**Goal:** 
Cross the bridge or defeat the goblins.

**Environment:**
- Narrow Bridge: Difficult terrain.
- Rushing River: DC 15 Athletics check if fallen in.
)markdown";
        encFile.write(encContent);
        encFile.close();
    }
    model.addFile(i18n("Sample Encounter"), encPath, simIdx);

    // Add Sample Actors
    QString valeriusPath = QStringLiteral("research/Characters/Valerius.json");
    QFile valFile(projectDir.absoluteFilePath(valeriusPath));
    if (valFile.open(QIODevice::WriteOnly)) {
        const char* valContent = R"json({
    "name": "Valerius the Bold",
    "stats": {
        "strength": 16,
        "dexterity": 12,
        "constitution": 14,
        "hp": 22,
        "ac": 16
    },
    "equipment": ["Longsword", "Shield", "Chain Mail"]
})json";
        valFile.write(valContent);
        valFile.close();
    }
    
    QString goblinPath = QStringLiteral("research/Characters/Goblin.json");
    QFile gobFile(projectDir.absoluteFilePath(goblinPath));
    if (gobFile.open(QIODevice::WriteOnly)) {
        const char* gobContent = R"json({
    "name": "Goblin Scout",
    "stats": {
        "strength": 8,
        "dexterity": 14,
        "constitution": 10,
        "hp": 7,
        "ac": 13
    },
    "equipment": ["Shortbow", "Scimitar"]
})json";
        gobFile.write(gobContent);
        gobFile.close();
    }

    setTree(model.projectData());
    saveProject();
}

void ProjectManager::closeProject()
{
    m_projectFilePath.clear();
    loadDefaults();
    Q_EMIT projectClosed();
}

bool ProjectManager::saveProject()
{
    if (m_projectFilePath.isEmpty()) return false;

    QFile file(m_projectFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(m_data);
    file.write(doc.toJson());
    return true;
}

QString ProjectManager::stylesheetFolderPath() const
{
    if (!isProjectOpen()) return QString();
    return QDir(projectPath()).absoluteFilePath(QStringLiteral("stylesheets"));
}

QStringList ProjectManager::stylesheetPaths() const
{
    QStringList paths;
    QDir dir(stylesheetFolderPath());
    if (dir.exists()) {
        const auto files = dir.entryList({QStringLiteral("*.css")}, QDir::Files);
        for (const auto &file : files) {
            paths.append(dir.absoluteFilePath(file));
        }
    }
    return paths;
}

int ProjectManager::countWordsInTree(const QJsonObject &tree, const QString &projectPath) const
{
    int count = 0;
    if (tree.value(QStringLiteral("type")).toInt() == 1) { // File
        QString relPath = tree.value(QStringLiteral("path")).toString();
        QString fullPath = QDir(projectPath).absoluteFilePath(relPath);
        QFile f(fullPath);
        if (f.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f.readAll());
            count = VariableManager::stripMetadata(content).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        }
    }
    
    QJsonArray children = tree.value(QStringLiteral("children")).toArray();
    for (const auto &child : children) {
      count += countWordsInTree(child.toObject(), projectPath);
    }
    return count;
}

int ProjectManager::calculateTotalWordCount() const
{
    if (!isProjectOpen()) return 0;
    return countWordsInTree(tree(), projectPath());
}
