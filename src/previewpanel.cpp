#include "previewpanel.h"
#include <QWebEngineView>
#include <QVBoxLayout>

PreviewPanel::PreviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_webView(new QWebEngineView(this))
    , m_debounceTimer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(200);
    connect(m_debounceTimer, &QTimer::timeout, this, &PreviewPanel::updatePreview);

    // Initial load
    m_webView->setHtml(wrapHtml(QString()));
}

void PreviewPanel::setMarkdown(const QString &markdown)
{
    m_pendingMarkdown = markdown;
    m_debounceTimer->start();
}

void PreviewPanel::updatePreview()
{
    QString htmlBody = m_parser.renderHtml(m_pendingMarkdown);
    m_webView->setHtml(wrapHtml(htmlBody));
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

QString PreviewPanel::wrapHtml(const QString &body) const
{
    // Basic CSS and KaTeX integration
    static const QString head = QStringLiteral(
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.css\">"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.js\"></script>"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/contrib/auto-render.min.js\" "
        "onload=\"renderMathInElement(document.body);\"></script>"
        "<style>"
        "body { font-family: sans-serif; line-height: 1.6; padding: 2em; max-width: 800px; margin: 0 auto; color: #333; }"
        "pre { background: #f4f4f4; padding: 1em; overflow-x: auto; border-radius: 4px; }"
        "code { font-family: monospace; background: #f4f4f4; padding: 0.2em 0.4em; border-radius: 3px; }"
        "table { border-collapse: collapse; width: 100%; margin: 1em 0; }"
        "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
        "th { background-color: #f2f2f2; }"
        "blockquote { border-left: 4px solid #ddd; padding-left: 1em; color: #666; margin-left: 0; }"
        "img { max-width: 100%; }"
        "</style>"
        "</head><body>"
    );
    static const QString foot = QStringLiteral("</body></html>");

    return head + body + foot;
}
