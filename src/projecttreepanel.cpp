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
#include "metadatadialog.h"
#include "variablemanager.h"

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

ProjectTreePanel::ProjectTreePanel(QWidget *parent)
    : QWidget(parent)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(100);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        // Use persistent index — automatically invalid after model reset or item removal
        if (m_activeFolderIndex.isValid()) {
            ProjectTreeItem *item = m_model->itemFromIndex(m_activeFolderIndex);
            if (item) Q_EMIT folderActivated(item);
        }
    });

    setupUi();
    
    connect(&ProjectManager::instance(), &ProjectManager::projectOpened, this, &ProjectTreePanel::onProjectOpened);
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed, this, &ProjectTreePanel::onProjectClosed);
    
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

    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_model = new ProjectTreeModel(this);
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
    m_treeView->setDefaultDropAction(Qt::LinkAction);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->hide(); // hidden until project open

    connect(m_treeView, &QTreeView::activated, this, &ProjectTreePanel::onItemActivated);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ProjectTreePanel::onCustomContextMenu);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        Q_UNUSED(deselected);
        if (selected.indexes().isEmpty()) return;
        QModelIndex index = selected.indexes().first();
        ProjectTreeItem *item = m_model->itemFromIndex(index);
        if (item->type == ProjectTreeItem::Folder) {
            m_activeFolderIndex = QPersistentModelIndex(index);
            Q_EMIT folderActivated(item);
        }
    });

    layout->addWidget(m_treeView);
}

void ProjectTreePanel::onProjectOpened()
{
    // Stop any pending refresh and invalidate the stored index before
    // setProjectData() deletes all existing ProjectTreeItem objects.
    m_refreshTimer->stop();
    m_activeFolderIndex = QPersistentModelIndex();
    m_emptyWidget->hide();
    m_treeView->show();
    m_model->setProjectData(ProjectManager::instance().tree());
    m_treeView->expandAll();
    m_addFolderBtn->setEnabled(true);
    m_addFileBtn->setEnabled(true);
}

void ProjectTreePanel::onProjectClosed()
{
    // Same guard: stop timer and clear index before items are destroyed.
    m_refreshTimer->stop();
    m_activeFolderIndex = QPersistentModelIndex();
    m_treeView->hide();
    m_emptyWidget->show();
    m_model->setProjectData(QJsonObject());
    m_addFolderBtn->setEnabled(false);
    m_addFileBtn->setEnabled(false);
}

void ProjectTreePanel::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    ProjectTreeItem *item = m_model->itemFromIndex(index);
    if (item->type == ProjectTreeItem::File) {
        Q_EMIT fileActivated(item->path);
    }
}

void ProjectTreePanel::onCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    QMenu menu(this);

    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), i18n("Add Folder"), this, &ProjectTreePanel::addFolder);
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-new")), i18n("Add File Link"), this, &ProjectTreePanel::addFile);
    
    if (index.isValid()) {
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18n("Project Rename"), this, &ProjectTreePanel::renameItem);
        ProjectTreeItem *item = m_model->itemFromIndex(index);
        if (item->type == ProjectTreeItem::File) {
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
    bool ok;
    QString name = QInputDialog::getText(this, i18n("Add Folder"), i18n("Folder Name:"), QLineEdit::Normal, i18n("New Folder"), &ok);
    if (ok && !name.isEmpty()) {
        m_model->addFolder(name, parent);
    }
}

void ProjectTreePanel::addFile()
{
    QModelIndex parent = m_treeView->currentIndex();
    QString projectDir = ProjectManager::instance().projectPath();
    
    QString filePath = QFileDialog::getOpenFileName(this, i18n("Select File to Link"), projectDir, 
        i18n("Supported Files (*.md *.markdown *.txt *.png *.jpg *.jpeg *.gif *.svg *.pdf);;All Files (*)"));
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    QString relativePath = QDir(projectDir).relativeFilePath(filePath);
    
    m_model->addFile(fi.completeBaseName(), relativePath, parent);
}

void ProjectTreePanel::removeItem()
{
    QModelIndex index = m_treeView->currentIndex();
    if (index.isValid()) {
        m_model->removeItem(index);
        // m_activeFolderIndex (QPersistentModelIndex) is automatically
        // invalidated by Qt if the indexed item was removed or is an ancestor
        // of the removed item.
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
    if (item->type != ProjectTreeItem::File) return;

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
    if (item->type == ProjectTreeItem::Folder) return item;
    return item->parent;
}

void ProjectTreePanel::saveTree()
{
    if (ProjectManager::instance().isProjectOpen()) {
        ProjectManager::instance().setTree(m_model->projectData());
        ProjectManager::instance().saveProject();
        
        requestRefresh();
    }
}

void ProjectTreePanel::requestRefresh()
{
    m_refreshTimer->start();
}
