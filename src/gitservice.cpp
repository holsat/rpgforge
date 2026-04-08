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

#include "gitservice.h"
#include "githubservice.h"
#include <git2.h>
#include <QtConcurrent>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QMutexLocker>
#include <QSettings>
#include <kwallet.h>

// Credential callback payload for HTTPS Git operations using a Personal Access Token.
struct PushCredPayload {
    QByteArray username; // "x-access-token" for GitHub PATs
    QByteArray password; // the token itself
};

static int pushCredCallback(git_credential **out, const char * /*url*/, const char * /*username_from_url*/,
                            unsigned int allowed_types, void *payload)
{
    if (!(allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT)) return GIT_PASSTHROUGH;
    auto *creds = static_cast<PushCredPayload *>(payload);
    return git_credential_userpass_plaintext_new(out, creds->username.constData(), creds->password.constData());
}

// Creates a git_signature using the repository's configured user.name/user.email.
// Falls back to reading RPGForge settings, then to a generic identity if nothing is set.
static int makeSignature(git_signature **out, git_repository *repo)
{
    if (git_signature_default(out, repo) == 0) return 0;

    // git_signature_default failed (no user.name/email in config); try app settings
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QString name  = settings.value(QStringLiteral("git/author_name"), QStringLiteral("RPG Forge")).toString();
    QString email = settings.value(QStringLiteral("git/author_email"), QStringLiteral("rpgforge@local")).toString();
    return git_signature_now(out, name.toUtf8().constData(), email.toUtf8().constData());
}

GitService::GitService(QObject *parent)
    : QObject(parent)
{
    git_libgit2_init();
}

GitService::~GitService()
{
    git_libgit2_shutdown();
}

GitService& GitService::instance()
{
    static GitService s_instance;
    return s_instance;
}

QFuture<bool> GitService::autoCommit(const QString &filePath, const QString &message)
{
    // ONLY auto-sync files in the manuscript directory
    // This prevents research, stylesheets, or other internal files from polluting the git history.
    if (!filePath.contains(QStringLiteral("/manuscript/"), Qt::CaseInsensitive)) {
        return QtConcurrent::run([]() { return true; });
    }

    QString msg = message.isEmpty()
        ? QStringLiteral("Auto-save: %1").arg(QFileInfo(filePath).fileName())
        : message;

    return QtConcurrent::run([this, filePath, msg]() {
        return internalCommit(filePath, msg);
    });
}

QFuture<bool> GitService::commitAll(const QString &repoPath, const QString &message)
{
    return QtConcurrent::run([repoPath, message]() {
        git_repository *repo = nullptr;
        git_index *index = nullptr;
        git_oid tree_oid, parent_oid, commit_oid;
        git_tree *tree = nullptr;
        git_signature *signature = nullptr;
        git_commit *parent = nullptr;
        bool success = false;

        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) goto cleanup;
        if (git_repository_index(&index, repo) != 0) goto cleanup;
        if (git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) != 0) goto cleanup;
        if (git_index_write(index) != 0) goto cleanup;
        if (git_index_write_tree(&tree_oid, index) != 0) goto cleanup;
        if (git_tree_lookup(&tree, repo, &tree_oid) != 0) goto cleanup;

        if (git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0) {
            if (git_commit_lookup(&parent, repo, &parent_oid) != 0) goto cleanup;
        }

        if (makeSignature(&signature, repo) != 0) goto cleanup;

        if (git_commit_create(&commit_oid, repo, "HEAD", signature, signature,
                              nullptr, message.toUtf8().constData(), tree,
                              parent ? 1 : 0, (const git_commit **)&parent) != 0) goto cleanup;

        success = true;

    cleanup:
        if (signature) git_signature_free(signature);
        if (tree) git_tree_free(tree);
        if (parent) git_commit_free(parent);
        if (index) git_index_free(index);
        if (repo) git_repository_free(repo);

        return success;
    });
}

bool GitService::initRepo(const QString &path)
{
    git_repository *repo = nullptr;
    int err = git_repository_init(&repo, path.toUtf8().constData(), 0);
    if (repo) git_repository_free(repo);
    return err == 0;
}

