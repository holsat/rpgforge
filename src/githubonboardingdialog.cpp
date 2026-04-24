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

#include "githubonboardingdialog.h"
#include "githubservice.h"
#include "projectmanager.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

GitHubOnboardingDialog::GitHubOnboardingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Connect to GitHub"));
    setupUi();

    connect(&GitHubService::instance(), &GitHubService::repoCreated, this, &GitHubOnboardingDialog::onRepoCreated);
    connect(&GitHubService::instance(), &GitHubService::errorOccurred, this, &GitHubOnboardingDialog::onError);
}

GitHubOnboardingDialog::~GitHubOnboardingDialog() = default;

void GitHubOnboardingDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(i18n("To back up your project to the cloud, you need a GitHub Personal Access Token."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *linkBtn = new QPushButton(i18n("Generate Token on GitHub..."), this);
    linkBtn->setIcon(QIcon::fromTheme(QStringLiteral("internet-services")));
    connect(linkBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/settings/tokens/new?description=RPGForge&scopes=repo")));
    });
    layout->addWidget(linkBtn);

    auto *form = new QVBoxLayout();
    
    form->addWidget(new QLabel(i18n("Personal Access Token:"), this));
    m_tokenEdit = new QLineEdit(this);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setText(GitHubService::instance().token());
    form->addWidget(m_tokenEdit);

    form->addWidget(new QLabel(i18n("GitHub Repository Name:"), this));
    m_repoNameEdit = new QLineEdit(this);
    QString suggestedName = ProjectManager::instance().projectName().toLower();
    suggestedName.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    if (suggestedName.startsWith(QLatin1Char('-'))) suggestedName.remove(0, 1);
    if (suggestedName.endsWith(QLatin1Char('-'))) suggestedName.chop(1);
    m_repoNameEdit->setText(suggestedName);
    form->addWidget(m_repoNameEdit);

    m_privateCheck = new QCheckBox(i18n("Private Repository"), this);
    m_privateCheck->setChecked(true);
    form->addWidget(m_privateCheck);

    layout->addLayout(form);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    layout->addWidget(m_statusLabel);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    auto *cancelBtn = new QPushButton(i18n("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancelBtn);

    m_connectBtn = new QPushButton(i18n("Sync to GitHub"), this);
    m_connectBtn->setDefault(true);
    connect(m_connectBtn, &QPushButton::clicked, this, &GitHubOnboardingDialog::validate);
    buttons->addWidget(m_connectBtn);

    layout->addLayout(buttons);
}

void GitHubOnboardingDialog::validate()
{
    if (m_tokenEdit->text().isEmpty()) {
        m_statusLabel->setText(i18n("Please enter a token."));
        return;
    }
    if (m_repoNameEdit->text().isEmpty()) {
        m_statusLabel->setText(i18n("Please enter a repository name."));
        return;
    }

    m_statusLabel->setStyleSheet(QStringLiteral("color: blue;"));
    m_statusLabel->setText(i18n("Connecting to GitHub..."));
    m_connectBtn->setEnabled(false);

    // Save token for future use
    GitHubService::instance().setToken(m_tokenEdit->text());
    
    // Create the repo
    GitHubService::instance().createRemoteRepo(m_repoNameEdit->text(), m_privateCheck->isChecked());
}

void GitHubOnboardingDialog::onRepoCreated(const QString &cloneUrl)
{
    Q_UNUSED(cloneUrl);
    accept();
}

void GitHubOnboardingDialog::onError(const QString &message)
{
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    m_statusLabel->setText(message);
    m_connectBtn->setEnabled(true);
    m_connectBtn->setText(i18n("Retry Sync"));
}

QString GitHubOnboardingDialog::token() const { return m_tokenEdit->text(); }
QString GitHubOnboardingDialog::repoName() const { return m_repoNameEdit->text(); }
bool GitHubOnboardingDialog::isPrivate() const { return m_privateCheck->isChecked(); }
