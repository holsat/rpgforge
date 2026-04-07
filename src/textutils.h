#pragma once

#include <QString>

/// Strips a leading ```json or ``` fence and trailing ``` from an LLM response,
/// returning the trimmed inner content ready for JSON parsing.
inline QString stripMarkdownFences(const QString &text)
{
    QString clean = text.trimmed();
    if (clean.startsWith(QLatin1String("```json"))) {
        clean = clean.mid(7);
        if (clean.endsWith(QLatin1String("```"))) clean.chop(3);
    } else if (clean.startsWith(QLatin1String("```"))) {
        clean = clean.mid(3);
        if (clean.endsWith(QLatin1String("```"))) clean.chop(3);
    }
    return clean.trimmed();
}
