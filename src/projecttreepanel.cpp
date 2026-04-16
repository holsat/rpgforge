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
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeView->setDefaultDropAction(Qt::MoveAction);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_treeView->hide(); // hidden until project open

    connect(m_treeView, &QTreeView::activated, this, &ProjectTreePanel::onItemActivated);
    connect(m_treeView, &QTreeView::clicked, this, &ProjectTreePanel::onItemActivated);
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
    if (m_isSaving) {
        qDebug() << "ProjectTreePanel: Skipping model reset due to self-save.";
        return;
    }
    
    qDebug() << "ProjectTreePanel: Refreshing authoritative tree view.";

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

    Q_EMIT syncProgress(50, i18n("Aligning hierarchy..."));

    // 4. Hierarchy Alignment (Move files on disk to match logical tree)
    // We do this by calculating the \"target\" path for every file in the tree
    // Target path is based on the names of parent folders.
    
    std::function<QString(ProjectTreeItem*)> getLogicalPath = [&](ProjectTreeItem *item) -> QString {
        if (!item || !item->parent || item->parent == m_model->itemFromIndex(QModelIndex())) {
            return QStringLiteral(".");
        }
        QString parentPath = getLogicalPath(item->parent);
        if (parentPath == QStringLiteral(".")) return item->name;
        return parentPath + QDir::separator() + item->name;
    };

    for (auto *item : allItems) {
        if (item->type == ProjectTreeItem::Folder) {
            item->path = getLogicalPath(item);
            continue;
        }

        QString logicalName = getLogicalPath(item->parent) + QDir::separator() + QFileInfo(item->path).fileName();
        QString targetRelPath = logicalName;
        QString oldAbsPath = QDir(projectPath).absoluteFilePath(item->path);
        QString newAbsPath = QDir(projectPath).absoluteFilePath(targetRelPath);

        if (oldAbsPath != newAbsPath && QFile::exists(oldAbsPath)) {
            QDir().mkpath(QFileInfo(newAbsPath).absolutePath());
            if (QFile::rename(oldAbsPath, newAbsPath)) {
                item->path = targetRelPath;
            }
        }
    }

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

void ProjectTreePanel::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (!item) return;

    if (item->type == ProjectTreeItem::File) {
        Q_EMIT fileActivated(item->path);
    } else if (item->type == ProjectTreeItem::Folder) {
        m_activeFolderIndex = QPersistentModelIndex(index);
        Q_EMIT folderActivated(item->path);
    }
}

void ProjectTreePanel::onCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
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

        // Category Submenu
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
        }
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Project Rename"), this, &ProjectTreePanel::renameItem);
        if (isFile) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("File Rename..."), this, &ProjectTreePanel::renameFile);
        }
        menu.addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Edit Metadata..."), this, &ProjectTreePanel::editMetadata);
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove"), this, &ProjectTreePanel::removeItem);
    }

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
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
    QString parentPath = parentItem ? parentItem->path : QString();

    QString projectDir = ProjectManager::instance().projectPath();
    
    QString filePath = QFileDialog::getOpenFileName(this, i18n("Select File to Link"), projectDir, 
        i18n("Supported Files (*.md *.markdown *.txt *.png *.jpg *.jpeg *.gif *.svg *.pdf);;All Files (*)"));
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    QString relativePath = QDir(projectDir).relativeFilePath(filePath);
    
    ProjectManager::instance().addFile(fi.completeBaseName(), relativePath, parentPath);
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

    QString projectPath = ProjectManager::instance().projectPath();
    QString oldAbsPath = QDir(projectPath).absoluteFilePath(item->path);
    QFileInfo fi(oldAbsPath);

    bool ok = false;
    QString newName = QInputDialog::getText(this, i18n("File Rename"),
        i18n("New file name:"), QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.isEmpty() || newName == fi.fileName()) return;

    QString newAbsPath = fi.dir().filePath(newName);
    if (!QFile::rename(oldAbsPath, newAbsPath)) {
        QMessageBox::warning(this, i18n("File Rename"),
            i18n("Could not rename \"%1\" to \"%2\".", fi.fileName(), newName));
        return;
    }

    // Update the stored relative path
    item->path = QDir(projectPath).relativeFilePath(newAbsPath);
    Q_EMIT m_model->dataChanged(index, index);
    saveTree();
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
