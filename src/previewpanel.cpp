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

#include "previewpanel.h"
#include "projectmanager.h"
#include "variablemanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QWebEngineView>
#include <QVBoxLayout>

PreviewPanel::PreviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_webView(new QWebEngineView(this))
    , m_styleWatcher(new QFileSystemWatcher(this))
    , m_debounceTimer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);
    connect(m_debounceTimer, &QTimer::timeout, this, &PreviewPanel::updatePreview);
    connect(m_webView, &QWebEngineView::loadFinished, this, &PreviewPanel::onLoadFinished);

    // Watch for CSS file changes
    connect(m_styleWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Some editors (e.g. vim) delete and recreate files on save — re-add the path
        if (QFile::exists(path) && !m_styleWatcher->files().contains(path)) {
            m_styleWatcher->addPath(path);
        }
        reloadStylesheet();
    });
    connect(m_styleWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        // Files added or removed from the stylesheets folder — refresh watch list and reload
        setupStylesheetWatcher();
        reloadStylesheet();
    });

    // Re-setup watcher when a project is opened or closed
    connect(&ProjectManager::instance(), &ProjectManager::projectOpened,
            this, &PreviewPanel::setupStylesheetWatcher);
    connect(&ProjectManager::instance(), &ProjectManager::projectClosed,
            this, &PreviewPanel::setupStylesheetWatcher);

    if (ProjectManager::instance().isProjectOpen()) {
        setupStylesheetWatcher();
    }

    // Load initial empty skeleton
    m_webView->setHtml(wrapHtml(QString()));
}

void PreviewPanel::setMarkdown(const QString &markdown)
{
    m_pendingMarkdown = markdown;
    m_debounceTimer->start();
}

void PreviewPanel::setProjectMode(bool enabled)
{
    if (m_projectMode != enabled) {
        m_projectMode = enabled;
        m_needsFullReload = true; // Refresh layout if switching modes
        m_debounceTimer->start();
    }
}

void PreviewPanel::setBaseUrl(const QUrl &url)
{
    // Use the project directory as base URL so relative image paths resolve correctly.
    // If no project is open, fall back to the file's parent directory.
    QUrl baseUrl;
    if (ProjectManager::instance().isProjectOpen()) {
        baseUrl = QUrl::fromLocalFile(ProjectManager::instance().projectPath() + QDir::separator());
    } else if (url.isLocalFile()) {
        baseUrl = QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath() + QDir::separator());
    } else {
        baseUrl = url;
    }

    if (m_baseUrl != baseUrl) {
        m_baseUrl = baseUrl;
        m_needsFullReload = true;
        m_debounceTimer->start();
    }
}

void PreviewPanel::reloadStylesheet()
{
    m_needsFullReload = true;
    m_debounceTimer->start();
}

void PreviewPanel::onLoadFinished(bool ok)
{
    m_isLoaded = ok;
    m_needsFullReload = false;
    if (ok) {
        // Now that the page is loaded, update with the latest markdown if any
        if (!m_pendingMarkdown.isEmpty()) {
            updatePreview();
        }
    }
}

void PreviewPanel::updatePreview()
{
    if (m_needsFullReload || !m_isLoaded) {
        m_webView->setHtml(wrapHtml(QString()), m_baseUrl);
        m_needsFullReload = false;
        return;
    }

    QString contentOnly = VariableManager::stripMetadata(m_pendingMarkdown);

    // Replace variables before rendering markdown to HTML
    QString processedMarkdown = VariableManager::instance().processMarkdown(contentOnly);
    QString htmlBody = m_parser.renderHtml(processedMarkdown);
    
    // Escape for JavaScript string
    QString escaped = htmlBody;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('\"'), QLatin1String("\\\""));
    escaped.replace(QLatin1Char('\''), QLatin1String("\\\'"));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    escaped.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    escaped.replace(QLatin1Char('\b'), QLatin1String("\\b"));
    escaped.replace(QLatin1Char('\f'), QLatin1String("\\f"));

    QString js = QStringLiteral(
        "var content = document.getElementById('content');"
        "if (content) {"
        "  content.innerHTML = \"%1\";"
        "  if (window.renderMathInElement) {"
        "    renderMathInElement(content);"
        "  }"
        "}"
    ).arg(escaped);

    m_webView->page()->runJavaScript(js);
}

void PreviewPanel::scrollBy(int x, int y)
{
    m_webView->page()->runJavaScript(QStringLiteral("window.scrollBy(%1, %2)").arg(x).arg(y));
}

