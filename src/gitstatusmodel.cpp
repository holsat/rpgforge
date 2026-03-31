#include "gitstatusmodel.h"

#include <git2.h>

#include <QColor>
#include <QDir>
#include <QFileInfo>

GitStatusModel::GitStatusModel(QObject *parent)
    : QObject(parent)
{
    git_libgit2_init();

    // Auto-refresh every 3 seconds when active
    m_refreshTimer.setInterval(3000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GitStatusModel::refresh);
}

GitStatusModel::~GitStatusModel()
{
    git_libgit2_shutdown();
}

void GitStatusModel::setRootPath(const QString &path)
{
    m_rootPath = path;
    refresh();
    m_refreshTimer.start();
}

GitStatusModel::FileStatus GitStatusModel::statusForFile(const QString &absolutePath) const
{
    if (!m_isGitRepo) return NotInRepo;
    auto it = m_statusMap.find(absolutePath);
    if (it != m_statusMap.end()) return it.value();
    // If file is in a git repo but not in the status map, it's unmodified/committed
    return Unmodified;
}

QString GitStatusModel::badgeForStatus(FileStatus status) const
{
    switch (status) {
    case Modified:    return QStringLiteral("M");
    case Staged:      return QStringLiteral("S");
    case Untracked:   return QStringLiteral("U");
    case Added:       return QStringLiteral("A");
    case Deleted:     return QStringLiteral("D");
    case Renamed:     return QStringLiteral("R");
    default:          return QString();
    }
}

QColor GitStatusModel::colorForStatus(FileStatus status) const
{
    switch (status) {
    case Modified:    return QColor(0xE5, 0xC0, 0x7B); // yellow
    case Staged:      return QColor(0x98, 0xC3, 0x79); // green
    case Untracked:   return QColor(0x7E, 0x7E, 0x7E); // grey
    case Added:       return QColor(0x98, 0xC3, 0x79); // green
    case Deleted:     return QColor(0xE0, 0x6C, 0x75); // red
    case Renamed:     return QColor(0x61, 0xAF, 0xEF); // blue
    default:          return QColor();
    }
}

void GitStatusModel::refresh()
{
    if (m_rootPath.isEmpty()) return;
    refreshFromLibgit2();
    Q_EMIT statusChanged();
}

void GitStatusModel::refreshFromLibgit2()
{
    m_statusMap.clear();
    m_isGitRepo = false;

    git_repository *repo = nullptr;
    // Discover the git repo from the root path (walks up to find .git)
    git_buf repoPath = GIT_BUF_INIT;
    int err = git_repository_discover(&repoPath, m_rootPath.toUtf8().constData(), 0, nullptr);
    if (err != 0) {
        git_buf_dispose(&repoPath);
        return;
    }

    err = git_repository_open(&repo, repoPath.ptr);
    git_buf_dispose(&repoPath);
    if (err != 0 || !repo) return;

    m_isGitRepo = true;

    // Get the workdir to build absolute paths
    const char *workdir = git_repository_workdir(repo);
    if (!workdir) {
        git_repository_free(repo);
        return;
    }
    QString workDir = QString::fromUtf8(workdir);

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;

    git_status_list *statusList = nullptr;
    err = git_status_list_new(&statusList, repo, &opts);
    if (err != 0 || !statusList) {
        git_repository_free(repo);
        return;
    }

    size_t count = git_status_list_entrycount(statusList);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *entry = git_status_byindex(statusList, i);
        if (!entry) continue;

        const char *path = nullptr;
        if (entry->head_to_index && entry->head_to_index->new_file.path) {
            path = entry->head_to_index->new_file.path;
        } else if (entry->index_to_workdir && entry->index_to_workdir->old_file.path) {
            path = entry->index_to_workdir->old_file.path;
        }
        if (!path) continue;

        QString absPath = QDir::cleanPath(workDir + QString::fromUtf8(path));
        unsigned int flags = entry->status;

        FileStatus status = Unmodified;

        // Index (staged) statuses take priority in display
        if (flags & GIT_STATUS_INDEX_NEW)         status = Added;
        else if (flags & GIT_STATUS_INDEX_MODIFIED)  status = Staged;
        else if (flags & GIT_STATUS_INDEX_DELETED)   status = Deleted;
        else if (flags & GIT_STATUS_INDEX_RENAMED)   status = Renamed;
        // Workdir (unstaged) statuses
        else if (flags & GIT_STATUS_WT_NEW)          status = Untracked;
        else if (flags & GIT_STATUS_WT_MODIFIED)     status = Modified;
        else if (flags & GIT_STATUS_WT_DELETED)      status = Deleted;
        else if (flags & GIT_STATUS_WT_RENAMED)      status = Renamed;
        else if (flags & GIT_STATUS_IGNORED)         status = Ignored;

        if (status != Ignored) {
            m_statusMap.insert(absPath, status);
        }
    }

    git_status_list_free(statusList);
    git_repository_free(repo);
}
