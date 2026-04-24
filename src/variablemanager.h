/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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
    void setLibraryVariables(const QMap<QString, QString> &vars);

    // Resolve an expression (e.g., "{{hp_base}} + 10")
    QString resolve(const QString &expression, bool shouldEvaluateMath = true) const;

    // Process a full markdown text and replace all {{var}} occurrences
    QString processMarkdown(const QString &markdown) const;

    // Get all variable names
    QStringList variableNames() const;

    // Helper to parse YAML front-matter from a markdown string
    static QMap<QString, QString> parseFrontMatter(const QString &markdown);

    struct DocumentMetadata {
        QString title;
        QString status;
        QString synopsis;
        QString label;
    };
    static DocumentMetadata extractMetadata(const QString &markdown);
    static QString stripMetadata(const QString &markdown);

Q_SIGNALS:
    void variablesChanged();

private:
    explicit VariableManager(QObject *parent = nullptr);
    
    QMap<QString, QString> m_projectVars;
    QMap<QString, QString> m_documentVars;
    QMap<QString, QString> m_panelVars;
    QMap<QString, QString> m_libraryVars;

    // Internal helper to get all merged variables (panel > doc > project)
    QMap<QString, QString> mergedVariables() const;

    // Simple expression evaluator (very basic for now)
    double evaluateMath(const QString &expr) const;
};

#endif // VARIABLEMANAGER_H
