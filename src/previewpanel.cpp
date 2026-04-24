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

#include "previewpanel.h"
#include "projectmanager.h"
#include "variablemanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWebEngineView>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QVBoxLayout>

namespace {
// Subclass only exists to surface Chromium console messages into our debug
// log. Renderer errors (e.g. "Not allowed to load local resource") are
// otherwise silent.
class LoggingWebEnginePage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message,
                                  int lineNumber,
                                  const QString &sourceID) override
    {
        const char *lvl = (level == ErrorMessageLevel)   ? "ERROR"
                        : (level == WarningMessageLevel) ? "WARN"
                        :                                  "INFO";
        qInfo().noquote() << "PreviewPanel[webengine]" << lvl
                          << sourceID << ":" << lineNumber << "-" << message;
    }
};
} // namespace
#include <QApplication>
#include <KColorScheme>
#include <KLocalizedString>

PreviewPanel::PreviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_webView(new QWebEngineView(this))
    , m_styleWatcher(new QFileSystemWatcher(this))
    , m_debounceTimer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    // Install a logging page subclass so Chromium renderer errors (e.g. blocked
    // file:// fetches) surface in our log instead of disappearing silently.
    m_webView->setPage(new LoggingWebEnginePage(m_webView));

    // Allow the preview page (loaded via setHtml with a file:// baseUrl) to
    // fetch other file:// resources — markdown-referenced images live under
    // the project's media/ directory and are loaded as <img src="file://...">.
    // QtWebEngine defaults are restrictive for cross-origin file access.
    if (auto *settings = m_webView->settings()) {
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    }

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

    // One stable temp file per process; loadHtmlViaTempFile() overwrites it
    // before each full reload. Stays in /tmp so it's cleaned on reboot.
    m_tempHtmlPath = QDir::tempPath()
        + QStringLiteral("/rpgforge-preview-")
        + QString::number(QCoreApplication::applicationPid())
        + QStringLiteral(".html");

    // Report Pandoc availability up-front so the user can tell from logs
    // whether the preview will use the native Pandoc pipeline or fall back
    // to cmark-gfm with legacy shims.
    if (QStandardPaths::findExecutable(QStringLiteral("pandoc")).isEmpty()) {
        qWarning().noquote() << "PreviewPanel: pandoc not found on PATH — preview will use cmark-gfm fallback.";
    } else {
        qInfo().noquote() << "PreviewPanel: using pandoc for markdown rendering.";
    }

    loadHtmlViaTempFile(wrapHtml(QString()));
}

