#ifndef DIAGNOSTICDETAILDIALOG_H
#define DIAGNOSTICDETAILDIALOG_H

#include <QDialog>
#include "analyzerservice.h"

class QLabel;
class QTextEdit;
class QPushButton;

class DiagnosticDetailDialog : public QDialog
{
    Q_OBJECT
public:
    enum Action { Cancel, Ignore, Fixed };

    explicit DiagnosticDetailDialog(const Diagnostic &diagnostic, QWidget *parent = nullptr);
    
    Action selectedAction() const { return m_action; }

private Q_SLOTS:
    void onIgnore();
    void onFixed();

private:
    Diagnostic m_diagnostic;
    Action m_action = Cancel;
    
    QLabel *m_infoLabel;
    QTextEdit *m_messageEdit;
};

#endif // DIAGNOSTICDETAILDIALOG_H