bool GitService::isRepo(const QString &path)
{
    git_buf repoPath = {nullptr, 0, 0};
    int err = git_repository_discover(&repoPath, path.toUtf8().constData(), 0, nullptr);
    git_buf_dispose(&repoPath);
    return err == 0;
}

struct DiffPayload {
    QList<DiffHunk> *hunks;
};

static int diff_line_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload)
{
    Q_UNUSED(delta);
    Q_UNUSED(hunk);
    auto *hunks = static_cast<QList<DiffHunk>*>(payload);

    if (line->origin == GIT_DIFF_LINE_CONTEXT) {
        return 0;
    }

    DiffLine dl;
    dl.content = QString::fromUtf8(line->content, line->content_len);
    dl.oldLine = line->old_lineno - 1;
    dl.newLine = line->new_lineno - 1;
    dl.type = (line->origin == GIT_DIFF_LINE_ADDITION) ? DiffLine::Added : DiffLine::Deleted;

    // Logic to decide if we need a new hunk or can append to the last one.
    // We group adjacent changes into a single hunk for better visualization.
    bool createNew = hunks->isEmpty();
    if (!createNew) {
        const auto &last = hunks->last();
        // If the gap is small (1 line), we merge them for a cleaner UI
        if (dl.type == DiffLine::Added) {
            if (dl.newLine > last.newStart + last.newLines + 1) createNew = true;
        } else {
            if (dl.oldLine > last.oldStart + last.oldLines + 1) createNew = true;
        }
    }

    if (createNew) {
        DiffHunk dh;
        dh.oldStart = (line->old_lineno > 0) ? line->old_lineno : (hunk ? hunk->old_start : 1);
        dh.newStart = (line->new_lineno > 0) ? line->new_lineno : (hunk ? hunk->new_start : 1);
        dh.type = (dl.type == DiffLine::Added) ? DiffHunk::Added : DiffHunk::Deleted;
        dh.oldLines = 0;
        dh.newLines = 0;
        hunks->append(dh);
    }

    auto &currentHunk = hunks->last();
    currentHunk.lines.append(dl);
    
    // Recalculate totals and types for the active hunk
    int oldLines = 0, newLines = 0;
    for (const auto &l : currentHunk.lines) {
        if (l.type == DiffLine::Deleted) oldLines++;
        else newLines++;
    }
    
    currentHunk.oldLines = oldLines;
    currentHunk.newLines = newLines;

    if (oldLines > 0 && newLines > 0) currentHunk.type = DiffHunk::Modified;
    else if (oldLines > 0) currentHunk.type = DiffHunk::Deleted;
    else currentHunk.type = DiffHunk::Added;

    return 0;
}

QFuture<QList<DiffHunk>> GitService::computeDiff(const QString &filePath, const QString &oldHash, const QString &newHash)
{
    return QtConcurrent::run([filePath, oldHash, newHash]() {
        QList<DiffHunk> hunks;
        git_repository *repo = nullptr;
        git_commit *oldCommit = nullptr, *newCommit = nullptr;
        git_tree *oldTree = nullptr, *newTree = nullptr;
        git_diff *diff = nullptr;
        git_buf repoPath = {nullptr, 0, 0};

        if (git_repository_discover(&repoPath, filePath.toUtf8().constData(), 0, nullptr) != 0) goto cleanup;
        if (git_repository_open(&repo, repoPath.ptr) != 0) goto cleanup;

        if (!oldHash.isEmpty()) {
            git_oid oldOid;
            git_oid_fromstr(&oldOid, oldHash.toUtf8().constData());
            if (git_commit_lookup(&oldCommit, repo, &oldOid) == 0) {
                git_commit_tree(&oldTree, oldCommit);
            }
        }

        if (!newHash.isEmpty()) {
            git_oid newOid;
            git_oid_fromstr(&newOid, newHash.toUtf8().constData());
            if (git_commit_lookup(&newCommit, repo, &newOid) == 0) {
                git_commit_tree(&newTree, newCommit);
            }
        }

        {
            git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
            QString relativePath = QDir(QString::fromUtf8(git_repository_workdir(repo))).relativeFilePath(filePath);
            const char *p = relativePath.toUtf8().constData();
            opts.pathspec.strings = (char **)&p;
            opts.pathspec.count = 1;
            opts.context_lines = 0;

            if (newTree) {
                git_diff_tree_to_tree(&diff, repo, oldTree, newTree, &opts);
            } else if (oldTree) {
                git_diff_tree_to_workdir(&diff, repo, oldTree, &opts);
            }

            if (diff) {
                git_diff_foreach(diff, nullptr, nullptr, nullptr, diff_line_cb, &hunks);
            }
        }

    cleanup:
        git_buf_dispose(&repoPath);
        if (diff) git_diff_free(diff);
        if (oldTree) git_tree_free(oldTree);
        if (newTree) git_tree_free(newTree);
        if (oldCommit) git_commit_free(oldCommit);
        if (newCommit) git_commit_free(newCommit);
        if (repo) git_repository_free(repo);
        return hunks;
    });
}

