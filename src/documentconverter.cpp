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

#include "documentconverter.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDebug>
#include <KLocalizedString>

DocumentConverter::DocumentConverter(QObject *parent)
    : QObject(parent)
{
}

DocumentConverter::ConversionResult DocumentConverter::convertToMarkdown(const QString &sourcePath, 
                                                                       const QString &mediaPrefix,
                                                                       const QString &targetMediaDir)
{
    ConversionResult result;
    if (!isPandocAvailable()) {
        result.error = i18n("Pandoc is not installed on the system.");
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.error = i18n("Failed to create temporary directory for conversion.");
        return result;
    }

    QString sanitizedPrefix = sanitizePrefix(mediaPrefix);
    QString mediaSubDir = QStringLiteral("media");
    QString tempMediaDir = tempDir.path() + QDir::separator() + mediaSubDir;

    // Run Pandoc: pandoc -f auto -t commonmark_x+raw_html --extract-media=. source.rtf
    // We run from tempDir so media is extracted into tempDir/media/
    QStringList args;
    args << QStringLiteral("-t") << QStringLiteral("commonmark_x+raw_html");
    args << QStringLiteral("--extract-media=.");
    args << sourcePath;

    QString errorOutput;
    QString markdown = runPandoc(args, tempDir.path(), &errorOutput);

    if (markdown.isEmpty() && !errorOutput.isEmpty()) {
        result.error = i18n("Pandoc error: %1", errorOutput);
        return result;
    }

    // Normalize alt text inside ![...](...):
    //   1. Collapse line breaks — cmark and the preview rewriter reject
    //      multi-line alt text and render the image invisible.
    //   2. Strip Word's boilerplate "Description automatically generated"
    //      phrase, which Pandoc faithfully carries over from the docx.
    //      Also clean trailing punctuation/whitespace left behind.
    {
        static const QRegularExpression altRe(
            QStringLiteral("!\\[([^\\]]*)\\]"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression wsRun(QStringLiteral("\\s+"));
        static const QRegularExpression wordBoilerplate(
            QStringLiteral("\\s*,?\\s*Description automatically generated\\s*"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression trailingPunct(
            QStringLiteral("[\\s,;:\\-]+$"));
        int offset = 0;
        auto it = altRe.globalMatch(markdown);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString alt = m.captured(1);
            QString cleaned = alt;
            cleaned.replace(wsRun, QStringLiteral(" "));
            cleaned.remove(wordBoilerplate);
            cleaned.replace(trailingPunct, QString());
            cleaned = cleaned.trimmed();
            if (cleaned == alt) continue;  // no change, skip the splice
            const QString replacement = QStringLiteral("![") + cleaned + QStringLiteral("]");
            const int start = m.capturedStart() + offset;
            const int len = m.capturedLength();
            markdown.replace(start, len, replacement);
            offset += replacement.length() - len;
        }
    }

    // Process extracted media
    QDir mediaDir(tempMediaDir);
    if (mediaDir.exists()) {
        QDir targetDir(targetMediaDir);
        if (!targetDir.exists()) {
            targetDir.mkpath(QStringLiteral("."));
        }

        QStringList entries = mediaDir.entryList(QDir::Files);
        for (const QString &entry : entries) {
            QString fullTempPath = mediaDir.absoluteFilePath(entry);
            QFileInfo info(entry);
            QString extension = info.suffix().toLower();
            
            QString baseNewName = sanitizedPrefix + QStringLiteral("_") + info.baseName();
            QString finalName = baseNewName + QStringLiteral(".") + extension;
            QString targetPath = targetDir.absoluteFilePath(finalName);

            // Matches any pandoc-emitted path ending in `/media/<entry>` inside
            // an image reference. Pandoc 3.x prepends `../../<docbasename>/`
            // when extract-media is "." and the input is given by absolute
            // path, so a substring replace leaves the traversal prefix stuck
            // in front of our renamed filename. The regex anchors on the
            // enclosing `(` and a following `)` or whitespace, and is
            // rebuilt per-entry because the filename is interpolated in.
            auto pathRefRegex = [&](const QString &fileName) {
                // Group 1 = the `(` literal so we can put it back in the
                // replacement. Everything between `(` and the filename is
                // discarded.
                return QRegularExpression(
                    QStringLiteral("(\\()[^)\\s]*media/")
                    + QRegularExpression::escape(fileName)
                    + QStringLiteral("(?=[)\\s])"));
            };

            if (extension == QStringLiteral("emf") || extension == QStringLiteral("wmf")) {
                const QString pngName = baseNewName + QStringLiteral(".png");
                const QString pngPath = targetDir.absoluteFilePath(pngName);
                if (convertEmfToPng(fullTempPath, pngPath)) {
                    markdown.replace(pathRefRegex(entry),
                        QStringLiteral("\\1media/") + pngName);
                    result.extractedMedia << pngPath;
                } else {
                    qWarning().noquote() << "DocumentConverter: failed to rasterize"
                                         << entry << "— removing reference from markdown";
                    // Drop the whole `![alt](anypath/media/<entry>...)` ref.
                    const QRegularExpression imgTag(
                        QStringLiteral("!\\[[^\\]]*\\]\\([^)\\s]*media/")
                        + QRegularExpression::escape(entry)
                        + QStringLiteral("[^)]*\\)"));
                    markdown.remove(imgTag);
                }
            } else {
                // Regular image
                if (QFile::exists(targetPath)) QFile::remove(targetPath);
                if (QFile::copy(fullTempPath, targetPath)) {
                    markdown.replace(pathRefRegex(entry),
                        QStringLiteral("\\1media/") + finalName);
                    result.extractedMedia << targetPath;
                }
            }
        }
    }

    result.success = true;
    result.markdown = markdown;
    return result;
}

bool DocumentConverter::isPandocAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pandoc")).isEmpty();
}

bool DocumentConverter::isInkscapeAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("inkscape")).isEmpty();
}

