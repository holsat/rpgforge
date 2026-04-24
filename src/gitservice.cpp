/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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
#include "projectmanager.h"
#include <git2.h>
#include <QtConcurrent>
#include <QThreadPool>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QMutexLocker>
#include <QSemaphore>
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
    m_pendingCommit = QtConcurrent::run([]{ return true; });
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

    m_pendingCommit = QtConcurrent::run([this, filePath, msg]() {
        return internalCommit(filePath, msg);
    });
    return m_pendingCommit;
}

QFuture<bool> GitService::commitAll(const QString &repoPath, const QString &message)
{
    m_pendingCommit = QtConcurrent::run([this, repoPath, message]() {
        QMutexLocker locker(&m_commitMutex);
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
    return m_pendingCommit;
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
            // Bind the UTF-8 byte array to a named local so the backing storage
            // for the pathspec pointer lives until git_diff_tree_to_tree/workdir
            // returns. Using .toUtf8().constData() inline would dangle.
            QByteArray relBytes = relativePath.toUtf8();
            const char *p = relBytes.constData();
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

    if (git_branch_lookup(&source_ref, repo, sourceBranch.toUtf8().constData(), GIT_BRANCH_LOCAL) != 0)
        goto merge_cleanup;

    if (git_annotated_commit_from_ref(&source_commit, repo, source_ref) != 0)
        goto merge_cleanup;

    {
        // Analyze merge shape before attempting work.
        git_merge_analysis_t analysis = GIT_MERGE_ANALYSIS_NONE;
        git_merge_preference_t preference = GIT_MERGE_PREFERENCE_NONE;
        const git_annotated_commit *heads[] = { source_commit };
        if (git_merge_analysis(&analysis, &preference, repo, heads, 1) != 0)
            goto merge_cleanup;

        // Nothing to do.
        if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
            success = true;
            goto merge_cleanup;
        }

        // Fast-forward: advance HEAD to source tip and check out its tree.
        if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
            const git_oid *targetOid = git_annotated_commit_id(source_commit);
            git_commit *targetCommit = nullptr;
            if (git_commit_lookup(&targetCommit, repo, targetOid) == 0) {
                git_tree *targetTree = nullptr;
                if (git_commit_tree(&targetTree, targetCommit) == 0) {
                    git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
                    co.checkout_strategy = GIT_CHECKOUT_SAFE;
                    if (git_checkout_tree(repo, reinterpret_cast<git_object*>(targetTree), &co) == 0) {
                        git_reference *head = nullptr;
                        if (git_repository_head(&head, repo) == 0) {
                            git_reference *newHead = nullptr;
                            if (git_reference_set_target(&newHead, head, targetOid, "ff merge") == 0) {
                                success = true;
                            }
                            if (newHead) git_reference_free(newHead);
                            git_reference_free(head);
                        }
                    }
                    git_tree_free(targetTree);
                }
                git_commit_free(targetCommit);
            }
            goto merge_cleanup;
        }

        // Normal merge: leaves index/workdir with conflicts to resolve, MERGE_HEAD is set,
        // and the merge commit is finalised by integrateExploration.
        git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
        git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
        checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

        if (git_merge(repo, heads, 1, &merge_opts, &checkout_opts) == 0) {
            success = true;
        }
    }

merge_cleanup:
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

QFuture<bool> GitService::extractBlob(const QString &repoPath, const QString &blobOid, const QString &outputPath)
{
    return QtConcurrent::run([repoPath, blobOid, outputPath]() -> bool {
        git_repository *repo = nullptr;
        git_blob *blob = nullptr;
        bool ok = false;

        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return false;

        git_oid oid;
        if (git_oid_fromstr(&oid, blobOid.toUtf8().constData()) != 0) goto blob_cleanup;
        if (git_blob_lookup(&blob, repo, &oid) != 0) goto blob_cleanup;

        {
            QFile f(outputPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) goto blob_cleanup;
            const void *data = git_blob_rawcontent(blob);
            qint64 size = static_cast<qint64>(git_blob_rawsize(blob));
            if (f.write(static_cast<const char*>(data), size) == size) ok = true;
            f.close();
        }

    blob_cleanup:
        if (blob) git_blob_free(blob);
        if (repo) git_repository_free(repo);
        return ok;
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

        // Map commit OID to branches and tags
        {
            QMap<QString, QStringList> commitBranches;
            QMap<QString, QStringList> commitTags;
            
            git_branch_iterator *branchIter = nullptr;
            if (git_branch_iterator_new(&branchIter, repo, GIT_BRANCH_LOCAL) == 0) {
                git_reference *ref = nullptr;
                git_branch_t type;
                while (git_branch_next(&ref, &type, branchIter) == 0) {
                    git_object *peeled = nullptr;
                    if (git_reference_peel(&peeled, ref, GIT_OBJECT_COMMIT) == 0) {
                        const git_oid *target = git_object_id(peeled);
                        char oid_str[GIT_OID_HEXSZ + 1];
                        git_oid_tostr(oid_str, sizeof(oid_str), target);
                        commitBranches[QString::fromLatin1(oid_str)] << QString::fromUtf8(git_reference_shorthand(ref));
                        git_object_free(peeled);
                    }
                    git_reference_free(ref);
                }
                git_branch_iterator_free(branchIter);
            }

            git_tag_foreach(repo, [](const char *name, git_oid *oid, void *payload) {
                auto *map = static_cast<QMap<QString, QStringList>*>(payload);
                char oid_str[GIT_OID_HEXSZ + 1];
                git_oid_tostr(oid_str, sizeof(oid_str), oid);
                // tag names come as refs/tags/Name
                QString tagName = QString::fromUtf8(name);
                if (tagName.startsWith(QLatin1String("refs/tags/"))) {
                    tagName = tagName.mid(10);
                }
                (*map)[QString::fromLatin1(oid_str)] << tagName;
                return 0;
            }, &commitTags);

            git_revwalk_new(&walker, repo);
            git_revwalk_sorting(walker, GIT_SORT_TIME);
            git_revwalk_push_glob(walker, "refs/heads/*");

            QString relativePath = QDir(QString::fromUtf8(git_repository_workdir(repo))).relativeFilePath(filePath);
            // Bind the UTF-8 byte array once and reuse it across iterations;
            // the pathspec pointer must remain valid for each
            // git_diff_tree_to_tree() call.
            QByteArray relBytes = relativePath.toUtf8();
            const char *pPathspec = relBytes.constData();
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
                        opts.pathspec.strings = (char **)&pPathspec;
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
                        entry.tags = commitTags.value(hashStr);
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

    if (makeSignature(&signature, repo) != 0) goto cleanup;

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

// ---------------------------------------------------------------------------
// Exploration graph
// ---------------------------------------------------------------------------

int GitService::getWordCountAtCommit(git_repository *repo, const QString &hash)
{
    git_oid oid;
    if (git_oid_fromstr(&oid, hash.toUtf8().constData()) != 0) return 0;

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &oid) != 0) return 0;

    git_tree *tree = nullptr;
    if (git_commit_tree(&tree, commit) != 0) {
        git_commit_free(commit);
        return 0;
    }

    // Look for the "manuscript" subtree
    git_tree_entry *msEntry = nullptr;
    if (git_tree_entry_bypath(&msEntry, tree, "manuscript") != 0) {
        git_tree_free(tree);
        git_commit_free(commit);
        return 0;
    }

    git_tree *msTree = nullptr;
    if (git_tree_entry_type(msEntry) != GIT_OBJECT_TREE ||
        git_tree_lookup(&msTree, repo, git_tree_entry_id(msEntry)) != 0) {
        git_tree_entry_free(msEntry);
        git_tree_free(tree);
        git_commit_free(commit);
        return 0;
    }
    git_tree_entry_free(msEntry);

    struct WalkPayload {
        git_repository *repo;
        int totalWords;
    } payload{repo, 0};

    git_tree_walk(msTree, GIT_TREEWALK_PRE, [](const char * /*root*/, const git_tree_entry *entry, void *data) -> int {
        auto *p = static_cast<WalkPayload *>(data);
        if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) return 0;

        git_blob *blob = nullptr;
        if (git_blob_lookup(&blob, p->repo, git_tree_entry_id(entry)) != 0) return 0;

        const char *content = static_cast<const char *>(git_blob_rawcontent(blob));
        git_object_size_t size = git_blob_rawsize(blob);

        bool inWord = false;
        for (git_object_size_t i = 0; i < size; ++i) {
            char c = content[i];
            bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (!ws && !inWord) {
                ++p->totalWords;
                inWord = true;
            } else if (ws) {
                inWord = false;
            }
        }

        git_blob_free(blob);
        return 0;
    }, &payload);

    git_tree_free(msTree);
    git_tree_free(tree);
    git_commit_free(commit);
    return payload.totalWords;
}

QFuture<QList<ExplorationNode>> GitService::getExplorationGraph(const QString &repoPath)
{
    return QtConcurrent::run([this, repoPath]() -> QList<ExplorationNode> {
        QList<ExplorationNode> nodes;
        git_repository *repo = nullptr;
        git_revwalk *walker = nullptr;

        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return nodes;
        if (git_revwalk_new(&walker, repo) != 0) {
            git_repository_free(repo);
            return nodes;
        }

        git_revwalk_sorting(walker, GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL);
        git_revwalk_push_glob(walker, "refs/heads/*");

        // Pass 1: walk all commits
        QMap<QString, int> hashToIndex;
        git_oid oid;
        while (git_revwalk_next(&oid, walker) == 0) {
            git_commit *commit = nullptr;
            if (git_commit_lookup(&commit, repo, &oid) != 0) continue;

            ExplorationNode node;
            char oid_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(oid_str, sizeof(oid_str), &oid);
            node.hash = QString::fromLatin1(oid_str);
            node.message = QString::fromUtf8(git_commit_message(commit));
            node.date = QDateTime::fromSecsSinceEpoch(git_commit_time(commit));

            unsigned int parentCount = git_commit_parentcount(commit);
            if (parentCount >= 1) {
                char parent_str[GIT_OID_HEXSZ + 1];
                git_oid_tostr(parent_str, sizeof(parent_str), git_commit_parent_id(commit, 0));
                node.primaryParentHash = QString::fromLatin1(parent_str);
            }
            if (parentCount >= 2) {
                char parent_str[GIT_OID_HEXSZ + 1];
                git_oid_tostr(parent_str, sizeof(parent_str), git_commit_parent_id(commit, 1));
                node.mergeParentHash = QString::fromLatin1(parent_str);
            }

            hashToIndex[node.hash] = nodes.size();
            nodes.append(node);
            git_commit_free(commit);
        }
        git_revwalk_free(walker);
        walker = nullptr;

        // Pass 2: assign branch names (main/master first)
        QStringList branchOrder;
        git_branch_iterator *branchIter = nullptr;
        if (git_branch_iterator_new(&branchIter, repo, GIT_BRANCH_LOCAL) == 0) {
            git_reference *ref = nullptr;
            git_branch_t type;
            while (git_branch_next(&ref, &type, branchIter) == 0) {
                QString name = QString::fromUtf8(git_reference_shorthand(ref));
                if (name == QLatin1String("main") || name == QLatin1String("master")) {
                    branchOrder.prepend(name);
                } else {
                    branchOrder.append(name);
                }
                git_reference_free(ref);
            }
            git_branch_iterator_free(branchIter);
        }

        for (const QString &branch : std::as_const(branchOrder)) {
            git_reference *ref = nullptr;
            if (git_branch_lookup(&ref, repo, branch.toUtf8().constData(), GIT_BRANCH_LOCAL) != 0) continue;

            git_object *peeled = nullptr;
            if (git_reference_peel(&peeled, ref, GIT_OBJECT_COMMIT) != 0) {
                git_reference_free(ref);
                continue;
            }
            const git_oid *tipOid = git_object_id(peeled);
            char tip_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(tip_str, sizeof(tip_str), tipOid);
            QString tipHash = QString::fromLatin1(tip_str);
            git_object_free(peeled);
            git_reference_free(ref);

            // Walk back from tip assigning branch name
            QString current = tipHash;
            while (!current.isEmpty()) {
                auto it = hashToIndex.constFind(current);
                if (it == hashToIndex.constEnd()) break;
                ExplorationNode &n = nodes[it.value()];
                if (!n.branchName.isEmpty()) break; // already claimed
                n.branchName = branch;
                current = n.primaryParentHash;
            }
        }

        // Pass 3: assign tags
        struct TagPayload {
            QMap<QString, int> *hashToIndex;
            QList<ExplorationNode> *nodes;
            git_repository *repo;
        } tagPayload{&hashToIndex, &nodes, repo};

        git_tag_foreach(repo, [](const char *name, git_oid *oid, void *data) -> int {
            auto *p = static_cast<TagPayload *>(data);
            QString tagName = QString::fromUtf8(name);
            if (tagName.startsWith(QLatin1String("refs/tags/")))
                tagName = tagName.mid(10);

            // Peel to commit (handles both lightweight and annotated tags)
            git_object *obj = nullptr;
            if (git_object_lookup(&obj, p->repo, oid, GIT_OBJECT_ANY) != 0) return 0;

            git_object *peeled = nullptr;
            if (git_object_peel(&peeled, obj, GIT_OBJECT_COMMIT) != 0) {
                git_object_free(obj);
                return 0;
            }

            const git_oid *commitOid = git_object_id(peeled);
            char oid_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(oid_str, sizeof(oid_str), commitOid);
            QString commitHash = QString::fromLatin1(oid_str);

            auto it = p->hashToIndex->constFind(commitHash);
            if (it != p->hashToIndex->constEnd()) {
                (*p->nodes)[it.value()].tags.append(tagName);
            }

            git_object_free(peeled);
            git_object_free(obj);
            return 0;
        }, &tagPayload);

        // Pass 4: word counts
        for (int i = 0; i < nodes.size(); ++i) {
            ExplorationNode &n = nodes[i];
            {
                QMutexLocker locker(&m_cacheMutex);
                auto cit = m_wordCountCache.constFind(n.hash);
                if (cit != m_wordCountCache.constEnd()) {
                    n.wordCount = cit.value();
                    continue;
                }
            }
            n.wordCount = getWordCountAtCommit(repo, n.hash);
            {
                QMutexLocker locker(&m_cacheMutex);
                m_wordCountCache[n.hash] = n.wordCount;
            }
        }

        // Compute wordCountDelta
        for (int i = 0; i < nodes.size(); ++i) {
            ExplorationNode &n = nodes[i];
            if (!n.primaryParentHash.isEmpty()) {
                auto it = hashToIndex.constFind(n.primaryParentHash);
                if (it != hashToIndex.constEnd()) {
                    n.wordCountDelta = n.wordCount - nodes[it.value()].wordCount;
                }
            }
        }

        git_repository_free(repo);
        return nodes;
    });
}

void GitService::scheduleWordCountForCommit(const QString &repoPath, const QString &hash)
{
    QThreadPool::globalInstance()->start([this, repoPath, hash]() {
        {
            QMutexLocker locker(&m_cacheMutex);
            if (m_wordCountCache.contains(hash)) return;
        }

        // Throttle concurrent libgit2 repository opens. Without this, a large
        // history can queue hundreds of tasks all opening the repo at once.
        m_wordCountSemaphore.acquire();

        git_repository *repo = nullptr;
        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) {
            m_wordCountSemaphore.release();
            return;
        }

        int count = getWordCountAtCommit(repo, hash);
        git_repository_free(repo);

        QMetaObject::invokeMethod(this, [this, hash, count] {
            QMutexLocker locker(&m_cacheMutex);
            m_wordCountCache[hash] = count;
        }, Qt::QueuedConnection);

        m_wordCountSemaphore.release();
    });
}

void GitService::loadWordCountCache(const QVariantMap &data)
{
    QMutexLocker locker(&m_cacheMutex);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        m_wordCountCache[it.key()] = it.value().toInt();
    }
}