QFuture<QList<DiffHunk>> GitService::computeFileDiff(const QString &file1, const QString &file2)
{
    return QtConcurrent::run([file1, file2]() {
        QList<DiffHunk> hunks;
        
        QFile f1(file1), f2(file2);
        if (!f1.open(QIODevice::ReadOnly) || !f2.open(QIODevice::ReadOnly)) return hunks;
        
        QByteArray b1 = f1.readAll(), b2 = f2.readAll();
        
        git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
        opts.context_lines = 0;

        git_diff_buffers(b1.constData(), b1.size(), nullptr,
                         b2.constData(), b2.size(), nullptr,
                         &opts, nullptr, nullptr, nullptr, diff_line_cb, &hunks);
        
        return hunks;
    });
}

QString GitService::currentBranch(const QString &path)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return QString();

    git_reference *head = nullptr;
    QString name;
    if (git_repository_head(&head, repo) == 0) {
        name = QString::fromUtf8(git_reference_shorthand(head));
        git_reference_free(head);
    }

    git_repository_free(repo);
    return name;
}

QStringList GitService::listBranches(const QString &path)
{
    QStringList list;
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return list;

    git_branch_iterator *iter = nullptr;
    if (git_branch_iterator_new(&iter, repo, GIT_BRANCH_LOCAL) == 0) {
        git_reference *ref = nullptr;
        git_branch_t type;
        while (git_branch_next(&ref, &type, iter) == 0) {
            list << QString::fromUtf8(git_reference_shorthand(ref));
            git_reference_free(ref);
        }
        git_branch_iterator_free(iter);
    }

    git_repository_free(repo);
    return list;
}

bool GitService::createBranch(const QString &path, const QString &branchName)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_oid head_oid;
    git_commit *head_commit = nullptr;
    git_reference *branch_ref = nullptr;
    bool success = false;

    if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
        if (git_commit_lookup(&head_commit, repo, &head_oid) == 0) {
            if (git_branch_create(&branch_ref, repo, branchName.toUtf8().constData(), head_commit, 0) == 0) {
                success = true;
            }
        }
    }

    if (branch_ref) git_reference_free(branch_ref);
    if (head_commit) git_commit_free(head_commit);
    git_repository_free(repo);
    return success;
}

bool GitService::checkoutBranch(const QString &path, const QString &branchName)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_reference *branch_ref = nullptr;
    git_object *target = nullptr;
    bool success = false;

    if (git_branch_lookup(&branch_ref, repo, branchName.toUtf8().constData(), GIT_BRANCH_LOCAL) == 0) {
        if (git_reference_peel(&target, branch_ref, GIT_OBJECT_COMMIT) == 0) {
            git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
            opts.checkout_strategy = GIT_CHECKOUT_SAFE;
            if (git_checkout_tree(repo, target, &opts) == 0) {
                if (git_repository_set_head(repo, git_reference_name(branch_ref)) == 0) {
                    success = true;
                }
            }
        }
    }

    if (target) git_object_free(target);
    if (branch_ref) git_reference_free(branch_ref);
    git_repository_free(repo);
    return success;
}

