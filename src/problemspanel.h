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

#ifndef PROBLEMSPANEL_H
#define PROBLEMSPANEL_H

#include <QWidget>
#include "analyzerservice.h"

class QTableWidget;
class QComboBox;

/**
 * @brief Bottom dock panel displaying project-wide diagnostics.
 */
class ProblemsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProblemsPanel(QWidget *parent = nullptr);
    ~ProblemsPanel() override;

Q_SIGNALS:
    void issueActivated(const QString &filePath, int line);
    void statsChanged(int errors, int warnings, int infos);

private Q_SLOTS:
    void onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);
    void onItemDoubleClicked(int row, int column);
    void onFilterChanged();

private:
    void setupUi();
    void refreshTable();

    QTableWidget *m_table;
    QComboBox *m_filterCombo;

    // Maps file path -> list of diagnostics
    QMap<QString, QList<Diagnostic>> m_diagnosticsMap;
};

#endif // PROBLEMSPANEL_H
