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
    QModelIndex effectiveParent = parentIndex;

    // Smart Folder Mapping: 
    // If it's a top-level folder in Scrivener that exists in our Research structure,
    // we should merge into our existing Research hierarchy.
    if (!parentIndex.isValid()) {
        bool isResearchType = (title == i18n("Characters") || title == i18n("Places") || title == i18n("Cultures") || type == QStringLiteral("ResearchFolder"));
        
        if (isResearchType || type == QStringLiteral("DraftFolder")) {
            ProjectTreeItem *root = model->itemFromIndex(QModelIndex());
            for (int i = 0; i < root->children.count(); ++i) {
                ProjectTreeItem *child = root->children[i];
                bool match = false;
                if (type == QStringLiteral("DraftFolder") && child->category == ProjectTreeItem::Manuscript) match = true;
                else if (type == QStringLiteral("ResearchFolder") && child->category == ProjectTreeItem::Research) match = true;
                else if (title == i18n("Characters") && child->category == ProjectTreeItem::Research) {
                    // Look deeper inside Research for Characters
                    for (int j = 0; j < child->children.count(); ++j) {
                        if (child->children[j]->category == ProjectTreeItem::Characters || child->children[j]->name == title) {
                            effectiveParent = model->index(j, 0, model->index(i, 0, QModelIndex()));
                            match = true; break;
                        }
                    }
                } else if (title == i18n("Places") && child->category == ProjectTreeItem::Research) {
                    for (int j = 0; j < child->children.count(); ++j) {
                        if (child->children[j]->category == ProjectTreeItem::Places || child->children[j]->name == title) {
                            effectiveParent = model->index(j, 0, model->index(i, 0, QModelIndex()));
                            match = true; break;
                        }
                    }
                }

                if (match && !effectiveParent.isValid()) {
                    effectiveParent = model->index(i, 0, QModelIndex());
                }
                if (effectiveParent.isValid()) break;
            }
        }
    }

    // Map Scrivener types to RPG Forge categories
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
        } else if (category == ProjectTreeItem::None) {
            category = parent->category;
        }
    }

    // Determine target directory structure
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
        // Reuse existing folder if it has the same name
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

        QDomElement children = element.firstChildElement(QStringLiteral("Children"));
        QDomElement child = children.firstChildElement(QStringLiteral("BinderItem"));
        while (!child.isNull()) {
            processBinderItem(child, currentIndex, scrivPath, targetProjectDir, model);
            child = child.nextSiblingElement(QStringLiteral("BinderItem"));
        }
    } else {
        // Leaf Node processing (RTF, TXT, etc.)
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
