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

#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QString>
#include <QList>
#include "gitservice.h"

class QTableWidget;
class QPushButton;

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(const QString &filePath, QWidget *parent = nullptr);
    ~HistoryDialog() override;

Q_SIGNALS:
    void viewVersion(const QString &hash, int index, const QDateTime &date, const QStringList &tags);
    void restoreVersion(const QString &hash);
    void compareVersion(const QString &hash);

private Q_SLOTS:
    void refresh();
    void onViewClicked();
    void onRestoreClicked();
    void onCompareClicked();
    void onTagClicked();

private:
    void setupUi();
    void updateTable(const QList<VersionInfo> &history);

    QString m_filePath;
    QList<VersionInfo> m_history;
    QTableWidget *m_table;
    QPushButton *m_viewBtn;
    QPushButton *m_restoreBtn;
    QPushButton *m_compareBtn;
    QPushButton *m_tagBtn;
};

#endif // HISTORYDIALOG_H
