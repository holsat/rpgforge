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

#ifndef PREVIEWPANEL_H
#define PREVIEWPANEL_H

#include <QWidget>
#include <QTimer>
#include <QUrl>
#include "markdownparser.h"

class QWebEngineView;
class QFileSystemWatcher;
class QResizeEvent;
template <typename T> class QFutureWatcher;

class PreviewPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override = default;

    void setMarkdown(const QString &markdown);
    void setProjectMode(bool enabled);
    void setBaseUrl(const QUrl &url);
    void scrollBy(int x, int y);
    void scrollToPercentage(double percentage);
    void scrollToLine(int line, bool smooth = true);
    void reloadStylesheet();

protected:
    /// Detect splitter resizes that flip the panel from collapsed (width 0)
    /// to visible. While collapsed, updatePreview() short-circuits and the
    /// rendering pipeline is skipped entirely. On the 0 -> >0 transition we
    /// kick the debounce so the latest pending markdown gets rendered once.
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void updatePreview();
    void onLoadFinished(bool ok);

private:
    QWebEngineView *m_webView;
    QFileSystemWatcher *m_styleWatcher;
    MarkdownParser m_parser;
    QString m_pendingMarkdown;
    QUrl m_baseUrl;
    QTimer *m_debounceTimer;
    bool m_projectMode = false;
    bool m_needsFullReload = true;
    bool m_isLoaded = false;

    /// Hash of the last successfully-rendered processedMarkdown input.
    /// updatePreview() short-circuits when the new input hashes equal —
    /// the pandoc subprocess is the dominant cost on big docs (~1s on
    /// 225 KB) so skipping no-ops is a real win.
    quint64 m_lastRenderedInputHash = 0;

    /// Generation counter for the async render coalescing scheme. Bumped on
    /// every dispatch; the worker carries its generation in the result, and
    /// the finished slot discards results whose generation no longer matches
    /// (a newer render superseded it).
    quint64 m_renderGeneration = 0;
    /// At most one render queued behind one in-flight render. When a new
    /// render is requested while a worker is running we set this flag and
    /// dispatch from the watcher's finished slot rather than stacking
    /// parallel workers.
    bool m_renderPending = false;
    /// QFutureWatcher used to marshal the worker's RenderResult back to the
    /// main thread. Reused across renders; only one runs at a time.
    QFutureWatcher<struct PreviewRenderResult> *m_renderWatcher = nullptr;
    /// Tracks whether the panel was width-zero at the last updatePreview
    /// short-circuit. resizeEvent uses this to detect the 0 -> >0 transition.
    bool m_wasCollapsed = true;

    void setupStylesheetWatcher();
    QString wrapHtml(const QString &body) const;
    QString loadProjectStylesheets() const;
    /// Write `html` to a stable temp file and load it via setUrl() so the
    /// page has a real file:// origin. setHtml(html, baseUrl) gives an
    /// opaque origin in QtWebEngine, which blocks cross-path file:// image
    /// fetches even with LocalContentCanAccessFileUrls = true.
    void loadHtmlViaTempFile(const QString &html);
    QString m_tempHtmlPath;
    /// Rewrite ![alt](path) in markdown source so relative paths
    /// resolve to absolute file:// URLs BEFORE the renderer runs.
    /// Tries the document directory first (CommonMark convention)
    /// and falls back to the project root (matches how Word/Scrivener
    /// imports were written). Paths that don't resolve against either
    /// are left as-is and show a broken-image indicator.
    QString resolveRelativeImageUrlsInMarkdown(const QString &markdown) const;
    /// Render markdown to an HTML fragment by spawning `pandoc -f markdown
    /// -t html` as a subprocess. Returns an empty QString and sets *ok to
    /// false when pandoc is absent, fails to start, exits non-zero, or
    /// times out. Caller should fall back to cmark-gfm + legacy shims in
    /// that case.
    QString renderWithPandoc(const QString &markdown, bool *ok = nullptr) const;
    /// Apply the pre-Pandoc regex rewrites needed to make cmark-gfm output
    /// understand Pandoc extension syntax (header IDs, image attributes).
    /// Only called on the fallback path when renderWithPandoc() fails —
    /// Pandoc handles all of this natively.
    void applyLegacyCmarkPandocShims(QString &htmlBody) const;
};

#endif // PREVIEWPANEL_H
