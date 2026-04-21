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

#ifndef VERSIONRECALLBROWSER_H
#define VERSIONRECALLBROWSER_H

#include <QDialog>
#include <QVariantMap>

class QTableView;
class QTextEdit;
class QPushButton;
class QLabel;
class QStandardItemModel;

/**
 * \brief Modal dialog for browsing a file's commit history and recalling a past version.
 *
 * Presents a table of commits that touched the file, colour-coded by branch
 * lane using the supplied colour map, with a read-only preview of the file
 * content at the selected commit.  When the user clicks "Recall", the dialog
 * emits versionSelected() and closes; the caller is responsible for
 * replacing the live file with the recalled content.
 *
 * \sa GitService::getHistory(), GitService::extractBlob()
 */
class VersionRecallBrowser : public QDialog
{
    Q_OBJECT
public:
    /**
     * \brief Constructs the dialog and begins loading commit history.
     *
     * History is loaded asynchronously via GitService::getHistory() so the
     * dialog opens immediately.  The table is populated once the future
     * resolves on the main thread.
     *
     * \param filePath   Absolute path to the file whose history is displayed.
     * \param repoPath   Absolute path to the root of the Git repository.
     * \param laneColors Map of branch name strings to colour strings (e.g.
     *                   "#a0c4ff") used to tint commit rows by branch.
     *                   Pass an empty map to use default colours.
     * \param parent     Optional parent widget.
     */
    explicit VersionRecallBrowser(const QString &filePath,
                                   const QString &repoPath,
                                   const QVariantMap &laneColors,
                                   QWidget *parent = nullptr);
    ~VersionRecallBrowser() override;

Q_SIGNALS:
    /**
     * \brief Emitted when the user confirms recall of a specific version.
     *
     * Fired on the main thread immediately before the dialog is accepted.
     * The caller is responsible for extracting the blob and replacing the
     * live file; the dialog does not perform the file replacement itself.
     *
     * \param filePath   Absolute path to the file being recalled (same value
     *                   passed to the constructor).
     * \param commitHash Full commit OID from which the file should be restored.
     */
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
