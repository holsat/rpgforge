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

#ifndef DOCUMENTCONVERTER_H
#define DOCUMENTCONVERTER_H

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief Handles document conversion (RTF, DOCX, PDF) to Markdown using Pandoc.
 * 
 * Also manages media extraction, EMF conversion, and filename sanitization.
 */
class DocumentConverter : public QObject
{
    Q_OBJECT

public:
    struct ConversionResult {
        bool success = false;
        QString markdown;
        QString error;
        QStringList extractedMedia; // Full paths to the final (renamed/converted) media files
    };

    explicit DocumentConverter(QObject *parent = nullptr);

    /**
     * @brief Converts a document to Markdown.
     * @param sourcePath Absolute path to the source file.
     * @param mediaPrefix Sanitized prefix for extracted media files.
     * @param targetMediaDir Absolute path where media should be moved.
     */
    ConversionResult convertToMarkdown(const QString &sourcePath, 
                                      const QString &mediaPrefix,
                                      const QString &targetMediaDir);

    static bool isPandocAvailable();
    static bool isInkscapeAvailable();
    
    /**
     * @brief Strictly sanitizes a string for use as a filename prefix.
     */
    static QString sanitizePrefix(const QString &input);

private:
    QString runPandoc(const QStringList &arguments, const QString &workingDir, QString *errorOutput);
    /// Rasterize an EMF/WMF to PNG. Prefers LibreOffice (best EMF renderer
    /// for Office-authored documents); falls back to Inkscape's PNG export.
    /// Do NOT convert EMF to SVG — Inkscape's EMF→SVG path produces an XML
    /// wrapper around a stripped-down raster and renders as garbage in the
    /// preview.
    bool convertEmfToPng(const QString &emfPath, const QString &pngPath);
    QString sanitizeFilename(const QString &filename);
};

#endif // DOCUMENTCONVERTER_H
