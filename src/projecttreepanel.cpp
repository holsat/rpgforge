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

#include "projecttreepanel.h"
#include "projecttreemodel.h"
#include "projectmanager.h"
#include "agentgatekeeper.h"
#include "metadatadialog.h"
#include "variablemanager.h"
#include "gitservice.h"
#include "githubservice.h"
#include "githubonboardingdialog.h"
#include "historydialog.h"
#include "markdownparser.h"
#include "synopsisservice.h"
#include "lorekeeperservice.h"
#include "librarianservice.h"
#include "mainwindow.h"
#include <QComboBox>
#include <QLabel>

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QToolButton>
#include <QMenu>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QProgressDialog>

ProjectTreePanel::ProjectTreePanel(QWidget *parent)
    : QWidget(parent)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(100);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        // Only update active folder state, don't force a view switch in the background
        if (m_activeFolderIndex.isValid()) {
            ProjectTreeItem *item = m_model->itemFromIndex(m_activeFolderIndex);
            // We don't Q_EMIT folderActivated here because it forces MainWindow to show the corkboard
        }
    });

    setupUi();
    
    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, &ProjectTreePanel::onProjectOpened);
    connect(&ProjectManager::instance(), &ProjectManager::treeChanged, this, &ProjectTreePanel::onProjectOpened);
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, &ProjectTreePanel::onProjectClosed);
    
    connect(&GitHubService::instance(), &GitHubService::repoCreated, this, &ProjectTreePanel::onRepoCreated);

    if (ProjectManager::instance().isProjectOpen()) {
        onProjectOpened();
    }
}

ProjectTreePanel::~ProjectTreePanel() = default;

void ProjectTreePanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Empty state
    m_emptyWidget = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(m_emptyWidget);
    m_createBtn = new QPushButton(i18n("Create New Project..."), this);
    m_createBtn->setIcon(QIcon::fromTheme(QStringLiteral("project-development-new")));
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_createBtn, 0, Qt::AlignCenter);
    emptyLayout->addStretch();
    layout->addWidget(m_emptyWidget);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(2, 0, 2, 0);
    toolbar->setSpacing(2);

    m_addFolderBtn = new QToolButton(this);
    m_addFolderBtn->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
    m_addFolderBtn->setToolTip(i18n("Add Folder"));
    connect(m_addFolderBtn, &QToolButton::clicked, this, &ProjectTreePanel::addFolder);
    toolbar->addWidget(m_addFolderBtn);

    m_addFileBtn = new QToolButton(this);
    m_addFileBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    m_addFileBtn->setToolTip(i18n("Add File Link"));
    connect(m_addFileBtn, &QToolButton::clicked, this, &ProjectTreePanel::addFile);
    toolbar->addWidget(m_addFileBtn);

    toolbar->addSpacing(10);
    m_syncBtn = new QToolButton(this);
    m_syncBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_syncBtn->setToolTip(i18n("Sync Project (Asset Import & Alignment)"));
    connect(m_syncBtn, &QToolButton::clicked, this, &ProjectTreePanel::syncProject);
    toolbar->addWidget(m_syncBtn);

    toolbar->addSpacing(10);
    toolbar->addWidget(new QLabel(i18n("Draft:"), this));
    m_explorationCombo = new QComboBox(this);
    m_explorationCombo->setFixedWidth(120);
    connect(m_explorationCombo, &QComboBox::currentTextChanged, this, &ProjectTreePanel::switchExploration);
    toolbar->addWidget(m_explorationCombo);

    m_newExplorationBtn = new QToolButton(this);
    m_newExplorationBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-branch")));
    m_newExplorationBtn->setToolTip(i18n("New Exploration (Draft Branch)"));
    connect(m_newExplorationBtn, &QToolButton::clicked, this, &ProjectTreePanel::createExploration);
    toolbar->addWidget(m_newExplorationBtn);

    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_model = ProjectManager::instance().model();
    connect(m_model, &ProjectTreeModel::dataChanged, this, &ProjectTreePanel::saveTree);
    connect(m_model, &ProjectTreeModel::rowsInserted, this, &ProjectTreePanel::saveTree);
    connect(m_model, &ProjectTreeModel::rowsRemoved, this, &ProjectTreePanel::saveTree);
    connect(m_model, &ProjectTreeModel::rowsRemoved, this, [this]() {
        // Show the empty state / create button when the tree becomes empty
        if (m_model->rowCount(QModelIndex()) == 0) {
            m_treeView->hide();
            m_emptyWidget->show();
            m_addFolderBtn->setEnabled(false);
            m_addFileBtn->setEnabled(false);
        }
    });
    connect(m_model, &ProjectTreeModel::rowsMoved, this, &ProjectTreePanel::saveTree);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    // InternalMove (NOT DragDrop) is critical: in DragDrop mode, after a
    // successful Move drop Qt's QAbstractItemViewPrivate::clearOrRemove()
    // calls model->removeRows() on the source persistent indexes. Our
    // moveItem() already moved the rows atomically via beginMoveRows(), so
    // the persistent indexes now point at the NEW location — and the
    // auto-remove then destroys the item at its new home. InternalMove
    // tells Qt the model handles the move itself; no auto-remove fires.
    m_treeView->setDragDropMode(QAbstractItemView::InternalMove);
    m_treeView->setDefaultDropAction(Qt::MoveAction);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Rename only on F2 (standard KDE convention). Without this, the default
    // edit triggers include DoubleClicked / SelectedClicked, which swallow
    // the click that should be activating the file and open in-place rename
    // instead — also triggering a setData/dataChanged cycle that races with
    // the background scanners.
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed);

    m_treeView->hide(); // hidden until project open

    // Single-click opens FILES in the editor; folders are selected only.
    // Switching to the corkboard requires an explicit double-click on the
    // folder. Previously single-click on any folder switched the central
    // view to the corkboard, which made navigating the tree destroy the
    // open editor as a side-effect (folderActivated fired on every click).
    connect(m_treeView, &QTreeView::clicked, this, &ProjectTreePanel::onItemClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectTreePanel::onItemActivated);
    // Note: NOT connecting QTreeView::activated. On KDE single-click style
    // it overlaps with `clicked`, causing folder corkboard activation on
    // every single click — exactly the bug we are fixing.
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ProjectTreePanel::onCustomContextMenu);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        Q_UNUSED(deselected);
        if (selected.indexes().isEmpty()) return;
        QModelIndex index = selected.indexes().first();
        ProjectTreeItem *item = m_model->itemFromIndex(index);
        if (item && item->type == ProjectTreeItem::Folder) {
            m_activeFolderIndex = QPersistentModelIndex(index);
            // We no longer emit folderActivated here to prevent background refreshes from hiding the editor.
        }
    });

    layout->addWidget(m_treeView);
}

