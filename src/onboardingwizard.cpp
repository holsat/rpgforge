#include <QtConcurrent/QtConcurrent>
#include <QPointer>
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

#include "onboardingwizard.h"
#include "mainwindow.h"
#include "llmservice.h"
#include "gitservice.h"
#include "githubservice.h"
#include "githubonboardingdialog.h"
#include "projectmanager.h"
#include "agentgatekeeper.h"
#include "scrivenerimporter.h"

#include <KLocalizedString>
#include <KColorScheme>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QProcess>
#include <QProgressDialog>
#include <QEventLoop>
#include <QFutureWatcher>

OnboardingWizard::OnboardingWizard(QWidget *parent)
    : QWizard(parent)
{
    setPage(Page_Welcome, createWelcomePage());
    setPage(Page_Project, createProjectPage());
    setPage(Page_Scrivener, createScrivenerPage());
    setPage(Page_GitImport, createGitImportPage());
    setPage(Page_AI, createAiPage());
    setPage(Page_Github, createGithubPage());
    setPage(Page_Finish, createFinishPage());

    setStartId(Page_Welcome);
    setWindowTitle(i18n("Welcome to RPG Forge"));
    setWizardStyle(ModernStyle);
}

int OnboardingWizard::nextId() const
{
    switch (currentId()) {
        case Page_Welcome:
            return Page_Project;
        case Page_Project:
            if (m_importScrivenerRadio->isChecked()) return Page_Scrivener;
            if (m_importGitRadio->isChecked()) return Page_GitImport;
            return Page_AI;
        case Page_Scrivener:
            return Page_AI;
        case Page_GitImport:
            return Page_AI;
        case Page_AI:
            return Page_Github;
        case Page_Github:
            return Page_Finish;
        default:
            return -1;
    }
}

QWizardPage* OnboardingWizard::createWelcomePage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("Welcome to RPG Forge"));

    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(i18n("RPG Forge is a modern IDE designed for game designers and worldbuilders. "
                                 "This wizard will help you set up your first project and connect to AI services."), page);
    label->setWordWrap(true);
    layout->addWidget(label);

    return page;
}

QWizardPage* OnboardingWizard::createProjectPage()
{
    auto *page = new OnboardingPage(this);
    page->setTitle(i18n("Create or Import Project"));

    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    m_projectNameEdit = new QLineEdit(page);
    m_projectNameEdit->setPlaceholderText(i18n("My Awesome RPG"));
    form->addRow(i18n("Project Name:"), m_projectNameEdit);

    m_projectDirEdit = new QLineEdit(page);
    m_projectDirEdit->setText(QDir::homePath() + QStringLiteral("/Documents/RPGForge"));
    
    auto *dirBtn = new QPushButton(i18n("Browse..."), page);
    connect(dirBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Project Root Directory"), QDir::homePath());
        if (!dir.isEmpty()) m_projectDirEdit->setText(dir);
    });

    auto *dirLayout = new QHBoxLayout();
    dirLayout->addWidget(m_projectDirEdit);
    dirLayout->addWidget(dirBtn);
    form->addRow(i18n("Base Directory:"), dirLayout);

    layout->addLayout(form);
    layout->addSpacing(20);

    m_newProjectRadio = new QRadioButton(i18n("Create a new blank project"), page);
    m_newProjectRadio->setChecked(true);
    m_importScrivenerRadio = new QRadioButton(i18n("Import an existing Scrivener project (.scriv)"), page);
    m_importGitRadio = new QRadioButton(i18n("Clone a project from a Git repository"), page);

    layout->addWidget(m_newProjectRadio);
    layout->addWidget(m_importScrivenerRadio);
    layout->addWidget(m_importGitRadio);

    page->registerFieldPublic(QStringLiteral("projectName*"), m_projectNameEdit);
    page->registerFieldPublic(QStringLiteral("projectDir*"), m_projectDirEdit);

    return page;
}

QWizardPage* OnboardingWizard::createScrivenerPage()
{
    auto *page = new OnboardingPage(this);
    page->setTitle(i18n("Import Scrivener Project"));

    auto *form = new QFormLayout(page);
    
    auto *scrivLayout = new QHBoxLayout();
    m_scrivenerPathEdit = new QLineEdit(page);
    auto *scrivBtn = new QPushButton(i18n("Browse..."), page);
    connect(scrivBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Scrivener Project (.scriv)"), QDir::homePath());
        if (!dir.isEmpty()) m_scrivenerPathEdit->setText(dir);
    });
    scrivLayout->addWidget(m_scrivenerPathEdit);
    scrivLayout->addWidget(scrivBtn);

    form->addRow(i18n("Scrivener Project:"), scrivLayout);

    auto *infoLabel = new QLabel(i18n("RPG Forge will convert your Scrivener binder structure and RTF files "
                                     "into a native project with Markdown files. Media assets will be preserved."), page);
    infoLabel->setWordWrap(true);
    form->addRow(infoLabel);

    page->registerFieldPublic(QStringLiteral("scrivenerPath*"), m_scrivenerPathEdit);

    return page;
}

