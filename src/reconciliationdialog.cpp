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

#include "reconciliationdialog.h"

#include "projectmanager.h"
#include "projecttreemodel.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemEditorCreator>
#include <QLabel>
#include <QMetaEnum>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// ReconciliationModel
// ---------------------------------------------------------------------------

namespace {

QString actionLabel(ReconciliationEntry::Action action)
{
    switch (action) {
    case ReconciliationEntry::None:          return i18n("Choose…");
    case ReconciliationEntry::Locate:        return i18n("Locate");
    case ReconciliationEntry::Remove:        return i18n("Remove");
    case ReconciliationEntry::RecreateEmpty: return i18n("Recreate empty");
    }
    return QString();
}

// Combobox delegate for the Action column.
class ActionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *cb = new QComboBox(parent);
        cb->addItem(actionLabel(ReconciliationEntry::None),
                    static_cast<int>(ReconciliationEntry::None));
        cb->addItem(actionLabel(ReconciliationEntry::Locate),
                    static_cast<int>(ReconciliationEntry::Locate));
        cb->addItem(actionLabel(ReconciliationEntry::Remove),
                    static_cast<int>(ReconciliationEntry::Remove));
        cb->addItem(actionLabel(ReconciliationEntry::RecreateEmpty),
                    static_cast<int>(ReconciliationEntry::RecreateEmpty));
        return cb;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *cb = qobject_cast<QComboBox*>(editor);
        if (!cb) return;
        const int action = index.data(Qt::EditRole).toInt();
        const int idx = cb->findData(action);
        if (idx >= 0) cb->setCurrentIndex(idx);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *cb = qobject_cast<QComboBox*>(editor);
        if (!cb) return;
        model->setData(index, cb->currentData(), Qt::EditRole);
    }
};

} // namespace

ReconciliationModel::ReconciliationModel(QList<ReconciliationEntry> entries, QObject *parent)
    : QAbstractTableModel(parent)
    , m_entries(std::move(entries))
{
}

int ReconciliationModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.size();
}

int ReconciliationModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant ReconciliationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const ReconciliationEntry &e = m_entries.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case PathColumn:
            return e.path;
        case DisplayNameColumn:
            return e.displayName;
        case SuggestionColumn:
            // Prefer the user-confirmed resolvedPath when Locate is chosen,
            // otherwise show the fuzzy suggestion if any.
            if (!e.resolvedPath.isEmpty()) return e.resolvedPath;
            return e.suggestedPath;
        case ActionColumn:
            if (role == Qt::EditRole) return static_cast<int>(e.action);
            return actionLabel(e.action);
        }
    }

    if (role == Qt::FontRole && index.column() == SuggestionColumn
        && e.resolvedPath.isEmpty() && !e.suggestedPath.isEmpty()) {
        QFont f;
        f.setItalic(true);
        return f;
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case PathColumn:
            return i18n("Tree path: %1", e.path);
        case SuggestionColumn:
            if (!e.resolvedPath.isEmpty()) {
                return i18n("Will be located at: %1", e.resolvedPath);
            }
            if (!e.suggestedPath.isEmpty()) {
                return i18n("Fuzzy-match suggestion — double-click the row to override, or choose Locate to confirm.");
            }
            return i18n("No on-disk match found.");
        default:
            break;
        }
    }

    return {};
}

bool ReconciliationModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) return false;
    if (index.row() < 0 || index.row() >= m_entries.size()) return false;
    if (role != Qt::EditRole) return false;

    ReconciliationEntry &e = m_entries[index.row()];
    if (index.column() == ActionColumn) {
        const int v = value.toInt();
        e.action = static_cast<ReconciliationEntry::Action>(v);
        // If the user chose Locate but there's already a suggestion, auto-
        // populate resolvedPath so the Apply step has something to work with.
        if (e.action == ReconciliationEntry::Locate && e.resolvedPath.isEmpty()) {
            e.resolvedPath = e.suggestedPath;
        }
        // If the user picked something other than Locate, clear any stale
        // resolvedPath left over from a previous click.
        if (e.action != ReconciliationEntry::Locate) {
            e.resolvedPath.clear();
        }
        const QModelIndex left = this->index(index.row(), 0);
        const QModelIndex right = this->index(index.row(), ColumnCount - 1);
        Q_EMIT dataChanged(left, right);
        return true;
    }
    return false;
}

Qt::ItemFlags ReconciliationModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags base = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ActionColumn) base |= Qt::ItemIsEditable;
    return base;
}

QVariant ReconciliationModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orient == Qt::Vertical) return section + 1;
    switch (section) {
    case PathColumn:        return i18n("Path");
    case DisplayNameColumn: return i18n("Display Name");
    case SuggestionColumn:  return i18n("Suggestion / Resolved");
    case ActionColumn:      return i18n("Action");
    }
    return {};
}

void ReconciliationModel::setAllActions(ReconciliationEntry::Action action)
{
    if (m_entries.isEmpty()) return;
    for (auto &e : m_entries) {
        e.action = action;
        if (action == ReconciliationEntry::Locate && e.resolvedPath.isEmpty()) {
            e.resolvedPath = e.suggestedPath;
        } else if (action != ReconciliationEntry::Locate) {
            e.resolvedPath.clear();
        }
    }
    Q_EMIT dataChanged(index(0, 0), index(m_entries.size() - 1, ColumnCount - 1));
}