void ProjectTreePanel::onProjectOpened()
{
    qDebug() << "ProjectTreePanel: Handling project update/opened. Model root children:" << m_model->rowCount(QModelIndex());
    if (m_isSaving) {
        qDebug() << "ProjectTreePanel: Skipping view reset during self-save.";
        return;
    }
    
    qDebug() << "ProjectTreePanel: Refreshing tree view and expanding all.";

    m_refreshTimer->stop();
    m_activeFolderIndex = QPersistentModelIndex();
    m_emptyWidget->hide();
    m_treeView->show();
    
    m_treeView->expandAll();
    m_addFolderBtn->setEnabled(true);
    m_addFileBtn->setEnabled(true);
    m_syncBtn->setEnabled(true);
    m_explorationCombo->setEnabled(true);
    m_newExplorationBtn->setEnabled(true);
    updateExplorationList();
}

void ProjectTreePanel::onProjectClosed()
{
    m_refreshTimer->stop();
    m_activeFolderIndex = QPersistentModelIndex();
    m_treeView->hide();
    m_emptyWidget->show();
    
    m_addFolderBtn->setEnabled(false);
    m_addFileBtn->setEnabled(false);
    m_syncBtn->setEnabled(false);
    m_explorationCombo->clear();
    m_explorationCombo->setEnabled(false);
    m_newExplorationBtn->setEnabled(false);
}

