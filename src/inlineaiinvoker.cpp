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

#include "inlineaiinvoker.h"

#include "debuglog.h"

#include <KTextEditor/View>
#include <KTextEditor/Document>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Cursor>
#include <KTextEditor/Range>

#include <KLocalizedString>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QStandardPaths>

// Placeholder text shown in the editor while the LLM generates. Kept
// short so the insertion doesn't shift the user's reading position.
static const QString kPlaceholderText = QStringLiteral("⏳ Generating…");

InlineAIInvoker::InlineAIInvoker(KTextEditor::View *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
{
    Q_ASSERT(m_view);

    // Ctrl+Shift+Space triggers an @-command on the current line.
    //
    // History: tried Ctrl+Return (collides with Kate's newline action)
    // and Ctrl+Shift+Return (collides with Kate's "Insert Newline
    // Below Current Line"). Ctrl+Shift+Space is confirmed unbound in
    // Kate's default keymap — easier to hit than Alt+Return and doesn't
    // compete with any text-entry action.
    //
    // We register as a QAction on the view because plain QShortcut
    // inside a KTextEditor::View doesn't reliably fire — Kate's
    // KActionCollection intercepts KeyPress before child QShortcuts
    // see it. QAction with Qt::WidgetWithChildrenShortcut context
    // goes through Qt's shortcut-override dispatch and fires
    // regardless of which Kate sub-widget has focus.
    auto *action = new QAction(this);
    action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space));
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(action, &QAction::triggered, this, [this]() {
        RPGFORGE_DLOG("INLINE-AI") << "shortcut activated — trying current line";
        tryDispatchOnCurrentLine();
    });
    m_view->addAction(action);
    RPGFORGE_DLOG("INLINE-AI") << "Ctrl+Shift+Space bound on view" << m_view;

    registerDefaultCommands();

    // Load any user-defined commands from the standard location.
    // Silent if the file doesn't exist (typical first-run state).
    const QString userCommandsPath = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .absoluteFilePath(QStringLiteral("inline_commands.json"));
    loadUserCommandsFromJson(userCommandsPath);
}

InlineAIInvoker::~InlineAIInvoker()
{
    clearActiveInvocation();
}

void InlineAIInvoker::registerCommand(const Command &cmd)
{
    m_commands.insert(cmd.name, cmd);
}

void InlineAIInvoker::loadUserCommandsFromJson(const QString &path)
{
    QFile f(path);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) {
        RPGFORGE_DLOG("INLINE-AI") << "cannot open user commands file:" << path;
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        RPGFORGE_DLOG("INLINE-AI") << "user commands file parse error:"
                                    << err.errorString() << "in" << path;
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("commands")).toArray();
    int loaded = 0;
    for (const QJsonValue &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();
        Command cmd;
        cmd.name = o.value(QStringLiteral("name")).toString().toLower();
        if (cmd.name.isEmpty()) continue;
        cmd.description = o.value(QStringLiteral("description")).toString();
        const QString depth = o.value(QStringLiteral("depth")).toString();
        cmd.depth = (depth.compare(QLatin1String("comprehensive"), Qt::CaseInsensitive) == 0)
            ? SynthesisDepth::Comprehensive
            : SynthesisDepth::Quick;
        cmd.extractEntity = o.value(QStringLiteral("extractEntity")).toBool(false);
        cmd.includeActiveDoc = o.value(QStringLiteral("includeActiveDoc")).toBool(false);
        cmd.systemPrompt = o.value(QStringLiteral("systemPrompt")).toString();
        cmd.maxTokens = o.value(QStringLiteral("maxTokens")).toInt(4096);
        registerCommand(cmd);
        ++loaded;
    }
    RPGFORGE_DLOG("INLINE-AI") << "loaded" << loaded << "user commands from" << path;
}

void InlineAIInvoker::cancelActive()
{
    if (!m_invocationActive) return;
    RPGFORGE_DLOG("INLINE-AI") << "cancelActive: aborting in-flight invocation";
    // If a placeholder is still on screen (stream never started), keep
    // the user's original @cmd line visible. If it did start, leave
    // whatever was streamed so far — the user can edit or delete.
    clearActiveInvocation();
}

