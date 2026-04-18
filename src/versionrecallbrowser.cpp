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

#include "versionrecallbrowser.h"
#include "gitservice.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QLocale>
#include <QIcon>
#include <QTemporaryDir>

VersionRecallBrowser::VersionRecallBrowser(const QString &filePath,
                                             const QString &repoPath,
                                             const QVariantMap &laneColors,
                                             QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_repoPath(repoPath)
    , m_laneColors(laneColors)
{
    setWindowTitle(i18n("Recall Version — %1", QFileInfo(filePath).fileName()));
    resize(620, 580);
    setupUi();
    loadHistory();
}

VersionRecallBrowser::~VersionRecallBrowser()
{
    if (!m_tempPreviewPath.isEmpty() && QFile::exists(m_tempPreviewPath)) {
        QFile::remove(m_tempPreviewPath);
    }
}

void VersionRecallBrowser::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Header: file-type icon + filename in bold
    auto *headerLayout = new QHBoxLayout();
    auto *iconLabel = new QLabel(this);
    const QFileInfo fi(m_filePath);
    const QIcon fileIcon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    iconLabel->setPixmap(fileIcon.pixmap(24, 24));
    headerLayout->addWidget(iconLabel);

    auto *nameLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(fi.fileName().toHtmlEscaped()), this);
    headerLayout->addWidget(nameLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Table
    m_model = new QStandardItemModel(0, 4, this);
    m_model->setHorizontalHeaderLabels({
        i18n("Date"),
        i18n("Exploration Path"),
        i18n("Milestone / Tag"),
        i18nc("column header: version index (1-based row number)", "#")
    });

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setMinimumHeight(200);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(0, Qt::DescendingOrder);
    mainLayout->addWidget(m_table, 1);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &VersionRecallBrowser::onRowSelected);

    // Preview label
    auto *readOnlyHint = new QLabel(i18n("Read-only preview"), this);
    auto hintFont = readOnlyHint->font();
    hintFont.setPointSize(hintFont.pointSize() - 1);
    readOnlyHint->setFont(hintFont);
    readOnlyHint->setStyleSheet(QStringLiteral("color: gray;"));
    mainLayout->addWidget(readOnlyHint);

    m_previewLabel = new QLabel(this);
    mainLayout->addWidget(m_previewLabel);

    // Preview text area
    m_preview = new QTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMinimumHeight(150);
    mainLayout->addWidget(m_preview);

    // Button box
    auto *buttonBox = new QDialogButtonBox(this);
    m_recallBtn = new QPushButton(i18n("Recall Version"), this);
    m_recallBtn->setEnabled(false);
    buttonBox->addButton(m_recallBtn, QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);

    connect(m_recallBtn, &QPushButton::clicked, this, &VersionRecallBrowser::onRecallClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void VersionRecallBrowser::loadHistory()
{
    auto future = GitService::instance().getHistory(m_filePath);
    future.then(this, [this](const QList<VersionInfo> &history) {
        m_model->removeRows(0, m_model->rowCount());

        const QLocale locale;
        for (int i = 0; i < history.count(); ++i) {
            const auto &info = history[i];

            // Column 0: Date — "(N) MMM dd, hh:mm"
            const QString dateStr = QStringLiteral("(%1) %2")
                .arg(i + 1)
                .arg(info.date.toString(QStringLiteral("MMM dd, hh:mm")));
            auto *dateItem = new QStandardItem(dateStr);
            dateItem->setData(info.hash, Qt::UserRole);
            dateItem->setData(info.date.toString(Qt::ISODate), Qt::UserRole + 1);

            // Column 1: Exploration Path — branch name with color swatch
            const QString branchName = info.branches.isEmpty()
                ? QString()
                : info.branches.first();
            auto *branchItem = new QStandardItem(branchName);
            if (!branchName.isEmpty() && m_laneColors.contains(branchName)) {
                branchItem->setData(QColor(m_laneColors.value(branchName).toString()),
                                    Qt::DecorationRole);
            }

            // Column 2: Milestone / Tag
            auto *tagItem = new QStandardItem(info.tags.join(QStringLiteral(", ")));

            // Column 3: 1-based version index. VersionInfo does not carry a
            // word count, and the previous "Word Count" column was actually
            // showing this index — the header now accurately reflects that.
            const QString indexStr = locale.toString(i + 1);
            auto *idxItem = new QStandardItem(indexStr);
            idxItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

            m_model->appendRow({dateItem, branchItem, tagItem, idxItem});
        }

        // Select the first row (most recent) by default
        if (m_model->rowCount() > 0) {
            m_table->selectRow(0);
        }
    });
}

void VersionRecallBrowser::onRowSelected(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    const int row = index.row();
    m_selectedHash = m_model->item(row, 0)->data(Qt::UserRole).toString();
    m_recallBtn->setEnabled(true);

    const QString dateText = m_model->item(row, 0)->text();
    m_previewLabel->setText(i18n("Version Preview: %1", dateText));

    loadPreview(m_selectedHash);
}

void VersionRecallBrowser::loadPreview(const QString &hash)
{
    m_preview->setPlainText(i18n("Loading preview..."));

    // Set up a temp path for extraction
    if (m_tempPreviewPath.isEmpty()) {
        m_tempPreviewPath = QDir::tempPath()
            + QStringLiteral("/rpgforge_preview_")
            + QString::number(qintptr(this), 16)
            + QStringLiteral(".tmp");
    }

    auto future = GitService::instance().extractVersion(m_filePath, hash, m_tempPreviewPath);
    future.then(this, [this](bool success) {
        if (!success) {
            m_preview->setPlainText(i18n("Failed to load preview."));
            return;
        }

        QFile file(m_tempPreviewPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_preview->setPlainText(i18n("Failed to read preview file."));
            return;
        }

        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        m_preview->setPlainText(content);
    });
}

void VersionRecallBrowser::onRecallClicked()
{
    auto result = QMessageBox::warning(
        this,
        i18n("Replace Current Draft?"),
        i18n("This will replace your current draft with the selected version.\n"
             "Your current draft will be saved as a new Milestone first."),
        QMessageBox::Ok | QMessageBox::Cancel);

    if (result == QMessageBox::Ok) {
        Q_EMIT versionSelected(m_filePath, m_selectedHash);
        accept();
    }
}
