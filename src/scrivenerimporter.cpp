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

#include "scrivenerimporter.h"
#include "documentconverter.h"
#include <QDomDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <KLocalizedString>

ScrivenerImporter::ScrivenerImporter(QObject *parent)
    : QObject(parent)
{
}

bool ScrivenerImporter::import(const QString &scrivPath, 
                              const QString &targetProjectDir,
                              ProjectTreeModel *model)
{
    QString scrivxFile = findScrivxFile(scrivPath);
    if (scrivxFile.isEmpty()) {
        Q_EMIT finished(false, i18n("Could not find .scrivx file in the project folder."));
        return false;
    }

    QFile file(scrivxFile);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT finished(false, i18n("Could not open Scrivener project file."));
        return false;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        Q_EMIT finished(false, i18n("Failed to parse Scrivener project XML."));
        return false;
    }

    QDomElement root = doc.documentElement();
    QDomElement binder = root.firstChildElement(QStringLiteral("Binder"));
    if (binder.isNull()) {
        Q_EMIT finished(false, i18n("Project binder not found."));
        return false;
    }

    // Count items for progress
    m_totalItems = binder.elementsByTagName(QStringLiteral("BinderItem")).count();
    m_processedItems = 0;

    Q_EMIT progress(0, i18n("Starting import..."));

    // Suppress per-row signals during the recursive tree build to avoid
    // rowsInserted → rowCount re-entrancy on partially-constructed items.
    model->beginBulkImport();

    // Process top-level binder items
    QDomElement item = binder.firstChildElement(QStringLiteral("BinderItem"));
    while (!item.isNull()) {
        processBinderItem(item, QModelIndex(), scrivPath, targetProjectDir, model);
        item = item.nextSiblingElement(QStringLiteral("BinderItem"));
    }

    model->endBulkImport();

    Q_EMIT finished(true, i18n("Import complete."));
    return true;
}

