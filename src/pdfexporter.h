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

#ifndef PDFEXPORTER_H
#define PDFEXPORTER_H

#include <QObject>
#include <QString>

class QWebEnginePage;
struct ProjectTreeItem;
struct CompileOptions;

class PdfExporter : public QObject
{
    Q_OBJECT

public:
    explicit PdfExporter(QObject *parent = nullptr);
    ~PdfExporter() override;

    void exportProject(const QString &outputPath, const CompileOptions &options);

Q_SIGNALS:
    void finished(bool success, const QString &message);
    void progress(int current, int total);

private:
    void processFolder(ProjectTreeItem *folder, QString &markdown, const CompileOptions &options, QStringList &errors, int &chapterCounter);
    QString wrapHtml(const QString &body, const CompileOptions &options) const;

    QWebEnginePage *m_page = nullptr;
};

#endif // PDFEXPORTER_H
