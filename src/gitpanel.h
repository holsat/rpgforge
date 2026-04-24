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

#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>

class QLabel;
class QPushButton;

/**
 * @brief Sidebar panel showing live Git status for the current project.
 *
 * Displays the current branch, whether there are uncommitted changes,
 * the last commit message, and buttons to commit all and push.
 */
class GitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitPanel(QWidget *parent = nullptr);
    ~GitPanel() override;

    /**
     * @brief Sets the root path of the project and refreshes the panel.
     */
    void setRootPath(const QString &path);

    /**
     * @brief Refreshes the panel from the current project path.
     */
    void refresh();

private Q_SLOTS:
    void onCommitAll();
    void onPush();

private:
    void setupUi();

    QLabel *m_branchLabel = nullptr;
    QLabel *m_changesLabel = nullptr;
    QLabel *m_lastCommitLabel = nullptr;
    QPushButton *m_commitBtn = nullptr;
    QPushButton *m_pushBtn = nullptr;

    QString m_rootPath;
};

#endif // GITPANEL_H