QVariantMap GitService::saveWordCountCache() const
{
    QMutexLocker locker(&m_cacheMutex);
    QVariantMap data;
    for (auto it = m_wordCountCache.constBegin(); it != m_wordCountCache.constEnd(); ++it) {
        data[it.key()] = it.value();
    }
    return data;
}

// ---------------------------------------------------------------------------
// Conflict detection
// ---------------------------------------------------------------------------

QFuture<QList<ConflictFile>> GitService::getConflictingFiles(const QString &repoPath)
{
    return QtConcurrent::run([repoPath]() -> QList<ConflictFile> {
        QList<ConflictFile> result;
        git_repository *repo = nullptr;
        git_index *index = nullptr;
        git_index_conflict_iterator *iter = nullptr;

        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) goto conflict_cleanup;
        if (git_repository_index(&index, repo) != 0) goto conflict_cleanup;
        if (git_index_read(index, 0) != 0) goto conflict_cleanup;
        if (git_index_conflict_iterator_new(&iter, index) != 0) goto conflict_cleanup;

        {
            const git_index_entry *ancestor = nullptr;
            const git_index_entry *ours = nullptr;
            const git_index_entry *theirs = nullptr;

            while (git_index_conflict_next(&ancestor, &ours, &theirs, iter) == 0) {
                ConflictFile cf;
                if (ours) cf.path = QString::fromUtf8(ours->path);
                else if (theirs) cf.path = QString::fromUtf8(theirs->path);
                else if (ancestor) cf.path = QString::fromUtf8(ancestor->path);

                char oid_str[GIT_OID_HEXSZ + 1];
                if (ancestor) {
                    git_oid_tostr(oid_str, sizeof(oid_str), &ancestor->id);
                    cf.ancestorHash = QString::fromLatin1(oid_str);
                }
                if (ours) {
                    git_oid_tostr(oid_str, sizeof(oid_str), &ours->id);
                    cf.oursHash = QString::fromLatin1(oid_str);
                }
                if (theirs) {
                    git_oid_tostr(oid_str, sizeof(oid_str), &theirs->id);
                    cf.theirsHash = QString::fromLatin1(oid_str);
                }
                result.append(cf);
            }
        }

    conflict_cleanup:
        if (iter) git_index_conflict_iterator_free(iter);
        if (index) git_index_free(index);
        if (repo) git_repository_free(repo);
        return result;
    });
}