void PreviewPanel::loadHtmlViaTempFile(const QString &html)
{
    QFile f(m_tempHtmlPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << "PreviewPanel: failed to open preview temp file"
                             << m_tempHtmlPath << ":" << f.errorString();
        // Fallback to setHtml so preview still shows something, even if
        // cross-path images will be broken until we recover.
        m_webView->setHtml(html, m_baseUrl);
        return;
    }
    f.write(html.toUtf8());
    f.close();
    m_webView->load(QUrl::fromLocalFile(m_tempHtmlPath));
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
    // Base URL controls how <img src="..."> and similar relative paths
    // resolve. Standard markdown convention: paths are relative to the
    // DOCUMENT's directory — not the project root. Using the project
    // root breaks every "../../media/X.png" link in a file nested one
    // or more directories deep, because the editor resolves it as
    // "project-root/../../media" which is two levels above the project.
    QUrl baseUrl;
    if (url.isLocalFile()) {
        baseUrl = QUrl::fromLocalFile(
            QFileInfo(url.toLocalFile()).absolutePath() + QDir::separator());
    } else if (ProjectManager::instance().isProjectOpen()) {
        // No document URL — fall back to project root so at least
        // project-relative paths resolve.
        baseUrl = QUrl::fromLocalFile(
            ProjectManager::instance().projectPath() + QDir::separator());
    } else {
        baseUrl = url;
    }

    qInfo().noquote() << "PreviewPanel::setBaseUrl input=" << url.toString()
                      << "resolved=" << baseUrl.toString();

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

QString PreviewPanel::resolveRelativeImageUrlsInMarkdown(const QString &markdown) const
{
    // Rewrite every ![alt](relative/path) in the MARKDOWN so relative
    // paths become absolute file:// URLs before cmark-gfm runs. The
    // app's documents mix two conventions:
    //   - Imported docs (Word importer, Scrivener) emit "./media/X.png"
    //     assuming the base is the project root.
    //   - Hand-authored / drag-dropped paths emit "../../media/X.png"
    //     assuming the base is the document's directory.
    //
    // A single baseUrl can only accommodate one of these. We try both:
    // doc-dir first (matches the CommonMark spec), then project-root.
    // Whichever resolves to a file that exists on disk wins. Rewriting
    // at the markdown level (rather than post-processing HTML) ensures
    // the absolute URL survives the downstream JS-escape + innerHTML
    // pipeline intact.
    //
    // URLs with a scheme (http://, https://, file://) and absolute
    // paths are left alone.
    if (markdown.isEmpty()) return markdown;
    if (!m_baseUrl.isLocalFile() && !ProjectManager::instance().isProjectOpen()) {
        return markdown;
    }

    const QString docDir = m_baseUrl.toLocalFile();
    const QString projectRoot = ProjectManager::instance().isProjectOpen()
        ? ProjectManager::instance().projectPath()
        : QString();

    auto resolve = [&](const QString &src) -> QString {
        if (src.isEmpty()) return src;
        static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
        if (scheme.match(src).hasMatch()) return src;

        // Pandoc's Word importer emits leading-slash paths like "/media/X.png"
        // meaning project-root-relative, not filesystem-absolute. Strip the
        // leading slash so the same two-base search handles both conventions.
        QString rel = src;
        if (rel.startsWith(QLatin1Char('/'))) {
            rel = rel.mid(1);
        }

        auto tryBase = [&](const QString &base) -> QString {
            if (base.isEmpty()) return QString();
            const QString candidate = QDir::cleanPath(QDir(base).absoluteFilePath(rel));
            if (!QFileInfo::exists(candidate)) return QString();

            // FullyEncoded so paths with spaces (e.g. "Kabal RPG Project")
            // become %20 — markdown image syntax rejects bare whitespace in
            // the URL and cmark-gfm silently falls back to literal text.
            const QString url = QString::fromLatin1(
                QUrl::fromLocalFile(candidate).toEncoded());
            qInfo().noquote() << "PreviewPanel: image resolved" << src << "->" << url;
            return url;
        };

        const QString fromDoc = tryBase(docDir);
        if (!fromDoc.isEmpty()) return fromDoc;
        const QString fromProject = tryBase(projectRoot);
        if (!fromProject.isEmpty()) return fromProject;

        qInfo().noquote() << "PreviewPanel: image path"
                          << src << "could not be resolved against"
                          << docDir << "or" << projectRoot;
        return src;
    };

    // Matches ![alt](path) and ![alt](path "title"). Alt can contain
    // anything except newlines and unmatched brackets; path has no
    // whitespace (standard cmark rule without angle-bracket wrapping).
    // Captures: (1) "![...]", (2) path, (3) optional " \"title\""
    static const QRegularExpression imgRe(
        QStringLiteral("(!\\[[^\\]\\n]*\\])\\(([^)\\s]+)((?:\\s+\"[^\"]*\")?)\\)"));

    QString out = markdown;
    int offset = 0;
    auto it = imgRe.globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString altPart = m.captured(1);
        const QString src = m.captured(2);
        const QString titlePart = m.captured(3);
        const QString resolved = resolve(src);
        if (resolved == src) continue;

        const QString replacement = altPart + QLatin1Char('(') + resolved + titlePart + QLatin1Char(')');
        const int start = m.capturedStart() + offset;
        const int len = m.capturedLength();
        out.replace(start, len, replacement);
        offset += replacement.length() - len;
    }
    return out;
}