void ReconciliationModel::acceptAllSuggestions()
{
    if (m_entries.isEmpty()) return;
    for (auto &e : m_entries) {
        if (!e.suggestedPath.isEmpty()) {
            e.action = ReconciliationEntry::Locate;
            e.resolvedPath = e.suggestedPath;
        }
    }
    Q_EMIT dataChanged(index(0, 0), index(m_entries.size() - 1, ColumnCount - 1));
}

void ReconciliationModel::setResolved(int row, const QString &resolvedPath)
{
    if (row < 0 || row >= m_entries.size()) return;
    m_entries[row].resolvedPath = resolvedPath;
    m_entries[row].action = resolvedPath.isEmpty()
        ? ReconciliationEntry::None
        : ReconciliationEntry::Locate;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

// ---------------------------------------------------------------------------
// ReconciliationDialog
// ---------------------------------------------------------------------------

ReconciliationDialog::ReconciliationDialog(const QList<ReconciliationEntry> &entries,
                                            QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Reconcile Project Files"));
    resize(820, 460);

    auto *mainLayout = new QVBoxLayout(this);

    auto *explanation = new QLabel(
        i18n("Project files on disk don't match the tree. Review each entry and choose an action."),
        this);
    explanation->setWordWrap(true);
    mainLayout->addWidget(explanation);

    m_model = new ReconciliationModel(entries, this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(ReconciliationModel::PathColumn,
                                                       QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ReconciliationModel::DisplayNameColumn,
                                                       QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ReconciliationModel::SuggestionColumn,
                                                       QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ReconciliationModel::ActionColumn,
                                                       QHeaderView::ResizeToContents);
    m_table->setItemDelegateForColumn(ReconciliationModel::ActionColumn,
                                       new ActionDelegate(this));
    connect(m_table, &QTableView::doubleClicked,
            this, &ReconciliationDialog::onRowDoubleClicked);
    mainLayout->addWidget(m_table, 1);

    auto *bulkRow = new QHBoxLayout();
    auto *locateAllBtn = new QPushButton(i18n("Locate all under directory…"), this);
    auto *removeAllBtn = new QPushButton(i18n("Remove all"), this);
    auto *acceptAllBtn = new QPushButton(i18n("Accept all suggestions"), this);
    bulkRow->addWidget(locateAllBtn);
    bulkRow->addWidget(removeAllBtn);
    bulkRow->addWidget(acceptAllBtn);
    bulkRow->addStretch(1);
    mainLayout->addLayout(bulkRow);

    connect(locateAllBtn, &QPushButton::clicked,
            this, &ReconciliationDialog::onLocateAllUnderDirectory);
    connect(removeAllBtn, &QPushButton::clicked,
            this, &ReconciliationDialog::onRemoveAll);
    connect(acceptAllBtn, &QPushButton::clicked,
            this, &ReconciliationDialog::onAcceptAllSuggestions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
                                         this);
    QPushButton *applyBtn = buttons->button(QDialogButtonBox::Apply);
    applyBtn->setDefault(true);
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

QList<ReconciliationEntry> ReconciliationDialog::entries() const
{
    return m_model ? m_model->entries() : QList<ReconciliationEntry>{};
}

void ReconciliationDialog::onAcceptAllSuggestions()
{
    if (m_model) m_model->acceptAllSuggestions();
}

void ReconciliationDialog::onRemoveAll()
{
    if (m_model) m_model->setAllActions(ReconciliationEntry::Remove);
}

void ReconciliationDialog::onLocateAllUnderDirectory()
{
    if (!m_model) return;
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        i18n("Choose a directory to re-run fuzzy matching against"));
    if (dir.isEmpty()) return;

    const QDir searchDir(dir);
    if (!searchDir.exists()) return;

    const auto &rows = m_model->entries();
    for (int row = 0; row < rows.size(); ++row) {
        const QString resolved = tryResolveOnDisk(searchDir, rows.at(row).path);
        if (!resolved.isEmpty()) {
            // resolved is relative to the chosen directory; translate into
            // project-relative form if the chosen directory is inside the
            // project. Otherwise store the absolute path — the apply layer
            // handles it.
            const QString projectRoot = ProjectManager::instance().projectPath();
            QString projectRelative;
            if (!projectRoot.isEmpty()) {
                const QString abs = searchDir.absoluteFilePath(resolved);
                projectRelative = QDir(projectRoot).relativeFilePath(abs);
            } else {
                projectRelative = searchDir.absoluteFilePath(resolved);
            }
            m_model->setResolved(row, projectRelative);
        }
    }
}

void ReconciliationDialog::onRowDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (index.column() == ReconciliationModel::ActionColumn) return;
    locateRowInteractively(index.row());
}

void ReconciliationDialog::locateRowInteractively(int row)
{
    const auto &rows = m_model->entries();
    if (row < 0 || row >= rows.size()) return;

    const QString projectRoot = ProjectManager::instance().projectPath();
    const QString startDir = projectRoot.isEmpty() ? QDir::homePath() : projectRoot;

    const QString chosen = QFileDialog::getOpenFileName(
        this,
        i18n("Locate file for %1", rows.at(row).displayName),
        startDir);
    if (chosen.isEmpty()) return;

    QString projectRelative = chosen;
    if (!projectRoot.isEmpty()) {
        projectRelative = QDir(projectRoot).relativeFilePath(chosen);
    }
    m_model->setResolved(row, projectRelative);
}