// ---------------------------------------------------------------------------
// Stash operations
// ---------------------------------------------------------------------------

QFuture<bool> GitService::stashChanges(const QString &repoPath, const QString &message)
{
    return QtConcurrent::run([repoPath, message]() -> bool {
        git_repository *repo = nullptr;
        git_signature *stasher = nullptr;
        git_oid stash_oid;
        bool success = false;

        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) goto stash_cleanup;
        if (makeSignature(&stasher, repo) != 0) goto stash_cleanup;

        if (git_stash_save(&stash_oid, repo, stasher, message.toUtf8().constData(), GIT_STASH_DEFAULT) == 0) {
            success = true;
        }

    stash_cleanup:
        if (stasher) git_signature_free(stasher);
        if (repo) git_repository_free(repo);
        return success;
    });
}

QList<StashEntry> GitService::listStashes(const QString &repoPath)
{
    QList<StashEntry> entries;
    git_repository *repo = nullptr;

    if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return entries;

    struct StashPayload {
        QList<StashEntry> *entries;
        git_repository *repo;
    } payload{&entries, repo};

    git_stash_foreach(repo, [](size_t index, const char *message, const git_oid *stash_id, void *data) -> int {
        auto *p = static_cast<StashPayload *>(data);
        StashEntry entry;
        entry.index = static_cast<int>(index);
        entry.message = QString::fromUtf8(message);

        char oid_str[GIT_OID_HEXSZ + 1];
        git_oid_tostr(oid_str, sizeof(oid_str), stash_id);
        entry.hash = QString::fromLatin1(oid_str);

        // Extract date from the stash commit
        git_commit *stashCommit = nullptr;
        if (git_commit_lookup(&stashCommit, p->repo, stash_id) == 0) {
            entry.date = QDateTime::fromSecsSinceEpoch(git_commit_time(stashCommit));
            git_commit_free(stashCommit);
        }

        // Parse onBranch from message pattern "Parked on '<branch>' — ..."
        int startIdx = entry.message.indexOf(QLatin1String("Parked on '"));
        if (startIdx >= 0) {
            startIdx += 11; // length of "Parked on '"
            int endIdx = entry.message.indexOf(QLatin1Char('\''), startIdx);
            if (endIdx > startIdx) {
                entry.onBranch = entry.message.mid(startIdx, endIdx - startIdx);
            }
        }

        p->entries->append(entry);
        return 0;
    }, &payload);

    git_repository_free(repo);
    return entries;
}

