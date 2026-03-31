#ifndef GITSTATUSMODEL_H
#define GITSTATUSMODEL_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>

// Wraps libgit2 to provide per-file git status
class GitStatusModel : public QObject
{
    Q_OBJECT

public:
    enum FileStatus {
        NotInRepo = 0,
        Unmodified,
        Modified,       // M - tracked and modified (unstaged)
        Staged,         // staged for commit
        Untracked,      // U - not tracked
        Added,          // A - newly added to index
        Deleted,        // D - deleted
        Renamed,        // R - renamed
        Ignored
    };
    Q_ENUM(FileStatus)

    explicit GitStatusModel(QObject *parent = nullptr);
    ~GitStatusModel() override;

    void setRootPath(const QString &path);
    FileStatus statusForFile(const QString &absolutePath) const;

    // Returns a single-character badge like VS Code (M, U, A, D, R, or empty)
    QString badgeForStatus(FileStatus status) const;
    // Returns a color for the badge
    QColor colorForStatus(FileStatus status) const;

    bool isGitRepo() const { return m_isGitRepo; }

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void statusChanged();

private:
    void refreshFromLibgit2();

    QString m_rootPath;
    bool m_isGitRepo = false;
    QHash<QString, FileStatus> m_statusMap; // absolute path -> status
    QTimer m_refreshTimer;
};

#endif // GITSTATUSMODEL_H
