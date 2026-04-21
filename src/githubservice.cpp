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

#include "githubservice.h"
#include <kwallet.h>
#include <KLocalizedString>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>

GitHubService& GitHubService::instance()
{
    static GitHubService s_instance;
    return s_instance;
}

GitHubService::GitHubService(QObject *parent)
    : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
}

GitHubService::~GitHubService() = default;

void GitHubService::setToken(const QString &token)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Asynchronous);
    if (!wallet) return;

    connect(wallet, &KWallet::Wallet::walletOpened, this, [this, wallet, token](bool opened) {
        if (opened) {
            if (!wallet->hasFolder(QStringLiteral("RPGForge"))) {
                wallet->createFolder(QStringLiteral("RPGForge"));
            }
            wallet->setFolder(QStringLiteral("RPGForge"));
            wallet->writePassword(QStringLiteral("GitHubToken"), token);
        }
        wallet->deleteLater();
        // Invalidate cache so next read fetches the new value
        m_tokenCache.clear();
    });
}

QString GitHubService::token() const
{
    // Return cached token if available, avoiding a synchronous KWallet open
    if (!m_tokenCache.isEmpty()) return m_tokenCache;

    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) return QString();

    if (wallet->hasFolder(QStringLiteral("RPGForge"))) {
        wallet->setFolder(QStringLiteral("RPGForge"));
        QString tok;
        if (wallet->readPassword(QStringLiteral("GitHubToken"), tok) == 0) {
            m_tokenCache = tok;
            delete wallet;
            return tok;
        }
    }
    delete wallet;
    return QString();
}

void GitHubService::createRemoteRepo(const QString &repoName, bool isPrivate)
{
    QString pat = token();
    if (pat.isEmpty()) {
        Q_EMIT errorOccurred(QStringLiteral("No GitHub token found. Please set one up first."));
        return;
    }

    QUrl url(QStringLiteral("https://api.github.com/user/repos"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + pat.toUtf8());
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "RPGForge-IDE");

    QJsonObject body;
    body[QStringLiteral("name")] = repoName;
    body[QStringLiteral("private")] = isPrivate;
    body[QStringLiteral("description")] = QStringLiteral("RPG project created with RPG Forge.");

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString cloneUrl = doc.object().value(QStringLiteral("clone_url")).toString();
            Q_EMIT repoCreated(cloneUrl);
        } else {
            QByteArray response = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            QString detail = doc.object().value(QStringLiteral("message")).toString();
            
            if (detail.isEmpty()) {
                Q_EMIT errorOccurred(reply->errorString());
            } else {
                // Check for specific common errors like "name already exists"
                if (detail.contains(QLatin1String("already exists"), Qt::CaseInsensitive)) {
                    Q_EMIT errorOccurred(i18n("A repository with this name already exists on your GitHub account."));
                } else {
                    Q_EMIT errorOccurred(i18n("GitHub Error: %1", detail));
                }
            }
        }
        reply->deleteLater();
    });
}
