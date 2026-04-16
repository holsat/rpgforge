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

#ifndef VERSIONRECALLBROWSER_H
#define VERSIONRECALLBROWSER_H

#include <QDialog>
#include <QVariantMap>

class QTableView;
class QTextEdit;
class QPushButton;
class QLabel;
class QStandardItemModel;

class VersionRecallBrowser : public QDialog
{
    Q_OBJECT
public:
    // filePath: absolute path to the file to browse history for
    // repoPath: root of the git repo
    explicit VersionRecallBrowser(const QString &filePath,
                                   const QString &repoPath,
                                   const QVariantMap &laneColors,  // branchName->colorString
                                   QWidget *parent = nullptr);
    ~VersionRecallBrowser() override;

Q_SIGNALS:
    // Emitted when user confirms recall; caller is responsible for the file replacement
    void versionSelected(const QString &filePath, const QString &commitHash);

private Q_SLOTS:
    void onRowSelected(const QModelIndex &index);
    void onRecallClicked();

private:
    void setupUi();
    void loadHistory();
    void loadPreview(const QString &hash);

    QString m_filePath;
    QString m_repoPath;
    QVariantMap m_laneColors;   // branchName -> "#rrggbb"

    QTableView        *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QTextEdit         *m_preview = nullptr;
    QPushButton       *m_recallBtn = nullptr;
    QLabel            *m_previewLabel = nullptr;

    // Currently selected hash
    QString m_selectedHash;

    // Temp file for preview extraction
    QString m_tempPreviewPath;
};

#endif // VERSIONRECALLBROWSER_H
