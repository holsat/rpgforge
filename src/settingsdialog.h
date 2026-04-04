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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "llmservice.h"

class QLineEdit;
class QComboBox;
class QTabWidget;
class QListWidget;
class QCheckBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void save();

private:
    void setupUi();
    void load();
    QWidget* createLLMTab();
    QWidget* createPromptsTab();
    QWidget* createAnalyzerTab();
    QWidget* createEditorTab();

    QTabWidget *m_tabWidget = nullptr;

    // Editor settings
    QCheckBox *m_typewriterScrollingCheck = nullptr;

    // LLM settings
    QComboBox *m_activeProviderCombo;
    QLineEdit *m_openaiKeyEdit;
    QLineEdit *m_openaiModelEdit;
    QLineEdit *m_openaiEndpointEdit;
    QLineEdit *m_anthropicKeyEdit;
    QLineEdit *m_anthropicModelEdit;
    QLineEdit *m_anthropicEndpointEdit;
    QLineEdit *m_ollamaModelEdit;
    QLineEdit *m_ollamaEndpointEdit;
    QLineEdit *m_embeddingModelEdit;

    // Prompts
    QListWidget *m_promptsList;

    // Analyzer
    QComboBox *m_analyzerRunModeCombo;
    QComboBox *m_analyzerProviderCombo;
    QLineEdit *m_analyzerModelEdit;
};

#endif // SETTINGSDIALOG_H
