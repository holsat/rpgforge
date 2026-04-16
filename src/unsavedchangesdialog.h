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

class UnsavedChangesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UnsavedChangesDialog(const QString &currentBranch,
                                   const QString &targetBranch,
                                   QWidget *parent = nullptr);

Q_SIGNALS:
    void saveRequested(const QString &milestoneMessage);
    void stashRequested(const QString &stashMessage);

private:
    void setupUi(const QString &currentBranch, const QString &targetBranch);

    QLineEdit *m_messageEdit = nullptr;
};

#endif // UNSAVEDCHANGESDIALOG_H