QFuture<bool> GitService::applyStash(const QString &repoPath, int stashIndex)
{
    return QtConcurrent::run([this, repoPath, stashIndex]() -> bool {
        if (hasUncommittedChanges(repoPath)) {
            Q_EMIT stashApplyBlockedByDirtyTree();
            return false;
        }

        // A stash apply rewrites the working tree; bracket the libgit2 ops
        // in an external-change window so the watcher reload happens once
        // via endExternalChange().
        ProjectManager::instance().beginExternalChange();
        struct EndGuard {
            ~EndGuard() { ProjectManager::instance().endExternalChange(); }
        } _endGuard;

        git_repository *repo = nullptr;
        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return false;

        git_stash_apply_options opts = GIT_STASH_APPLY_OPTIONS_INIT;
        opts.flags = GIT_STASH_APPLY_REINSTATE_INDEX;

        bool success = false;
        if (git_stash_apply(repo, static_cast<size_t>(stashIndex), &opts) == 0) {
            git_stash_drop(repo, static_cast<size_t>(stashIndex));
            success = true;
        }

        git_repository_free(repo);
        return success;
    });
}

QFuture<bool> GitService::dropStash(const QString &repoPath, int stashIndex)
{
    return QtConcurrent::run([repoPath, stashIndex]() -> bool {
        // dropStash doesn't rewrite the working tree, but the stash refs
        // inside .git/ change, and we want the behaviour consistent with
        // applyStash so consumers see the same post-op reload contract.
        ProjectManager::instance().beginExternalChange();
        struct EndGuard {
            ~EndGuard() { ProjectManager::instance().endExternalChange(); }
        } _endGuard;

        git_repository *repo = nullptr;
        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return false;

        bool success = (git_stash_drop(repo, static_cast<size_t>(stashIndex)) == 0);
        git_repository_free(repo);
        return success;
    });
}

