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
#include <KParts/ReadWritePart>

class KompareInterface;

class VisualDiffView : public QWidget
{
    Q_OBJECT

public:
    explicit VisualDiffView(QWidget *parent = nullptr);
    ~VisualDiffView() override;

    void setDiff(const QString &filePath, const QString &oldHash, const QString &newHash = QString());
    void setFiles(const QString &file1, const QString &file2);
    void setConflict(const QString &repoPath,
                     const QString &filePath,
                     const QString &ancestorHash,
                     const QString &oursHash,
                     const QString &theirsHash);
    
    KParts::ReadWritePart* part() const { return m_part; }

Q_SIGNALS:
    void saveRequested(const QString &filePath);
    void reloadRequested(const QString &filePath);

private Q_SLOTS:
    void saveChanges();
    void swapDiff();

private:
    void setupUi();
    void loadKomparePart();

    QString m_filePath;
    QString m_oldHash;
    QString m_newHash;
    QString m_file1;
    QString m_file2;
    QString m_tempOld;
    QString m_tempNew;
    QString m_tempAncestor;
    QString m_tempOurs;
    QString m_tempTheirs;
    bool m_swapped = false;

    KParts::ReadWritePart *m_part = nullptr;
    KompareInterface *m_kompareInterface = nullptr;
};

#endif // VISUALDIFFVIEW_H
