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

#ifndef GITHUBONBOARDINGDIALOG_H
#define GITHUBONBOARDINGDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QLabel;

/**
 * @brief Dialog to guide the user through GitHub setup and remote repository creation.
 */
class GitHubOnboardingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GitHubOnboardingDialog(QWidget *parent = nullptr);
    ~GitHubOnboardingDialog() override;

    QString token() const;
    QString repoName() const;
    bool isPrivate() const;

private Q_SLOTS:
    void validate();
    void onRepoCreated(const QString &cloneUrl);
    void onError(const QString &message);

private:
    void setupUi();

    QLineEdit *m_tokenEdit;
    QLineEdit *m_repoNameEdit;
    QCheckBox *m_privateCheck;
    QLabel *m_statusLabel;
    QPushButton *m_connectBtn;
};

#endif // GITHUBONBOARDINGDIALOG_H
