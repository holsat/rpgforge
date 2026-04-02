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

#include "variablecompletionmodel.h"
#include "variablemanager.h"
#include <KTextEditor/View>
#include <KTextEditor/Document>
#include <QIcon>
#include <KLocalizedString>
#include <iostream>

VariableCompletionModel::VariableCompletionModel(QObject *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
    // Important: Kate Word Completion says setHasGroups(false) but implements a hierarchy
    // Actually, setting it to false means Kate's internal engine handles the ungrouped items
    // but the model can still provide its own grouping structure.
    setHasGroups(false);
    updateVariables();
}

void VariableCompletionModel::updateVariables()
{
    m_variables.clear();
    const auto names = VariableManager::instance().variableNames();
    for (const QString &name : names) {
        if (name.startsWith(QLatin1String("CALC:"))) {
            m_variables.append(name.mid(5));
        } else {
            m_variables.append(name);
        }
    }
    m_variables.sort(Qt::CaseInsensitive);
}

int VariableCompletionModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        // Toplevel: 1 row for the "Variables" group header
        return m_variables.isEmpty() ? 0 : 1;
    }
    
    if (!parent.parent().isValid()) {
        // Parent is the group header: return the actual variables
        return m_variables.size();
    }
    
    // Items themselves have no children
    return 0;
}

QModelIndex VariableCompletionModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        if (row == 0) {
            return createIndex(row, column, quintptr(0)); // ID 0 for header
        }
        return QModelIndex();
    }
    
    if (!parent.parent().isValid()) {
        if (row >= 0 && row < m_variables.size()) {
            return createIndex(row, column, quintptr(1)); // ID 1 for items
        }
    }
    
    return QModelIndex();
}

QModelIndex VariableCompletionModel::parent(const QModelIndex &index) const
{
    if (index.isValid() && index.internalId() == 1) {
        // Item's parent is the header at (0,0)
        return createIndex(0, 0, quintptr(0));
    }
    return QModelIndex();
}

QVariant VariableCompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    // Group Header Handling
    if (!index.parent().isValid()) {
        if (role == Qt::DisplayRole) {
            return i18n("Variables");
        }
        if (role == GroupRole) {
            return Qt::DisplayRole;
        }
        if (role == InheritanceDepth) {
            return 1000; // Show after normal code completions
        }
        return QVariant();
    }

    // Item Handling
    int row = index.row();
    if (row < 0 || row >= m_variables.size()) return QVariant();

    const QString &name = m_variables.at(row);

    if (role == Qt::DisplayRole || role == KTextEditor::CodeCompletionModel::Name) {
        if (index.column() == KTextEditor::CodeCompletionModel::Name) {
            return name;
        }
    }

    if (role == KTextEditor::CodeCompletionModel::Icon && index.column() == KTextEditor::CodeCompletionModel::Icon) {
        static QIcon icon = QIcon::fromTheme(QStringLiteral("code-variable"));
        return icon;
    }

    if (role == KTextEditor::CodeCompletionModel::CompletionRole) {
        return (int)KTextEditor::CodeCompletionModel::Variable;
    }

    if (role == KTextEditor::CodeCompletionModel::MatchQuality) {
        return 10;
    }

    if (role == InheritanceDepth) return 0;
    if (role == UnimportantItemRole) return false;

    return QVariant();
}

void VariableCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);

    std::cerr << "VCM: completionInvoked" << std::endl;
    beginResetModel();
    updateVariables();
    endResetModel();
}

bool VariableCompletionModel::shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position)
{
    Q_UNUSED(userBehaved);
    if (!view || !view->document() || !position.isValid()) return false;

    QString line = view->document()->line(position.line());
    int col = position.column();
    if (col > line.length()) return false;

    int searchCol = col;
    while (searchCol > 0) {
        QChar c = line.at(searchCol - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            searchCol--;
        } else {
            break;
        }
    }

    if (searchCol >= 2 && line.at(searchCol - 1) == QLatin1Char('{') && line.at(searchCol - 2) == QLatin1Char('{')) {
        return true;
    }

    return false;
}

KTextEditor::Range VariableCompletionModel::completionRange(KTextEditor::View *view, const KTextEditor::Cursor &position)
{
    if (!view || !view->document()) return KTextEditor::Range::invalid();

    KTextEditor::Document *doc = view->document();
    QString line = doc->line(position.line());
    int col = position.column();
    if (col > line.length()) col = line.length();

    int start = col;
    while (start > 0) {
        QChar c = line.at(start - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_')) {
            start--;
        } else {
            break;
        }
    }

    return KTextEditor::Range(position.line(), start, position.line(), col);
}

bool VariableCompletionModel::shouldAbortCompletion(KTextEditor::View *view, const KTextEditor::Range &range, const QString &currentCompletion)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(currentCompletion);
    return false; 
}

QString VariableCompletionModel::filterString(KTextEditor::View *view, const KTextEditor::Range &range, const KTextEditor::Cursor &position)
{
    return view->document()->text(KTextEditor::Range(range.start(), position));
}

void VariableCompletionModel::executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid()) return;

    QString completion = m_variables.at(index.row());
    view->document()->replaceText(word, completion);
    
    KTextEditor::Cursor cursor = view->cursorPosition();
    QString line = view->document()->line(cursor.line());
    if (!line.mid(cursor.column()).startsWith(QLatin1String("}}"))) {
        view->document()->insertText(cursor, QStringLiteral("}}"));
    }
}