bool GitService::mergeBranch(const QString &path, const QString &sourceBranch)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_reference *source_ref = nullptr;
    git_annotated_commit *source_commit = nullptr;
    bool success = false;

    if (git_branch_lookup(&source_ref, repo, sourceBranch.toUtf8().constData(), GIT_BRANCH_LOCAL) == 0) {
        if (git_annotated_commit_from_ref(&source_commit, repo, source_ref) == 0) {
            git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
            git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
            checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

            if (git_merge(repo, (const git_annotated_commit **)&source_commit, 1, &merge_opts, &checkout_opts) == 0) {
                success = true;
            }
        }
    }

    if (source_commit) git_annotated_commit_free(source_commit);
    if (source_ref) git_reference_free(source_ref);
    git_repository_free(repo);
    return success;
}

QFuture<bool> GitService::cloneRepo(const QString &url, const QString &path)
{
    // Read the cached token on the calling thread to avoid cross-thread access
    // to KWallet; the GitHubService cache is populated on the main thread.
    const QString token = GitHubService::instance().token();

    return QtConcurrent::run([url, path, token]() {
        git_repository *repo = nullptr;
        git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;

        PushCredPayload creds{
            QByteArrayLiteral("x-access-token"),
            token.toUtf8()
        };

        if (!token.isEmpty()) {
            clone_opts.fetch_opts.callbacks.credentials = pushCredCallback;
            clone_opts.fetch_opts.callbacks.payload = &creds;
        }

        int err = git_clone(&repo, url.toUtf8().constData(), path.toUtf8().constData(), &clone_opts);
        if (repo) git_repository_free(repo);
        return err == 0;
    });
}

QFuture<bool> GitService::extractVersion(const QString &filePath, const QString &hash, const QString &destPath)
{
    return QtConcurrent::run([filePath, hash, destPath]() {
        git_repository *repo = nullptr;
        git_commit *commit = nullptr;
        git_tree *tree = nullptr;
        git_tree_entry *entry = nullptr;
        git_blob *blob = nullptr;
        git_buf repoPath = {nullptr, 0, 0};
        bool success = false;

        if (git_repository_discover(&repoPath, filePath.toUtf8().constData(), 0, nullptr) != 0) goto cleanup;
        if (git_repository_open(&repo, repoPath.ptr) != 0) goto cleanup;

        git_oid oid;
        if (git_oid_fromstr(&oid, hash.toUtf8().constData()) != 0) goto cleanup;
        if (git_commit_lookup(&commit, repo, &oid) != 0) goto cleanup;
        if (git_commit_tree(&tree, commit) != 0) goto cleanup;

        {
            QString relativePath = QDir(QString::fromUtf8(git_repository_workdir(repo))).relativeFilePath(filePath);
            if (git_tree_entry_bypath(&entry, tree, relativePath.toUtf8().constData()) == 0) {
                if (git_blob_lookup(&blob, repo, git_tree_entry_id(entry)) == 0) {
                    QFile file(destPath);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write((const char *)git_blob_rawcontent(blob), git_blob_rawsize(blob));
                        success = true;
                    }
                }
            }
        }

    cleanup:
        git_buf_dispose(&repoPath);
        if (blob) git_blob_free(blob);
        if (entry) git_tree_entry_free(entry);
        if (tree) git_tree_free(tree);
        if (commit) git_commit_free(commit);
        if (repo) git_repository_free(repo);
        return success;
    });
}