QString DocumentConverter::sanitizePrefix(const QString &input)
{
    // 1. Replace spaces with underscores
    QString result = input;
    result.replace(QLatin1Char(' '), QLatin1Char('_'));

    // 2. Remove all shell-special characters: * " ' [ ] { } ( ) # ! @ $ % < > & | ; ? backslash.
    // Allow Alphanumeric, underscores, hyphens, and DOTS.
    static QRegularExpression re(QStringLiteral("[^a-zA-Z0-9_\\-\\.]"));
    result.remove(re);

    return result;
}

QString DocumentConverter::runPandoc(const QStringList &arguments, const QString &workingDir, QString *errorOutput)
{
    QProcess proc;
    proc.setWorkingDirectory(workingDir);
    proc.start(QStringLiteral("pandoc"), arguments);
    
    if (!proc.waitForStarted()) {
        if (errorOutput) *errorOutput = i18n("Could not start pandoc process.");
        return QString();
    }

    if (!proc.waitForFinished(30000)) { // 30s timeout
        proc.kill();
        if (errorOutput) *errorOutput = i18n("Pandoc timed out.");
        return QString();
    }

    if (errorOutput) *errorOutput = QString::fromUtf8(proc.readAllStandardError());
    return QString::fromUtf8(proc.readAllStandardOutput());
}

bool DocumentConverter::convertEmfToPng(const QString &emfPath, const QString &pngPath)
{
    const QFileInfo emfInfo(emfPath);
    const QFileInfo outInfo(pngPath);

    // LibreOffice renders Office-authored EMF/WMF far more faithfully than
    // Inkscape — Microsoft and LibreOffice share ancestry in Office file
    // format handling. Try it first.
    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
        QProcess proc;
        proc.start(soffice, {
            QStringLiteral("--headless"),
            QStringLiteral("--convert-to"), QStringLiteral("png"),
            QStringLiteral("--outdir"), outInfo.absolutePath(),
            emfPath,
        });
        if (proc.waitForFinished(30000) && proc.exitCode() == 0) {
            // soffice writes to <outdir>/<emf-basename>.png — rename if the
            // caller wanted a different filename.
            const QString sofficeOut = outInfo.absolutePath()
                + QDir::separator()
                + emfInfo.completeBaseName()
                + QStringLiteral(".png");
            if (sofficeOut != pngPath) {
                QFile::remove(pngPath);
                QFile::rename(sofficeOut, pngPath);
            }
            if (QFileInfo::exists(pngPath)) return true;
        }
    }

    // Inkscape as fallback — render EMF directly to raster (NOT to SVG; its
    // EMF→SVG path produces broken wrappers).
    const QString inkscape = QStandardPaths::findExecutable(QStringLiteral("inkscape"));
    if (!inkscape.isEmpty()) {
        QProcess proc;
        proc.start(inkscape, {
            emfPath,
            QStringLiteral("--export-type=png"),
            QStringLiteral("--export-dpi=150"),
            QStringLiteral("--export-filename=") + pngPath,
        });
        if (proc.waitForFinished(30000) && proc.exitCode() == 0
            && QFileInfo::exists(pngPath)) {
            return true;
        }
    }

    return false;
}