void ScrivenerImporter::processBinderItem(const QDomElement &element, 
                                         const QModelIndex &parentIndex,
                                         const QString &scrivPath,
                                         const QString &targetProjectDir,
                                         ProjectTreeModel *model)
{
    QString uuid = element.attribute(QStringLiteral("UUID"));
    QString type = element.attribute(QStringLiteral("Type"));
    QString title = element.firstChildElement(QStringLiteral("Title")).text();
    
    if (title.isEmpty()) title = i18n("Untitled");

    m_processedItems++;
    int percentage = (m_processedItems * 100) / qMax(1, m_totalItems);
    Q_EMIT progress(percentage, i18n("Importing: %1", title));

    QModelIndex currentIndex;
    ProjectTreeItem::Category category = ProjectTreeItem::None;

    // Map Scrivener types to RPG Forge categories
    if (type == QStringLiteral("DraftFolder")) {
        category = ProjectTreeItem::Manuscript;
    } else if (type == QStringLiteral("ResearchFolder")) {
        category = ProjectTreeItem::Research;
    } else if (type == QStringLiteral("TrashFolder")) {
        // Skip trash for now or handle specifically
        return;
    } else if (parentIndex.isValid()) {
        ProjectTreeItem *parent = model->itemFromIndex(parentIndex);
        if (parent->category == ProjectTreeItem::Manuscript || parent->category == ProjectTreeItem::Chapter) {
            category = (type == QStringLiteral("Folder")) ? ProjectTreeItem::Chapter : ProjectTreeItem::Scene;
        } else {
            category = parent->category;
        }
    }

    // Determine target directory structure based on category
    QString baseDir;
    switch (category) {
        case ProjectTreeItem::Manuscript:
        case ProjectTreeItem::Chapter:
        case ProjectTreeItem::Scene:
            baseDir = QStringLiteral("manuscript");
            break;
        case ProjectTreeItem::Research:
            baseDir = QStringLiteral("research");
            break;
        default:
            baseDir = QString(); // Root-level or uncategorized
    }

    // Calculate relative path for this item
    QString parentRelPath;
    if (parentIndex.isValid()) {
        parentRelPath = model->itemFromIndex(parentIndex)->path;
    } else if (!baseDir.isEmpty()) {
        parentRelPath = baseDir;
    }

    if (type == QStringLiteral("Folder") || type == QStringLiteral("DraftFolder") || type == QStringLiteral("ResearchFolder")) {
        // Create folder with a path to enable drag-and-drop tracking
        QString safeFolderName = DocumentConverter::sanitizePrefix(title);
        QString folderPath = parentRelPath.isEmpty() ? safeFolderName : parentRelPath + QDir::separator() + safeFolderName;
        
        currentIndex = model->addFolder(title, folderPath, parentIndex);
        if (category != ProjectTreeItem::None) {
            model->setData(currentIndex, static_cast<int>(category), ProjectTreeModel::CategoryRole);
        }

        // Recursively process children
        QDomElement children = element.firstChildElement(QStringLiteral("Children"));
        QDomElement child = children.firstChildElement(QStringLiteral("BinderItem"));
        while (!child.isNull()) {
            processBinderItem(child, currentIndex, scrivPath, targetProjectDir, model);
            child = child.nextSiblingElement(QStringLiteral("BinderItem"));
        }
    } else {
        // It's a leaf node (Text, PDF, Image, etc.)
        QString dataDir = scrivPath + QStringLiteral("/Files/Data/") + uuid;
        QDir dir(dataDir);
        
        QString sourceFile;
        QString extension;
        
        if (dir.exists()) {
            // Find the most relevant content file
            QStringList entries = dir.entryList(QDir::Files);
            // Priority: .rtf > .txt > .pdf > .jpg/.png
            if (entries.contains(QStringLiteral("content.rtf"))) {
                sourceFile = dir.absoluteFilePath(QStringLiteral("content.rtf"));
                extension = QStringLiteral("rtf");
            } else if (entries.contains(QStringLiteral("content.txt"))) {
                sourceFile = dir.absoluteFilePath(QStringLiteral("content.txt"));
                extension = QStringLiteral("txt");
            } else {
                for (const auto &e : entries) {
                    if (e.startsWith(QStringLiteral("content."))) {
                        sourceFile = dir.absoluteFilePath(e);
                        extension = QFileInfo(e).suffix().toLower();
                        break;
                    }
                }
            }
        }

        if (!sourceFile.isEmpty()) {
            QString safeName = DocumentConverter::sanitizePrefix(title);
            QString targetExt = (extension == QStringLiteral("rtf") || extension == QStringLiteral("txt")) ? QStringLiteral("md") : extension;
            QString fileName = safeName + QStringLiteral(".") + targetExt;
            
            QString fullTargetRelPath = parentRelPath.isEmpty() ? fileName : parentRelPath + QDir::separator() + fileName;
            QString absoluteTargetPath = QDir(targetProjectDir).absoluteFilePath(fullTargetRelPath);

            // Ensure directory exists
            QDir().mkpath(QFileInfo(absoluteTargetPath).absolutePath());

            bool success = false;
            if (extension == QStringLiteral("rtf")) {
                DocumentConverter converter;
                QString mediaDir = targetProjectDir + QStringLiteral("/media");
                auto result = converter.convertToMarkdown(sourceFile, safeName, mediaDir);
                if (result.success) {
                    QFile outFile(absoluteTargetPath);
                    if (outFile.open(QIODevice::WriteOnly)) {
                        outFile.write(result.markdown.toUtf8());
                        success = true;
                    }
                }
            } else if (extension == QStringLiteral("txt")) {
                // Direct copy/read for text
                QFile inFile(sourceFile);
                if (inFile.open(QIODevice::ReadOnly)) {
                    QFile outFile(absoluteTargetPath);
                    if (outFile.open(QIODevice::WriteOnly)) {
                        outFile.write(inFile.readAll());
                        success = true;
                    }
                }
            } else {
                // Binary copy for PDF, Images, etc.
                success = QFile::copy(sourceFile, absoluteTargetPath);
            }

            if (success) {
                currentIndex = model->addFile(title, fullTargetRelPath, parentIndex);
                if (category != ProjectTreeItem::None) {
                    model->setData(currentIndex, static_cast<int>(category), ProjectTreeModel::CategoryRole);
                }
            }
        }

        // IMPORTANT: In Scrivener, "Text" items can ALSO have children (acting as both document and folder)
        QDomElement children = element.firstChildElement(QStringLiteral("Children"));
        if (!children.isNull()) {
            // If it has children, it must behave like a folder in our tree
            if (!currentIndex.isValid()) {
                // If the file import failed or wasn't attempted, create a folder instead
                QString safeName = DocumentConverter::sanitizePrefix(title);
                QString folderPath = parentRelPath.isEmpty() ? safeName : parentRelPath + QDir::separator() + safeName;
                currentIndex = model->addFolder(title, folderPath, parentIndex);
            }

            QDomElement child = children.firstChildElement(QStringLiteral("BinderItem"));
            while (!child.isNull()) {
                processBinderItem(child, currentIndex, scrivPath, targetProjectDir, model);
                child = child.nextSiblingElement(QStringLiteral("BinderItem"));
            }
        }
    }
}

QString ScrivenerImporter::findScrivxFile(const QString &scrivPath)
{
    QDir dir(scrivPath);
    QStringList filters;
    filters << QStringLiteral("*.scrivx");
    QStringList files = dir.entryList(filters, QDir::Files);
    if (files.isEmpty()) return QString();
    return dir.absoluteFilePath(files.first());
}

QString ScrivenerImporter::getDraftPath(const QString &scrivPath)
{
    return scrivPath + QStringLiteral("/Files/Draft");
}

QString ScrivenerImporter::getDataPath(const QString &scrivPath)
{
    return scrivPath + QStringLiteral("/Files/Data");
}