void InlineAIInvoker::registerDefaultCommands()
{
    // @lore <entity>  — Comprehensive, entity-focused. Matches
    // LoreKeeper's dossier-generation pipeline: wide retrieval, multi-
    // hop synthesis, citation-preserving assembly. Best for "tell me
    // about X" style queries where breadth matters.
    registerCommand({
        QStringLiteral("lore"),
        i18n("Project-wide lore synthesis for a named entity (multi-hop)."),
        SynthesisDepth::Comprehensive,
        /*extractEntity=*/true,
        /*includeActiveDoc=*/false,
        QStringLiteral("Centre your answer on the requested entity. Draw on "
                       "all relevant project material and cite sources inline."),
        8192
    });

    // @forge <request>  — Comprehensive, no entity. "Write me the next
    // scene", "How does the magic system handle X?", etc.
    registerCommand({
        QStringLiteral("forge"),
        i18n("Project-wide synthesis (multi-hop, comprehensive retrieval)."),
        SynthesisDepth::Comprehensive,
        false,
        /*includeActiveDoc=*/true,
        QStringLiteral("Synthesise across the project. Prioritise consistency "
                       "with existing lore and the author's established voice."),
        8192
    });

    // @chat <prompt>  — Quick single-pass. For short questions that
    // don't need multi-hop synthesis. Fast.
    registerCommand({
        QStringLiteral("chat"),
        i18n("Quick single-pass question (one retrieval, one generation)."),
        SynthesisDepth::Quick,
        false,
        /*includeActiveDoc=*/true,
        QString(),
        2048
    });

    // @rewrite <text>  — Active-doc context, user asks for an edit.
    registerCommand({
        QStringLiteral("rewrite"),
        i18n("Rewrite the supplied text in the project's voice."),
        SynthesisDepth::Quick,
        false,
        /*includeActiveDoc=*/true,
        QStringLiteral("Rewrite the user-provided text. Preserve meaning and "
                       "factual content. Match the author's established tone "
                       "and register. Return ONLY the rewritten passage."),
        4096
    });

    // @expand <seed>  — Take an outline / sketch and grow it.
    registerCommand({
        QStringLiteral("expand"),
        i18n("Expand a short outline or sketch into fuller prose."),
        SynthesisDepth::Comprehensive,
        false,
        /*includeActiveDoc=*/true,
        QStringLiteral("Expand the user's short seed into fuller prose, "
                       "consistent with the project's established world, "
                       "characters, and voice. Return ONLY the expanded text."),
        8192
    });

    // @summarize  — No user prompt needed; the active doc IS the query.
    registerCommand({
        QStringLiteral("summarize"),
        i18n("One-paragraph summary of the active document."),
        SynthesisDepth::Quick,
        false,
        /*includeActiveDoc=*/true,
        QStringLiteral("Write a single-paragraph summary of the active "
                       "document. Focus on the narrative throughline and "
                       "the most salient characters or setting elements."),
        1024
    });
}

// eventFilter() retained only so existing installEventFilter calls on
// subclasses don't break link-time; all actual key handling now lives
// in the QShortcuts installed by the constructor.
bool InlineAIInvoker::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    Q_UNUSED(event);
    return false;
}

