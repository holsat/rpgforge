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

#ifndef PREVIEWPANEL_H
#define PREVIEWPANEL_H

#include <QWidget>
#include <QTimer>
#include <QUrl>
#include "markdownparser.h"

class QWebEngineView;
class QFileSystemWatcher;

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
    void reloadStylesheet();

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
    bool m_needsFullReload = true;
    bool m_isLoaded = false;

    void setupStylesheetWatcher();
    QString wrapHtml(const QString &body) const;
    QString loadProjectStylesheets() const;
};

#endif // PREVIEWPANEL_H
