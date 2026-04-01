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

VariableCompletionModel::VariableCompletionModel(QObject *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
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
    setRowCount(m_variables.size());
}

int VariableCompletionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_variables.size();
}

QVariant VariableCompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_variables.size()) {
        return QVariant();
    }

    if (index.column() == KTextEditor::CodeCompletionModel::Name && role == Qt::DisplayRole) {
        return m_variables.at(index.row());
    }

    if (index.column() == KTextEditor::CodeCompletionModel::Icon && role == Qt::DecorationRole) {
        static QIcon icon(QIcon::fromTheme(QStringLiteral("code-variable")));
        return icon;
    }

    if (role == KTextEditor::CodeCompletionModel::CompletionRole) {
        return (int)KTextEditor::CodeCompletionModel::Variable;
    }

    return QVariant();
}

void VariableCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);

    beginResetModel();
    updateVariables();
    endResetModel();
    std::cerr << "completionInvoked: " << m_variables.size() << " variables available" << std::endl;
}

bool VariableCompletionModel::shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position)
{
    Q_UNUSED(userBehaved);
    if (!view || !view->document()) return false;

    std::cerr << "shouldStartCompletion called: insertedText='" << insertedText.toStdString()
              << "' pos=(" << position.line() << "," << position.column() << ")" << std::endl;

    QString line = view->document()->line(position.line());
    int col = position.column();
    if (col > line.length()) return false;

    // Walk backwards from cursor over variable-name chars
    int searchCol = col;
    while (searchCol > 0) {
        QChar c = line.at(searchCol - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            searchCol--;
        } else {
            break;
        }
    }

    // Check if we're right after {{ (with optional partial variable name between {{ and cursor)
    if (searchCol >= 2 && line.at(searchCol - 1) == QLatin1Char('{') && line.at(searchCol - 2) == QLatin1Char('{')) {
        std::cerr << "  -> TRIGGER completion (inside {{)" << std::endl;
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

void VariableCompletionModel::executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_variables.size()) return;

    QString completion = m_variables.at(index.row());
    view->document()->replaceText(word, completion);
    
    KTextEditor::Cursor cursor = view->cursorPosition();
    QString line = view->document()->line(cursor.line());
    if (!line.mid(cursor.column()).startsWith(QLatin1String("}}"))) {
        view->document()->insertText(cursor, QStringLiteral("}}"));
    }
}
