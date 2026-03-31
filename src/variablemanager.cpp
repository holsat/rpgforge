#include "variablemanager.h"
#include <QRegularExpression>
#include <QJSEngine>
#include <QDebug>

VariableManager& VariableManager::instance()
{
    static VariableManager inst;
    return inst;
}

VariableManager::VariableManager(QObject *parent)
    : QObject(parent)
{
}

void VariableManager::setProjectVariables(const QMap<QString, QString> &vars)
{
    if (m_projectVars != vars) {
        m_projectVars = vars;
        Q_EMIT variablesChanged();
    }
}

void VariableManager::setDocumentVariables(const QMap<QString, QString> &vars)
{
    if (m_documentVars != vars) {
        m_documentVars = vars;
        Q_EMIT variablesChanged();
    }
}

void VariableManager::setPanelVariables(const QMap<QString, QString> &vars)
{
    if (m_panelVars != vars) {
        m_panelVars = vars;
        Q_EMIT variablesChanged();
    }
}

QMap<QString, QString> VariableManager::mergedVariables() const
{
    // Merged variables (Panel > Document > Project)
    QMap<QString, QString> merged = m_projectVars;
    for (auto it = m_documentVars.begin(); it != m_documentVars.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    for (auto it = m_panelVars.begin(); it != m_panelVars.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QString VariableManager::resolve(const QString &expression) const
{
    // Find all occurrences of {{variable_name}}
    static QRegularExpression varRegex(QStringLiteral("\\{\\{([A-Za-z0-9_\\.]+)\\}\\}"));
    
    QString result = expression;
    auto vars = mergedVariables();
    
    // Resolve nested variables (up to a limit to prevent cycles)
    int depth = 0;
    bool changed = true;
    while (changed && depth < 10) {
        changed = false;
        depth++;
        
        auto it = varRegex.globalMatch(result);
        QList<QRegularExpressionMatch> matches;
        while (it.hasNext()) matches.append(it.next());

        for (int i = matches.size() - 1; i >= 0; --i) {
            auto match = matches.at(i);
            QString name = match.captured(1);
            if (vars.contains(name)) {
                result.replace(match.capturedStart(), match.capturedLength(), vars.value(name));
                changed = true;
            }
        }
    }

    // After resolving all variables, if the string looks like a math expression, evaluate it
    // Check if it contains only digits, operators, and parentheses
    static QRegularExpression mathCheckRegex(QStringLiteral("^[0-9\\+\\-\\*/\\(\\)\\.\\s]+$"));
    if (mathCheckRegex.match(result).hasMatch() && result.trimmed().length() > 0) {
        // Only evaluate if it's not JUST a single number (optional optimization)
        // or if it contains operators.
        if (result.contains(QRegularExpression(QStringLiteral("[\\+\\-\\*/\\(\\)]")))) {
            QJSEngine engine;
            auto val = engine.evaluate(result);
            if (!val.isError()) {
                result = val.toString();
            }
        }
    }
    
    return result;
}

QString VariableManager::processMarkdown(const QString &markdown) const
{
    // Find all {{variable_name}} and replace them with their resolved values
    static QRegularExpression varRegex(QStringLiteral("\\{\\{([A-Za-z0-9_\\.]+)\\}\\}"));
    
    QString result = markdown;
    
    auto it = varRegex.globalMatch(result);
    // Work backwards to avoid offset issues
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext()) {
        matches.append(it.next());
    }
    
    for (int i = matches.size() - 1; i >= 0; --i) {
        auto match = matches.at(i);
        QString resolved = resolve(match.captured(0));
        result.replace(match.capturedStart(), match.capturedLength(), resolved);
    }
    
    return result;
}

QStringList VariableManager::variableNames() const
{
    return mergedVariables().keys();
}

QMap<QString, QString> VariableManager::parseFrontMatter(const QString &markdown)
{
    QMap<QString, QString> vars;
    
    // Check for front matter (must start at line 0 with ---)
    if (!markdown.startsWith(QLatin1String("---"))) {
        return vars;
    }
    
    int endIdx = markdown.indexOf(QLatin1String("---"), 3);
    if (endIdx == -1) {
        return vars;
    }
    
    QString frontMatter = markdown.mid(3, endIdx - 3);
    QStringList lines = frontMatter.split(QLatin1Char('\n'));
    
    static QRegularExpression lineRegex(QStringLiteral("^([A-Za-z0-9_\\.]+):\\s*(.*)$"));
    for (const QString &line : lines) {
        auto match = lineRegex.match(line.trimmed());
        if (match.hasMatch()) {
            vars.insert(match.captured(1), match.captured(2).trimmed());
        }
    }
    
    return vars;
}
