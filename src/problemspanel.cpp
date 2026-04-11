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

#include "problemspanel.h"
#include "diagnosticdetaildialog.h"
#include "projectmanager.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QIcon>
#include <QDir>
#include <QLabel>

ProblemsPanel::ProblemsPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connect(&AnalyzerService::instance(), &AnalyzerService::diagnosticsUpdated, this, &ProblemsPanel::onDiagnosticsUpdated);
}

ProblemsPanel::~ProblemsPanel() = default;

void ProblemsPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(5, 5, 5, 5);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItems({i18n("All Issues"), i18n("Errors Only"), i18n("Warnings Only"), i18n("Info Only")});
    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, &ProblemsPanel::onFilterChanged);
    
    toolbarLayout->addWidget(new QLabel(i18n("Filter:"), this));
    toolbarLayout->addWidget(m_filterCombo);
    toolbarLayout->addStretch();

    layout->addLayout(toolbarLayout);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({i18n("Severity"), i18n("Message"), i18n("File"), i18n("Line")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    
    // Enable word wrap and dynamic row height to prevent cutoff
    m_table->setWordWrap(true);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    connect(m_table, &QTableWidget::cellClicked, this, &ProblemsPanel::onItemClicked);
    connect(m_table, &QTableWidget::cellActivated, this, &ProblemsPanel::onItemClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ProblemsPanel::onItemDoubleClicked);

    layout->addWidget(m_table);
}

void ProblemsPanel::onDiagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics)
{
    m_diagnosticsMap[filePath] = diagnostics;
    refreshTable();
}

void ProblemsPanel::onFilterChanged()
{
    refreshTable();
}

void ProblemsPanel::refreshTable()
{
    m_table->setRowCount(0);

    int filterMode = m_filterCombo->currentIndex(); // 0=All, 1=Errors, 2=Warnings, 3=Info
    int totalErrors = 0;
    int totalWarnings = 0;
    int totalInfos = 0;

    for (auto it = m_diagnosticsMap.constBegin(); it != m_diagnosticsMap.constEnd(); ++it) {
        QString filePath = it.key();
        QString relPath = QDir(ProjectManager::instance().projectPath()).relativeFilePath(filePath);

        for (const Diagnostic &d : it.value()) {
            // Check if suppressed globally
            if (AnalyzerService::instance().isSuppressed(d.message)) continue;

            if (d.severity == DiagnosticSeverity::Error) totalErrors++;
            else if (d.severity == DiagnosticSeverity::Warning) totalWarnings++;
            else totalInfos++;

            if (filterMode == 1 && d.severity != DiagnosticSeverity::Error) continue;
            if (filterMode == 2 && d.severity != DiagnosticSeverity::Warning) continue;
            if (filterMode == 3 && d.severity != DiagnosticSeverity::Info) continue;

            int row = m_table->rowCount();
            m_table->insertRow(row);

            QIcon icon;
            QString sevLabel;
            if (d.severity == DiagnosticSeverity::Error) {
                icon = QIcon::fromTheme(QStringLiteral("dialog-error"));
                sevLabel = i18n("Error");
            } else if (d.severity == DiagnosticSeverity::Warning) {
                icon = QIcon::fromTheme(QStringLiteral("dialog-warning"));
                sevLabel = i18n("Warning");
            } else {
                icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
                sevLabel = i18n("Info");
            }

            auto *sevItem = new QTableWidgetItem(icon, sevLabel);
            // Store diagnostic in UserRole for the double-click dialog
            sevItem->setData(Qt::UserRole, QVariant::fromValue(row)); // Dummy, we use map lookups later

            auto *msgItem = new QTableWidgetItem(d.message);
            auto *fileItem = new QTableWidgetItem(relPath);
            fileItem->setData(Qt::UserRole, filePath); // Store absolute path
            auto *lineItem = new QTableWidgetItem(QString::number(d.line + 1));
            lineItem->setData(Qt::UserRole, d.line); // Store 0-based index

            m_table->setItem(row, 0, sevItem);
            m_table->setItem(row, 1, msgItem);
            m_table->setItem(row, 2, fileItem);
            m_table->setItem(row, 3, lineItem);
        }
    }

    Q_EMIT statsChanged(totalErrors, totalWarnings, totalInfos);
}

void ProblemsPanel::onItemDoubleClicked(int row, int /*column*/)
{
    auto *fileItem = m_table->item(row, 2);
    auto *lineItem = m_table->item(row, 3);
    auto *msgItem = m_table->item(row, 1);
    auto *sevItem = m_table->item(row, 0);
    
    if (!fileItem || !lineItem || !msgItem) return;

    QString path = fileItem->data(Qt::UserRole).toString();
    int line = lineItem->data(Qt::UserRole).toInt();
    QString message = msgItem->text();

    // Reconstruct diagnostic for dialog
    Diagnostic d;
    d.filePath = path;
    d.line = line;
    d.message = message;
    QString sevText = sevItem->text();
    if (sevText == i18n("Error")) d.severity = DiagnosticSeverity::Error;
    else if (sevText == i18n("Warning")) d.severity = DiagnosticSeverity::Warning;
    else d.severity = DiagnosticSeverity::Info;

    DiagnosticDetailDialog dialog(d, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.selectedAction() == DiagnosticDetailDialog::Ignore || dialog.selectedAction() == DiagnosticDetailDialog::Fixed) {
            // Suppress the message globally
            AnalyzerService::instance().suppressDiagnostic(d.message);
            
            // Remove from local cache and refresh
            QList<Diagnostic> &list = m_diagnosticsMap[path];
            list.removeIf([&](const Diagnostic &ld) { return ld.message == message; });
            refreshTable();
            return;
        }
    }

    // Default action: jump to line
    Q_EMIT issueActivated(path, line);
}

void ProblemsPanel::onItemClicked(int row, int column)
{
    Q_UNUSED(column);
    auto *fileItem = m_table->item(row, 2);
    auto *lineItem = m_table->item(row, 3);
    if (fileItem && lineItem) {
        QString path = fileItem->data(Qt::UserRole).toString();
        int line = lineItem->data(Qt::UserRole).toInt();
        // Convert from 1-based AI line to 0-based Editor line
        Q_EMIT issueActivated(path, qMax(0, line - 1));
    }
}
