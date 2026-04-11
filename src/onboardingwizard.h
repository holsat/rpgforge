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
class QProgressDialog;

class OnboardingPage : public QWizardPage {
    Q_OBJECT
public:
    using QWizardPage::QWizardPage;
    void registerFieldPublic(const QString &name, QWidget *widget) {
        registerField(name, widget);
    }
};

class OnboardingWizard : public QWizard
{
    Q_OBJECT

public:
    explicit OnboardingWizard(QWidget *parent = nullptr);
    ~OnboardingWizard() override = default;

    enum { Page_Welcome, Page_Project, Page_Scrivener, Page_GitImport, Page_AI, Page_Github, Page_Finish };

    int nextId() const override;
    void accept() override;

private:
    OnboardingPage* createWelcomePage();
    OnboardingPage* createProjectPage();
    OnboardingPage* createScrivenerPage();
    OnboardingPage* createGitImportPage();
    OnboardingPage* createAiPage();
    OnboardingPage* createGithubPage();
    OnboardingPage* createFinishPage();

    void checkOllama();
    void testAiConnection();

    // Choice
    QRadioButton *m_createNewRadio;
    QRadioButton *m_newProjectRadio = nullptr;
    QRadioButton *m_importScrivenerRadio;
    QRadioButton *m_importGitRadio;
    QLineEdit *m_scrivenerPathEdit;
    QLineEdit *m_scExternalPathEdit = nullptr;
    QLineEdit *m_gitUrlEdit;

    // Fields
    QLineEdit *m_projectNameEdit;
    QLineEdit *m_projectDirEdit;

    // AI
    QComboBox *m_aiProviderCombo;
    QComboBox *m_aiModelCombo;
    QLineEdit *m_aiKeyEdit;
    QLabel *m_testStatusLabel;
    QPushButton *m_testAiBtn;
    QLabel *m_ollamaStatus;
};

#endif // ONBOARDINGWIZARD_H
