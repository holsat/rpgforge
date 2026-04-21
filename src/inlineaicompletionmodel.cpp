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

#include "inlineaicompletionmodel.h"

#include <KTextEditor/View>
#include <KTextEditor/Document>
#include <QIcon>

InlineAICompletionModel::InlineAICompletionModel(InlineAIInvoker *invoker,
                                                  QObject *parent)
    : KTextEditor::CodeCompletionModel(parent)
    , m_invoker(invoker)
{
    setHasGroups(false);
}

void InlineAICompletionModel::refreshCommands()
{
    m_commands.clear();
    if (m_invoker) m_commands = m_invoker->commands();
    // Stable alphabetical order so users can rely on position.
    std::sort(m_commands.begin(), m_commands.end(),
              [](const InlineAIInvoker::Command &a, const InlineAIInvoker::Command &b) {
                  return a.name < b.name;
              });
}

int InlineAICompletionModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid() && !m_commands.isEmpty()) {
        return 1;   // group header
    } else if (parent.parent().isValid()) {
        return 0;   // rows have no children
    }
    return m_commands.count();
}

QModelIndex InlineAICompletionModel::index(int row, int column,
                                            const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return (row == 0) ? createIndex(row, column, quintptr(0))
                          : QModelIndex();
    } else if (parent.parent().isValid()) {
        return QModelIndex();
    }
    if (row < 0 || row >= m_commands.count() || column < 0 || column >= ColumnCount) {
        return QModelIndex();
    }
    return createIndex(row, column, quintptr(1));
}

QModelIndex InlineAICompletionModel::parent(const QModelIndex &index) const
{
    if (index.internalId()) {
        return createIndex(0, 0, quintptr(0));
    }
    return QModelIndex();
}

QVariant InlineAICompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};

    if (role == UnimportantItemRole) return QVariant(true);
    if (role == InheritanceDepth) return 9999;

    if (!index.parent().isValid()) {
        switch (role) {
        case Qt::DisplayRole: return QStringLiteral("Inline AI Commands");
        case GroupRole: return Qt::DisplayRole;
        }
        return {};
    }

    if (index.row() < 0 || index.row() >= m_commands.count()) return {};
    const auto &cmd = m_commands.at(index.row());

    if (index.column() == KTextEditor::CodeCompletionModel::Name
        && role == Qt::DisplayRole) {
        return QVariant(QStringLiteral("@") + cmd.name);
    }
    if (index.column() == KTextEditor::CodeCompletionModel::Postfix
        && role == Qt::DisplayRole) {
        return cmd.description;
    }
    if (index.column() == KTextEditor::CodeCompletionModel::Icon
        && role == Qt::DecorationRole) {
        static QIcon icon(QIcon::fromTheme(QStringLiteral("tools-wizard")));
        return icon;
    }

    return {};
}

void InlineAICompletionModel::completionInvoked(KTextEditor::View *view,
                                                 const KTextEditor::Range &range,
                                                 InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);
    refreshCommands();
}

bool InlineAICompletionModel::shouldStartCompletion(KTextEditor::View *view,
                                                     const QString &insertedText,
                                                     bool userBehaved,
                                                     const KTextEditor::Cursor &position)
{
    Q_UNUSED(insertedText);
    Q_UNUSED(userBehaved);
    if (!view || !view->document() || !position.isValid()) return false;

    const QString line = view->document()->line(position.line());
    const int col = position.column();
    if (col <= 0 || col > line.length()) return false;

    // Walk backwards past command-name characters to find the trigger `@`.
    int searchCol = col;
    while (searchCol > 0) {
        const QChar c = line.at(searchCol - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            --searchCol;
        } else {
            break;
        }
    }
    // Must be `@` just before, and the `@` must be at line start or
    // after whitespace (so `foo@bar` in prose doesn't trigger).
    if (searchCol < 1) return false;
    if (line.at(searchCol - 1) != QLatin1Char('@')) return false;
    if (searchCol - 1 > 0) {
        const QChar prev = line.at(searchCol - 2);
        if (!prev.isSpace()) return false;
    }
    return true;
}

KTextEditor::Range InlineAICompletionModel::completionRange(KTextEditor::View *view,
                                                             const KTextEditor::Cursor &position)
{
    if (!view || !view->document()) return KTextEditor::Range::invalid();

    const QString line = view->document()->line(position.line());
    int col = position.column();
    if (col > line.length()) col = line.length();

    // Include the leading `@` in the completion range so
    // executeCompletionItem replaces "@rew" with "@rewrite ".
    int start = col;
    while (start > 0) {
        const QChar c = line.at(start - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            --start;
        } else if (c == QLatin1Char('@')) {
            --start;
            break;
        } else {
            break;
        }
    }
    return KTextEditor::Range(position.line(), start, position.line(), col);
}

QString InlineAICompletionModel::filterString(KTextEditor::View *view,
                                               const KTextEditor::Range &range,
                                               const KTextEditor::Cursor &position)
{
    QString text = view->document()->text(KTextEditor::Range(range.start(), position));
    // Drop the leading `@` so Kate filters the displayed name prefix
    // (which also has `@`) by what the user typed after it.
    if (text.startsWith(QLatin1Char('@'))) text = text.mid(1);
    return text;
}

void InlineAICompletionModel::executeCompletionItem(KTextEditor::View *view,
                                                     const KTextEditor::Range &word,
                                                     const QModelIndex &index) const
{
    if (!index.isValid() || !index.parent().isValid()) return;
    if (index.row() < 0 || index.row() >= m_commands.count()) return;

    const auto &cmd = m_commands.at(index.row());
    const QString insertion = QStringLiteral("@") + cmd.name + QLatin1Char(' ');
    view->document()->replaceText(word, insertion);
}
