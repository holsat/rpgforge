#ifndef VARIABLEMANAGER_H
#define VARIABLEMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>

class VariableManager : public QObject
{
    Q_OBJECT

public:
    static VariableManager& instance();

    // Set variables from different sources
    void setProjectVariables(const QMap<QString, QString> &vars);
    void setDocumentVariables(const QMap<QString, QString> &vars);
    void setPanelVariables(const QMap<QString, QString> &vars);

    // Resolve an expression (e.g., "{{hp_base}} + 10")
    QString resolve(const QString &expression, bool shouldEvaluateMath = true) const;

    // Process a full markdown text and replace all {{var}} occurrences
    QString processMarkdown(const QString &markdown) const;

    // Get all variable names
    QStringList variableNames() const;

    // Helper to parse YAML front-matter from a markdown string
    static QMap<QString, QString> parseFrontMatter(const QString &markdown);

Q_SIGNALS:
    void variablesChanged();

private:
    explicit VariableManager(QObject *parent = nullptr);
    
    QMap<QString, QString> m_projectVars;
    QMap<QString, QString> m_documentVars;
    QMap<QString, QString> m_panelVars;

    // Internal helper to get all merged variables (panel > doc > project)
    QMap<QString, QString> mergedVariables() const;

    // Simple expression evaluator (very basic for now)
    double evaluateMath(const QString &expr) const;
};

#endif // VARIABLEMANAGER_H
