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

#include "pdfexporter.h"
#include "projectmanager.h"
#include "projecttreemodel.h"
#include "markdownparser.h"
#include "variablemanager.h"
#include "compiledialog.h"

#include <KLocalizedString>
#include <QWebEnginePage>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QRegularExpression>

static int statusToLevel(const QString &status) {
    if (status == QStringLiteral("Draft")) return 0;
    if (status == QStringLiteral("Work in Progress")) return 1;
    if (status == QStringLiteral("In Review")) return 2;
    if (status == QStringLiteral("Final")) return 3;
    return 0;
}

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent), m_page(new QWebEnginePage(this))
{
}

PdfExporter::~PdfExporter() = default;

void PdfExporter::exportProject(const QString &outputPath, const CompileOptions &options)
{
    if (!ProjectManager::instance().isProjectOpen()) {
        Q_EMIT finished(false, QStringLiteral("No project open"));
        return;
    }

    QString markdown;
    QStringList validationErrors;
    auto treeData = ProjectManager::instance().tree();
    ProjectTreeModel model;
    model.setProjectData(treeData);
    
    processFolder(model.itemFromIndex(QModelIndex()), markdown, options, validationErrors);

    if (!validationErrors.isEmpty()) {
        QString errorMsg = i18n("Found %1 broken image links. Please fix them before exporting:").arg(validationErrors.size());
        errorMsg += QStringLiteral("\n\n") + validationErrors.join(QLatin1Char('\n'));
        Q_EMIT finished(false, errorMsg);
        return;
    }

    if (markdown.isEmpty()) {
        Q_EMIT finished(false, QStringLiteral("Project contains no files matching the status filter."));
        return;
    }

    // Resolve variables
    QString resolved = VariableManager::instance().processMarkdown(markdown);
    
    // Render to HTML
    MarkdownParser parser;
    QString htmlBody = parser.renderHtml(resolved);

    // Add TOC if requested
    if (options.createTOC) {
        auto headings = parser.parseHeadings(resolved);
        if (!headings.isEmpty()) {
            QString tocHtml = QStringLiteral("<div class=\"toc\"><h1>Table of Contents</h1><ul>");
            for (const auto &h : headings) {
                tocHtml += QStringLiteral("<li style=\"margin-left: %1em\">%2</li>").arg((h.level - 1) * 1.5).arg(h.text);
            }
            tocHtml += QStringLiteral("</ul></div><div class=\"page-break\"></div>");
            htmlBody.prepend(tocHtml);
        }
    }

    QString fullHtml = wrapHtml(htmlBody, options);

    // DEBUG: Write HTML to a temp file to see what's being loaded
    QFile debugFile(outputPath + QStringLiteral(".html"));
    if (debugFile.open(QIODevice::WriteOnly)) {
        debugFile.write(fullHtml.toUtf8());
        debugFile.close();
    }

    // Use absolute path for base URL
    QUrl baseUrl = QUrl::fromLocalFile(QDir(ProjectManager::instance().projectPath()).absolutePath() + QDir::separator());

    // Use a single connection for loadFinished
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_page, &QWebEnginePage::loadFinished, this, [this, outputPath, connection](bool ok) {
        if (!*connection) return;
        disconnect(*connection);
        
        if (!ok) {
            Q_EMIT finished(false, QStringLiteral("Failed to load content into the rendering engine. Check for broken image links or invalid HTML."));
            return;
        }

        m_page->printToPdf(outputPath);
    });

    auto pdfConnection = std::make_shared<QMetaObject::Connection>();
    *pdfConnection = connect(m_page, &QWebEnginePage::pdfPrintingFinished, this, [this, pdfConnection](const QString &filePath, bool success) {
        if (!*pdfConnection) return;
        disconnect(*pdfConnection);
        Q_UNUSED(filePath);
        Q_EMIT finished(success, success ? QStringLiteral("PDF exported successfully") : QStringLiteral("Failed to print PDF file."));
    });

    m_page->setHtml(fullHtml, baseUrl);
}