void ProjectTreePanel::syncProject()
{
    if (!ProjectManager::instance().isProjectOpen()) return;
    AgentGatekeeper::instance().pauseAll();

    QString projectPath = ProjectManager::instance().projectPath();
    
    // 1. Initialize Git if not already done
    if (!GitService::instance().isRepo(projectPath)) {
        if (!GitService::instance().initRepo(projectPath)) {
            AgentGatekeeper::instance().resumeAll();
            Q_EMIT syncFinished(false, i18n("Failed to initialize Git repository."));
            return;
        }
    }

    Q_EMIT syncStarted();
    Q_EMIT syncProgress(5, i18n("Preparing to sync..."));

    // 2. Collect all files from the project tree
    auto treeData = m_model->projectData();
    
    struct FileTask {
        QString name;
        QString oldRelPath;
        QString newRelPath;
        ProjectTreeItem::Type type;
    };
    
    QList<ProjectTreeItem*> allItems;
    std::function<void(ProjectTreeItem*)> collectItems = [&](ProjectTreeItem *item) {
        if (item && item != m_model->itemFromIndex(QModelIndex())) {
            allItems.append(item);
        }
        if (item) {
            for (auto *child : item->children) {
                collectItems(child);
            }
        }
    };
    collectItems(m_model->itemFromIndex(QModelIndex()));

    Q_EMIT syncProgress(10, i18n("Scanning files..."));

    // 3. Asset Import (Scan Markdown files for external links)
    MarkdownParser parser;
    QString mediaDir = QDir(projectPath).absoluteFilePath(QStringLiteral("media"));
    QDir().mkpath(mediaDir);

    for (auto *item : allItems) {
        if (item->type != ProjectTreeItem::File) continue;
        
        QString suffix = QFileInfo(item->path).suffix().toLower();
        if (suffix != QLatin1String("md") && suffix != QLatin1String("markdown") && suffix != QLatin1String("txt")) {
            continue;
        }

        QString fullPath = QDir(projectPath).absoluteFilePath(item->path);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadWrite)) continue;

        QString content = QString::fromUtf8(file.readAll());
        auto links = parser.extractLinks(content);
        bool contentChanged = false;

        for (const auto &link : links) {
            // Resolve link relative to the file it's in
            QString linkAbsPath = QDir(QFileInfo(fullPath).absolutePath()).absoluteFilePath(link.url);
            
            // If the file exists and is outside the project directory
            if (QFile::exists(linkAbsPath) && !linkAbsPath.startsWith(projectPath)) {
                QFileInfo fi(linkAbsPath);
                QString newFileName = fi.fileName();
                QString targetPath = QDir(mediaDir).absoluteFilePath(newFileName);
                
                // Copy to media/
                if (QFile::copy(linkAbsPath, targetPath) || QFile::exists(targetPath)) {
                    // Update the link in markdown
                    QString newRelLink = QDir(QFileInfo(fullPath).absolutePath()).relativeFilePath(targetPath);
                    content.replace(link.url, newRelLink);
                    contentChanged = true;
                    
                    // Add the new file to the project tree if not there
                    m_model->addFileWithSmartDiscovery(targetPath, QModelIndex());
                }
            }
        }

        if (contentChanged) {
            file.seek(0);
            file.resize(0);
            file.write(content.toUtf8());
        }
        file.close();
    }

    // Hierarchy alignment (previously step 4) removed: the Phase-6
    // refactor made the filesystem the authoritative source for tree
    // structure, and ProjectManager::moveItem/renameItem keep disk and
    // the live model in sync atomically. There is no longer a "logical
    // tree" that can drift from disk, so no alignment pass is needed.

    Q_EMIT syncProgress(80, i18n("Saving changes..."));

    // 5. Final Save
    saveTree();
    SynopsisService::instance().pause();
    m_model->setProjectData(m_model->projectData()); // Refresh view
    SynopsisService::instance().resume();
    m_treeView->expandAll();

    // Check if there are any uncommitted changes before committing
    // This prevents duplicate \"Initial sync\" commits if Sync is hit twice.
    if (!GitService::instance().hasUncommittedChanges(projectPath)) {
        // No changes, skip commit and go straight to push
        Q_EMIT syncProgress(90, i18n("Pushing to GitHub..."));
        auto pushResult = GitService::instance().push(projectPath);
        pushResult.then(this, [this, projectPath](bool success) {
            AgentGatekeeper::instance().resumeAll();
            if (success) {
                Q_EMIT syncFinished(true, i18n("Project synchronized and pushed to GitHub."));
            } else {
                Q_EMIT syncFinished(true, i18n("Project synchronized locally."));
                if (QMessageBox::question(this, i18n("Connect to GitHub"), 
                    i18n("Project is synchronized locally. Would you like to also back it up to GitHub?")) == QMessageBox::Yes) {
                    GitHubOnboardingDialog dialog(this);
                    dialog.exec();
                }
            }
        });
        return;
    }
    
    // Perform an initial commit of everything
    Q_EMIT syncProgress(85, i18n("Committing changes..."));
    auto commitFuture = GitService::instance().commitAll(projectPath, i18n("Initial sync and project organization"));
    
    commitFuture.then(this, [this, projectPath](bool success) {
        if (!success) {
            Q_EMIT syncProgress(90, i18n("Commit failed, attempting push anyway..."));
        }

        // 6. GitHub Remote & Push
        Q_EMIT syncProgress(95, i18n("Pushing to GitHub..."));
        auto pushResult = GitService::instance().push(projectPath);
        pushResult.then(this, [this, projectPath](bool success) {
            AgentGatekeeper::instance().resumeAll();
            if (success) {
                Q_EMIT syncFinished(true, i18n("Project synchronized and pushed to GitHub."));
            } else {
                // No remote or push failed. Ask to connect.
                Q_EMIT syncFinished(true, i18n("Project synchronized locally."));
                if (QMessageBox::question(this, i18n("Connect to GitHub"), 
                    i18n("Project is synchronized locally. Would you like to also back it up to GitHub?")) == QMessageBox::Yes) {
                    
                    GitHubOnboardingDialog dialog(this);
                    if (dialog.exec() == QDialog::Accepted) {
                        // onRepoCreated will handle the push
                    }
                }
            }
        });
    });
}

