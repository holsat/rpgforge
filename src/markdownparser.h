#ifndef MARKDOWNPARSER_H
#define MARKDOWNPARSER_H

#include <QString>
#include <QVector>

struct HeadingInfo {
    int level;      // 1-6
    QString text;   // heading text content
    int line;       // 0-based line number in the source document
};

// Parses markdown text and extracts heading information using cmark-gfm.
// Call MarkdownParser::init() once at application startup before using.
class MarkdownParser
{
public:
    // Must be called once at startup to register cmark-gfm extensions
    static void init();

    MarkdownParser() = default;
    ~MarkdownParser() = default;

    // Parse the given markdown text and return all headings
    QVector<HeadingInfo> parseHeadings(const QString &markdown) const;

    // Render markdown to HTML with GFM extensions
    QString renderHtml(const QString &markdown) const;

    // Find the heading context (ancestor chain) for a given line number.
    static QVector<HeadingInfo> headingContextForLine(const QVector<HeadingInfo> &headings, int line);

    // Get all sibling headings at the same level under the same parent.
    static QVector<HeadingInfo> siblingsOf(const QVector<HeadingInfo> &headings, const HeadingInfo &heading);
};

#endif // MARKDOWNPARSER_H
