#ifndef VARIABLECOMPLETIONMODEL_H
#define VARIABLECOMPLETIONMODEL_H

#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>

class VariableCompletionModel : public KTextEditor::CodeCompletionModel, public KTextEditor::CodeCompletionModelControllerInterface
{
    Q_OBJECT
    Q_INTERFACES(KTextEditor::CodeCompletionModelControllerInterface)

public:
    explicit VariableCompletionModel(QObject *parent = nullptr);
    ~VariableCompletionModel() override = default;

    // CodeCompletionModel interface
    void completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType) override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    // CodeCompletionModelControllerInterface interface
    bool shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userBehaved, const KTextEditor::Cursor &position) override;
    void executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const override;

private:
    QStringList m_variables;
    void updateVariables();
};

#endif // VARIABLECOMPLETIONMODEL_H
