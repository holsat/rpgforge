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

#ifndef COMPILEDIALOG_H
#define COMPILEDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;

struct CompileOptions {
    bool filterByStatus = false;
    QString minStatus = QStringLiteral("Draft");
    bool applyStylesheet = true;
    bool createTOC = false;
    bool showPageNumbers = true;
};

class CompileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompileDialog(QWidget *parent = nullptr);
    ~CompileDialog() override;

    CompileOptions options() const;

private:
    void setupUi();

    QCheckBox *m_filterCheck = nullptr;
    QComboBox *m_statusCombo = nullptr;
    QCheckBox *m_stylesheetCheck = nullptr;
    QCheckBox *m_tocCheck = nullptr;
    QCheckBox *m_pageNumbersCheck = nullptr;
};

#endif // COMPILEDIALOG_H