QWizardPage* OnboardingWizard::createGitImportPage()
{
    auto *page = new OnboardingPage(this);
    page->setTitle(i18n("Clone Git Repository"));

    auto *form = new QFormLayout(page);
    m_gitUrlEdit = new QLineEdit(page);
    m_gitUrlEdit->setPlaceholderText(QStringLiteral("https://github.com/user/repo.git"));
    form->addRow(i18n("Repository URL:"), m_gitUrlEdit);

    auto *infoLabel = new QLabel(i18n("Your project will be cloned into the base directory specified earlier."), page);
    infoLabel->setWordWrap(true);
    form->addRow(infoLabel);

    page->registerFieldPublic(QStringLiteral("gitUrl*"), m_gitUrlEdit);

    return page;
}

QWizardPage* OnboardingWizard::createAiPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("Connect AI Assistant"));

    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    m_aiProviderCombo = new QComboBox(page);
    m_aiProviderCombo->addItems({QStringLiteral("Ollama (Local)"), QStringLiteral("OpenAI"), QStringLiteral("Anthropic")});
    form->addRow(i18n("AI Provider:"), m_aiProviderCombo);

    m_aiModelCombo = new QComboBox(page);
    m_aiModelCombo->setEditable(true);
    form->addRow(i18n("Model Name:"), m_aiModelCombo);

    m_aiKeyEdit = new QLineEdit(page);
    m_aiKeyEdit->setEchoMode(QLineEdit::Password);
    form->addRow(i18n("API Key:"), m_aiKeyEdit);

    layout->addLayout(form);

    m_testAiBtn = new QPushButton(i18n("Test Connection"), page);
    connect(m_testAiBtn, &QPushButton::clicked, this, &OnboardingWizard::testAiConnection);
    layout->addWidget(m_testAiBtn);

    m_testStatusLabel = new QLabel(page);
    m_testStatusLabel->setWordWrap(true);
    layout->addWidget(m_testStatusLabel);

    layout->addSpacing(20);
    m_ollamaStatus = new QLabel(page);
    m_ollamaStatus->setWordWrap(true);
    m_ollamaStatus->hide();
    layout->addWidget(m_ollamaStatus);

    connect(m_aiProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_aiKeyEdit->setEnabled(index != 0); // Disable for Ollama
        m_aiModelCombo->clear();
        if (index == 0) {
            m_aiModelCombo->addItems({QStringLiteral("llama3"), QStringLiteral("mistral"), QStringLiteral("phi3")});
            checkOllama();
        } else if (index == 1) {
            m_aiModelCombo->addItems({QStringLiteral("gpt-4o"), QStringLiteral("gpt-4-turbo"), QStringLiteral("gpt-3.5-turbo")});
            m_ollamaStatus->hide();
        } else {
            m_aiModelCombo->addItems({QStringLiteral("claude-3-5-sonnet-20240620"), QStringLiteral("claude-3-opus-20240229")});
            m_ollamaStatus->hide();
        }
    });

    // Trigger initial state
    m_aiProviderCombo->currentIndexChanged(0);

    return page;
}

QWizardPage* OnboardingWizard::createGithubPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("GitHub Integration (Optional)"));

    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(i18n("Connect your account to enable automatic cloud backups and worldbuilding collaborations."), page);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *loginBtn = new QPushButton(i18n("Login to GitHub..."), page);
    connect(loginBtn, &QPushButton::clicked, this, [this]() {
        GitHubOnboardingDialog dialog(this);
        dialog.exec();
    });
    layout->addWidget(loginBtn);

    return page;
}

QWizardPage* OnboardingWizard::createFinishPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("All Set!"));

    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(i18n("Congratulations! RPG Forge is ready for your next adventure.\n\n"
                                 "You can always change these settings later in the application preferences."), page);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();

    return page;
}

void OnboardingWizard::testAiConnection()
{
    m_testStatusLabel->setText(i18n("Testing connection..."));
    m_testAiBtn->setEnabled(false);

    LLMRequest req;
    req.provider = static_cast<LLMProvider>(m_aiProviderCombo->currentIndex());
    req.model = m_aiModelCombo->currentText();
    req.stream = false;
    
    LLMMessage msg;
    msg.role = QStringLiteral("user");
    msg.content = QStringLiteral("Respond with only the word 'OK' if you can hear me.");
    req.messages << msg;

    // Temporarily set the key for testing
    QString oldKey = LLMService::instance().apiKey(req.provider);
    LLMService::instance().setApiKey(req.provider, m_aiKeyEdit->text());

    QPointer<OnboardingWizard> weakThis(this);
    LLMService::instance().sendNonStreamingRequest(req, [weakThis, oldKey, req](const QString &response) {
        // Restore old key (though wizard will overwrite it on accept anyway)
        LLMService::instance().setApiKey(req.provider, oldKey);
        
        if (!weakThis) return;
        
        if (response.trimmed().contains(QStringLiteral("OK"), Qt::CaseInsensitive)) {
            weakThis->m_testStatusLabel->setText(i18n("<font color='green'>Success! Connection verified.</font>"));
        } else {
            weakThis->m_testStatusLabel->setText(i18n("<font color='red'>Failed: Received unexpected response. Check your key and model.</font>"));
        }
        
        weakThis->m_testAiBtn->setEnabled(true);
    });
}

