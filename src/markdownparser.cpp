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

#include "markdownparser.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include <QByteArray>

void MarkdownParser::init()
{
    // Must be called exactly once before any parsing
    cmark_gfm_core_extensions_ensure_registered();
}

MarkdownParser::MarkdownParser()
{
}

MarkdownParser::~MarkdownParser()
{
    clearCache();
}

void MarkdownParser::clearCache() const
{
    if (m_document) {
        cmark_node_free(static_cast<cmark_node*>(m_document));
        m_document = nullptr;
    }
    if (m_parser) {
        cmark_parser_free(static_cast<cmark_parser*>(m_parser));
        m_parser = nullptr;
    }
}

void MarkdownParser::ensureParsed(const QString &markdown) const
{
    if (m_document && markdown == m_lastMarkdown) {
        return;
    }

    clearCache();
    m_lastMarkdown = markdown;

    if (markdown.isEmpty()) return;

    QByteArray utf8 = markdown.toUtf8();
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    if (!parser) return;

    // Attach GFM extensions
    const char *exts[] = {"table", "strikethrough", "autolink", "tasklist"};
    for (const char *name : exts) {
        cmark_syntax_extension *ext = cmark_find_syntax_extension(name);
        if (ext) cmark_parser_attach_syntax_extension(parser, ext);
    }

    cmark_parser_feed(parser, utf8.constData(), utf8.size());
    cmark_node *doc = cmark_parser_finish(parser);

    if (!doc) {
        cmark_parser_free(parser);
        return;
    }

    m_document = doc;
    m_parser = parser;
}

QString MarkdownParser::renderHtml(const QString &markdown) const
{
    if (markdown.isEmpty()) return QString();

    ensureParsed(markdown);
    if (!m_document || !m_parser) return QString();

    // CMARK_OPT_SOURCEPOS adds data-sourcepos="line:col-line:col" to every block
    // CMARK_OPT_HARDBREAKS renders softbreaks as hardbreaks (<br />)
    // CMARK_OPT_UNSAFE allows inline HTML and CSS (needed for some Markdown features)
    int options = CMARK_OPT_SOURCEPOS | CMARK_OPT_HARDBREAKS | CMARK_OPT_UNSAFE;
    
    cmark_node* docNode = static_cast<cmark_node*>(m_document);
    cmark_parser* parserObj = static_cast<cmark_parser*>(m_parser);
    
    if (!docNode || !parserObj) return QString();

    char *rendered = cmark_render_html(docNode, 
                                       options, 
                                       cmark_parser_get_syntax_extensions(parserObj));
    
    if (!rendered) return QString();

    QString html = QString::fromUtf8(rendered);

    free(rendered);
    return html;
}

QVector<LinkInfo> MarkdownParser::extractLinks(const QString &markdown) const
{
    QVector<LinkInfo> links;
    if (markdown.isEmpty()) return links;

    ensureParsed(markdown);
    if (!m_document) return links;

    cmark_iter *iter = cmark_iter_new(static_cast<cmark_node*>(m_document));
    cmark_event_type evType;
    while ((evType = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (evType != CMARK_EVENT_ENTER) continue;

        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
            LinkInfo info;
            info.isImage = (type == CMARK_NODE_IMAGE);
            
            const char *url = cmark_node_get_url(node);
            if (url) {
                info.url = QString::fromUtf8(url);
            }

            // Extract display text/alt text
            QString text;
            cmark_node *child = cmark_node_first_child(node);
            while (child) {
                if (cmark_node_get_type(child) == CMARK_NODE_TEXT) {
                    const char *literal = cmark_node_get_literal(child);
                    if (literal) text += QString::fromUtf8(literal);
                }
                child = cmark_node_next(child);
            }
            info.text = text;
            links.append(info);
        }
    }

    cmark_iter_free(iter);
    return links;
}

QVector<HeadingInfo> MarkdownParser::parseHeadings(const QString &markdown) const
{
    QVector<HeadingInfo> headings;
    if (markdown.isEmpty()) return headings;

    ensureParsed(markdown);
    if (!m_document) return headings;

    // Walk the AST looking for heading nodes
    cmark_iter *iter = cmark_iter_new(static_cast<cmark_node*>(m_document));
    if (!iter) return headings;

    cmark_event_type evType;
    while ((evType = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (evType != CMARK_EVENT_ENTER) continue;

        cmark_node *node = cmark_iter_get_node(iter);
        if (cmark_node_get_type(node) != CMARK_NODE_HEADING) continue;

        HeadingInfo info;
        info.level = cmark_node_get_heading_level(node);
        info.line = cmark_node_get_start_line(node) - 1; // cmark uses 1-based lines

        // Extract text content from all child text/code nodes
        QString text;
        cmark_node *child = cmark_node_first_child(node);
        while (child) {
            cmark_node_type type = cmark_node_get_type(child);
            if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE) {
                const char *literal = cmark_node_get_literal(child);
                if (literal) {
                    text += QString::fromUtf8(literal);
                }
            }
            child = cmark_node_next(child);
        }

        info.text = text.trimmed();
        if (!info.text.isEmpty()) {
            headings.append(info);
        }
    }

    cmark_iter_free(iter);
    return headings;
}

QVector<HeadingInfo> MarkdownParser::headingContextForLine(
    const QVector<HeadingInfo> &headings, int line)
{
    QVector<HeadingInfo> stack;

    for (const auto &h : headings) {
        if (h.line > line) break;

        // Pop headings from the stack that are at the same or lower level
        while (!stack.isEmpty() && stack.last().level >= h.level) {
            stack.removeLast();
        }
        stack.append(h);
    }

    return stack;
}

QVector<HeadingInfo> MarkdownParser::siblingsOf(
    const QVector<HeadingInfo> &headings, const HeadingInfo &heading)
{
    QVector<HeadingInfo> siblings;

    // Find the parent heading (the heading just before this one with a lower level)
    int parentLine = -1;
    int parentLevel = 0;
    for (const auto &h : headings) {
        if (h.line >= heading.line) break;
        if (h.level < heading.level) {
            parentLine = h.line;
            parentLevel = h.level;
        }
    }

    // Collect all headings at the same level that share the same parent
    for (const auto &h : headings) {
        if (h.level != heading.level) {
            // If we hit a heading at the parent's level or above (after our parent),
            // we've left the parent's scope
            if (h.level <= parentLevel && h.line > parentLine) break;
            continue;
        }

        // Check this heading is under the same parent
        if (parentLine < 0) {
            siblings.append(h);
        } else if (h.line > parentLine) {
            siblings.append(h);
        }
    }

    return siblings;
}
