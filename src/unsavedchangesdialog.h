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

#ifndef UNSAVEDCHANGESDIALOG_H
#define UNSAVEDCHANGESDIALOG_H

#include <QDialog>

class QLineEdit;

/**
 * \brief Dialog that resolves uncommitted changes before an exploration switch.
 *
 * Shown when the user attempts to switch to a different exploration branch
 * while the working tree is dirty.  The dialog presents two resolution paths:
 * commit the changes as a milestone, or park them as a stash entry.  It
 * emits exactly one of its two signals when the user confirms a choice.
 *
 * \sa GitService::switchExploration(), GitService::stashChanges()
 */
class UnsavedChangesDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * \brief Constructs the dialog describing the pending branch switch.
     *
     * The dialog text names both branches so the user understands the
     * context of the choice.
     *
     * \param currentBranch Name of the branch currently checked out.
     * \param targetBranch  Name of the branch the user wants to switch to.
     * \param parent        Optional parent widget.
     */
    explicit UnsavedChangesDialog(const QString &currentBranch,
                                   const QString &targetBranch,
                                   QWidget *parent = nullptr);

Q_SIGNALS:
    /**
     * \brief Emitted when the user chooses to commit changes as a milestone.
     *
     * Fired on the main thread when the user confirms the "Save as Milestone"
     * action.  The caller should pass the message to GitService::commitAll().
     *
     * \param milestoneMessage User-supplied commit message for the milestone.
     */
    void saveRequested(const QString &milestoneMessage);

    /**
     * \brief Emitted when the user chooses to park changes as a stash entry.
     *
     * Fired on the main thread when the user confirms the "Park Changes"
     * action.  The caller should pass the message to GitService::stashChanges().
     *
     * \param stashMessage User-supplied description for the stash entry.
     */
    void stashRequested(const QString &stashMessage);

private:
    void setupUi(const QString &currentBranch, const QString &targetBranch);

    QLineEdit *m_messageEdit = nullptr;
};

#endif // UNSAVEDCHANGESDIALOG_H
