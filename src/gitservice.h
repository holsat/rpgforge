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
#include <QSemaphore>

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

/**
 * \brief Represents a single commit in the exploration graph.
 *
 * Each node corresponds to one Git commit and carries enough information
 * to render a branch-lane graph and annotate word-count progress.
 * Word-count fields are populated asynchronously by
 * GitService::scheduleWordCountForCommit(); they default to zero until
 * the background computation finishes.
 */
struct ExplorationNode {
    QString hash;                ///< Full 40-character commit OID.
    QString primaryParentHash;   ///< First parent OID as hex string; empty for root commits.
    QString mergeParentHash;     ///< Second parent OID for merge commits; empty otherwise.
    QString branchName;          ///< Name of the branch this node belongs to.
    QDateTime date;              ///< Commit author timestamp.
    QString message;             ///< First line of the commit message.
    QStringList tags;            ///< Tag (landmark) names pointing at this commit.
    int wordCount = 0;           ///< Total word count of manuscript/ at this commit.
    int wordCountDelta = 0;      ///< Word-count change relative to primaryParentHash.
};

/**
 * \brief Describes a file that is in a conflicted state in the Git index.
 *
 * All three blob OIDs correspond to the standard merge stages recorded by
 * libgit2 after a conflicting merge.  An empty OID string means the
 * respective stage is absent (e.g., a file added on both sides has no
 * ancestor OID).
 */
struct ConflictFile {
    QString path;           ///< Repository-relative path of the conflicted file.
    QString ancestorHash;   ///< Blob OID of the common ancestor (stage 1); may be empty.
    QString oursHash;       ///< Blob OID of the current-branch version (stage 2).
    QString theirsHash;     ///< Blob OID of the incoming-branch version (stage 3).
};

/**
 * \brief Represents one entry in the Git stash list.
 *
 * Populated by GitService::listStashes().  The \c onBranch field is
 * extracted by parsing the conventional "Parked on 'X' — …" stash message
 * written by GitService::stashChanges(); it may be empty for stashes
 * created by other tools.
 */
struct StashEntry {
    int index = 0;       ///< 0-based position in `git stash list` output.
    QString message;     ///< Full stash reference message, e.g. "stash@{0}: …".
    QString hash;        ///< Commit OID of the stash object.
    QDateTime date;      ///< Timestamp of the stash commit.
    QString onBranch;    ///< Branch name parsed from the message; may be empty.
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
     * \brief Extracts a raw Git blob by OID to the given absolute output path.
     *
     * Runs on a QtConcurrent worker thread.  The future resolves to \c true
     * when the blob has been written successfully, or \c false on any
     * libgit2 or I/O error.  Use \c .then(this, …) to marshal the result
     * back to the main thread before touching UI.
     *
     * \param repoPath  Absolute path to the root of the Git repository.
     * \param blobOid   Full or abbreviated 40-character blob OID.
     * \param outputPath Absolute path where the blob content will be written.
     *                   The file is created or overwritten.
     * \return A future that resolves to \c true on success, \c false on failure.
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
     * \brief Returns the full exploration graph for a repository.
     *
     * Walks every local branch and assembles an ordered list of
     * ExplorationNode objects that describe the commit DAG.  Nodes are
     * sorted newest-first within each lane.  Word-count fields are zeroed
     * on return; call scheduleWordCountForCommit() to populate them
     * asynchronously.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread before updating the UI.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \return A future resolving to the list of exploration nodes, or an
     *         empty list if the repository cannot be opened.
     */
    QFuture<QList<ExplorationNode>> getExplorationGraph(const QString &repoPath);

    /**
     * \brief Returns the list of files currently in a conflicted state in the index.
     *
     * Inspects the Git index for stage-1/2/3 entries and returns one
     * ConflictFile per conflicted path.  The call is meaningful only
     * immediately after a merge that left conflicts; the list will be empty
     * if the index is clean.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread before updating the UI.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \return A future resolving to the list of conflicting files, or an
     *         empty list if no conflicts exist or the repository cannot be opened.
     */
    QFuture<QList<ConflictFile>> getConflictingFiles(const QString &repoPath);

    /**
     * \brief Stashes all uncommitted changes with a descriptive message.
     *
     * Writes a stash entry using the conventional message format
     * "Parked on '<branch>' — <message>", which allows StashEntry::onBranch
     * to be recovered later.  Requires a clean working tree after completion;
     * the working directory is left in the HEAD state.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \param message  User-visible description stored in the stash entry.
     * \return A future resolving to \c true on success, \c false on failure.
     */
    QFuture<bool> stashChanges(const QString &repoPath, const QString &message);

    /**
     * \brief Returns all stash entries for the repository.
     *
     * This call is synchronous and executes on the calling thread.
     * It is safe to invoke from the main thread because it performs only an
     * in-memory libgit2 stash iteration with no file I/O.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \return Ordered list of stash entries (index 0 is the most recent),
     *         or an empty list if there are no stashes or the repository
     *         cannot be opened.
     */
    QList<StashEntry> listStashes(const QString &repoPath);

