#ifndef PREVIEWPANEL_H
#define PREVIEWPANEL_H

#include <QWidget>
#include <QTimer>
#include "markdownparser.h"

class QWebEngineView;

class PreviewPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override = default;

    void setMarkdown(const QString &markdown);
    void scrollBy(int x, int y);
    void scrollToPercentage(double percentage);

private Q_SLOTS:
    void updatePreview();

private:
    QWebEngineView *m_webView;
    MarkdownParser m_parser;
    QString m_pendingMarkdown;
    QTimer *m_debounceTimer;

    QString wrapHtml(const QString &body) const;
};

#endif // PREVIEWPANEL_H
