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
    m_variables = VariableManager::instance().variableNames();
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
    
    // Start completion if we just typed '{{'
    if (insertedText == QLatin1String("{")) {
        KTextEditor::Document *doc = view->document();
        QString line = doc->line(position.line());
        if (position.column() >= 2 && line.at(position.column() - 2) == QLatin1Char('{')) {
            return true;
        }
    }
    return false;
}

void VariableCompletionModel::executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_variables.size()) return;

    QString completion = m_variables.at(index.row());
    // We want to insert the variable name and close the braces if needed
    // But KTextEditor handles the replacement of 'word'.
    // If we triggered on '{{', the word might be empty or partial.
    view->document()->replaceText(word, completion + QStringLiteral("}}"));
}
