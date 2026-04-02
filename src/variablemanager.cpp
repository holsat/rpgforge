/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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

#include "variablemanager.h"
#include <QRegularExpression>
#include <QDebug>
#include <iostream>

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
    QMap<QString, QString> merged = m_projectVars;
    for (auto it = m_documentVars.begin(); it != m_documentVars.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    for (auto it = m_panelVars.begin(); it != m_panelVars.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QString VariableManager::resolve(const QString &expression, bool shouldEvaluateMath) const
{
    static const int MAX_RESULT_SIZE = 1024 * 1024;
    static QRegularExpression varRegex(QStringLiteral("\\{\\{([A-Za-z0-9_\\.]+)\\}\\}"));
    
    QString result = expression;
    auto vars = mergedVariables();
    
    if (expression.contains(QLatin1String("{{"))) {
        std::cerr << "VariableManager: Resolving expression [" << expression.toStdString() << "]" << std::endl;
    }

    int depth = 0;
    bool changed = true;
    while (changed && depth < 10) {
        changed = false;
        depth++;
        
        auto matches = varRegex.globalMatch(result);
        QList<QRegularExpressionMatch> matchList;
        while (matches.hasNext()) matchList.append(matches.next());

        for (int i = matchList.size() - 1; i >= 0; --i) {
            const auto &match = matchList.at(i);
            QString name = match.captured(1);
            if (vars.contains(name)) {
                QString val = vars.value(name);
                if (val.startsWith(QLatin1String("CALC:"))) val = val.mid(5);
                if (result.length() - match.capturedLength() + val.length() > MAX_RESULT_SIZE) continue;
                result.replace(match.capturedStart(), match.capturedLength(), val);
                changed = true;
            }
        }
    }

    // Simplified math: no QJSEngine for now to ensure stability
    return result;
}

QString VariableManager::processMarkdown(const QString &markdown) const
{
    static QRegularExpression varRegex(QStringLiteral("\\{\\{([A-Za-z0-9_\\.]+)\\}\\}"));
    QString result = markdown;
    auto vars = mergedVariables();
    
    auto it = varRegex.globalMatch(result);
    if (!it.hasNext()) return markdown;

    QList<QRegularExpressionMatch> matches;
    auto it2 = varRegex.globalMatch(result);
    while (it2.hasNext()) matches.append(it2.next());
    
    for (int i = matches.size() - 1; i >= 0; --i) {
        auto match = matches.at(i);
        QString name = match.captured(1);
        if (!vars.contains(name)) continue;

        QString rawVal = vars.value(name);
        bool shouldCalc = rawVal.startsWith(QLatin1String("CALC:"));
        QString expression = shouldCalc ? rawVal.mid(5) : rawVal;
        
        QString resolved = resolve(expression, shouldCalc);
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
    if (!markdown.startsWith(QLatin1String("---"))) return vars;
    int endIdx = markdown.indexOf(QLatin1String("---"), 3);
    if (endIdx == -1) return vars;
    
    QString frontMatter = markdown.mid(3, endIdx - 3);
    static QRegularExpression lineRegex(QStringLiteral("^([A-Za-z0-9_\\.]+):\\s*(.*)$"));
    for (const QString &line : frontMatter.split(QLatin1Char('\n'))) {
        auto match = lineRegex.match(line.trimmed());
        if (match.hasMatch()) vars.insert(match.captured(1), match.captured(2).trimmed());
    }
    return vars;
}

VariableManager::DocumentMetadata VariableManager::extractMetadata(const QString &markdown)
{
    auto vars = parseFrontMatter(markdown);
    DocumentMetadata meta;
    meta.title = vars.value(QStringLiteral("title"));
    meta.status = vars.value(QStringLiteral("status"), QStringLiteral("Draft"));
    meta.synopsis = vars.value(QStringLiteral("synopsis"));
    meta.label = vars.value(QStringLiteral("label"));
    return meta;
}

QString VariableManager::stripMetadata(const QString &markdown)
{
    if (!markdown.startsWith(QLatin1String("---"))) return markdown;
    int endIdx = markdown.indexOf(QLatin1String("---"), 3);
    if (endIdx == -1) return markdown;
    return markdown.mid(endIdx + 3).trimmed();
}