void ProjectTreePanel::onRepoCreated(const QString &cloneUrl)
{
    if (!ProjectManager::instance().isProjectOpen()) return;
    AgentGatekeeper::instance().pauseAll();
    QString projectPath = ProjectManager::instance().projectPath();

    if (GitService::instance().setRemote(projectPath, cloneUrl)) {
        Q_EMIT syncStarted();
        Q_EMIT syncProgress(95, i18n("Pushing to GitHub..."));
        GitService::instance().push(projectPath).then(this, [this](bool success) {
            if (success) {
                Q_EMIT syncFinished(true, i18n("Project pushed to GitHub."));
            } else {
                Q_EMIT syncFinished(false, i18n("Failed to push to GitHub."));
            }
        });
    }
}

void ProjectTreePanel::createExploration()
{
    if (!ProjectManager::instance().isProjectOpen()) return;
    QString projectPath = ProjectManager::instance().projectPath();

    bool ok;
    QString name = QInputDialog::getText(this, i18n("New Exploration"),
        i18n("Exploration (Branch) Name:"), QLineEdit::Normal, QString(), &ok);
    
    if (ok && !name.isEmpty()) {
        if (GitService::instance().createBranch(projectPath, name)) {
            updateExplorationList();
            // Re-open project to refresh from disk? 
            // In hidden Git, we should ensure project state is saved.
            ProjectManager::instance().saveProject();
            // Trigger a refresh of the tree view
            SynopsisService::instance().pause();
            m_model->setProjectData(ProjectManager::instance().model()->projectData());
            SynopsisService::instance().resume();
            m_treeView->expandAll();
        } else {
            QMessageBox::critical(this, i18n("Error"), i18n("Failed to create exploration branch."));
        }
    }
}

void ProjectTreePanel::switchExploration(const QString &name)
{
    if (name.isEmpty() || !ProjectManager::instance().isProjectOpen()) return;
    QString projectPath = ProjectManager::instance().projectPath();
    QString current = GitService::instance().currentBranch(projectPath);

    if (name == current) return;

    // Check for uncommitted changes
    if (GitService::instance().hasUncommittedChanges(projectPath)) {
        auto result = QMessageBox::question(this, i18n("Save & Switch Exploration"),
            i18n("You have unsaved changes in your current exploration (%1). "
                 "They will be automatically saved before switching to %2. "
                 "\n\nDo you want to proceed?").arg(current, name),
            QMessageBox::Yes | QMessageBox::No);
        
        if (result != QMessageBox::Yes) {
            updateExplorationList(); // Reset combo to current branch
            return;
        }

        // Automatic commit
        GitService::instance().commitAll(projectPath, i18n("Automatic save before switching to %1", name));
    }

    AgentGatekeeper::instance().pauseAll();
    if (GitService::instance().checkoutBranch(projectPath, name)) {
        // Refresh project data from disk (this also syncs the authoritative model)
        ProjectManager::instance().openProject(ProjectManager::instance().projectFilePath());
        
        SynopsisService::instance().pause();
        m_model->setProjectData(ProjectManager::instance().model()->projectData());
        SynopsisService::instance().resume();
        m_treeView->expandAll();
        AgentGatekeeper::instance().resumeAll();
    } else {
        AgentGatekeeper::instance().resumeAll();
        QMessageBox::critical(this, i18n("Error"), i18n("Failed to switch to exploration branch."));
        updateExplorationList();
    }
}

void ProjectTreePanel::updateExplorationList()
{
    if (!ProjectManager::instance().isProjectOpen()) return;
    QString projectPath = ProjectManager::instance().projectPath();

    m_explorationCombo->blockSignals(true);
    m_explorationCombo->clear();
    
    QStringList branches = GitService::instance().listBranches(projectPath);
    QString current = GitService::instance().currentBranch(projectPath);
    
    m_explorationCombo->addItems(branches);
    m_explorationCombo->setCurrentText(current);
    m_explorationCombo->blockSignals(false);
}

// Thin wrapper around tryResolveOnDisk() shared with ProjectManager::validateTree.
// Returns the on-disk relative path a leaf Folder-typed item should really be
// pointing at, or an empty string if none can be found. Compensates for
// legacy / Scrivener-imported tree entries where documents were recorded as
// extensionless Folder nodes.
static QString resolveFolderItemAsFile(const ProjectTreeItem *item)
{
    if (!item) return QString();
    if (item->path.isEmpty()) return QString();
    if (!item->children.isEmpty()) return QString();
    if (!ProjectManager::instance().isProjectOpen()) return QString();

    const QDir projectDir(ProjectManager::instance().projectPath());
    return tryResolveOnDisk(projectDir, item->path);
}

