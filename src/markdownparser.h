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

#ifndef MARKDOWNPARSER_H
#define MARKDOWNPARSER_H

#include <QString>
#include <QVector>

struct HeadingInfo {
    int level;      // 1-6
    QString text;   // heading text content
    int line;       // 0-based line number in the source document
};

struct LinkInfo {
    bool isImage;
    QString url;
    QString text;
};

// Parses markdown text and extracts heading information using cmark-gfm.
// Call MarkdownParser::init() once at application startup before using.
class MarkdownParser
{
public:
    // Must be called once at startup to register cmark-gfm extensions
    static void init();

    MarkdownParser();
    ~MarkdownParser();

    // Parse the given markdown text and return all headings
    QVector<HeadingInfo> parseHeadings(const QString &markdown) const;

    // Render markdown to HTML with GFM extensions
    QString renderHtml(const QString &markdown) const;

    // Extract all links and images from the markdown
    QVector<LinkInfo> extractLinks(const QString &markdown) const;

    // Find the heading context (ancestor chain) for a given line number.
    static QVector<HeadingInfo> headingContextForLine(const QVector<HeadingInfo> &headings, int line);

    // Get all sibling headings at the same level under the same parent.
    static QVector<HeadingInfo> siblingsOf(const QVector<HeadingInfo> &headings, const HeadingInfo &heading);

private:
    struct Private;
    void ensureParsed(const QString &markdown) const;
    void clearCache() const;

    mutable QString m_lastMarkdown;
    mutable void* m_document = nullptr; // cmark_node*
    mutable void* m_parser = nullptr;   // cmark_parser*
};

#endif // MARKDOWNPARSER_H
