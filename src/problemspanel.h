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

#ifndef PROBLEMSPANEL_H
#define PROBLEMSPANEL_H

#include <QWidget>
#include "analyzerservice.h"

class QTableWidget;
class QComboBox;
class QCheckBox;

/**
 * @brief Bottom dock panel displaying project-wide diagnostics.
 */
class ProblemsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProblemsPanel(QWidget *parent = nullptr);
    ~ProblemsPanel() override;

public Q_SLOTS:
    /**
     * @brief Tell the panel which document is currently open in the
     * editor. Diagnostics for this path are sorted to the top of the
     * list, and (if "Show only current document" is checked) other
     * diagnostics are hidden. Pass an empty string when no document is
     * open.
     */
    void setCurrentDocument(const QString &filePath);

Q_SIGNALS:
    void issueActivated(const QString &filePath, int line);
    void statsChanged(int errors, int warnings, int infos);

private Q_SLOTS:
    void onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);
    void onItemClicked(int row, int column);
    void onItemDoubleClicked(int row, int column);
    void onFilterChanged();
    void onCurrentOnlyToggled(bool checked);
    void persistColumnWidth(int logicalIndex, int oldSize, int newSize);

private:
    void setupUi();
    void refreshTable();
    void loadColumnWidths();

    QTableWidget *m_table;
    QComboBox *m_filterCombo;
    // When checked, refreshTable() filters out diagnostics whose
    // filePath does not equal m_currentDocumentPath. State is persisted
    // in QSettings under "analyzer/show_current_only".
    QCheckBox *m_currentOnlyCheck = nullptr;

    // Maps file path -> list of diagnostics
    QMap<QString, QList<Diagnostic>> m_diagnosticsMap;

    // Absolute path of the document currently open in the editor.
    // Empty when no document is open. Used for sort-priority + filter.
    QString m_currentDocumentPath;
};

#endif // PROBLEMSPANEL_H
