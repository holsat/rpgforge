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

            // Handle EMF/WMF conversion. Rasterize to PNG via LibreOffice/
            // Inkscape — the prior EMF→SVG pipeline produced XML wrappers that
            // the preview could not render. If rasterization fails the image
            // reference is dropped from the markdown so the preview doesn't
            // show a broken link to an .emf the browser can't decode.
            if (extension == QStringLiteral("emf") || extension == QStringLiteral("wmf")) {
                QString pngName = baseNewName + QStringLiteral(".png");
                QString pngPath = targetDir.absoluteFilePath(pngName);
                if (convertEmfToPng(fullTempPath, pngPath)) {
                    QString oldRef = mediaSubDir + QStringLiteral("/") + entry;
                    QString newRef = QStringLiteral("media/") + pngName;
                    markdown.replace(oldRef, newRef);
                    result.extractedMedia << pngPath;
                } else {
                    qWarning().noquote() << "DocumentConverter: failed to rasterize"
                                         << entry << "— removing reference from markdown";
                    QString oldRef = mediaSubDir + QStringLiteral("/") + entry;
                    static const QRegularExpression imgTag(
                        QStringLiteral("!\\[[^\\]]*\\]\\(%1[^)]*\\)").arg(
                            QRegularExpression::escape(oldRef)));
                    markdown.remove(imgTag);
                }
            } else {
                // Regular image
                if (QFile::exists(targetPath)) QFile::remove(targetPath);
                if (QFile::copy(fullTempPath, targetPath)) {
                    // Update markdown reference
                    QString oldRef = mediaSubDir + QStringLiteral("/") + entry;
                    QString newRef = QStringLiteral("media/") + finalName;
                    markdown.replace(oldRef, newRef);
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
