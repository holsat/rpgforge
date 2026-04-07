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

#ifndef GITHUBSERVICE_H
#define GITHUBSERVICE_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

/**
 * @brief Service for GitHub API interactions and secure token storage via KWallet.
 */
class GitHubService : public QObject
{
    Q_OBJECT

public:
    static GitHubService& instance();

    /**
     * @brief Securely stores the GitHub Personal Access Token in KWallet.
     */
    void setToken(const QString &token);

    /**
     * @brief Retrieves the GitHub token from KWallet.
     */
    QString token() const;

    /**
     * @brief Creates a new remote repository on GitHub for the current project.
     * @param repoName Name of the repository.
     * @param isPrivate Whether the repo should be private.
     */
    void createRemoteRepo(const QString &repoName, bool isPrivate = true);

    /**
     * @brief Checks if a token is already stored and valid.
     */
    bool hasValidToken() const { return !token().isEmpty(); }

Q_SIGNALS:
    void repoCreated(const QString &cloneUrl);
    void errorOccurred(const QString &message);

private:
    explicit GitHubService(QObject *parent = nullptr);
    ~GitHubService() override;

    GitHubService(const GitHubService&) = delete;
    GitHubService& operator=(const GitHubService&) = delete;

    QNetworkAccessManager *m_networkManager;

    // Token cache: avoids synchronous KWallet open on every request.
    // Invalidated when setToken() is called.
    mutable QString m_tokenCache;
};

#endif // GITHUBSERVICE_H
