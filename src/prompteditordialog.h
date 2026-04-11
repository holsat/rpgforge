#ifndef PROMPTEDITORDIALOG_H
#define PROMPTEDITORDIALOG_H

#include <QDialog>

class QTextEdit;
class QCheckBox;

class PromptEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PromptEditorDialog(const QString &title, const QString &content, QWidget *parent = nullptr);
    
    QString content() const;

private Q_SLOTS:
    void toggleJson(bool checked);

private:
    QTextEdit *m_editor;
    QCheckBox *m_jsonToggle;
    QString m_plainText;
};

#endif // PROMPTEDITORDIALOG_H
