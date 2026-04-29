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

#include "problemspanel.h"
#include "diagnosticdetaildialog.h"
#include "projectmanager.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>
#include <QIcon>
#include <QDir>
#include <QLabel>
#include <QSettings>

namespace {
// Default column widths in pixels. Used when no persisted width is
// recorded — Severity and Line are narrow utility columns; File gets a
// reasonable initial width and Message takes the rest.
constexpr int kDefaultSeverityWidth = 90;
constexpr int kDefaultFileWidth     = 240;
constexpr int kDefaultLineWidth     = 60;
constexpr int kDefaultMessageWidth  = 480;

QString columnSettingsKey(int logicalIndex)
{
    return QStringLiteral("analyzer/column_width_%1").arg(logicalIndex);
}
} // namespace

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

    // "Show only current document" — when checked, the table is
    // restricted to diagnostics whose filePath matches the currently
    // open editor doc. Persisted across sessions.
    m_currentOnlyCheck = new QCheckBox(i18n("Show only messages from current document"), this);
    {
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        m_currentOnlyCheck->setChecked(
            s.value(QStringLiteral("analyzer/show_current_only"), false).toBool());
    }
    connect(m_currentOnlyCheck, &QCheckBox::toggled, this, &ProblemsPanel::onCurrentOnlyToggled);
    toolbarLayout->addSpacing(12);
    toolbarLayout->addWidget(m_currentOnlyCheck);

    auto *titleLabel = new QLabel(i18n("<b>Analyzer Service</b>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(titleLabel);
    toolbarLayout->addStretch();

    layout->addLayout(toolbarLayout);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({i18n("Severity"), i18n("Message"), i18n("File"), i18n("Line")});

    // All columns user-resizable. The previous layout pinned Message
    // (col 1) to Stretch, which hid the divider and prevented manual
    // resize. Interactive on every column lets the user grab any edge;
    // persisted widths are loaded next.
    auto *header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setSectionsMovable(false);
    loadColumnWidths();
    connect(header, &QHeaderView::sectionResized,
            this, &ProblemsPanel::persistColumnWidth);

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

void ProblemsPanel::loadColumnWidths()
{
    QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const int defaults[4] = {
        kDefaultSeverityWidth, kDefaultMessageWidth,
        kDefaultFileWidth, kDefaultLineWidth,
    };
    for (int i = 0; i < 4; ++i) {
        const int w = s.value(columnSettingsKey(i), defaults[i]).toInt();
        if (w > 10) m_table->setColumnWidth(i, w);
    }
}

void ProblemsPanel::persistColumnWidth(int logicalIndex, int /*oldSize*/, int newSize)
{
    if (logicalIndex < 0 || logicalIndex > 3) return;
    if (newSize <= 10) return; // ignore collapse-to-zero
    QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    s.setValue(columnSettingsKey(logicalIndex), newSize);
}

void ProblemsPanel::setCurrentDocument(const QString &filePath)
{
    if (m_currentDocumentPath == filePath) return;
    m_currentDocumentPath = filePath;
    refreshTable();
}

void ProblemsPanel::onCurrentOnlyToggled(bool checked)
{
    QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    s.setValue(QStringLiteral("analyzer/show_current_only"), checked);
    refreshTable();
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
    const bool currentOnly = m_currentOnlyCheck && m_currentOnlyCheck->isChecked();
    int totalErrors = 0;
    int totalWarnings = 0;
    int totalInfos = 0;

    // Build two ordered buckets so current-document diagnostics appear
    // at the top regardless of QMap's natural lexicographic order.
    struct PendingRow {
        QString filePath;
        QString relPath;
        Diagnostic diag;
    };
    QList<PendingRow> currentRows;
    QList<PendingRow> otherRows;

    const QString projectRoot = ProjectManager::instance().projectPath();

    for (auto it = m_diagnosticsMap.constBegin(); it != m_diagnosticsMap.constEnd(); ++it) {
        const QString filePath = it.key();
        const QString relPath = QDir(projectRoot).relativeFilePath(filePath);
        const bool isCurrent = !m_currentDocumentPath.isEmpty()
                               && filePath == m_currentDocumentPath;

        for (const Diagnostic &d : it.value()) {
            // Suppression check applies before counting so the stats
            // line reflects only diagnostics the user could actually
            // see if they cleared all filters.
            if (AnalyzerService::instance().isSuppressed(d.message)) continue;

            if (d.severity == DiagnosticSeverity::Error) totalErrors++;
            else if (d.severity == DiagnosticSeverity::Warning) totalWarnings++;
            else totalInfos++;

            if (filterMode == 1 && d.severity != DiagnosticSeverity::Error) continue;
            if (filterMode == 2 && d.severity != DiagnosticSeverity::Warning) continue;
            if (filterMode == 3 && d.severity != DiagnosticSeverity::Info) continue;

            if (currentOnly && !isCurrent) continue;

            (isCurrent ? currentRows : otherRows).append({filePath, relPath, d});
        }
    }

    auto appendRow = [this](const PendingRow &r) {
        const Diagnostic &d = r.diag;
        const int row = m_table->rowCount();
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
        sevItem->setData(Qt::UserRole, QVariant::fromValue(row));

        auto *msgItem = new QTableWidgetItem(d.message);
        auto *fileItem = new QTableWidgetItem(r.relPath);
        fileItem->setData(Qt::UserRole, r.filePath);
        // d.line is 1-based (from the LLM's annotated line prefix); display as-is.
        auto *lineItem = new QTableWidgetItem(QString::number(d.line));
        lineItem->setData(Qt::UserRole, d.line);

        m_table->setItem(row, 0, sevItem);
        m_table->setItem(row, 1, msgItem);
        m_table->setItem(row, 2, fileItem);
        m_table->setItem(row, 3, lineItem);
    };

    for (const PendingRow &r : currentRows) appendRow(r);
    for (const PendingRow &r : otherRows)   appendRow(r);

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

    // Default action: jump to line. d.line is 1-based (from LLM); editor is 0-based.
    Q_EMIT issueActivated(path, qMax(0, line - 1));
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
