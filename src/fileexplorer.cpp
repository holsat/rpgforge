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

#include "fileexplorer.h"
#include "gitstatusmodel.h"

#include <KLocalizedString>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

// Custom delegate that draws file-type icons and git status badges
class FileExplorerDelegate : public QStyledItemDelegate
{
public:
    FileExplorerDelegate(QFileSystemModel *model, GitStatusModel *git, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_model(model), m_git(git)
    {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Only decorate the name column (column 0)
        if (index.column() != 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // Replace icon with file-type-specific icon
        QString path = m_model->filePath(index);
        QFileInfo fi(path);
        if (!fi.isDir()) {
            QString suffix = fi.suffix().toLower();
            if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown") ||
                suffix == QLatin1String("mkd")) {
                opt.icon = QIcon::fromTheme(QStringLiteral("text-x-markdown"),
                           QIcon::fromTheme(QStringLiteral("text-plain")));
            } else if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
                       suffix == QLatin1String("jpeg") || suffix == QLatin1String("gif") ||
                       suffix == QLatin1String("svg") || suffix == QLatin1String("webp") ||
                       suffix == QLatin1String("bmp")) {
                opt.icon = QIcon::fromTheme(QStringLiteral("image-x-generic"));
            } else if (suffix == QLatin1String("pdf")) {
                opt.icon = QIcon::fromTheme(QStringLiteral("application-pdf"));
            } else if (suffix == QLatin1String("yaml") || suffix == QLatin1String("yml")) {
                opt.icon = QIcon::fromTheme(QStringLiteral("text-x-yaml"),
                           QIcon::fromTheme(QStringLiteral("text-plain")));
            } else if (suffix == QLatin1String("json")) {
                opt.icon = QIcon::fromTheme(QStringLiteral("text-x-json"),
                           QIcon::fromTheme(QStringLiteral("application-json")));
            }
        }

        // Draw the base item
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        // Draw git status badge on the right side
        if (m_git && m_git->isGitRepo()) {
            auto status = m_git->statusForFile(path);
            QString badge = m_git->badgeForStatus(status);
            if (!badge.isEmpty()) {
                QColor color = m_git->colorForStatus(status);
                QFont font = painter->font();
                font.setBold(true);
                font.setPointSize(font.pointSize() - 1);
                painter->save();
                painter->setFont(font);
                painter->setPen(color);
                QRect badgeRect = option.rect;
                badgeRect.setLeft(badgeRect.right() - 20);
                painter->drawText(badgeRect, Qt::AlignCenter, badge);
                painter->restore();
            }
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        // Add space for git badge
        if (index.column() == 0) {
            size.setWidth(size.width() + 24);
        }
        return size;
    }

private:
    QFileSystemModel *m_model;
    GitStatusModel *m_git;
};


FileExplorer::FileExplorer(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

FileExplorer::~FileExplorer() = default;

void FileExplorer::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Toolbar row
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(2, 0, 2, 0);
    toolbar->setSpacing(2);

    auto *openBtn = new QToolButton(this);
    openBtn->setText(i18n("Open Folder..."));
    openBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openBtn->setIcon(QIcon::fromTheme(QStringLiteral("folder-open")));
    connect(openBtn, &QToolButton::clicked, this, &FileExplorer::openFolder);
    toolbar->addWidget(openBtn);

    m_hiddenBtn = new QToolButton(this);
    m_hiddenBtn->setCheckable(true);
    m_hiddenBtn->setChecked(m_showHidden);
    m_hiddenBtn->setToolTip(i18n("Show/Hide Hidden Files"));
    m_hiddenBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-hidden"),
                         QIcon::fromTheme(QStringLiteral("visibility"))));
    connect(m_hiddenBtn, &QToolButton::toggled, this, &FileExplorer::toggleHiddenFiles);
    toolbar->addWidget(m_hiddenBtn);
    toolbar->addStretch();

    layout->addLayout(toolbar);

    // File system model
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(false); // allow drag-and-drop moves and renames
    m_model->setNameFilterDisables(false);
    applyFilter();

    // Git status
    m_gitStatus = new GitStatusModel(this);
    connect(m_gitStatus, &GitStatusModel::statusChanged, this, &FileExplorer::onGitStatusChanged);

    // Tree view
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setItemDelegate(new FileExplorerDelegate(m_model, m_gitStatus, this));

    // Only show the Name column
    m_treeView->hideColumn(1); // Size
    m_treeView->hideColumn(2); // Type
    m_treeView->hideColumn(3); // Date Modified

    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);

    // Drag and drop for moving files
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    m_treeView->setDefaultDropAction(Qt::LinkAction);

    // Context menu
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &FileExplorer::onCustomContextMenu);

    // Double-click to open
    connect(m_treeView, &QTreeView::activated, this, &FileExplorer::onItemActivated);

    layout->addWidget(m_treeView);

    // Default to home directory until a project is opened
    setRootPath(QDir::homePath());
}