void PreviewPanel::updatePreview()
{
    if (m_needsFullReload || !m_isLoaded) {
        loadHtmlViaTempFile(wrapHtml(QString()));
        m_needsFullReload = false;
        return;
    }

    if (!m_webView->page()) return;

    QString contentOnly = VariableManager::stripMetadata(m_pendingMarkdown);

    // Replace variables before rendering markdown to HTML
    QString processedMarkdown = VariableManager::instance().processMarkdown(contentOnly);
    // Rewrite ![alt](relative/path) AT THE MARKDOWN LEVEL before cmark
    // renders. HTML post-processing was unreliable because the rendered
    // HTML goes through a JS-escape + innerHTML assignment that mangled
    // file:// URLs in edge cases. Rewriting the source markdown
    // produces a ready-to-render absolute URL that survives every
    // downstream step intact.
    processedMarkdown = resolveRelativeImageUrlsInMarkdown(processedMarkdown);

    // Preferred path: Pandoc subprocess. It natively handles header IDs
    // ({#slug}), image attributes ({width=... height=...}), grid tables,
    // definition lists, and other Pandoc-flavored markdown extensions we
    // use throughout imported manuscripts.
    bool pandocOk = false;
    QString htmlBody = renderWithPandoc(processedMarkdown, &pandocOk);
    if (!pandocOk) {
        // Fallback path: cmark-gfm + legacy regex shims that patch
        // Pandoc-extension syntax into HTML. Kept as a safety net for
        // installs where pandoc is unavailable.
        qInfo().noquote() << "PreviewPanel: pandoc render failed — falling back to cmark-gfm.";
        htmlBody = m_parser.renderHtml(processedMarkdown);
    }
    // Always run the shim pass as belt-and-suspenders. Pandoc output
    // doesn't contain literal {#...} / {width=...} tokens so the regex
    // no-ops there; the cmark fallback genuinely needs the rewrite.
    // Catches edge cases where either renderer leaks an attribute token.
    applyLegacyCmarkPandocShims(htmlBody);

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
    if (m_webView->page()) {
        m_webView->page()->runJavaScript(QStringLiteral("window.scrollBy(%1, %2)").arg(x).arg(y));
    }
}

