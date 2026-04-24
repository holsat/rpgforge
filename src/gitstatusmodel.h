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