bool InlineAIInvoker::tryDispatchOnCurrentLine()
{
    if (!m_view || !m_view->document()) return false;

    KTextEditor::Document *doc = m_view->document();
    const KTextEditor::Cursor cursor = m_view->cursorPosition();
    if (!cursor.isValid()) return false;

    const int lineNumber = cursor.line();
    const QString rawLine = doc->line(lineNumber);

    // Command pattern: leading whitespace, @, identifier, whitespace,
    // then the prompt body (to end of line). Returns empty match
    // groups when the current line isn't an @-invocation.
    static const QRegularExpression trigger(
        QStringLiteral("^\\s*@([a-zA-Z][a-zA-Z0-9_]*)(?:\\s+(.*))?$"));
    const auto m = trigger.match(rawLine);
    if (!m.hasMatch()) return false;

    const QString commandName = m.captured(1).toLower();
    const QString rest = m.captured(2).trimmed();
    if (!m_commands.contains(commandName)) {
        RPGFORGE_DLOG("INLINE-AI") << "unknown command on line:" << rawLine;
        return false;
    }

    // For entity-extracting commands (@lore), the first word after the
    // command is the entity; the user prompt is the remainder. For
    // other commands the whole rest is the user prompt.
    const Command cmd = m_commands.value(commandName);
    QString entityName;
    QString userPrompt;
    if (cmd.extractEntity) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) {
            entityName = rest;  // whole thing is the entity, prompt is empty
        } else {
            entityName = rest.left(sp).trimmed();
            userPrompt = rest.mid(sp + 1).trimmed();
        }
        if (userPrompt.isEmpty()) {
            userPrompt = i18n("Provide comprehensive information about %1 "
                              "drawn from the project.", entityName);
        }
    } else {
        userPrompt = rest;
        if (userPrompt.isEmpty() && cmd.name != QLatin1String("summarize")) {
            // Summarize is the only command that tolerates an empty
            // prompt (it targets the active doc). Others need a prompt.
            RPGFORGE_DLOG("INLINE-AI") << "command @" << commandName
                                        << "needs a prompt; ignoring";
            return false;
        }
        if (userPrompt.isEmpty()) {
            userPrompt = i18n("Summarise the active document.");
        }
    }

    RPGFORGE_DLOG("INLINE-AI") << "dispatching @" << commandName
                                << "entity=" << entityName
                                << "prompt=" << userPrompt;

    beginInvocation(cmd, userPrompt, entityName,
                    lineNumber, rawLine.length(), rawLine);
    return true;
}

void InlineAIInvoker::beginInvocation(const Command &cmd,
                                       const QString &userPrompt,
                                       const QString &entityName,
                                       int lineNumber,
                                       int lineLength,
                                       const QString &originalLine)
{
    // Cancel any in-flight invocation first. Only one at a time.
    if (m_invocationActive) {
        RPGFORGE_DLOG("INLINE-AI") << "cancelling prior in-flight invocation";
        clearActiveInvocation();
    }

    KTextEditor::Document *doc = m_view->document();
    if (!doc) {
        RPGFORGE_DLOG("INLINE-AI") << "no document — aborting";
        return;
    }

    // EditingTransaction (RAII) batches the line-replacement so Ctrl+Z
    // reverts it as one undo step. Streamed updates get their own
    // transactions in the chunk callback.
    {
        KTextEditor::Document::EditingTransaction txn(doc);
        const KTextEditor::Range commandRange(lineNumber, 0, lineNumber, lineLength);
        doc->replaceText(commandRange, kPlaceholderText);
    }

    // MovingRange tracks the placeholder as the user (or further
    // streamed chunks) edits the surrounding text. Expand-out behavior
    // means insertions INSIDE the range grow it, so streamed tokens
    // naturally accumulate.
    const KTextEditor::Range placeholderRange(
        lineNumber, 0, lineNumber, kPlaceholderText.length());
    m_activeRange = doc->newMovingRange(placeholderRange,
        KTextEditor::MovingRange::ExpandLeft | KTextEditor::MovingRange::ExpandRight);

    m_invocationActive = true;
    m_activeOriginalLine = originalLine;
    m_activeAccumulated.clear();
    // Seed the type-over guard with the placeholder text. First chunk
    // arrival will compare current range content against this; if the
    // user has already typed inside the placeholder, we bail out
    // before clobbering their text.
    m_lastWrittenText = kPlaceholderText;

    // Build the request. Active-doc inclusion pulls the whole editor
    // content as priority-0 context when the command opted in.
    RagAssistRequest req;
    req.serviceName = QStringLiteral("Inline AI: @") + cmd.name;
    req.settingsKey = QStringLiteral("inline_ai/") + cmd.name + QStringLiteral("_model");
    req.userPrompt = userPrompt;
    req.entityName = entityName;
    req.systemPrompt = cmd.systemPrompt;
    req.depth = cmd.depth;
    req.maxTokens = cmd.maxTokens;
    req.stream = true;
    req.requireCitations = true;

    if (cmd.includeActiveDoc) {
        const QString activeDoc = doc->text();
        const QString activePath = doc->url().toLocalFile();
        if (!activeDoc.isEmpty()) {
            ContextSource src;
            src.label = i18n("ACTIVE DOCUMENT");
            src.content = activeDoc;
            src.citation = activePath;
            src.priority = 0;
            req.extraSources.append(src);
        }
        req.activeFilePath = activePath;
    }

    // Callbacks: onChunk updates the MovingRange incrementally;
    // onComplete writes the final text; onError restores the original
    // @cmd line with an error comment.
    QPointer<InlineAIInvoker> weakThis(this);
    RagAssistCallbacks cb;
    // Log which passages the RAG pipeline surfaced so we can see
    // whether the retrieval is actually contributing context, vs.
    // the LLM only having the active-doc extraSource.
    cb.onPassagesRetrieved = [](const QString &requestId, const QStringList &paths) {
        RPGFORGE_DLOG("INLINE-AI") << "RAG retrieved" << paths.size()
                                    << "passage(s) for request" << requestId;
        for (const QString &p : paths) {
            RPGFORGE_DLOG("INLINE-AI") << "  passage:" << p;
        }
    };
    cb.onChunk = [weakThis](const QString &, const QString &chunk) {
        if (!weakThis || !weakThis->m_invocationActive || !weakThis->m_activeRange) return;

        KTextEditor::Document *d = weakThis->m_view->document();
        if (!d) return;

        // User-type-over abort: if the text currently sitting in the
        // MovingRange doesn't match what we wrote last time, the user
        // has edited the placeholder/streamed output. Abandon the
        // stream rather than clobbering their edits.
        const QString currentText = d->text(weakThis->m_activeRange->toRange());
        if (currentText != weakThis->m_lastWrittenText
            && !weakThis->m_lastWrittenText.isEmpty()) {
            RPGFORGE_DLOG("INLINE-AI") << "user edited pinned range mid-stream — aborting";
            weakThis->clearActiveInvocation();
            return;
        }

        weakThis->m_activeAccumulated += chunk;
        KTextEditor::Document::EditingTransaction txn(d);
        d->replaceText(weakThis->m_activeRange->toRange(),
                       weakThis->m_activeAccumulated);
        weakThis->m_lastWrittenText = weakThis->m_activeAccumulated;
    };
    cb.onComplete = [weakThis](const QString &, const QString &fullText) {
        if (!weakThis || !weakThis->m_invocationActive) return;
        weakThis->finalizeInvocation(fullText);
    };
    cb.onError = [weakThis](const QString &, const QString &message) {
        if (!weakThis || !weakThis->m_invocationActive) return;
        weakThis->handleError(message);
    };

    m_activeRequestId = RagAssistService::instance().generate(req, cb);
}