    /**
     * \brief Applies a stash entry and drops it from the stash list on success.
     *
     * If the working tree has uncommitted modifications that would conflict
     * with the stash, the operation is aborted and the
     * stashApplyBlockedByDirtyTree() signal is emitted on the main thread
     * via Qt::AutoConnection.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread.
     *
     * \param repoPath   Absolute path to the root of the Git repository.
     * \param stashIndex 0-based index into the stash list (as returned by listStashes()).
     * \return A future resolving to \c true if the stash was applied and
     *         dropped, \c false otherwise.
     * \sa stashApplyBlockedByDirtyTree()
     */
    QFuture<bool> applyStash(const QString &repoPath, int stashIndex);

    /**
     * \brief Removes a stash entry without applying its changes.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread.
     *
     * \param repoPath   Absolute path to the root of the Git repository.
     * \param stashIndex 0-based index into the stash list (as returned by listStashes()).
     * \return A future resolving to \c true if the entry was dropped,
     *         \c false on error.
     */
    QFuture<bool> dropStash(const QString &repoPath, int stashIndex);

    /**
     * \brief Switches to an exploration branch, chaining after any pending auto-commit.
     *
     * Waits for any in-flight autoCommit() future before performing the
     * checkout, so that in-progress writes are not interrupted.  If the
     * working tree is dirty when the checkout runs, the operation fails and
     * explorationSwitchFailed() is emitted on the main thread via
     * Qt::AutoConnection with a human-readable reason string.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread.
     *
     * \param repoPath   Absolute path to the root of the Git repository.
     * \param branchName Name of the local branch to check out.
     * \return A future resolving to \c true on a successful checkout,
     *         \c false otherwise.
     * \sa explorationSwitchFailed()
     */
    QFuture<bool> switchExploration(const QString &repoPath, const QString &branchName);

    /**
     * \brief Creates a new exploration branch from HEAD and checks it out.
     *
     * This call is synchronous and executes on the calling thread.
     * It is intended to be called only from the main thread when the user
     * explicitly requests a new branch; callers must ensure the working
     * tree is clean before invoking.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \param name     Name for the new branch.  Must be a valid Git ref name.
     * \return \c true if the branch was created and checked out successfully,
     *         \c false on any libgit2 error.
     */
    bool createExploration(const QString &repoPath, const QString &name);

    /**
     * \brief Merges a source exploration branch into the current branch.
     *
     * Performs a three-way merge.  If the merge completes without conflicts
     * a merge commit is created automatically.  If conflicts arise, the
     * index is left in the conflicted state and the caller should invoke
     * getConflictingFiles() to present the conflicts to the user.
     *
     * Runs on a QtConcurrent worker thread.  Use \c .then(this, …) to
     * marshal the result back to the main thread.
     *
     * \param repoPath     Absolute path to the root of the Git repository.
     * \param sourceBranch Name of the local branch to merge into HEAD.
     * \return A future resolving to \c true if the merge (including any
     *         auto-commit) succeeded, \c false if conflicts were found or
     *         a libgit2 error occurred.
     * \sa getConflictingFiles()
     */
    QFuture<bool> integrateExploration(const QString &repoPath, const QString &sourceBranch);

    /**
     * \brief Schedules asynchronous word-count computation for a single commit.
     *
     * Launches a QtConcurrent task that counts words in the manuscript/
     * directory as it existed at \p hash.  When the count is available it
     * is stored in the internal cache.  Callers should connect to whatever
     * signal triggers a graph refresh; there is no per-commit completion
     * signal.  Calls for a hash already present in the cache are silently
     * ignored.
     *
     * This method is safe to call from the main thread.
     *
     * \param repoPath Absolute path to the root of the Git repository.
     * \param hash     Full 40-character commit OID.
     * \sa loadWordCountCache(), saveWordCountCache()
     */
    void scheduleWordCountForCommit(const QString &repoPath, const QString &hash);

    /**
     * \brief Restores the word-count cache from a previously serialized map.
     *
     * Intended to be called once at project-open time with the map produced
     * by ProjectManager::loadExplorationData().  Existing cache entries are
     * replaced.  Must be called on the main thread.
     *
     * \param data Map of commit hash strings to word-count integers, as
     *             produced by saveWordCountCache().
     * \sa saveWordCountCache(), ProjectManager::loadExplorationData()
     */
    void loadWordCountCache(const QVariantMap &data);

    /**
     * \brief Serializes the word-count cache to a QVariantMap for persistence.
     *
     * Returns a map of commit hash string to word-count integer suitable
     * for passing to ProjectManager::saveExplorationData().  This method
     * is thread-safe; it acquires an internal read lock on the cache.
     *
     * \return Map of commit hash strings to word-count integer values.
     * \sa loadWordCountCache(), ProjectManager::saveExplorationData()
     */
    QVariantMap saveWordCountCache() const;

Q_SIGNALS:
    /**
     * \brief Emitted when applyStash() is blocked by uncommitted changes.
     *
     * Fired on the main thread via Qt::AutoConnection from a worker thread.
     * The caller should prompt the user to commit or stash their changes
     * before retrying the apply.
     */
    void stashApplyBlockedByDirtyTree();

    /**
     * \brief Emitted when switchExploration() fails to check out the target branch.
     *
     * Fired on the main thread via Qt::AutoConnection from a worker thread.
     *
     * \param reason Human-readable description of why the switch failed
     *               (e.g., "working tree is dirty").
     */
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

    // Throttles scheduleWordCountForCommit() so that large histories do not
    // swamp QThreadPool with concurrent libgit2 repository opens.
    QSemaphore m_wordCountSemaphore{4};
};

#endif // GITSERVICE_H
