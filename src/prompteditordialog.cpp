#include "prompteditordialog.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

PromptEditorDialog::PromptEditorDialog(const QString &title, const QString &content, QWidget *parent)
    : QDialog(parent), m_plainText(content)
{
    setWindowTitle(i18n("Edit Prompt: %1", title));
    
    auto *layout = new QVBoxLayout(this);
    
    m_editor = new QTextEdit(this);
    m_editor->setPlainText(content);
    m_editor->setAcceptRichText(false);
    layout->addWidget(m_editor);
    
    auto *tools = new QHBoxLayout();
    m_jsonToggle = new QCheckBox(i18n("JSON Mode"), this);
    connect(m_jsonToggle, &QCheckBox::toggled, this, &PromptEditorDialog::toggleJson);
    tools->addWidget(m_jsonToggle);
    tools->addStretch();
    layout->addLayout(tools);
    
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    
    resize(600, 400);
}

QString PromptEditorDialog::content() const
{
    if (m_jsonToggle->isChecked()) {
        // Parse current JSON and return the prompt value
        QJsonDocument doc = QJsonDocument::fromJson(m_editor->toPlainText().toUtf8());
        if (doc.isObject()) {
            return doc.object().value(QStringLiteral("prompt")).toString();
        }
    }
    return m_editor->toPlainText();
}

void PromptEditorDialog::toggleJson(bool checked)
{
    if (checked) {
        // Convert plain text to JSON
        m_plainText = m_editor->toPlainText();
        QJsonObject obj;
        obj[QStringLiteral("prompt")] = m_plainText;
        QJsonDocument doc(obj);
        m_editor->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    } else {
        // Convert JSON back to plain text (if valid)
        QJsonDocument doc = QJsonDocument::fromJson(m_editor->toPlainText().toUtf8());
        if (doc.isObject()) {
            m_editor->setPlainText(doc.object().value(QStringLiteral("prompt")).toString());
        } else {
            // If invalid JSON, just use what's there but warn or ignore?
            // For safety, we can just leave it or use our cached plain text
            m_editor->setPlainText(m_plainText);
        }
    }
}
