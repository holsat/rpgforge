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

#include "historydialog.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>

HistoryDialog::HistoryDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setWindowTitle(i18n("History: %1", QFileInfo(filePath).fileName()));
    resize(850, 500); // Made wider for the branch column
    setupUi();
    refresh();
}

HistoryDialog::~HistoryDialog() = default;

void HistoryDialog::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        i18n("Version"),
        i18n("Date & Time"),
        i18n("Branch"),
        i18n("Bookmarks"),
        i18n("Description")
    });
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    layout->addWidget(m_table);

    auto *buttons = new QHBoxLayout();
    
    m_viewBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), i18n("View Version"), this);
    m_viewBtn->setEnabled(false);
    connect(m_viewBtn, &QPushButton::clicked, this, &HistoryDialog::onViewClicked);
    buttons->addWidget(m_viewBtn);

    m_compareBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-compare")), i18n("Compare..."), this);
    m_compareBtn->setEnabled(false);
    connect(m_compareBtn, &QPushButton::clicked, this, &HistoryDialog::onCompareClicked);
    buttons->addWidget(m_compareBtn);

    m_restoreBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-revert")), i18n("Permanently Restore"), this);
    m_restoreBtn->setEnabled(false);
    connect(m_restoreBtn, &QPushButton::clicked, this, &HistoryDialog::onRestoreClicked);
    buttons->addWidget(m_restoreBtn);

    buttons->addStretch();

    m_tagBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("bookmark-new")), i18n("Bookmark..."), this);
    m_tagBtn->setEnabled(false);
    connect(m_tagBtn, &QPushButton::clicked, this, &HistoryDialog::onTagClicked);
    buttons->addWidget(m_tagBtn);

    auto *closeBtn = new QPushButton(i18n("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(closeBtn);

    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = !m_table->selectedItems().isEmpty();
        m_viewBtn->setEnabled(hasSelection);
        m_restoreBtn->setEnabled(hasSelection);
        m_compareBtn->setEnabled(hasSelection);
        m_tagBtn->setEnabled(hasSelection);
    });
}

void HistoryDialog::refresh()
{
    auto future = GitService::instance().getHistory(m_filePath);
    future.then(this, [this](const QList<VersionInfo> &history) {
        updateTable(history);
    });
}

void HistoryDialog::updateTable(const QList<VersionInfo> &history)
{
    m_history = history;
    m_table->setSortingEnabled(false); // Disable while updating
    m_table->setRowCount(history.count());
    
    for (int i = 0; i < history.count(); ++i) {
        const auto &info = history[i];
        
        // Use i+1 or internal index? Git history index is just for UI labels usually.
        auto *itemIdx = new QTableWidgetItem(QString::number(history.count() - i));
        itemIdx->setData(Qt::UserRole, i); // Store index in m_history
        m_table->setItem(i, 0, itemIdx);

        m_table->setItem(i, 1, new QTableWidgetItem(info.date.toString(Qt::ISODate)));
        
        QString branches = info.branches.join(QStringLiteral(", "));
        m_table->setItem(i, 2, new QTableWidgetItem(branches));
        
        m_table->setItem(i, 3, new QTableWidgetItem(info.tags.join(QStringLiteral(", "))));
        m_table->setItem(i, 4, new QTableWidgetItem(info.message));
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(1, Qt::DescendingOrder); // Sort by date by default
}

void HistoryDialog::onViewClicked()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    
    // Get actual index from data since we might be sorted
    int historyIdx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    if (historyIdx < 0 || historyIdx >= m_history.count()) return;
    
    const auto &info = m_history[historyIdx];
    Q_EMIT viewVersion(info.hash, historyIdx + 1, info.date, info.tags);
}

void HistoryDialog::onRestoreClicked()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    
    int historyIdx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    if (historyIdx < 0 || historyIdx >= m_history.count()) return;
    
    const auto &info = m_history[historyIdx];
    if (QMessageBox::warning(this, i18n("Restore Version"), 
        i18n("Are you sure you want to permanently overwrite the current file with this version?"),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        Q_EMIT restoreVersion(info.hash);
    }
}

void HistoryDialog::onCompareClicked()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    
    int historyIdx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    if (historyIdx < 0 || historyIdx >= m_history.count()) return;
    
    Q_EMIT compareVersion(m_history[historyIdx].hash);
}

void HistoryDialog::onTagClicked()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    
    int historyIdx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    if (historyIdx < 0 || historyIdx >= m_history.count()) return;
    
    QString hash = m_history[historyIdx].hash;
    
    bool ok;
    QString tagName = QInputDialog::getText(this, i18n("Create Bookmark"), 
        i18n("Bookmark Name:"), QLineEdit::Normal, QString(), &ok);
    
    if (ok && !tagName.isEmpty()) {
        if (GitService::instance().createTag(m_filePath, tagName, hash)) {
            refresh();
        } else {
            QMessageBox::critical(this, i18n("Error"), i18n("Failed to create bookmark. Name might already exist."));
        }
    }
}