void ProjectTreePanel::onItemClicked(const QModelIndex &index)
{
    // Single-click handler: opens FILES. Real folders are just selected so
    // navigating the tree does not destroy the editor's open document.
    qDebug() << "ProjectTreePanel::onItemClicked indexValid=" << index.isValid()
             << "row=" << (index.isValid() ? index.row() : -1);
    if (!index.isValid()) return;
    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (!item) {
        qDebug() << "ProjectTreePanel::onItemClicked itemFromIndex returned null — model rejected the index (dangling pointer guard)";
        return;
    }
    qDebug() << "ProjectTreePanel::onItemClicked name=" << item->name
             << "type=" << static_cast<int>(item->type)
             << "(0=Folder 1=File)"
             << "path=" << item->path
             << "category=" << static_cast<int>(item->category);

    if (item->type == ProjectTreeItem::File) {
        qDebug() << "ProjectTreePanel::onItemClicked emitting fileActivated for" << item->path;
        Q_EMIT fileActivated(item->path);
        return;
    }

    if (item->type == ProjectTreeItem::Folder) {
        // Compatibility fallback: legacy / Scrivener-imported items can be
        // typed as Folder while really being documents on disk. Detect that
        // case and open the file. validateTree() heals these on next load
        // but we want clicks to work in the current session too.
        const QString resolved = resolveFolderItemAsFile(item);
        if (!resolved.isEmpty()) {
            qDebug() << "ProjectTreePanel::onItemClicked Folder-typed item resolves to file"
                     << resolved << "— treating as file open";
            Q_EMIT fileActivated(resolved);
            return;
        }

        // Real folder — just select it (do not switch the central view).
        qDebug() << "ProjectTreePanel::onItemClicked folder selected (no view switch)";
        m_activeFolderIndex = QPersistentModelIndex(index);
        return;
    }

    qDebug() << "ProjectTreePanel::onItemClicked item type is neither File nor Folder — ignored";
}

void ProjectTreePanel::onItemActivated(const QModelIndex &index)
{
    // Activation handler (double-click or Enter): files open in the editor,
    // folders open the corkboard. Single-click no longer routes here — see
    // the connection block in setupUi().
    if (!index.isValid()) return;
    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (!item) return;

    if (item->type == ProjectTreeItem::File) {
        Q_EMIT fileActivated(item->path);
        return;
    }

    if (item->type == ProjectTreeItem::Folder) {
        // Same compatibility fallback as in onItemClicked: a "Folder" leaf
        // that maps to a file on disk should open as a file, not toggle the
        // corkboard. Otherwise double-clicking a legacy document entry would
        // surprise the user with an empty corkboard.
        const QString resolved = resolveFolderItemAsFile(item);
        if (!resolved.isEmpty()) {
            Q_EMIT fileActivated(resolved);
            return;
        }
        m_activeFolderIndex = QPersistentModelIndex(index);
        Q_EMIT folderActivated(item->path);
    }
}

