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

#ifndef VARIABLESPANEL_H
#define VARIABLESPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QMap>

class QTreeWidgetItem;
class QToolButton;
class QLabel;
class QProgressBar;

/**
 * QTreeWidget subclass that supports dragging variable names into the editor.
 * Produces text/plain MIME data in the format {{variable_name}}.
 */
class DraggableVariableTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DraggableVariableTree(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QPoint m_dragStartPos;
    QString fullVariableName(QTreeWidgetItem *item) const;
};

struct Variable {
    QString name;
    QString value;
    QString description;
};

class LibrarianService;

class VariablesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablesPanel(QWidget *parent = nullptr);
    ~VariablesPanel() override = default;

    void setLibrarianService(LibrarianService *service);

    // Get all defined variables
    QMap<QString, QString> variables() const;

Q_SIGNALS:
    void variablesChanged();
    void forceLoreKeeperScan();

public Q_SLOTS:
    void onLoreScanStarted(const QString &filePath);
    void onLoreScanFinished(const QString &filePath);

private Q_SLOTS:
    void addVariable();
    void addVariant();
    void removeVariable();
    /// Delete a librarian-extracted entity from the library DB.
    /// Invoked by the context menu's "Remove from Library" action.
    void removeLibraryEntry();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onCustomContextMenu(const QPoint &pos);
    void refreshLibrary();
    void triggerReindex();

private:
    DraggableVariableTree *m_treeWidget;
    QToolButton *m_addButton;
    QToolButton *m_addVariantButton;
    QToolButton *m_removeButton;
    QToolButton *m_reindexButton;
    QToolButton *m_loreScanButton;
    QProgressBar *m_loreScanProgress;

    LibrarianService *m_librarianService = nullptr;
    QTreeWidgetItem *m_customVarsRoot = nullptr;
    QTreeWidgetItem *m_libraryRoot = nullptr;

    void setupUi();
    void recalculateAll();
    void saveVariables();
    void loadVariables();
};

#endif // VARIABLESPANEL_H
