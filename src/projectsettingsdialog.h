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

#ifndef PROJECTSETTINGSDIALOG_H
#define PROJECTSETTINGSDIALOG_H

#include <QDialog>

class QPushButton;

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QTableWidget;
class QPlainTextEdit;

class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectSettingsDialog(QWidget *parent = nullptr);
    ~ProjectSettingsDialog() override;

    void save();

private Q_SLOTS:
    /// Rewrite the LoreKeeper extraction prompt via the configured LLM
    /// following prompt-engineering best practices. Invoked by the
    /// "Enhance Prompt" button on the LoreKeeper tab.
    void enhanceCurrentPrompt();

private:
    void setupUi();
    void load();

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QDoubleSpinBox *m_marginLeftSpin = nullptr;
    QDoubleSpinBox *m_marginRightSpin = nullptr;
    QDoubleSpinBox *m_marginTopSpin = nullptr;
    QDoubleSpinBox *m_marginBottomSpin = nullptr;
    QCheckBox *m_showPageNumbersCheck = nullptr;
    QLineEdit *m_stylesheetEdit = nullptr;

    // LoreKeeper
    QTableWidget *m_lkTable = nullptr;
    QPlainTextEdit *m_lkPromptEdit = nullptr;
    QPushButton *m_lkEnhanceBtn = nullptr;
};

#endif // PROJECTSETTINGSDIALOG_H
