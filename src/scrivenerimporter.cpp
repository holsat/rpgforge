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

#include "scrivenerimporter.h"
#include "documentconverter.h"
#include "projectmanager.h"
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
    QModelIndex effectiveParent = parentIndex;

    // Helper to find standard folders anywhere in the tree
    auto findCategoryFolder = [&](ProjectTreeItem::Category cat, const QString &name) -> QModelIndex {
        std::function<QModelIndex(const QModelIndex&)> search;
        search = [&](const QModelIndex &parent) -> QModelIndex {
            int rows = model->rowCount(parent);
            for (int i = 0; i < rows; ++i) {
                QModelIndex idx = model->index(i, 0, parent);
                ProjectTreeItem *it = model->itemFromIndex(idx);
                if (it->category == cat || (cat == ProjectTreeItem::None && it->name == name)) return idx;
                QModelIndex found = search(idx);
                if (found.isValid()) return found;
            }
            return QModelIndex();
        };
        return search(QModelIndex());
    };

    // Smart Folder Mapping for top-level Scrivener items
    if (!parentIndex.isValid()) {
        if (type == QStringLiteral("DraftFolder")) {
            currentIndex = findCategoryFolder(ProjectTreeItem::Manuscript, i18n("Manuscript"));
        } else if (type == QStringLiteral("ResearchFolder")) {
            currentIndex = findCategoryFolder(ProjectTreeItem::Research, i18n("Research"));
        } else if (title == i18n("Characters")) {
            currentIndex = findCategoryFolder(ProjectTreeItem::Characters, title);
        } else if (title == i18n("Places")) {
            currentIndex = findCategoryFolder(ProjectTreeItem::Places, title);
        } else if (title == i18n("Cultures")) {
            currentIndex = findCategoryFolder(ProjectTreeItem::Cultures, title);
        }
        
        // If we found a destination folder, it means we are "merging" this Scrivener folder
        // into our existing one. We set currentIndex so we don't create a new folder,
        // and we will process its children under this index.
        if (currentIndex.isValid()) {
            effectiveParent = model->parent(currentIndex);
            category = model->itemFromIndex(currentIndex)->category;
        }
    }

    // Determine category if not already set by merge
    if (!currentIndex.isValid()) {
        if (type == QStringLiteral("DraftFolder")) {
            category = ProjectTreeItem::Manuscript;
        } else if (type == QStringLiteral("ResearchFolder")) {
            category = ProjectTreeItem::Research;
        } else if (type == QStringLiteral("TrashFolder")) {
            return;
        } else if (effectiveParent.isValid()) {
            ProjectTreeItem *parent = model->itemFromIndex(effectiveParent);
            if (parent->category == ProjectTreeItem::Manuscript || parent->category == ProjectTreeItem::Chapter) {
                category = (type == QStringLiteral("Folder")) ? ProjectTreeItem::Chapter : ProjectTreeItem::Scene;
            } else {
                category = parent->category;
            }
        }
    }

    // Determine base directory for file storage
    QString baseDir;
    switch (category) {
        case ProjectTreeItem::Manuscript: case ProjectTreeItem::Chapter: case ProjectTreeItem::Scene:
            baseDir = QStringLiteral("manuscript"); break;
        case ProjectTreeItem::Research: case ProjectTreeItem::Characters: case ProjectTreeItem::Places: case ProjectTreeItem::Cultures:
            baseDir = QStringLiteral("research"); break;
        default: break;
    }

    QString parentRelPath;
    if (effectiveParent.isValid()) {
        parentRelPath = model->itemFromIndex(effectiveParent)->path;
    } else if (!baseDir.isEmpty()) {
        parentRelPath = baseDir;
    }

    if (type == QStringLiteral("Folder") || type == QStringLiteral("DraftFolder") || type == QStringLiteral("ResearchFolder")) {
        if (!currentIndex.isValid()) {
            // Check for existing folder with same name under parent (general reuse)
            ProjectTreeItem *parentItem = model->itemFromIndex(effectiveParent);
            for (int i = 0; i < parentItem->children.count(); ++i) {
                if (parentItem->children[i]->name == title && parentItem->children[i]->type == ProjectTreeItem::Folder) {
                    currentIndex = model->index(i, 0, effectiveParent);
                    break;
                }
            }

            if (!currentIndex.isValid()) {
                QString safeFolderName = DocumentConverter::sanitizePrefix(title);
                QString folderPath = parentRelPath.isEmpty() ? safeFolderName : parentRelPath + QDir::separator() + safeFolderName;
                currentIndex = model->addFolder(title, folderPath, effectiveParent);
                if (category != ProjectTreeItem::None) {
                    model->setData(currentIndex, static_cast<int>(category), ProjectTreeModel::CategoryRole);
                }
            }
        }

        // Recursively process children
        QDomElement children = element.firstChildElement(QStringLiteral("Children"));
        QDomElement child = children.firstChildElement(QStringLiteral("BinderItem"));
        while (!child.isNull()) {
            processBinderItem(child, currentIndex, scrivPath, targetProjectDir, model);
            child = child.nextSiblingElement(QStringLiteral("BinderItem"));
        }
    } else {
        // Leaf Node processing
        QString dataDir = scrivPath + QStringLiteral("/Files/Data/") + uuid;
        QDir dir(dataDir);
        QString sourceFile;
        QString extension;
        
        if (dir.exists()) {
            QStringList entries = dir.entryList(QDir::Files);
            if (entries.contains(QStringLiteral("content.rtf"))) {
                sourceFile = dir.absoluteFilePath(QStringLiteral("content.rtf")); extension = QStringLiteral("rtf");
            } else if (entries.contains(QStringLiteral("content.txt"))) {
                sourceFile = dir.absoluteFilePath(QStringLiteral("content.txt")); extension = QStringLiteral("txt");
            } else {
                for (const auto &e : entries) {
                    if (e.startsWith(QStringLiteral("content."))) {
                        sourceFile = dir.absoluteFilePath(e); extension = QFileInfo(e).suffix().toLower(); break;
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

            QDir().mkpath(QFileInfo(absoluteTargetPath).absolutePath());

            bool success = false;
            if (extension == QStringLiteral("rtf")) {
                DocumentConverter converter;
                QString mediaDir = targetProjectDir + QStringLiteral("/media");
                auto result = converter.convertToMarkdown(sourceFile, safeName, mediaDir);
                if (result.success) {
                    QFile outFile(absoluteTargetPath);
                    if (outFile.open(QIODevice::WriteOnly)) {
                        outFile.write(result.markdown.toUtf8()); success = true;
                    }
                }
            } else if (extension == QStringLiteral("txt")) {
                QFile inFile(sourceFile);
                if (inFile.open(QIODevice::ReadOnly)) {
                    QFile outFile(absoluteTargetPath);
                    if (outFile.open(QIODevice::WriteOnly)) {
                        outFile.write(inFile.readAll()); success = true;
                    }
                }
            } else {
                success = QFile::copy(sourceFile, absoluteTargetPath);
            }

            if (success) {
                currentIndex = model->addFile(title, fullTargetRelPath, effectiveParent);
                if (category != ProjectTreeItem::None) {
                    model->setData(currentIndex, static_cast<int>(category), ProjectTreeModel::CategoryRole);
                }
            }
        }

        // Process children of "Text" items
        QDomElement children = element.firstChildElement(QStringLiteral("Children"));
        if (!children.isNull()) {
            if (!currentIndex.isValid()) {
                QString safeName = DocumentConverter::sanitizePrefix(title);
                QString folderPath = parentRelPath.isEmpty() ? safeName : parentRelPath + QDir::separator() + safeName;
                currentIndex = model->addFolder(title, folderPath, effectiveParent);
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
