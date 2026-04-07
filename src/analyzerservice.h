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

#ifndef ANALYZERSERVICE_H
#define ANALYZERSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include "knowledgebase.h"

struct DiagnosticReference {
    QString filePath;
    int line;
};

enum class DiagnosticSeverity { Error, Warning, Info };

struct Diagnostic {
    QString filePath;
    int line;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    QString message;
    QList<DiagnosticReference> references;
};

/**
 * @brief Service responsible for LLM-powered background analysis and RAG.
 */
class AnalyzerService : public QObject
{
    Q_OBJECT

public:
    static AnalyzerService& instance();

    /**
     * @brief Triggers an analysis for the given file and content.
     * Searches KnowledgeBase for RAG context and requests JSON diagnostics from the LLM.
     */
    void analyzeDocument(const QString &filePath, const QString &content);

    /**
     * @brief Parses a JSON array response into a list of diagnostics.
     */
    static QList<Diagnostic> parseDiagnostics(const QString &jsonResponse);

Q_SIGNALS:
    void analysisStarted(const QString &filePath);
    void diagnosticsUpdated(const QString &filePath, const QList<Diagnostic> &diagnostics);
    void analysisFailed(const QString &filePath, const QString &error);

private Q_SLOTS:
    void onRagSearchCompleted(const QString &filePath, const QString &content, const QList<struct SearchResult> &results);

private:
    explicit AnalyzerService(QObject *parent = nullptr);
    ~AnalyzerService() override;

    AnalyzerService(const AnalyzerService&) = delete;
    AnalyzerService& operator=(const AnalyzerService&) = delete;

    QString m_activeAnalysisFile;
};

#endif // ANALYZERSERVICE_H
