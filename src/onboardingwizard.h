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

#ifndef ONBOARDINGWIZARD_H
#define ONBOARDINGWIZARD_H

#include <QWizard>
#include <QString>
#include "llmservice.h"

class QLineEdit;
class QComboBox;
class QLabel;
class QCheckBox;
class QRadioButton;
class QPushButton;

class OnboardingWizard : public QWizard
{
    Q_OBJECT

public:
    enum { Page_Welcome, Page_Project, Page_Scrivener, Page_GitImport, Page_AI, Page_Github, Page_Finish };
    explicit OnboardingWizard(QWidget *parent = nullptr);
    ~OnboardingWizard() override;

    QString projectName() const;
    QString projectDir() const;
    LLMProvider aiProvider() const;
    QString aiKey() const;
    QString aiModel() const;
    QString githubToken() const;
    QString githubRepo() const;
    bool isGithubPrivate() const;

    void accept() override;
    bool validateCurrentPage() override;
    int nextId() const override;

private:
    QWizardPage* createWelcomePage();
    QWizardPage* createProjectPage();
    QWizardPage* createScrivenerPage();
    QWizardPage* createGitImportPage();
    QWizardPage* createAiPage();
    QWizardPage* createGithubPage();
    QWizardPage* createFinishPage();

    void updateAiFields();
    void checkOllama();
    void testAiConnection();

    // Choice
    QRadioButton *m_createNewRadio;
    QRadioButton *m_importScrivenerRadio;
    QRadioButton *m_importGitRadio;
    QLineEdit *m_scrivenerPathEdit;
    QLineEdit *m_gitUrlEdit;

    // Fields
    QLineEdit *m_projectNameEdit;
    QLineEdit *m_projectDirEdit;

    QComboBox *m_aiProviderCombo;
    QLineEdit *m_aiKeyEdit;
    QComboBox *m_aiModelCombo;
    QLabel *m_aiInstructions;
    QLabel *m_ollamaStatus;
    QPushButton *m_testAiBtn;
    QLabel *m_testStatusLabel;

    QLineEdit *m_githubTokenEdit;
    QLineEdit *m_githubRepoEdit;
    QCheckBox *m_githubPrivateCheck;
};

#endif // ONBOARDINGWIZARD_H