void InlineAIInvoker::finalizeInvocation(const QString &finalText)
{
    if (!m_invocationActive || !m_activeRange) {
        clearActiveInvocation();
        return;
    }

    KTextEditor::Document *doc = m_view->document();
    if (doc) {
        KTextEditor::Document::EditingTransaction txn(doc);
        doc->replaceText(m_activeRange->toRange(), finalText);
    }
    RPGFORGE_DLOG("INLINE-AI") << "completed invocation; finalText length="
                                << finalText.size();
    clearActiveInvocation();
}

void InlineAIInvoker::handleError(const QString &message)
{
    if (!m_invocationActive || !m_activeRange) {
        clearActiveInvocation();
        return;
    }

    KTextEditor::Document *doc = m_view->document();
    if (doc) {
        // Replace the placeholder with the original @cmd line +
        // an HTML comment recording the error. The comment is
        // invisible in every rendered form (preview, PDF, GitHub
        // markdown view) but persists in the source so the user can
        // see what went wrong without losing their prompt.
        const QString recovery = m_activeOriginalLine +
            QStringLiteral("\n<!-- @-invocation failed: ") + message + QStringLiteral(" -->");
        KTextEditor::Document::EditingTransaction txn(doc);
        doc->replaceText(m_activeRange->toRange(), recovery);
    }
    RPGFORGE_DLOG("INLINE-AI") << "invocation error:" << message;
    clearActiveInvocation();
}

void InlineAIInvoker::clearActiveInvocation()
{
    if (m_activeRange) {
        // Release the MovingRange. Kate will destruct it once nobody
        // else holds a reference.
        delete m_activeRange;
        m_activeRange = nullptr;
    }
    m_activeAccumulated.clear();
    m_activeOriginalLine.clear();
    m_activeRequestId.clear();
    m_lastWrittenText.clear();
    m_invocationActive = false;
}