QFuture<QList<VersionInfo>> GitService::getHistory(const QString &filePath)
{
    return QtConcurrent::run([filePath]() {
        QList<VersionInfo> history;
        git_repository *repo = nullptr;
        git_revwalk *walker = nullptr;
        git_buf repoPath = {nullptr, 0, 0};

        if (git_repository_discover(&repoPath, filePath.toUtf8().constData(), 0, nullptr) != 0) goto history_cleanup;
        if (git_repository_open(&repo, repoPath.ptr) != 0) goto history_cleanup;

        // Map commit OID to branches
        {
            QMap<QString, QStringList> commitBranches;
            git_branch_iterator *branchIter = nullptr;
            if (git_branch_iterator_new(&branchIter, repo, GIT_BRANCH_LOCAL) == 0) {
                git_reference *ref = nullptr;
                git_branch_t type;
                while (git_branch_next(&ref, &type, branchIter) == 0) {
                    git_oid target;
                    if (git_reference_peel((git_object **)&target, ref, GIT_OBJECT_COMMIT) == 0) {
                        char oid_str[GIT_OID_HEXSZ + 1];
                        git_oid_tostr(oid_str, sizeof(oid_str), &target);
                        commitBranches[QString::fromLatin1(oid_str)] << QString::fromUtf8(git_reference_shorthand(ref));
                    }
                    git_reference_free(ref);
                }
                git_branch_iterator_free(branchIter);
            }

            git_revwalk_new(&walker, repo);
            git_revwalk_sorting(walker, GIT_SORT_TIME);
            git_revwalk_push_glob(walker, "refs/heads/*");

            QString relativePath = QDir(QString::fromUtf8(git_repository_workdir(repo))).relativeFilePath(filePath);
            git_oid oid;
            while (git_revwalk_next(&oid, walker) == 0) {
                git_commit *commit = nullptr;
                if (git_commit_lookup(&commit, repo, &oid) == 0) {
                    bool modified = false;
                    if (git_commit_parentcount(commit) > 0) {
                        git_commit *parent = nullptr;
                        git_tree *tree = nullptr, *parentTree = nullptr;
                        git_diff *diff = nullptr;
                        
                        git_commit_parent(&parent, commit, 0);
                        git_commit_tree(&tree, commit);
                        git_commit_tree(&parentTree, parent);
                        
                        git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
                        const char *p = relativePath.toUtf8().constData();
                        opts.pathspec.strings = (char **)&p;
                        opts.pathspec.count = 1;
                        
                        git_diff_tree_to_tree(&diff, repo, parentTree, tree, &opts);
                        if (git_diff_num_deltas(diff) > 0) modified = true;
                        
                        git_diff_free(diff);
                        git_tree_free(parentTree);
                        git_tree_free(tree);
                        git_commit_free(parent);
                    } else {
                        modified = true;
                    }

                    if (modified) {
                        VersionInfo entry;
                        char oid_str[GIT_OID_HEXSZ + 1];
                        git_oid_tostr(oid_str, sizeof(oid_str), &oid);
                        QString hashStr = QString::fromLatin1(oid_str);
                        entry.hash = hashStr;
                        const git_signature *sig = git_commit_author(commit);
                        entry.author = QString::fromUtf8(sig->name);
                        entry.date = QDateTime::fromSecsSinceEpoch(git_commit_time(commit));
                        entry.message = QString::fromUtf8(git_commit_message(commit));
                        entry.branches = commitBranches.value(hashStr);
                        history.append(entry);
                    }
                    git_commit_free(commit);
                }
            }
        }

    history_cleanup:
        git_buf_dispose(&repoPath);
        if (walker) git_revwalk_free(walker);
        if (repo) git_repository_free(repo);
        return history;
    });
}

bool GitService::internalCommit(const QString &filePath, const QString &message)
{
    QMutexLocker locker(&m_commitMutex);
    git_repository *repo = nullptr;
    git_index *index = nullptr;
    git_oid tree_oid, parent_oid, commit_oid;
    git_tree *tree = nullptr;
    git_signature *signature = nullptr;
    git_commit *parent = nullptr;
    git_buf repoPath = {nullptr, 0, 0};
    bool success = false;

    if (git_repository_discover(&repoPath, filePath.toUtf8().constData(), 0, nullptr) != 0) goto cleanup;
    if (git_repository_open(&repo, repoPath.ptr) != 0) goto cleanup;

    if (git_repository_index(&index, repo) != 0) goto cleanup;

    {
        QString relativePath = QDir(QString::fromUtf8(git_repository_workdir(repo))).relativeFilePath(filePath);
        if (git_index_add_bypath(index, relativePath.toUtf8().constData()) != 0) goto cleanup;
        if (git_index_write(index) != 0) goto cleanup;
    }

    if (git_index_write_tree(&tree_oid, index) != 0) goto cleanup;
    if (git_tree_lookup(&tree, repo, &tree_oid) != 0) goto cleanup;

    if (git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0) {
        if (git_commit_lookup(&parent, repo, &parent_oid) != 0) goto cleanup;
    }

    if (git_signature_now(&signature, "RPG Forge", "rpgforge@example.com") != 0) goto cleanup;

    if (git_commit_create(&commit_oid, repo, "HEAD", signature, signature,
                          nullptr, message.toUtf8().constData(), tree,
                          parent ? 1 : 0, (const git_commit **)&parent) != 0) goto cleanup;

    success = true;

cleanup:
    git_buf_dispose(&repoPath);
    if (signature) git_signature_free(signature);
    if (tree) git_tree_free(tree);
    if (parent) git_commit_free(parent);
    if (index) git_index_free(index);
    if (repo) git_repository_free(repo);

    return success;
}

