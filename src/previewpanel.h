#ifndef PREVIEWPANEL_H
#define PREVIEWPANEL_H

#include <QWidget>
#include <QTimer>
#include <QUrl>
#include "markdownparser.h"

class QWebEngineView;

class PreviewPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override = default;

    void setMarkdown(const QString &markdown);
    void setBaseUrl(const QUrl &url);
    void scrollBy(int x, int y);
    void scrollToPercentage(double percentage);
    void scrollToLine(int line, bool smooth = true);

private Q_SLOTS:
    void updatePreview();
    void onLoadFinished(bool ok);

private:
    QWebEngineView *m_webView;
    MarkdownParser m_parser;
    QString m_pendingMarkdown;
    QUrl m_baseUrl;
    QTimer *m_debounceTimer;
    bool m_needsFullReload = true;
    bool m_isLoaded = false;

    QString wrapHtml(const QString &body) const;
};

#endif // PREVIEWPANEL_H