void PreviewPanel::scrollToPercentage(double percentage)
{
    // Scroll to a percentage of the document height
    QString js = QStringLiteral(
        "(function() {"
        "  var body = document.body,"
        "      html = document.documentElement;"
        "  var height = Math.max(body.scrollHeight, body.offsetHeight, "
        "                       html.clientHeight, html.scrollHeight, html.offsetHeight);"
        "  var viewHeight = window.innerHeight;"
        "  window.scrollTo(0, (height - viewHeight) * %1);"
        "})();"
    ).arg(percentage);
    m_webView->page()->runJavaScript(js);
}

void PreviewPanel::scrollToLine(int line, bool smooth)
{
    // Find the element with data-sourcepos that corresponds to this line
    // cmark uses 1-based line numbers.
    QString js = QStringLiteral(
        "(function() {"
        "  var targetLine = %1 + 1;"
        "  var els = document.querySelectorAll('[data-sourcepos]');"
        "  var best = null;"
        "  for (var i = 0; i < els.length; i++) {"
        "    var el = els[i];"
        "    var pos = el.getAttribute('data-sourcepos');"
        "    if (pos) {"
        "      var startLine = parseInt(pos.split(':')[0]);"
        "      if (startLine <= targetLine) {"
        "        best = el;"
        "      } else {"
        "        break;" // Elements are ordered by line
        "      }"
        "    }"
        "  }"
        "  if (best) {"
        "    best.scrollIntoView({behavior: '%2', block: 'start'});"
        "  }"
        "})();"
    ).arg(line).arg(smooth ? QStringLiteral("smooth") : QStringLiteral("auto"));
    m_webView->page()->runJavaScript(js);
}

void PreviewPanel::setupStylesheetWatcher()
{
    // Clear previously watched paths
    if (!m_styleWatcher->files().isEmpty())
        m_styleWatcher->removePaths(m_styleWatcher->files());
    if (!m_styleWatcher->directories().isEmpty())
        m_styleWatcher->removePaths(m_styleWatcher->directories());

    if (!ProjectManager::instance().isProjectOpen()) return;

    // Watch the stylesheets/ directory so we notice files being added/removed
    QString folderPath = ProjectManager::instance().stylesheetFolderPath();
    if (QDir(folderPath).exists()) {
        m_styleWatcher->addPath(folderPath);
    }

    // Watch each individual CSS file for content changes
    for (const QString &cssPath : ProjectManager::instance().stylesheetPaths()) {
        m_styleWatcher->addPath(cssPath);
    }
}

QString PreviewPanel::loadProjectStylesheets() const
{
    const QStringList paths = ProjectManager::instance().stylesheetPaths();
    if (paths.isEmpty()) return {};

    QString combined;
    for (const QString &path : paths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            combined += QString::fromUtf8(f.readAll());
            combined += QLatin1Char('\n');
        }
    }
    return combined;
}

QString PreviewPanel::wrapHtml(const QString &body) const
{
    // Base CSS for preview — provides sensible defaults
    static const QString baseCss = QStringLiteral(
        "body { font-family: sans-serif; line-height: 1.6; padding: 2em; max-width: 800px; margin: 0 auto; color: #333; }"
        "pre { background: #f4f4f4; padding: 1em; overflow-x: auto; border-radius: 4px; }"
        "code { font-family: monospace; background: #f4f4f4; padding: 0.2em 0.4em; border-radius: 3px; }"
        "table { border-collapse: collapse; width: 100%; margin: 1em 0; }"
        "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
        "th { background-color: #f2f2f2; }"
        "blockquote { border-left: 4px solid #ddd; padding-left: 1em; color: #666; margin-left: 0; }"
        "img { max-width: 100%; height: auto; }"
    );

    // Load project stylesheets (if any) — applied after base CSS so they can override
    QString projectCss = loadProjectStylesheets();

    // Strip @page rules from project CSS — they only apply to PDF/print and cause
    // WebEngine rendering issues in the preview
    static const QRegularExpression pageRule(QStringLiteral("@page\\s*\\{[^}]*\\}"));
    projectCss.remove(pageRule);

    return QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.css\">"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.js\"></script>"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/contrib/auto-render.min.js\" "
        "onload=\"window.renderMathInElement = renderMathInElement; renderMathInElement(document.body);\"></script>"
        "<style>%1</style>"
        "<style>%2</style>"
        "</head><body><div id=\"content\">%3</div></body></html>"
    ).arg(baseCss, projectCss, body);
}
