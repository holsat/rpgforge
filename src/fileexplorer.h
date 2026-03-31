#ifndef FILEEXPLORER_H
#define FILEEXPLORER_H

#include <QModelIndex>
#include <QWidget>

class QTreeView;
class QFileSystemModel;
class QLineEdit;
class QToolButton;
class GitStatusModel;

class FileExplorer : public QWidget
{
    Q_OBJECT

public:
    explicit FileExplorer(QWidget *parent = nullptr);
    ~FileExplorer() override;

    void setRootPath(const QString &path);
    QString rootPath() const;

    bool showHiddenFiles() const { return m_showHidden; }
    void setShowHiddenFiles(bool show);

Q_SIGNALS:
    void fileActivated(const QUrl &url);

private Q_SLOTS:
    void onItemActivated(const QModelIndex &index);
    void onCustomContextMenu(const QPoint &pos);
    void onGitStatusChanged();
    void toggleHiddenFiles();

    void renameItem();
    void deleteItem();
    void newFile();
    void newFolder();
    void openFolder();

private:
    void setupUi();
    void applyFilter();

    QTreeView *m_treeView = nullptr;
    QFileSystemModel *m_model = nullptr;
    GitStatusModel *m_gitStatus = nullptr;
    QToolButton *m_hiddenBtn = nullptr;
    QModelIndex m_contextMenuIndex;
    bool m_showHidden = false;
};

#endif // FILEEXPLORER_H