void ProjectTreePanel::onCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    // Keep the view's current index in sync with what was right-clicked so
    // menu handlers (Add File Link, Add Folder, etc.) read the right parent
    // via currentIndex(). Without this they operate on the previous
    // selection, causing "added nothing" surprises.
    if (index.isValid()) {
        m_treeView->setCurrentIndex(index);
    }
    QMenu menu(this);

    auto selected = m_treeView->selectionModel()->selectedRows();
    if (selected.count() == 2) {
        ProjectTreeItem *item1 = m_model->itemFromIndex(selected.at(0));
        ProjectTreeItem *item2 = m_model->itemFromIndex(selected.at(1));
        
        if (item1 && item2 && item1->type == ProjectTreeItem::File && item2->type == ProjectTreeItem::File) {
            QString path1 = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(item1->path);
            QString path2 = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(item2->path);
            
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-compare")), i18n("Compare Selected Files"), this, [this, path1, path2]() {
                Q_EMIT diffRequested(path1, path2);
            });
            menu.addSeparator();
        }
    }

    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), i18n("Add Folder"), this, &ProjectTreePanel::addFolder);
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-new")), i18n("Add File Link"), this, &ProjectTreePanel::addFile);
    
    if (index.isValid()) {
        menu.addSeparator();
        ProjectTreeItem *item = m_model->itemFromIndex(index);
        if (!item) return;

        QString itemPath = item->path;
        bool isFile = (item->type == ProjectTreeItem::File);

        // Category submenu is only offered for items that the user is allowed
        // to recategorize. The three authoritative top-level folders
        // (manuscript/, lorekeeper/, research/) have their category derived
        // from path and cannot be overridden.
        if (!m_model->isAuthoritativeRoot(item)) {
            auto *categoryMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("tag")), i18n("Set Category"));

            auto addCategoryAction = [&](const QString &text, const QString &icon, ProjectTreeItem::Category cat) {
                auto *action = categoryMenu->addAction(QIcon::fromTheme(icon), text);
                action->setCheckable(true);
                // We resolve the item in the lambda to be safe
                connect(action, &QAction::triggered, this, [this, itemPath, cat]() {
                    ProjectTreeItem *resolved = m_model->findItem(itemPath);
                    if (resolved) {
                        resolved->category = cat;
                        QModelIndex idx = m_model->indexForItem(resolved);
                        if (idx.isValid()) m_model->dataChanged(idx, idx, {Qt::DecorationRole});

                        // Authoritative save
                        ProjectManager::instance().saveProject();
                    }
                });
                // Initial check state
                ProjectTreeItem *resolved = m_model->findItem(itemPath);
                if (resolved) action->setChecked(resolved->category == cat);
            };

            addCategoryAction(i18n("None"), QStringLiteral("folder"), ProjectTreeItem::None);
            addCategoryAction(i18n("Manuscript"), QStringLiteral("document-edit"), ProjectTreeItem::Manuscript);
            addCategoryAction(i18n("Research"), QStringLiteral("search"), ProjectTreeItem::Research);
            addCategoryAction(i18n("Chapter"), QStringLiteral("book-contents"), ProjectTreeItem::Chapter);
            addCategoryAction(i18n("Scene"), QStringLiteral("document-edit-symbolic"), ProjectTreeItem::Scene);
            addCategoryAction(i18n("Characters"), QStringLiteral("user-identity"), ProjectTreeItem::Characters);
            addCategoryAction(i18n("Places"), QStringLiteral("applications-graphics"), ProjectTreeItem::Places);
            addCategoryAction(i18n("Cultures"), QStringLiteral("view-list-details"), ProjectTreeItem::Cultures);
            addCategoryAction(i18n("Stylesheet"), QStringLiteral("applications-graphics-symbolic"), ProjectTreeItem::Stylesheet);
            addCategoryAction(i18n("Notes"), QStringLiteral("note-sticky"), ProjectTreeItem::Notes);
        }

        menu.addSeparator();
        if (isFile) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-history")), i18n("View History..."), this, [this, itemPath]() {
                ProjectTreeItem *resolved = m_model->findItem(itemPath);
                if (!resolved) return;

                QString projectPath = ProjectManager::instance().projectPath();
                QString fullPath = QDir(projectPath).absoluteFilePath(resolved->path);
                
                auto *dialog = new HistoryDialog(fullPath, this);
                connect(dialog, &HistoryDialog::viewVersion, this, [this, fullPath, itemPath](const QString &hash, int vIndex, const QDateTime &date, const QStringList &tags) {
                    QString tempDir = QDir::homePath() + QStringLiteral("/.rpgforge/.versions/");
                    QString tempPath = tempDir + hash + QStringLiteral("_") + QFileInfo(fullPath).fileName();
                    
                    auto future = GitService::instance().extractVersion(fullPath, hash, tempPath);
                    future.then(this, [this, tempPath, itemPath, vIndex, date, tags](bool success) {
                        if (success) {
                            ProjectTreeItem *res = m_model->findItem(itemPath);
                            if (res) {
                                QModelIndex resIdx = m_model->indexForItem(res);
                                QString label = QStringLiteral("v%1 (%2)").arg(QString::number(vIndex), date.toString(Qt::ISODate));
                                if (!tags.isEmpty()) label += QStringLiteral(" [%1]").arg(tags.join(QLatin1Char(',')));
                                
                                m_model->addTransientVersionLink(label, tempPath, resIdx);
                                m_treeView->expand(resIdx);
                                
                                // Persist the change via authoritative model save
                                ProjectManager::instance().saveProject();

                                Q_EMIT fileActivated(tempPath);
                            }
                        }
                    });
                });
                connect(dialog, &HistoryDialog::restoreVersion, this, [this, fullPath](const QString &hash) {
                    auto future = GitService::instance().extractVersion(fullPath, hash, fullPath);
                    future.then(this, [this, fullPath](bool success) {
                        if (success) {
                            Q_EMIT fileActivated(fullPath);
                        }
                    });
                });
                connect(dialog, &HistoryDialog::compareVersion, this, [this, fullPath](const QString &hash) {
                    Q_EMIT diffRequested(fullPath, hash);
                });
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();
            });

            QString fullPathForRecall = QDir(ProjectManager::instance().projectPath()).absoluteFilePath(itemPath);
            QAction *recallAct = menu.addAction(
                QIcon::fromTheme(QStringLiteral("document-revert")),
                i18n("Recall Version..."));
            connect(recallAct, &QAction::triggered, this, [fullPathForRecall, this] {
                Q_EMIT recallVersionRequested(fullPathForRecall);
            });
        }
        menu.addSeparator();
        // Authoritative top-level folders cannot be renamed or removed: the
        // model rejects those operations, and offering them in the menu
        // would just present the user with actions that silently no-op.
        const bool isAuthoritative = m_model->isAuthoritativeRoot(item);
        if (!isAuthoritative) {
            const QString renameLabel = isFile ? i18n("Rename...") : i18n("Rename Folder...");
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), renameLabel, this, &ProjectTreePanel::renameItem);
            if (isFile) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Rename File..."), this, &ProjectTreePanel::renameFile);
            }
        }
        menu.addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Edit Metadata..."), this, &ProjectTreePanel::editMetadata);

        // "Rescan Document" — for files only, and only .md/.markdown
        // where it makes sense. Runs the same heuristic + LLM-discovery
        // pass the Variables-panel "Re-index" button triggers, but
        // scoped to this single document. Useful after editing a file
        // to update its extracted variables without waiting for the
        // full-project rescan timer.
        if (isFile) {
            const QString suffix = QFileInfo(itemPath).suffix().toLower();
            if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                               i18n("Rescan Document"), this, [this, itemPath]() {
                    rescanSingleDocument(itemPath);
                });
            }
        }

        if (!isAuthoritative) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove"), this, &ProjectTreePanel::removeItem);
        }
    }

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void ProjectTreePanel::rescanSingleDocument(const QString &relativePath)
{
    if (relativePath.isEmpty() || !ProjectManager::instance().isProjectOpen()) return;

    const QString abs = QDir(ProjectManager::instance().projectPath())
                           .absoluteFilePath(relativePath);

    // Two-pass rescan:
    //  1. LoreKeeperService::indexDocument — triggers LLM entity discovery
    //     + dossier regeneration for whatever entities the file mentions.
    //  2. LibrarianService::scanFile — re-runs the table/list variable
    //     extractor on this file, replacing the prior extracted entries.
    LoreKeeperService::instance().indexDocument(abs);

    // LibrarianService's scanFile expects the absolute path (matches
    // its scanAll output and its fs watcher behaviour).
    if (auto *window = qobject_cast<MainWindow*>(this->window())) {
        if (auto *lib = window->librarianService()) {
            lib->scanFile(abs);
        }
    }

    // Also request a synopsis refresh for the file. SynopsisService
    // expects a project-relative path.
    SynopsisService::instance().requestUpdate(relativePath, /*force=*/true);

    qInfo().noquote() << "ProjectTreePanel: triggered rescan of" << abs;
}

