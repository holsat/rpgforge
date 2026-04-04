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

#ifndef VISUALDIFFVIEW_H
#define VISUALDIFFVIEW_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QRect>
#include "gitservice.h"

namespace KTextEditor { class View; class Document; class MovingRange; }

class VisualDiffView : public QWidget
{
    Q_OBJECT

public:
    explicit VisualDiffView(QWidget *parent = nullptr);
    ~VisualDiffView() override;

    void setDiff(const QString &filePath, const QString &oldHash, const QString &newHash = QString());
    void setFiles(const QString &file1, const QString &file2);

    struct HunkInfo {
        int index;
        QRect leftRect;
        QRect rightRect;
        DiffHunk data;
    };

    void applyHunk(int hunkIndex);
    QList<HunkInfo> m_visibleHunks;

Q_SIGNALS:
    void saveRequested(const QString &filePath);
    void reloadRequested(const QString &filePath);

private Q_SLOTS:
    void syncScroll();
    void updateConnectors();
    void swapDiff();
    void saveChanges();

private:
    void setupUi();
    void highlightDiff();
    void clearHighlights();

    QString m_filePath;
    QString m_oldHash;
    QString m_newHash;
    QString m_file1; // For arbitrary file comparison
    QString m_file2;
    QList<DiffHunk> m_hunks;
    bool m_swapped = false;

    KTextEditor::Document *m_oldDoc;
    KTextEditor::Document *m_newDoc;
    KTextEditor::View *m_oldView;
    KTextEditor::View *m_newView;
    QWidget *m_connector;

    QList<KTextEditor::MovingRange*> m_highlights;
};

#endif // VISUALDIFFVIEW_H
