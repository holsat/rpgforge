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

#ifndef GITSERVICE_H
#define GITSERVICE_H

#include <QObject>
#include <QString>
#include <QFuture>
#include <QDateTime>
#include <QMutex>
#include <QMap>

typedef struct git_repository git_repository;

struct VersionInfo {
    QString hash;
    QDateTime date;
    QString author;
    QString message;
    QStringList tags;
    QStringList branches;
    int index; // 1-based version number
};

struct DiffLine {
    enum Type { Added, Deleted, Unchanged, Modified };
    Type type;
    int oldLine; // 0-based, -1 if added
    int newLine; // 0-based, -1 if deleted
    QString content;
};

struct DiffHunk {
    enum Type { Added, Deleted, Modified };
    Type type;
    int oldStart;
    int oldLines;
    int newStart;
    int newLines;
    QList<DiffLine> lines;
};

struct ExplorationNode {
    QString hash;
    QString primaryParentHash;   // first parent OID as hex string, empty if root
    QString mergeParentHash;     // second parent OID (merge commits only), else empty
    QString branchName;          // which branch this node belongs to
    QDateTime date;
    QString message;
    QStringList tags;            // landmark names if this commit is tagged
    int wordCount = 0;           // total word count of manuscript/ at this commit
    int wordCountDelta = 0;      // word count difference vs primaryParentHash
};

struct ConflictFile {
    QString path;
    QString ancestorHash;        // stage 1
    QString oursHash;            // stage 2
    QString theirsHash;          // stage 3
};

struct StashEntry {
    int index = 0;               // 0-based index into stash list
    QString message;
    QString hash;                // commit hash of the stash object
    QDateTime date;
    QString onBranch;            // parsed from message: "Parked on 'X' — ..."
};

/**
 * @brief Singleton service for background Git operations.
 */
class GitService : public QObject
{
    Q_OBJECT

public:
    static GitService& instance();

    /**
     * @brief Automatically commits a file if it is within a Git repository.
     */
    QFuture<bool> autoCommit(const QString &filePath, const QString &message = QString());

    /**
     * @brief Commits all changes in the repository.
     */
    QFuture<bool> commitAll(const QString &repoPath, const QString &message);

    /**
     * @brief Initializes a new Git repository at the given path.
     */
    bool initRepo(const QString &path);

    /**
     * @brief Checks if a directory is part of a Git repository.
     */
    bool isRepo(const QString &path);

    /**
     * @brief Returns the version history of a file.
     */
    QFuture<QList<VersionInfo>> getHistory(const QString &filePath);

    /**
     * @brief Extracts a specific version of a file to an output path.
     */
    QFuture<bool> extractVersion(const QString &filePath, const QString &hash, const QString &outputPath);

    /**
     * @brief Extracts a raw git blob by OID to the given absolute output path.
     */
    QFuture<bool> extractBlob(const QString &repoPath, const QString &blobOid, const QString &outputPath);

    /**
     * @brief Computes the difference between two versions of a file.
     */
    QFuture<QList<DiffHunk>> computeDiff(const QString &filePath, const QString &oldHash, const QString &newHash = QString());

    /**
     * @brief Computes the difference between two arbitrary files.
     */
    QFuture<QList<DiffHunk>> computeFileDiff(const QString &file1, const QString &file2);

    /**
     * @brief Returns the name of the current branch.
     */
    QString currentBranch(const QString &path);

    /**
     * @brief Lists all local branches.
     */
    QStringList listBranches(const QString &path);

    /**
     * @brief Creates and switches to a new branch.
     */
    bool createBranch(const QString &path, const QString &branchName);

    /**
     * @brief Switches to an existing branch.
     */
    bool checkoutBranch(const QString &path, const QString &branchName);

    /**
     * @brief Merges a branch into the current branch.
     */
    bool mergeBranch(const QString &path, const QString &sourceBranch);

    /**
     * @brief Deletes a branch.
     */
    bool deleteBranch(const QString &path, const QString &branchName);

    /**
     * @brief Checks if there are any uncommitted changes in the repository.
     */
    bool hasUncommittedChanges(const QString &path);

    /**
     * @brief Pushes changes to a remote.
     */
    QFuture<bool> push(const QString &path, const QString &remoteName = QStringLiteral("origin"));

    /**
     * @brief Sets or updates a remote URL.
     */
    bool setRemote(const QString &path, const QString &url, const QString &name = QStringLiteral("origin"));

    /**
     * @brief Creates a tag at a specific hash.
     */
    bool createTag(const QString &path, const QString &tagName, const QString &hash = QString());

    /**
     * @brief Clones a remote repository to a local path.
     */
    QFuture<bool> cloneRepo(const QString &url, const QString &localPath);

    /**
     * @brief Returns the full exploration graph for a repository.
     */
    QFuture<QList<ExplorationNode>> getExplorationGraph(const QString &repoPath);

    /**
     * @brief Returns a list of conflicting files in the index.
     */
    QFuture<QList<ConflictFile>> getConflictingFiles(const QString &repoPath);

    /**
     * @brief Stashes all uncommitted changes.
     */
    QFuture<bool> stashChanges(const QString &repoPath, const QString &message);

    /**
     * @brief Lists all stash entries.
     */
    QList<StashEntry> listStashes(const QString &repoPath);

    /**
     * @brief Applies and drops a stash entry.
     */
    QFuture<bool> applyStash(const QString &repoPath, int stashIndex);

    /**
     * @brief Drops a stash entry without applying.
     */
    QFuture<bool> dropStash(const QString &repoPath, int stashIndex);

    /**
     * @brief Switches to an exploration branch, chaining after any pending commit.
     */
    QFuture<bool> switchExploration(const QString &repoPath, const QString &branchName);

    /**
     * @brief Creates a new exploration branch and checks it out.
     */
    bool createExploration(const QString &repoPath, const QString &name);

    /**
     * @brief Merges a source branch into the current branch, creating a merge commit if needed.
     */
    QFuture<bool> integrateExploration(const QString &repoPath, const QString &sourceBranch);

    /**
     * @brief Schedules async word-count computation for a single commit.
     */
    void scheduleWordCountForCommit(const QString &repoPath, const QString &hash);

    /**
     * @brief Loads word-count cache from a previously saved QVariantMap.
     */
    void loadWordCountCache(const QVariantMap &data);

    /**
     * @brief Saves word-count cache to a QVariantMap for persistence.
     */
    QVariantMap saveWordCountCache() const;

Q_SIGNALS:
    void stashApplyBlockedByDirtyTree();
    void explorationSwitchFailed(const QString &reason);

private:
    explicit GitService(QObject *parent = nullptr);
    ~GitService() override;

    // Disallow copy/move
    GitService(const GitService&) = delete;
    GitService& operator=(const GitService&) = delete;

    bool internalCommit(const QString &filePath, const QString &message);
    int getWordCountAtCommit(git_repository *repo, const QString &hash);

    // Serializes concurrent internalCommit calls from QtConcurrent threads to
    // prevent races on the libgit2 repository index.
    QMutex m_commitMutex;

    QMap<QString, int> m_wordCountCache;   // commit hash → word count
    mutable QMutex m_cacheMutex;           // protects m_wordCountCache
    QFuture<bool> m_pendingCommit;         // tracks last autoCommit future
};

#endif // GITSERVICE_H