void PdfExporter::processFolder(ProjectTreeItem *folder, QString &markdown, const CompileOptions &options, QStringList &errors)
{
    int minLevel = statusToLevel(options.minStatus);

    for (auto *child : folder->children) {
        if (child->type == ProjectTreeItem::File) {
            QString projectDir = ProjectManager::instance().projectPath();
            QString fullPath = QDir(projectDir).absoluteFilePath(child->path);
            
            // Check status filter
            bool skip = false;
            if (options.filterByStatus) {
                QFile file(fullPath);
                int fileLevel = 0;
                if (file.open(QIODevice::ReadOnly)) {
                    auto meta = VariableManager::extractMetadata(QString::fromUtf8(file.readAll()));
                    fileLevel = statusToLevel(meta.status);
                    file.close();
                } else {
                    fileLevel = statusToLevel(child->status);
                }
                
                if (fileLevel < minLevel) skip = true;
            }

            if (skip) continue;

            QFile file(fullPath);
            if (file.open(QIODevice::ReadOnly)) {
                if (!markdown.isEmpty()) markdown += QStringLiteral("\n\n<div class=\"page-break\"></div>\n\n");
                
                QString content = QString::fromUtf8(file.readAll());
                
                // VALIDATION PASS
                QStringList lines = content.split(QLatin1Char('\n'));
                static QRegularExpression imgRegex(QStringLiteral("\\!\\[.*\\]\\((.*)\\)"));
                for (int i = 0; i < lines.size(); ++i) {
                    auto it = imgRegex.globalMatch(lines[i]);
                    while (it.hasNext()) {
                        auto match = it.next();
                        QString link = match.captured(1);
                        // Strip query params/anchors
                        if (link.contains(QLatin1Char('#'))) link = link.split(QLatin1Char('#')).first();
                        if (link.contains(QLatin1Char('?'))) link = link.split(QLatin1Char('?')).first();
                        
                        // Resolve relative to the markdown file itself
                        QString absLink = QDir(QFileInfo(fullPath).absolutePath()).absoluteFilePath(link);
                        if (!QFile::exists(absLink)) {
                            errors.append(i18n("%1 (Line %2): Broken link -> %3").arg(child->name).arg(i + 1).arg(link));
                        }
                    }
                }

                markdown += VariableManager::stripMetadata(content);
            }
        } else {
            processFolder(child, markdown, options, errors);
        }
    }
}

QString PdfExporter::wrapHtml(const QString &body, const CompileOptions &options) const
{
    auto &pm = ProjectManager::instance();
    QString pageSize = pm.pageSize().toLower();
    double ml = pm.marginLeft();
    double mr = pm.marginRight();
    double mt = pm.marginTop();
    double mb = pm.marginBottom();

    QString css = QStringLiteral(
        "@page {"
        "  size: %1;"
        "  margin: %2mm %3mm %4mm %5mm;"
    ).arg(pageSize).arg(mt).arg(mr).arg(mb).arg(ml);

    if (options.showPageNumbers) {
        css += QStringLiteral(
            "  @bottom-right {"
            "    content: counter(page);"
            "    font-size: 10pt;"
            "  }"
        );
    }
    css += QStringLiteral("}");

    css += QStringLiteral(
        "body { font-family: serif; line-height: 1.5; color: #000; }"
        ".page-break { break-before: page; }"
        "h1, h2, h3 { break-after: avoid; }"
        "img { max-width: 100%; height: auto; }"
        ".toc h1 { text-align: center; }"
        ".toc ul { list-style-type: none; padding: 0; }"
    );

    // Apply all project stylesheets from the stylesheets/ folder
    if (options.applyStylesheet) {
        for (const QString &stylePath : pm.stylesheetPaths()) {
            QFile styleFile(stylePath);
            if (styleFile.open(QIODevice::ReadOnly)) {
                css += QString::fromUtf8(styleFile.readAll());
                css += QLatin1Char('\n');
            }
        }
    }

    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body>%2</body></html>"
    ).arg(css).arg(body);
}