// ---------------------------------------------------------------------------
// Exploration switching / creation / integration
// ---------------------------------------------------------------------------

QFuture<bool> GitService::switchExploration(const QString &repoPath, const QString &branchName)
{
    return m_pendingCommit.then(QtFuture::Launch::Async, [this, repoPath, branchName](bool commitOk) -> bool {
        if (!commitOk) {
            Q_EMIT explorationSwitchFailed(tr("Auto-save failed before switching explorations."));
            return false;
        }

        // Pause the filesystem watcher while libgit2 rewrites the working
        // tree; the matching endExternalChange() performs a single
        // reloadFromDisk() so the tree + metadata reflect the branch's
        // on-disk state.
        ProjectManager::instance().beginExternalChange();
        const bool ok = checkoutBranch(repoPath, branchName);
        ProjectManager::instance().endExternalChange();
        return ok;
    });
}

bool GitService::createExploration(const QString &repoPath, const QString &name)
{
    if (!createBranch(repoPath, name)) return false;
    return checkoutBranch(repoPath, name);
}

QFuture<bool> GitService::integrateExploration(const QString &repoPath, const QString &sourceBranch)
{
    return QtConcurrent::run([this, repoPath, sourceBranch]() -> bool {
        // Pause the filesystem watcher while libgit2 rewrites the working
        // tree. A merge can touch many files; without this, each write
        // would fire a watcher event and race the final reloadFromDisk()
        // scheduled by endExternalChange().
        ProjectManager::instance().beginExternalChange();
        // Ensure the window is closed on every exit path. Lambda capture
        // by value of repoPath is fine; we use a trivial struct guard.
        struct EndGuard {
            ~EndGuard() { ProjectManager::instance().endExternalChange(); }
        } _endGuard;

        if (!mergeBranch(repoPath, sourceBranch)) return false;

        git_repository *repo = nullptr;
        if (git_repository_open(&repo, repoPath.toUtf8().constData()) != 0) return false;

        bool success = true;
        int state = git_repository_state(repo);

        if (state == GIT_REPOSITORY_STATE_MERGE) {
            git_index *index = nullptr;
            if (git_repository_index(&index, repo) != 0) {
                // Cannot read index - merge state is broken; surface as failure.
                success = false;
            } else if (git_index_has_conflicts(index)) {
                // Caller resolves conflicts; integration is not successful yet.
                git_index_free(index);
                git_repository_free(repo);
                return false;
            } else {
                // Create merge commit.
                git_oid tree_oid, commit_oid;
                git_tree *tree = nullptr;
                git_signature *signature = nullptr;
                git_commit *headCommit = nullptr;
                git_commit *mergeCommit = nullptr;
                git_oid head_oid, merge_oid;

                if (git_index_write_tree(&tree_oid, index) != 0) { success = false; goto integrate_cleanup; }
                if (git_tree_lookup(&tree, repo, &tree_oid) != 0) { success = false; goto integrate_cleanup; }
                if (makeSignature(&signature, repo) != 0) { success = false; goto integrate_cleanup; }

                if (git_reference_name_to_id(&head_oid, repo, "HEAD") != 0) { success = false; goto integrate_cleanup; }
                if (git_commit_lookup(&headCommit, repo, &head_oid) != 0) { success = false; goto integrate_cleanup; }

                {
                    // Read MERGE_HEAD.
                    QByteArray mergeHeadPath = QByteArray(git_repository_path(repo)) + "MERGE_HEAD";
                    QFile mergeHeadFile(QString::fromUtf8(mergeHeadPath));
                    if (!mergeHeadFile.open(QIODevice::ReadOnly)) { success = false; goto integrate_cleanup; }
                    QByteArray mergeHeadHex = mergeHeadFile.readAll().trimmed();
                    mergeHeadFile.close();

                    if (git_oid_fromstr(&merge_oid, mergeHeadHex.constData()) != 0) { success = false; goto integrate_cleanup; }
                    if (git_commit_lookup(&mergeCommit, repo, &merge_oid) != 0) { success = false; goto integrate_cleanup; }

                    QString msg = QStringLiteral("Integrated: %1").arg(sourceBranch);
                    const git_commit *parents[2] = {headCommit, mergeCommit};

                    if (git_commit_create(&commit_oid, repo, "HEAD", signature, signature,
                                          nullptr, msg.toUtf8().constData(), tree,
                                          2, parents) != 0) {
                        success = false;
                    }
                }

            integrate_cleanup:
                if (mergeCommit) git_commit_free(mergeCommit);
                if (headCommit) git_commit_free(headCommit);
                if (signature) git_signature_free(signature);
                if (tree) git_tree_free(tree);
                git_index_free(index);
            }

            // Only clear MERGE_HEAD/MERGE_MSG on success. On failure we leave
            // the repository in its merge state so the user can manually
            // resolve via the conflict banner; discarding the markers here
            // would strand a merged index without any way to finish the merge.
            if (success) {
                git_repository_state_cleanup(repo);
            }
        }

        // Schedule word count for the new HEAD commit
        if (success) {
            git_oid head_oid;
            if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
                char oid_str[GIT_OID_HEXSZ + 1];
                git_oid_tostr(oid_str, sizeof(oid_str), &head_oid);
                QString newHash = QString::fromLatin1(oid_str);
                git_repository_free(repo);
                scheduleWordCountForCommit(repoPath, newHash);
                return true;
            }
        }

        git_repository_free(repo);
        return success;
    });
}
