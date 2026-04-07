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
    } else if (type == QStringLiteral("Folder")) {
        // Inherit category from parent if possible
        if (parentIndex.isValid()) {
            ProjectTreeItem *parent = model->itemFromIndex(parentIndex);
            if (parent->category == ProjectTreeItem::Manuscript || parent->category == ProjectTreeItem::Chapter) {
                category = ProjectTreeItem::Chapter;
            } else {
                category = parent->category;
            }
        }
    }

    if (type == QStringLiteral("Folder") || type == QStringLiteral("DraftFolder") || type == QStringLiteral("ResearchFolder")) {
        // Create folder
        currentIndex = model->addFolder(title, QString(), parentIndex);
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
    } else if (type == QStringLiteral("Text")) {
        // Convert and import text
        QString rtfPath = scrivPath + QStringLiteral("/Files/Data/") + uuid + QStringLiteral("/content.rtf");
        if (QFile::exists(rtfPath)) {
            DocumentConverter converter;
            QString mediaDir = targetProjectDir + QStringLiteral("/media");
            
            // Calculate a relative path based on binder hierarchy for the filename
            QString relPath;
            if (parentIndex.isValid()) {
                ProjectTreeItem *parent = model->itemFromIndex(parentIndex);
                relPath = parent->path + QDir::separator();
            }
            
            QString safeName = DocumentConverter::sanitizePrefix(title);
            QString fileName = safeName + QStringLiteral(".md");
            QString fullTargetRelPath = relPath + fileName;
            QString absoluteTargetPath = QDir(targetProjectDir).absoluteFilePath(fullTargetRelPath);

            // Ensure directory exists
            QDir().mkpath(QFileInfo(absoluteTargetPath).absolutePath());

            auto result = converter.convertToMarkdown(rtfPath, safeName, mediaDir);
            if (result.success) {
                QFile outFile(absoluteTargetPath);
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(result.markdown.toUtf8());
                    outFile.close();
                    
                    currentIndex = model->addFile(title, fullTargetRelPath, parentIndex);
                    
                    // Set category
                    ProjectTreeItem *parentItem = model->itemFromIndex(parentIndex);
                    if (parentItem->category == ProjectTreeItem::Manuscript || parentItem->category == ProjectTreeItem::Chapter) {
                        model->setData(currentIndex, static_cast<int>(ProjectTreeItem::Scene), ProjectTreeModel::CategoryRole);
                    } else {
                        model->setData(currentIndex, static_cast<int>(parentItem->category), ProjectTreeModel::CategoryRole);
                    }
                }
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