void PreviewPanel::scrollToPercentage(double percentage)
{
    if (!m_webView->page()) return;
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
    if (!m_webView->page()) return;
    // Find the preceding AND following blocks that straddle the target line,
    // then linearly interpolate the preview's scroll position between them.
    // Snapping to block.start makes the preview lag when the editor's first
    // visible line falls mid-block (e.g. a long paragraph spanning 10 lines
    // in the editor but rendering as one short wrapped <p> in the preview).
    // Interpolation keeps the preview aligned with the editor's actual
    // first-visible line.
    //
    // Matches cmark-gfm's data-sourcepos and Pandoc's data-pos (same
    // "startLine:startCol-endLine:endCol" format). Skips Pandoc's inline
    // span wrappers (data-wrapper="1"). Editor lines are 0-based; sourcepos
    // is 1-based.
    QString js = QStringLiteral(
        "(function() {"
        "  var targetLine = %1 + 1;"
        "  var els = document.querySelectorAll('[data-sourcepos], [data-pos]');"
        "  var prev = null, prevLine = 0;"
        "  var next = null, nextLine = 0;"
        "  for (var i = 0; i < els.length; i++) {"
        "    var el = els[i];"
        "    if (el.getAttribute('data-wrapper') === '1') continue;"
        "    var pos = el.getAttribute('data-sourcepos') || el.getAttribute('data-pos');"
        "    if (!pos) continue;"
        "    var startLine = parseInt(pos.split(':')[0]);"
        "    if (isNaN(startLine)) continue;"
        "    if (startLine <= targetLine) {"
        "      prev = el; prevLine = startLine;"
        "    } else {"
        "      next = el; nextLine = startLine;"
        "      break;"
        "    }"
        "  }"
        "  if (!prev) return;"
        "  var scroller = document.scrollingElement || document.documentElement;"
        "  if (!next || nextLine <= prevLine) {"
        "    prev.scrollIntoView({behavior: '%2', block: 'start'});"
        "    return;"
        "  }"
        "  var t = (targetLine - prevLine) / (nextLine - prevLine);"
        "  if (t < 0) t = 0; else if (t > 1) t = 1;"
        "  var prevTop = prev.getBoundingClientRect().top + scroller.scrollTop;"
        "  var nextTop = next.getBoundingClientRect().top + scroller.scrollTop;"
        "  var target = prevTop + t * (nextTop - prevTop);"
        "  if ('%2' === 'smooth') {"
        "    scroller.scrollTo({top: target, behavior: 'smooth'});"
        "  } else {"
        "    scroller.scrollTop = target;"
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

QString PreviewPanel::renderWithPandoc(const QString &markdown, bool *ok) const
{
    auto fail = [ok]() {
        if (ok) *ok = false;
        return QString();
    };

    const QString pandocExe = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
    if (pandocExe.isEmpty()) {
        return fail();
    }

    // commonmark_x+sourcepos: Pandoc-flavored markdown with source-position
    // attributes (data-pos="line:col-line:col") on every block. scrollToLine
    // uses those to keep the preview in lockstep with the editor's cursor.
    // NOT -s / --standalone: wrapHtml() supplies the full document shell,
    // including <head>, CSS, and KaTeX. Pandoc's own wrapper would collide.
    QProcess proc;
    proc.start(pandocExe, {
        QStringLiteral("-f"), QStringLiteral("commonmark_x+sourcepos"),
        QStringLiteral("-t"), QStringLiteral("html"),
    });
    if (!proc.waitForStarted(2000)) {
        return fail();
    }

    proc.write(markdown.toUtf8());
    proc.closeWriteChannel();

    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(500);
        return fail();
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString stderrOut = QString::fromUtf8(proc.readAllStandardError());
        qWarning().noquote() << "PreviewPanel: pandoc exited with code"
                             << proc.exitCode() << "-" << stderrOut.trimmed();
        return fail();
    }

    if (ok) *ok = true;
    return QString::fromUtf8(proc.readAllStandardOutput());
}

void PreviewPanel::applyLegacyCmarkPandocShims(QString &htmlBody) const
{
    // Promote Pandoc's header_attributes syntax (e.g. "## Title {#slug}")
    // into real HTML ids. cmark-gfm doesn't recognize the extension and
    // emits it as literal trailing text inside the heading. Rewrite to
    // <hN id="slug">Title</hN> so the tokens disappear from the rendered
    // preview AND anchor links / future scroll-to-heading sync work.
    static const QRegularExpression headingAttrRe(
        QStringLiteral("<h([1-6])(\\s[^>]*)?>(.*?)\\s*\\{#([^\\s}]+)(?:\\s[^}]*)?\\}\\s*</h\\1>"));
    htmlBody.replace(headingAttrRe, QStringLiteral("<h\\1 id=\"\\4\"\\2>\\3</h\\1>"));

    // Same story for Pandoc image attributes: ![](src){width="X" height="Y"}.
    // Extract width/height into an inline style on the <img> and drop the
    // trailing {...} text. The attr block can span multiple lines.
    static const QRegularExpression imgAttrRe(
        QStringLiteral("(<img\\b[^>]*?)(\\s*/?>)\\s*\\{([^}]+)\\}"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kvRe(
        QStringLiteral("(\\w+)\\s*=\\s*\"?([^\"\\s}]+)\"?"));
    int offset = 0;
    auto it = imgAttrRe.globalMatch(htmlBody);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString imgPrefix = m.captured(1);
        const QString imgEnd = m.captured(2);
        const QString attrs = m.captured(3);

        QString styleFragment;
        auto kvIt = kvRe.globalMatch(attrs);
        while (kvIt.hasNext()) {
            const auto kv = kvIt.next();
            const QString key = kv.captured(1).toLower();
            const QString val = kv.captured(2);
            if (key == QLatin1String("width") || key == QLatin1String("height")) {
                styleFragment += key + QLatin1Char(':') + val + QLatin1Char(';');
            }
        }

        QString replacement = imgPrefix;
        if (!styleFragment.isEmpty()) {
            replacement += QStringLiteral(" style=\"") + styleFragment + QLatin1Char('"');
        }
        replacement += imgEnd;

        const int start = m.capturedStart() + offset;
        const int len = m.capturedLength();
        htmlBody.replace(start, len, replacement);
        offset += replacement.length() - len;
    }
}

QString PreviewPanel::wrapHtml(const QString &body) const
{
    // Derive theme colors from the application palette
    QPalette pal = qApp->palette();
    QString textColor = pal.color(QPalette::Text).name();
    QString bgColor = pal.color(QPalette::Base).name();
    QString linkColor = pal.color(QPalette::Link).name();
    
    // Use KColorScheme for more specific semantic colors if available
    KColorScheme scheme(pal.currentColorGroup(), KColorScheme::View);
    QString codeBgColor = scheme.background(KColorScheme::AlternateBackground).color().name();
    QString borderColor = scheme.foreground(KColorScheme::InactiveText).color().name();

    // Base CSS for preview — derived from the current theme
    const QString baseCss = QStringLiteral(
        "body { font-family: sans-serif; line-height: 1.6; padding: 2em; max-width: 800px; margin: 0 auto; color: %1; background-color: %2; }"
        "a { color: %3; }"
        "pre { background: %4; padding: 1em; overflow-x: auto; border-radius: 4px; border: 1px solid %5; }"
        "code { font-family: monospace; background: %4; padding: 0.2em 0.4em; border-radius: 3px; }"
        "table { border-collapse: collapse; width: 100%; margin: 1em 0; }"
        "th, td { border: 1px solid %5; padding: 8px; text-align: left; }"
        "th { background-color: %4; }"
        "blockquote { border-left: 4px solid %5; padding-left: 1em; color: %5; margin-left: 0; }"
        "img { max-width: 100%; height: auto; }"
    ).arg(textColor, bgColor, linkColor, codeBgColor, borderColor);

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