void FileExplorer::setRootPath(const QString &path)
{
    QModelIndex rootIndex = m_model->setRootPath(path);
    m_treeView->setRootIndex(rootIndex);
    m_gitStatus->setRootPath(path);
}

QString FileExplorer::rootPath() const
{
    return m_model->rootPath();
}

void FileExplorer::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    QString path = m_model->filePath(index);
    QFileInfo fi(path);
    if (fi.isFile()) {
        Q_EMIT fileActivated(QUrl::fromLocalFile(path));
    }
}

void FileExplorer::onCustomContextMenu(const QPoint &pos)
{
    m_contextMenuIndex = m_treeView->indexAt(pos);

    QMenu menu(this);

    menu.addAction(QIcon::fromTheme(QStringLiteral("document-new")),
                   i18n("New File..."), this, &FileExplorer::newFile);
    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                   i18n("New Folder..."), this, &FileExplorer::newFolder);

    if (m_contextMenuIndex.isValid()) {
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")),
                       i18n("Rename..."), this, &FileExplorer::renameItem);
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                       i18n("Delete"), this, &FileExplorer::deleteItem);
    }

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void FileExplorer::renameItem()
{
    if (!m_contextMenuIndex.isValid()) return;

    QString oldName = m_model->fileName(m_contextMenuIndex);
    bool ok = false;
    QString newName = QInputDialog::getText(this, i18n("Rename"),
        i18n("New name:"), QLineEdit::Normal, oldName, &ok);

    if (ok && !newName.isEmpty() && newName != oldName) {
        QModelIndex parentIndex = m_contextMenuIndex.parent();
        QString parentPath = m_model->filePath(parentIndex);
        QString oldPath = m_model->filePath(m_contextMenuIndex);
        QString newPath = QDir(parentPath).filePath(newName);

        QFile::rename(oldPath, newPath);
        m_gitStatus->refresh();
    }
}

void FileExplorer::deleteItem()
{
    if (!m_contextMenuIndex.isValid()) return;

    QString path = m_model->filePath(m_contextMenuIndex);
    QString name = m_model->fileName(m_contextMenuIndex);

    auto result = QMessageBox::question(this, i18n("Delete"),
        i18n("Are you sure you want to delete \"%1\"?", name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result == QMessageBox::Yes) {
        QFileInfo fi(path);
        if (fi.isDir()) {
            QDir(path).removeRecursively();
        } else {
            QFile::remove(path);
        }
        m_gitStatus->refresh();
    }
}

void FileExplorer::newFile()
{
    // Determine parent directory
    QString parentPath;
    if (m_contextMenuIndex.isValid()) {
        QString path = m_model->filePath(m_contextMenuIndex);
        QFileInfo fi(path);
        parentPath = fi.isDir() ? path : fi.absolutePath();
    } else {
        parentPath = m_model->rootPath();
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, i18n("New File"),
        i18n("File name:"), QLineEdit::Normal, QStringLiteral("untitled.md"), &ok);

    if (ok && !name.isEmpty()) {
        QString filePath = QDir(parentPath).filePath(name);
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            m_gitStatus->refresh();
            // Open the newly created file
            Q_EMIT fileActivated(QUrl::fromLocalFile(filePath));
        }
    }
}

void FileExplorer::newFolder()
{
    QString parentPath;
    if (m_contextMenuIndex.isValid()) {
        QString path = m_model->filePath(m_contextMenuIndex);
        QFileInfo fi(path);
        parentPath = fi.isDir() ? path : fi.absolutePath();
    } else {
        parentPath = m_model->rootPath();
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, i18n("New Folder"),
        i18n("Folder name:"), QLineEdit::Normal, QString(), &ok);

    if (ok && !name.isEmpty()) {
        QDir(parentPath).mkdir(name);
        m_gitStatus->refresh();
    }
}

void FileExplorer::openFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, i18n("Open Folder"),
        m_model->rootPath(), QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        setRootPath(dir);
    }
}

void FileExplorer::toggleHiddenFiles()
{
    m_showHidden = m_hiddenBtn->isChecked();
    applyFilter();
}

void FileExplorer::setShowHiddenFiles(bool show)
{
    m_showHidden = show;
    if (m_hiddenBtn) {
        m_hiddenBtn->setChecked(show);
    }
    applyFilter();
}

void FileExplorer::applyFilter()
{
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_showHidden) {
        filters |= QDir::Hidden;
    }
    if (m_model) {
        m_model->setFilter(filters);
    }
}

void FileExplorer::onGitStatusChanged()
{
    // Force the tree view to repaint so git badges update
    m_treeView->viewport()->update();
}