void OnboardingWizard::checkOllama()
{
    QProcess proc;
    proc.start(QStringLiteral("ollama"), {QStringLiteral("--version")});
    if (!proc.waitForFinished(1000) || proc.exitCode() != 0) {
        m_ollamaStatus->show();
        const QColor errorFg = KColorScheme(QPalette::Active, KColorScheme::Window).foreground(KColorScheme::NegativeText).color();
        m_ollamaStatus->setText(QStringLiteral("<font color='%1'>%2</font>")
            .arg(errorFg.name(), i18n("Note: Ollama does not seem to be running. Please install it to use local models.")));
    } else {
        m_ollamaStatus->hide();
    }
}

void OnboardingWizard::accept()
{
    QString baseDir = m_projectDirEdit->text();
    QString name = m_projectNameEdit->text();
    QString fullDir;

    if (m_importScrivenerRadio->isChecked()) {
        QString scrivPath = m_scExternalPathEdit ? m_scExternalPathEdit->text() : m_scrivenerPathEdit->text();
        if (name.isEmpty()) name = QFileInfo(scrivPath).baseName();
        fullDir = QDir(baseDir).absoluteFilePath(name);

        AgentGatekeeper::instance().pauseAll();

        if (ProjectManager::instance().createProject(fullDir, name)) {
            auto *progress = new QProgressDialog(i18n("Importing Scrivener Project..."), i18n("Cancel"), 0, 100, this);
            progress->setWindowModality(Qt::WindowModal);
            progress->show();

            QEventLoop loop;
            auto importer = std::make_shared<ScrivenerImporter>();
            
            connect(importer.get(), &ScrivenerImporter::progress, progress, &QProgressDialog::setValue);
            connect(importer.get(), &ScrivenerImporter::progress, progress, [progress](int, const QString &msg) {
                progress->setLabelText(msg);
            });

            bool success = false;
            QJsonObject resultData;
            QtConcurrent::run([importer, scrivPath, fullDir]() {
                ProjectTreeModel backgroundModel;
                bool ok = importer->import(scrivPath, fullDir, &backgroundModel);
                return qMakePair(ok, backgroundModel.projectData());
            }).then([&loop, &success, &resultData](QPair<bool, QJsonObject> result) {
                success = result.first;
                resultData = result.second;
                loop.quit();
            });

            loop.exec();
            
            if (success) {
                ProjectManager::instance().setTree(resultData);
                ProjectManager::instance().saveProject();
            }
            progress->close();
            delete progress;
        }
        
        AgentGatekeeper::instance().resumeAll();
    } else if (m_importGitRadio->isChecked()) {
        QString gitUrl = m_gitUrlEdit->text();
        name = QFileInfo(gitUrl).baseName();
        if (name.endsWith(QStringLiteral(".git"))) name.chop(4);
        fullDir = QDir(baseDir).absoluteFilePath(name);
        
        QDir().mkpath(fullDir);
        
        auto *progress = new QProgressDialog(i18n("Cloning repository..."), i18n("Cancel"), 0, 0, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->show();
        
        QEventLoop loop;
        bool success = false;
        
        QFuture<bool> future = GitService::instance().cloneRepo(gitUrl, fullDir);
        auto watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
        connect(watcher, &QFutureWatcher<bool>::finished, [&success, watcher]() {
            success = watcher->result();
        });
        watcher->setFuture(future);
        
        loop.exec();
        progress->close();
        
        if (success) {
            ProjectManager::instance().openProject(fullDir);
        } else {
            QMessageBox::warning(this, i18n("Git Error"), i18n("Failed to clone repository."));
        }
    } else {
        fullDir = QDir(baseDir).absoluteFilePath(name);
        AgentGatekeeper::instance().pauseAll();
        if (ProjectManager::instance().createProject(fullDir, name)) {
            ProjectManager::instance().setupDefaultProject(fullDir, name);
        }
        AgentGatekeeper::instance().resumeAll();
    }

    // Save AI Settings
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(m_aiProviderCombo->currentIndex());
    settings.setValue(QStringLiteral("llm/provider"), static_cast<int>(provider));
    
    QString model = m_aiModelCombo->currentText();
    if (!model.isEmpty()) {
        if (provider == LLMProvider::OpenAI) settings.setValue(QStringLiteral("llm/openai/model"), model);
        else if (provider == LLMProvider::Anthropic) settings.setValue(QStringLiteral("llm/anthropic/model"), model);
        else settings.setValue(QStringLiteral("llm/ollama/model"), model);
    }

    if (!m_aiKeyEdit->text().isEmpty()) {
        LLMService::instance().setApiKey(provider, m_aiKeyEdit->text());
    }

    settings.setValue(QStringLiteral("firstRunComplete"), true);
    settings.sync();

    QWizard::accept();
}