bool GitService::deleteBranch(const QString &path, const QString &branchName)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_reference *branch_ref = nullptr;
    bool success = false;

    if (git_branch_lookup(&branch_ref, repo, branchName.toUtf8().constData(), GIT_BRANCH_LOCAL) == 0) {
        if (git_branch_delete(branch_ref) == 0) {
            success = true;
        }
    }

    if (branch_ref) git_reference_free(branch_ref);
    git_repository_free(repo);
    return success;
}

bool GitService::hasUncommittedChanges(const QString &path)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;

    git_status_list *status = nullptr;
    bool changes = false;
    if (git_status_list_new(&status, repo, &opts) == 0) {
        changes = git_status_list_entrycount(status) > 0;
        git_status_list_free(status);
    }

    git_repository_free(repo);
    return changes;
}

QFuture<bool> GitService::push(const QString &path, const QString &remoteName)
{
    // Read the cached token on the calling thread to avoid cross-thread access
    // to KWallet; the GitHubService cache is populated on the main thread.
    const QString token = GitHubService::instance().token();

    return QtConcurrent::run([path, remoteName, token]() {
        git_repository *repo = nullptr;
        git_remote *remote = nullptr;
        git_reference *head = nullptr;
        bool success = false;

        if (git_repository_open(&repo, path.toUtf8().constData()) != 0) goto cleanup;
        if (git_remote_lookup(&remote, repo, remoteName.toUtf8().constData()) != 0) goto cleanup;
        if (git_repository_head(&head, repo) != 0) goto cleanup;

        {
            // Build refspec "refs/heads/<branch>:refs/heads/<branch>"
            QByteArray branchRef(git_reference_name(head));
            QByteArray refspec = branchRef + ":" + branchRef;
            char *refspecPtr = refspec.data();
            git_strarray refspecs{&refspecPtr, 1};

            PushCredPayload creds{
                QByteArrayLiteral("x-access-token"),
                token.toUtf8()
            };

            git_push_options push_opts = GIT_PUSH_OPTIONS_INIT;
            push_opts.callbacks.credentials = pushCredCallback;
            push_opts.callbacks.payload = &creds;

            success = (git_remote_push(remote, &refspecs, &push_opts) == 0);
        }

    cleanup:
        if (head)   git_reference_free(head);
        if (remote) git_remote_free(remote);
        if (repo)   git_repository_free(repo);
        return success;
    });
}

bool GitService::setRemote(const QString &path, const QString &url, const QString &name)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    int err = git_remote_set_url(repo, name.toUtf8().constData(), url.toUtf8().constData());
    
    git_repository_free(repo);
    return err == 0;
}

bool GitService::createTag(const QString &path, const QString &tagName, const QString &hash)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, path.toUtf8().constData()) != 0) return false;

    git_oid oid, tag_oid;
    git_object *target = nullptr;
    git_signature *signature = nullptr;
    bool success = false;

    if (hash.isEmpty()) {
        git_reference_name_to_id(&oid, repo, "HEAD");
    } else {
        git_oid_fromstr(&oid, hash.toUtf8().constData());
    }

    if (git_object_lookup(&target, repo, &oid, GIT_OBJECT_COMMIT) == 0) {
        makeSignature(&signature, repo);
        if (git_tag_create(&tag_oid, repo, tagName.toUtf8().constData(), target, signature, "", 0) == 0) {
            success = true;
        }
    }

    if (signature) git_signature_free(signature);
    if (target) git_object_free(target);
    git_repository_free(repo);
    return success;
}
