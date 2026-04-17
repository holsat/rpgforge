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

#include <QPageLayout>
#include <QPageSize>

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
    auto treeData = ProjectManager::instance().model()->projectData();
    ProjectTreeModel model;
    model.setProjectData(treeData);
    
    // Find Manuscript folder - STRICTLY limit to this
    ProjectTreeItem *manuscript = nullptr;
    ProjectTreeItem *root = model.itemFromIndex(QModelIndex());
    for (auto *child : root->children) {
        if (child->category == ProjectTreeItem::Manuscript) {
            manuscript = child;
            break;
        }
    }

    int chapterCounter = 0;
    if (manuscript) {
        processFolder(manuscript, markdown, options, validationErrors, chapterCounter);
    } else {
        Q_EMIT finished(false, i18n("No 'Manuscript' folder found. To export, right-click a folder in the Project Explorer and set its category to 'Manuscript'."));
        return;
    }

    if (!validationErrors.isEmpty()) {
        QString errorMsg = i18n("Found %1 broken image links. Please fix them before exporting:", (int)validationErrors.size());
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

        auto &pm = ProjectManager::instance();
        QPageSize pageSize;
        if (pm.pageSize().toLower() == QLatin1String("letter")) pageSize = QPageSize(QPageSize::Letter);
        else if (pm.pageSize().toLower() == QLatin1String("legal")) pageSize = QPageSize(QPageSize::Legal);
        else pageSize = QPageSize(QPageSize::A4);

        QPageLayout layout(pageSize, QPageLayout::Portrait, 
            QMarginsF(pm.marginLeft(), pm.marginTop(), pm.marginRight(), pm.marginBottom()),
            QPageLayout::Millimeter);

        m_page->printToPdf(outputPath, layout);
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

void PdfExporter::processFolder(ProjectTreeItem *folder, QString &markdown, const CompileOptions &options, QStringList &errors, int &chapterCounter)
{
    // Skip internal folders if not in Manuscript
    QString folderNameLower = folder->name.toLower();
    bool isManuscript = (folder->category == ProjectTreeItem::Manuscript);
    bool isChapter = (folder->category == ProjectTreeItem::Chapter);

    if (folderNameLower == QStringLiteral("media") || folderNameLower == QStringLiteral("stylesheets")) {
        if (!isManuscript) return;
    }

    if (isChapter) {
        chapterCounter++;
        if (!markdown.isEmpty()) markdown += QStringLiteral("\n\n<div class=\"page-break\"></div>\n\n");
        markdown += QStringLiteral("# Chapter %1: %2\n\n").arg(QString::number(chapterCounter), folder->name);
    }

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
                if (!markdown.isEmpty() && child->category != ProjectTreeItem::Scene && !isChapter) {
                    markdown += QStringLiteral("\n\n<div class=\"page-break\"></div>\n\n");
                }
                
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
                            errors.append(i18n("%1 (Line %2): Broken link -> %3", child->name, i + 1, link));
                        }
                    }
                }

                markdown += VariableManager::stripMetadata(content) + QStringLiteral("\n\n");
            }
        } else {
            processFolder(child, markdown, options, errors, chapterCounter);
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
        "}"
    ).arg(pageSize).arg(mt).arg(mr).arg(mb).arg(ml);

    css += QStringLiteral(
        "body { font-family: serif; line-height: 1.5; color: #000; }"
        ".page-break { break-before: page; }"
        "h1, h2, h3 { break-after: avoid; }"
        "img { max-width: 100%; height: auto; }"
        ".toc h1 { text-align: center; }"
        ".toc ul { list-style-type: none; padding: 0; }"
    );

    // CSS for page numbering (Chromium specific trick for simple page numbers)
    // Note: chromium doesn't support @bottom-right well, so we use a different approach
    // for professional-looking footers if needed, but for now we'll improve the font safety.
    if (options.showPageNumbers) {
        css += QStringLiteral(
            "#footer { position: fixed; bottom: 0; right: 0; font-size: 12pt; font-family: sans-serif; }"
        );
    }

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

    QString footerHtml;
    if (options.showPageNumbers) {
        // Modern approach: @page margin boxes are best but if they fail we'd need a JS injector.
        // Let's try @page again with better padding/sizing or a fixed element.
        // Chromium's printToPdf actually supports a 'headerTemplate' and 'footerTemplate' 
        // but QWebEngineView's printToPdf is simpler.
        // Let's use the CSS @page with more robust positioning.
        
        css.replace(QStringLiteral("@page {"), QStringLiteral(
            "@page {\n"
            "  @bottom-right {\n"
            "    content: counter(page);\n"
            "    font-size: 11pt;\n"
            "    font-family: sans-serif;\n"
            "    vertical-align: middle;\n"
            "    padding-bottom: 5mm;\n"
            "  }\n"
        ));
    }

    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body>%2%3</body></html>"
    ).arg(css).arg(body).arg(footerHtml);
}