void ProjectTreePanel::addFolder()
{
    QModelIndex parent = m_treeView->currentIndex();
    ProjectTreeItem *parentItem = m_model->itemFromIndex(parent);
    QString parentPath = parentItem ? parentItem->path : QString();

    bool ok;
    QString name = QInputDialog::getText(this, i18n("Add Folder"), i18n("Folder Name:"), QLineEdit::Normal, i18n("New Folder"), &ok);
    if (ok && !name.isEmpty()) {
        ProjectManager::instance().addFolder(name, QString(), parentPath);
    }
}

void ProjectTreePanel::addFile()
{
    QModelIndex parent = m_treeView->currentIndex();
    ProjectTreeItem *parentItem = m_model->itemFromIndex(parent);
    // If the right-click landed on a file, use its containing folder instead.
    if (parentItem && parentItem->type == ProjectTreeItem::File) {
        parentItem = parentItem->parent;
    }
    QString parentPath = parentItem ? parentItem->path : QString();

    const QString projectDir = ProjectManager::instance().projectPath();
    const QString picked = QFileDialog::getOpenFileName(this, i18n("Select File to Link"), projectDir,
        i18n("Supported Files (*.md *.markdown *.txt *.png *.jpg *.jpeg *.gif *.svg *.pdf);;All Files (*)"));
    if (picked.isEmpty()) return;

    const QFileInfo pickedInfo(picked);
    const QString pickedAbs = pickedInfo.absoluteFilePath();
    const bool insideProject = pickedAbs == projectDir
        || pickedAbs.startsWith(projectDir + QDir::separator());

    QString finalRelPath;
    if (insideProject) {
        finalRelPath = QDir(projectDir).relativeFilePath(pickedAbs);
    } else {
        // External file: copy into the clicked folder (or project root) with
        // a sanitized filename — spaces become underscores, literal quotes
        // stripped. Links to files outside the project break the preview's
        // image resolver and the drag-drop path rewriter.
        const QString parentAbs = parentPath.isEmpty()
            ? projectDir
            : QDir(projectDir).absoluteFilePath(parentPath);
        if (!QDir().mkpath(parentAbs)) {
            QMessageBox::warning(this, i18n("Add File Link"),
                i18n("Could not access the destination folder."));
            return;
        }

        QString sanitized = pickedInfo.fileName();
        sanitized.replace(QLatin1Char(' '), QLatin1Char('_'));
        sanitized.remove(QLatin1Char('\''));
        sanitized.remove(QLatin1Char('"'));
        QString destAbs = QDir(parentAbs).absoluteFilePath(sanitized);

        // Avoid overwriting: if the target exists, append _2, _3, ...
        if (QFileInfo::exists(destAbs)) {
            const QFileInfo sfi(sanitized);
            const QString base = sfi.completeBaseName();
            const QString suffix = sfi.suffix();
            for (int n = 2; n < 1000; ++n) {
                const QString candidate = suffix.isEmpty()
                    ? QStringLiteral("%1_%2").arg(base).arg(n)
                    : QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(suffix);
                destAbs = QDir(parentAbs).absoluteFilePath(candidate);
                if (!QFileInfo::exists(destAbs)) break;
            }
        }

        if (!QFile::copy(pickedAbs, destAbs)) {
            QMessageBox::warning(this, i18n("Add File Link"),
                i18n("Failed to copy \"%1\" into the project.", pickedInfo.fileName()));
            return;
        }
        finalRelPath = QDir(projectDir).relativeFilePath(destAbs);
    }

    // Keep the original basename (with spaces, if any) as the display name
    // so the tree reads nicely even when the on-disk filename was sanitized.
    ProjectManager::instance().addFile(pickedInfo.completeBaseName(), finalRelPath, parentPath);
}

