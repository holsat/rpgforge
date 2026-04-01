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

VariableCompletionModel::VariableCompletionModel(QObject *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
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
    if (parent.isValid()) return 0;
    return m_variables.size();
}

QVariant VariableCompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_variables.size()) return QVariant();

    const QString &name = m_variables.at(index.row());
    if (role == KTextEditor::CodeCompletionModel::Name || role == Qt::DisplayRole) return name;
    if (role == KTextEditor::CodeCompletionModel::CompletionRole) return KTextEditor::CodeCompletionModel::Variable;
    return QVariant();
}

void VariableCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);
    
    // SAFE: Only reset the model when specifically requested by the UI
    beginResetModel();
    updateVariables();
    endResetModel();
}

bool VariableCompletionModel::shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position)
{
    Q_UNUSED(view);
    Q_UNUSED(userBehaved);
    Q_UNUSED(position);

    // Only trigger on typing the second '{'.
    // No document access here to prevent recursion.
    return (insertedText == QLatin1String("{"));
}

KTextEditor::Range VariableCompletionModel::completionRange(KTextEditor::View *view, const KTextEditor::Cursor &position)
{
    if (!view || !view->document()) return KTextEditor::Range::invalid();

    // Safely detect current word boundary
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
    
    // Safely check for closing braces without triggering recursion
    KTextEditor::Cursor cursor = view->cursorPosition();
    QString line = view->document()->line(cursor.line());
    if (!line.mid(cursor.column()).startsWith(QLatin1String("}}"))) {
        view->document()->insertText(cursor, QStringLiteral("}}"));
    }
}
