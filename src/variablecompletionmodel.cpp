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
#include <iostream>

// Modeled exactly after KateWordCompletionModel from
// ktexteditor/src/completion/katewordcompletion.cpp

VariableCompletionModel::VariableCompletionModel(QObject *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
    setHasGroups(false);
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

// --- Two-level hierarchy: root -> group header -> items ---
// This exactly matches KateWordCompletionModel's pattern.

int VariableCompletionModel::rowCount(const QModelIndex &parent) const
{
    int result;
    if (!parent.isValid() && !m_variables.isEmpty()) {
        result = 1; // One root node to define the custom group
    } else if (parent.parent().isValid()) {
        result = 0; // Completion items have no children
    } else {
        result = m_variables.count();
    }
    static int callCount = 0;
    if (++callCount <= 20) {
        std::cerr << "VCM: rowCount(valid=" << parent.isValid() << ", id=" << (parent.isValid() ? parent.internalId() : -1) << ") = " << result << std::endl;
    }
    return result;
}

QModelIndex VariableCompletionModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        if (row == 0) {
            return createIndex(row, column, quintptr(0));
        } else {
            return QModelIndex();
        }
    } else if (parent.parent().isValid()) {
        return QModelIndex();
    }

    if (row < 0 || row >= m_variables.count() || column < 0 || column >= ColumnCount) {
        return QModelIndex();
    }

    return createIndex(row, column, quintptr(1));
}

QModelIndex VariableCompletionModel::parent(const QModelIndex &index) const
{
    if (index.internalId()) {
        return createIndex(0, 0, quintptr(0));
    } else {
        return QModelIndex();
    }
}

QVariant VariableCompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role == UnimportantItemRole) {
        return QVariant(true);
    }
    if (role == InheritanceDepth) {
        return 10000;
    }

    if (!index.parent().isValid()) {
        // Group header
        switch (role) {
        case Qt::DisplayRole:
            return QStringLiteral("Variables");
        case GroupRole:
            return Qt::DisplayRole;
        }
    }

    if (index.column() == KTextEditor::CodeCompletionModel::Name && role == Qt::DisplayRole) {
        std::cerr << "VCM: data Name row=" << index.row() << " id=" << index.internalId() << " -> " << m_variables.at(index.row()).toStdString() << std::endl;
        return m_variables.at(index.row());
    }

    if (index.column() == KTextEditor::CodeCompletionModel::Icon && role == Qt::DecorationRole) {
        static QIcon icon(QIcon::fromTheme(QStringLiteral("code-variable")));
        return icon;
    }

    return QVariant();
}

void VariableCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);

    // Match KateWordCompletionModel: just update data, no beginResetModel/endResetModel.
    // The presentation model calls createGroups() after this returns.
    updateVariables();
    std::cerr << "VCM: completionInvoked, " << m_variables.size() << " vars, rowCount(root)=" << rowCount(QModelIndex()) << std::endl;
}

bool VariableCompletionModel::shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position)
{
    Q_UNUSED(insertedText);
    Q_UNUSED(userBehaved);
    if (!view || !view->document() || !position.isValid()) return false;

    QString line = view->document()->line(position.line());
    int col = position.column();
    if (col > line.length()) return false;

    // Walk backwards over variable-name characters
    int searchCol = col;
    while (searchCol > 0) {
        QChar c = line.at(searchCol - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            searchCol--;
        } else {
            break;
        }
    }

    // Trigger if cursor is inside {{ ... (unclosed)
    if (searchCol >= 2 && line.at(searchCol - 1) == QLatin1Char('{') && line.at(searchCol - 2) == QLatin1Char('{')) {
        std::cerr << "VCM: shouldStartCompletion -> TRUE" << std::endl;
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

KTextEditor::CodeCompletionModelControllerInterface::MatchReaction VariableCompletionModel::matchingItem(const QModelIndex &matched)
{
    Q_UNUSED(matched);
    // Never hide the completion list — we always want to show variable suggestions
    return None;
}

QString VariableCompletionModel::filterString(KTextEditor::View *view, const KTextEditor::Range &range, const KTextEditor::Cursor &position)
{
    return view->document()->text(KTextEditor::Range(range.start(), position));
}

void VariableCompletionModel::executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid()) return;
    if (index.row() < 0 || index.row() >= m_variables.size()) return;

    QString completion = m_variables.at(index.row());
    view->document()->replaceText(word, completion);

    KTextEditor::Cursor cursor = view->cursorPosition();
    QString line = view->document()->line(cursor.line());
    if (!line.mid(cursor.column()).startsWith(QLatin1String("}}"))) {
        view->document()->insertText(cursor, QStringLiteral("}}"));
    }
}
