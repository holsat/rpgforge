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
    beginResetModel();
    m_variables.clear();
    const auto names = VariableManager::instance().variableNames();
    for (const QString &name : names) {
        if (name.startsWith(QLatin1String("CALC:"))) {
            m_variables.append(name.mid(5));
        } else {
            m_variables.append(name);
        }
    }
    endResetModel();
}

int VariableCompletionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_variables.size();
}

QVariant VariableCompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_variables.size()) return QVariant();

    if (role == Qt::DisplayRole) {
        return m_variables.at(index.row());
    }
    
    if (role == Qt::DecorationRole) {
        return QIcon::fromTheme(QStringLiteral("code-variable"));
    }

    if (role == KTextEditor::CodeCompletionModel::CompletionRole) {
        return KTextEditor::CodeCompletionModel::Variable;
    }

    return QVariant();
}

void VariableCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED(view);
    Q_UNUSED(range);
    Q_UNUSED(invocationType);
    updateVariables();
}

bool VariableCompletionModel::shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position)
{
    Q_UNUSED(userBehaved);
    
    // Trigger if user typed '{' and the previous character was also '{'
    if (insertedText == QLatin1String("{")) {
        KTextEditor::Document *doc = view->document();
        if (position.column() >= 2) {
            QString prev2 = doc->text(KTextEditor::Range(position.line(), position.column() - 2, position.line(), position.column()));
            if (prev2 == QLatin1String("{{")) {
                return true;
            }
        }
    }
    
    // Also trigger if we are already inside {{ and the user types a letter
    KTextEditor::Document *doc = view->document();
    QString line = doc->line(position.line());
    int start = position.column();
    while (start > 0 && line.at(start - 1).isLetterOrNumber()) {
        start--;
    }
    if (start >= 2 && line.mid(start - 2, 2) == QLatin1String("{{")) {
        return true;
    }

    return false;
}

void VariableCompletionModel::executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_variables.size()) return;

    QString completion = m_variables.at(index.row());
    KTextEditor::Document *doc = view->document();
    
    // We want to replace the current "word" (variable name being typed)
    // and potentially the prefix {{ if we're doing a full replacement.
    // However, KTextEditor usually gives us the range of the current word.
    doc->replaceText(word, completion);
    
    // Check if we need to add the closing }}
    KTextEditor::Cursor cursor = view->cursorPosition();
    QString restOfLine = doc->line(cursor.line()).mid(cursor.column());
    if (!restOfLine.startsWith(QLatin1String("}}"))) {
        doc->insertText(cursor, QStringLiteral("}}"));
    }
}
