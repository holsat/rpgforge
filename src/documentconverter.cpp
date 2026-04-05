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

            // Handle EMF/WMF conversion
            if (extension == QStringLiteral("emf") || extension == QStringLiteral("wmf")) {
                QString svgName = baseNewName + QStringLiteral(".svg");
                QString svgPath = targetDir.absoluteFilePath(svgName);
                if (convertEmfToSvg(fullTempPath, svgPath)) {
                    // Update markdown to point to SVG instead of EMF
                    QString oldRef = mediaSubDir + QStringLiteral("/") + entry;
                    QString newRef = QStringLiteral("media/") + svgName;
                    markdown.replace(oldRef, newRef);
                    result.extractedMedia << svgPath;
                }
                
                // Also keep PNG as fallback if possible (using high-res raster)
                // For now, let's just do SVG as it's cleaner for RPG Forge
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

    // 2. Remove all shell-special characters: * " ' [ ] { } ( ) # ! @ $ % < > & | ; ? \\
    // Only allow Alphanumeric, underscores, hyphens, and dots (though we prefer no dots in prefix)
    static QRegularExpression re(QStringLiteral("[^a-zA-Z0-9_\\-]"));
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

bool DocumentConverter::convertEmfToSvg(const QString &emfPath, const QString &svgPath)
{
    // Try Inkscape first
    QString inkscape = QStandardPaths::findExecutable(QStringLiteral("inkscape"));
    if (!inkscape.isEmpty()) {
        // inkscape source.emf --export-type=svg --export-filename=target.svg
        QProcess proc;
        proc.start(inkscape, {emfPath, QStringLiteral("--export-type=svg"), QStringLiteral("--export-filename=") + svgPath});
        return proc.waitForFinished() && proc.exitCode() == 0;
    }

    // Fallback to libreoffice/soffice if available
    QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
        // soffice --headless --convert-to svg --outdir dir source.emf
        QFileInfo info(svgPath);
        QProcess proc;
        proc.start(soffice, {QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("svg"), 
                            QStringLiteral("--outdir"), info.absolutePath(), emfPath});
        return proc.waitForFinished() && proc.exitCode() == 0;
    }

    return false;
}
