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

private:
    explicit GitService(QObject *parent = nullptr);
    ~GitService() override;

    // Disallow copy/move
    GitService(const GitService&) = delete;
    GitService& operator=(const GitService&) = delete;

    bool internalCommit(const QString &filePath, const QString &message);
};

#endif // GITSERVICE_H
