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

#ifndef CLONEDIALOG_H
#define CLONEDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;

class CloneDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CloneDialog(QWidget *parent = nullptr);
    ~CloneDialog() override;

    QString url() const;
    QString localPath() const;

private:
    void setupUi();

    QLineEdit *m_urlEdit;
    QLineEdit *m_pathEdit;
};

#endif // CLONEDIALOG_H
