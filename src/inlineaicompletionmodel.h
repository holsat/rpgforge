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

#ifndef INLINEAICOMPLETIONMODEL_H
#define INLINEAICOMPLETIONMODEL_H

#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>

#include "inlineaiinvoker.h"

/**
 * Offers a popup listing the registered @-commands when the user
 * types `@` at the start of a line (or after whitespace). Mirrors
 * VariableCompletionModel's structure: walks backwards on each keystroke
 * to decide whether the cursor is "inside an @-command prefix", then
 * lets Kate present the filtered list. Each row shows the command name
 * and its description as second-line annotation.
 */
class InlineAICompletionModel : public KTextEditor::CodeCompletionModel,
                                 public KTextEditor::CodeCompletionModelControllerInterface
{
    Q_OBJECT
    Q_INTERFACES(KTextEditor::CodeCompletionModelControllerInterface)

public:
    explicit InlineAICompletionModel(InlineAIInvoker *invoker,
                                      QObject *parent = nullptr);
    ~InlineAICompletionModel() override = default;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void completionInvoked(KTextEditor::View *view,
                           const KTextEditor::Range &range,
                           InvocationType invocationType) override;

    bool shouldStartCompletion(KTextEditor::View *view,
                               const QString &insertedText,
                               bool userBehaved,
                               const KTextEditor::Cursor &position) override;
    KTextEditor::Range completionRange(KTextEditor::View *view,
                                        const KTextEditor::Cursor &position) override;
    QString filterString(KTextEditor::View *view,
                         const KTextEditor::Range &range,
                         const KTextEditor::Cursor &position) override;
    void executeCompletionItem(KTextEditor::View *view,
                               const KTextEditor::Range &word,
                               const QModelIndex &index) const override;

private:
    void refreshCommands();

    InlineAIInvoker *m_invoker;
    QList<InlineAIInvoker::Command> m_commands;
};

#endif // INLINEAICOMPLETIONMODEL_H
