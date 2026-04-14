#include "diagnosticdetaildialog.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>

DiagnosticDetailDialog::DiagnosticDetailDialog(const Diagnostic &diagnostic, QWidget *parent)
    : QDialog(parent), m_diagnostic(diagnostic)
{
    setWindowTitle(i18n("Issue Details"));
    
    auto *layout = new QVBoxLayout(this);
    
    QString severityStr;
    switch(diagnostic.severity) {
        case DiagnosticSeverity::Error: severityStr = i18n("Error"); break;
        case DiagnosticSeverity::Warning: severityStr = i18n("Warning"); break;
        case DiagnosticSeverity::Info: severityStr = i18n("Info"); break;
    }
    
    m_infoLabel = new QLabel(this);
    m_infoLabel->setText(i18n("<b>%1</b> in %2 at line %3", 
                               severityStr, 
                               QFileInfo(diagnostic.filePath).fileName(), 
                               diagnostic.line));
    layout->addWidget(m_infoLabel);
    
    m_messageEdit = new QTextEdit(this);
    m_messageEdit->setPlainText(diagnostic.message);
    m_messageEdit->setReadOnly(true);
    layout->addWidget(m_messageEdit);
    
    auto *buttonBox = new QDialogButtonBox(this);
    
    auto *ignoreBtn = buttonBox->addButton(i18n("Ignore"), QDialogButtonBox::ActionRole);
    ignoreBtn->setToolTip(i18n("Don't show this message again in this session."));
    connect(ignoreBtn, &QPushButton::clicked, this, &DiagnosticDetailDialog::onIgnore);
    
    auto *fixedBtn = buttonBox->addButton(i18n("Fixed"), QDialogButtonBox::ActionRole);
    fixedBtn->setToolTip(i18n("Remove this message, I've fixed the issue."));
    connect(fixedBtn, &QPushButton::clicked, this, &DiagnosticDetailDialog::onFixed);
    
    buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    layout->addWidget(buttonBox);
    
    resize(500, 300);
}

void DiagnosticDetailDialog::onIgnore()
{
    m_action = Ignore;
    accept();
}

void DiagnosticDetailDialog::onFixed()
{
    m_action = Fixed;
    accept();
}
