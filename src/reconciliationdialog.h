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

#ifndef RECONCILIATIONDIALOG_H
#define RECONCILIATIONDIALOG_H

#include <QAbstractTableModel>
#include <QDialog>
#include <QList>

#include "reconciliationtypes.h"

class QTableView;
class QStyledItemDelegate;

/**
 * \brief Table model backing ReconciliationDialog.
 *
 * Columns (left to right):
 *   0: Path          (read-only, project-relative)
 *   1: Display Name  (read-only)
 *   2: Suggestion    (read-only, italic when non-empty)
 *   3: Action        (editable via combobox delegate)
 *
 * Exposes the mutable underlying QList<ReconciliationEntry> through entries()
 * so the dialog's Apply handler can read back the user's decisions.
 */
class ReconciliationModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        PathColumn = 0,
        DisplayNameColumn,
        SuggestionColumn,
        ActionColumn,
        ColumnCount
    };

    explicit ReconciliationModel(QList<ReconciliationEntry> entries, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orient, int role = Qt::DisplayRole) const override;

    const QList<ReconciliationEntry>& entries() const { return m_entries; }

    /** Bulk: set the same action on every row. Also clears resolvedPath if
     *  the target action isn't Locate (since resolvedPath is only meaningful
     *  for Locate). */
    void setAllActions(ReconciliationEntry::Action action);

    /** Bulk: for every row, if suggestedPath is non-empty, set action=Locate
     *  and resolvedPath=suggestedPath. Rows without a suggestion are left
     *  alone. */
    void acceptAllSuggestions();

    /** For one row, set the resolvedPath and flip the action to Locate. */
    void setResolved(int row, const QString &resolvedPath);

private:
    QList<ReconciliationEntry> m_entries;
};

/**
 * \brief Batch dialog shown when validateTree() discovers tree entries that
 *        don't exist on disk.
 *
 * The user picks a per-row action (Locate / Remove / Recreate empty) or uses
 * the bulk buttons to apply the same action to every row. On Apply, the
 * dialog accepts and MainWindow reads back entries() and drives the matching
 * ProjectManager mutations inside a beginBatch()/endBatch() window.
 */
class ReconciliationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ReconciliationDialog(const QList<ReconciliationEntry> &entries,
                                  QWidget *parent = nullptr);

    /** Returns the current (possibly mutated) entry list. Call after exec()
     *  succeeds. */
    QList<ReconciliationEntry> entries() const;

private Q_SLOTS:
    void onLocateAllUnderDirectory();
    void onRemoveAll();
    void onAcceptAllSuggestions();
    void onRowDoubleClicked(const QModelIndex &index);

private:
    void locateRowInteractively(int row);

    ReconciliationModel *m_model = nullptr;
    QTableView *m_table = nullptr;
};

#endif // RECONCILIATIONDIALOG_H