void ProjectTreePanel::removeItem()
{
    QModelIndex index = m_treeView->currentIndex();
    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (item) {
        ProjectManager::instance().removeItem(item->path);
    }
}

void ProjectTreePanel::renameItem()
{
    QModelIndex index = m_treeView->currentIndex();
    if (index.isValid()) {
        m_treeView->edit(index);
    }
}

void ProjectTreePanel::renameFile()
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return;

    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (!item || item->type != ProjectTreeItem::File) return;

    const QString oldBasename = QFileInfo(item->path).fileName();

    bool ok = false;
    QString newFileName = QInputDialog::getText(this, i18n("File Rename"),
        i18n("New file name:"), QLineEdit::Normal, oldBasename, &ok);
    if (!ok || newFileName.isEmpty() || newFileName == oldBasename) return;

    // Route through PM::renameItem so disk + tree name + path cascade
    // happen atomically and the display name follows the new filename.
    const QString newDisplayName = QFileInfo(newFileName).completeBaseName();
    if (!ProjectManager::instance().renameItem(item->path, newDisplayName)) {
        QMessageBox::warning(this, i18n("File Rename"),
            i18n("Could not rename \"%1\" to \"%2\".", oldBasename, newFileName));
    }
}

void ProjectTreePanel::editMetadata()
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return;

    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (!item) return;
    
    // If it's a file, we can also update the metadata in the markdown file
    QString projectPath = ProjectManager::instance().projectPath();
    QString fullPath;
    if (item->type == ProjectTreeItem::File) {
        fullPath = QDir(projectPath).absoluteFilePath(item->path);
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            auto meta = VariableManager::extractMetadata(content);
            // Sync with file if not empty
            if (!meta.title.isEmpty()) item->name = meta.title;
            if (!meta.status.isEmpty()) item->status = meta.status;
            if (!meta.synopsis.isEmpty()) item->synopsis = meta.synopsis;
            file.close();
        }
    }

    MetadataDialog dialog(item->name, item->status, item->synopsis, this);
    if (dialog.exec() == QDialog::Accepted) {
        item->name = dialog.title();
        item->status = dialog.status();
        item->synopsis = dialog.synopsis();
        
        if (item->type == ProjectTreeItem::File && !fullPath.isEmpty()) {
            QFile file(fullPath);
            QString content;
            if (file.open(QIODevice::ReadOnly)) {
                content = QString::fromUtf8(file.readAll());
                file.close();
            }

            // Construct new front matter
            QString newFrontMatter = QStringLiteral("---\n");
            newFrontMatter += QStringLiteral("title: %1\n").arg(item->name);
            newFrontMatter += QStringLiteral("status: %1\n").arg(item->status);
            newFrontMatter += QStringLiteral("synopsis: %1\n").arg(item->synopsis);
            newFrontMatter += QStringLiteral("---\n\n");

            // Strip old metadata
            QString stripped = VariableManager::stripMetadata(content);
            QString newContent = newFrontMatter + stripped;

            if (file.open(QIODevice::WriteOnly)) {
                file.write(newContent.toUtf8());
                file.close();
            }
        }
        
        saveTree();
        // Force refresh of the display
        Q_EMIT m_model->dataChanged(index, index);
    }
}

QModelIndex ProjectTreePanel::currentIndex() const
{
    return m_treeView->currentIndex();
}

ProjectTreeItem* ProjectTreePanel::activeFolder() const
{
    if (!m_activeFolderIndex.isValid()) return nullptr;
    return m_model->itemFromIndex(m_activeFolderIndex);
}

ProjectTreeItem* ProjectTreePanel::currentFolder() const
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) return m_model->itemFromIndex(QModelIndex());

    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (item && item->type == ProjectTreeItem::Folder) return item;
    if (item) return item->parent;
    return m_model->itemFromIndex(QModelIndex());
}

void ProjectTreePanel::saveTree()
{
    if (m_isSaving) return;
    if (ProjectManager::instance().isProjectOpen()) {
        AgentGatekeeper::instance().pauseAll();
        m_isSaving = true;
        // The ProjectManager already owns the model, so we just need to tell it to save its current state.
        ProjectManager::instance().saveProject();
        m_isSaving = false;
        AgentGatekeeper::instance().resumeAll();
        
        requestRefresh();
    }
}

void ProjectTreePanel::requestRefresh()
{
    m_refreshTimer->start();
}
