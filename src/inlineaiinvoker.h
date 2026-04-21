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

#ifndef INLINEAIINVOKER_H
#define INLINEAIINVOKER_H

#include <QObject>
#include <QMap>
#include <QString>

#include "ragassistservice.h"

namespace KTextEditor {
    class View;
    class Document;
    class MovingRange;
}

/**
 * \file inlineaiinvoker.h
 *
 * Wires an inline AI trigger into a KTextEditor::View. When the user
 * types a line like
 *
 *     @lore Tell me about Jalal
 *
 * and presses Ctrl+Enter (or Ctrl+Return on some keyboards), the
 * invoker parses the first word after `@` as a registered command,
 * builds a RagAssistRequest from the command's template, replaces the
 * `@cmd ...` line with a streaming placeholder, and streams the LLM
 * response into the editor at that exact location.
 *
 * **Pinned insertion point.** The output is anchored to a
 * KTextEditor::MovingRange spanning the placeholder. The user is free
 * to move the cursor, scroll, or click elsewhere while the request is
 * in flight — streamed chunks always land at the pinned location.
 *
 * **User-type abort.** If the user starts typing INTO the pinned
 * range while the stream is active (intending to replace it or bail
 * out), the active request is abandoned and the placeholder's content
 * is left as the user typed it. The MovingRange's onRangeInvalid
 * feedback is the trigger.
 *
 * **Single-undo.** The entire operation — line replacement, placeholder
 * insertion, streamed updates, final finalization — is wrapped in a
 * Document::editStart()/editEnd() pair so Ctrl+Z reverts the AI
 * insertion as a single undo step.
 *
 * **Error recovery.** On LLM failure or timeout, the placeholder is
 * replaced with an HTML comment describing the error and the original
 * `@cmd line` is restored on the line above so the user can retry.
 *
 * **Command registry.** Commands are registered at construction time
 * (see registerDefaultCommands()) and can be extended at runtime via
 * registerCommand() for user-defined shortcuts. Each command maps to
 * a template that populates the RagAssistRequest's depth, system
 * prompt, entity-extraction mode, and active-doc-as-context flag.
 */
class InlineAIInvoker : public QObject
{
    Q_OBJECT

public:
    /**
     * Template describing how an `@name` invocation gets turned into
     * a RagAssistRequest. Registered in the invoker's command map.
     */
    struct Command {
        QString name;                         ///< @-trigger, e.g. "lore"
        QString description;                  ///< shown in the completion popup
        SynthesisDepth depth = SynthesisDepth::Quick;
        bool extractEntity = false;           ///< first word after @cmd is treated as entityName
        bool includeActiveDoc = false;        ///< attach active doc as priority-0 extraSource
        QString systemPrompt;                 ///< appended to the service's baseline system prompt
        int maxTokens = 4096;
    };

    explicit InlineAIInvoker(KTextEditor::View *view, QObject *parent = nullptr);
    ~InlineAIInvoker() override;

    /**
     * Adds a command to the registry. Overwrites any existing command
     * with the same `name`.
     */
    void registerCommand(const Command &cmd);

    /**
     * Returns the list of registered command templates — used by the
     * `@`-trigger autocomplete popup and for diagnostics.
     */
    QList<Command> commands() const { return m_commands.values(); }

    /**
     * Loads user-defined command templates from a JSON file. Called
     * automatically from the constructor against the standard path
     * ($XDG_DATA_HOME/rpgforge/inline_commands.json). Exposed so tests
     * can load fixtures. User-defined commands override built-ins with
     * the same name.
     *
     * Schema:
     *   {
     *     "commands": [
     *       {
     *         "name":            "poetry",
     *         "description":     "...",
     *         "depth":           "quick" | "comprehensive",
     *         "extractEntity":   false,
     *         "includeActiveDoc": true,
     *         "systemPrompt":    "...",
     *         "maxTokens":       2048
     *       }
     *     ]
     *   }
     *
     * Missing fields fall back to Command defaults. Invalid JSON is
     * logged and ignored.
     */
    void loadUserCommandsFromJson(const QString &path);

    /**
     * Cancel any in-flight invocation. Safe to call whether or not one
     * is active; used by the UI when the user wants to bail out
     * explicitly (e.g. a future "Stop Generation" action) and by
     * beginInvocation itself when a second invocation races the first.
     */
    void cancelActive();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * Seeds the default set of commands (lore, forge, chat, rewrite,
     * expand, summarize). Called from the constructor.
     */
    void registerDefaultCommands();

    /**
     * Attempts to parse and dispatch an @-command on the current line.
     * Returns true if a command was found and dispatched (so the
     * caller can swallow the key press), false if the line didn't
     * match any known command.
     */
    bool tryDispatchOnCurrentLine();

    /**
     * Creates the placeholder text, installs a MovingRange around it,
     * and kicks off the async RagAssistService::generate call.
     */
    void beginInvocation(const Command &cmd,
                         const QString &userPrompt,
                         const QString &entityName,
                         int lineNumber,
                         int lineLength,
                         const QString &originalLine);

    /**
     * Replaces the placeholder text with the final response and
     * closes the MovingRange. Safe to call once per invocation.
     */
    void finalizeInvocation(const QString &finalText);

    /**
     * Replaces the placeholder with an error comment and restores the
     * original @cmd line above it so the user can retry or edit.
     */
    void handleError(const QString &message);

    /**
     * Resets internal state and releases the MovingRange. Called when
     * the invocation finishes, errors, or is aborted by the user.
     */
    void clearActiveInvocation();

    KTextEditor::View *m_view;
    QMap<QString, Command> m_commands;

    // Active-invocation state. Only one in-flight invocation per view
    // at a time — a second Ctrl+Enter while one is streaming cancels
    // the first.
    KTextEditor::MovingRange *m_activeRange = nullptr;
    QString m_activeRequestId;
    QString m_activeOriginalLine;
    QString m_activeAccumulated;    ///< streamed tokens accumulated so far
    QString m_lastWrittenText;      ///< what we last replaced into m_activeRange;
                                    ///< compared against current range content before
                                    ///< each chunk write to detect user edits
    bool m_invocationActive = false;
};

#endif // INLINEAIINVOKER_H
