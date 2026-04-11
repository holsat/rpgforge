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
#include "projectmanager.h"
#include "scrivenerimporter.h"

#include <KLocalizedString>
#include <KColorScheme>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QSettings>
#include <QRegularExpression>
#include <QProgressDialog>
#include <QEventLoop>
#include <QMessageBox>
#include <QFutureWatcher>

class WizardPage : public QWizardPage {
public:
    using QWizardPage::QWizardPage;
    void registerField(const QString &name, QWidget *widget, const char *property = nullptr, const char *changedSignal = nullptr) {
        QWizardPage::registerField(name, widget, property, changedSignal);
    }
};

OnboardingWizard::OnboardingWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(i18n("Welcome to RPG Forge"));
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(600, 480);

    setPage(Page_Welcome, createWelcomePage());
    setPage(Page_Project, createProjectPage());
    setPage(Page_Scrivener, createScrivenerPage());
    setPage(Page_GitImport, createGitImportPage());
    setPage(Page_AI, createAiPage());
    setPage(Page_Github, createGithubPage());
    setPage(Page_Finish, createFinishPage());
    
    setStartId(Page_Welcome);
}

OnboardingWizard::~OnboardingWizard() = default;

int OnboardingWizard::nextId() const
{
    switch (currentId()) {
        case Page_Welcome:
            if (m_importScrivenerRadio->isChecked()) return Page_Scrivener;
            if (m_importGitRadio->isChecked()) return Page_GitImport;
            return Page_Project;
        case Page_Project:
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
    auto *label = new QLabel(i18n(
        "RPG Forge is a specialized IDE designed for game designers and worldbuilders. "
        "It combines professional writing tools, version control, and AI-powered assistance "
        "to help you craft your masterpiece.\n\n"
        "How would you like to start?"
    ), page);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_createNewRadio = new QRadioButton(i18n("Create a new project from a template"), page);
    m_importScrivenerRadio = new QRadioButton(i18n("Import an existing Scrivener project (.scriv)"), page);
    m_importGitRadio = new QRadioButton(i18n("Import from a Git repository"), page);
    m_createNewRadio->setChecked(true);

    layout->addSpacing(20);
    layout->addWidget(m_createNewRadio);
    layout->addWidget(m_importScrivenerRadio);
    layout->addWidget(m_importGitRadio);
    layout->addStretch();

    return page;
}

bool OnboardingWizard::validateCurrentPage()
{
    if (currentId() == Page_Project || currentId() == Page_Scrivener) {
        QString baseDir = m_projectDirEdit->text();
        QDir dir(baseDir);
        if (!dir.exists()) {
            if (!dir.mkpath(QStringLiteral("."))) {
                QMessageBox::critical(this, i18n("Error"), i18n("Could not create project directory: %1", baseDir));
                return false;
            }
        }
    } else if (currentId() == Page_AI) {
        if (aiProvider() == LLMProvider::Ollama) {
            QString model = aiModel();
            // Check if model is already installed
            // For simplicity, we'll try to pull it anyway (Ollama is fast if already exists)
            // But we show a progress dialog.
            auto *progress = new QProgressDialog(i18n("Preparing model: %1...", model), i18n("Cancel"), 0, 100, this);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->show();

            bool success = false;
            QString error;
            QEventLoop loop;

            LLMService::instance().pullModel(model, 
                [progress](double p, const QString &status) {
                    progress->setValue(static_cast<int>(p * 100));
                    progress->setLabelText(i18n("Status: %1", status));
                },
                [&success, &error, &loop](bool s, const QString &e) {
                    success = s;
                    error = e;
                    loop.quit();
                }
            );

            connect(progress, &QProgressDialog::canceled, &loop, &QEventLoop::quit);
            loop.exec();

            if (!success && !progress->wasCanceled()) {
                QMessageBox::warning(this, i18n("Ollama Error"), i18n("Failed to pull model: %1", error));
                // We let them continue anyway in case it was a minor error or already present
            }
            progress->deleteLater();
        }
    }
    return true;
}

QWizardPage* OnboardingWizard::createProjectPage()
{
    auto *page = new WizardPage(this);
    page->setTitle(i18n("Project Identity"));
    page->setSubTitle(i18n("Tell us what you're working on."));

    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    m_projectNameEdit = new QLineEdit(page);
    m_projectNameEdit->setPlaceholderText(i18n("e.g. Shadows of the Ancients"));
    form->addRow(i18n("Project Name:"), m_projectNameEdit);

    auto *dirLayout = new QHBoxLayout();
    m_projectDirEdit = new QLineEdit(page);
    m_projectDirEdit->setText(QDir::homePath() + QStringLiteral("/Documents/RPGProjects"));
    auto *browseBtn = new QPushButton(i18n("Browse..."), page);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Project Directory"), m_projectDirEdit->text());
        if (!dir.isEmpty()) m_projectDirEdit->setText(dir);
    });
    dirLayout->addWidget(m_projectDirEdit);
    dirLayout->addWidget(browseBtn);
    form->addRow(i18n("Project Folder:"), dirLayout);

    layout->addLayout(form);
    
    auto *hint = new QLabel(i18n("A folder named after your project will be created inside this directory."), page);
    hint->setForegroundRole(QPalette::PlaceholderText);
    hint->setStyleSheet(QStringLiteral("font-style: italic;"));
    layout->addWidget(hint);
    layout->addStretch();

    page->registerField(QStringLiteral("projectName*"), m_projectNameEdit);
    page->registerField(QStringLiteral("projectDir*"), m_projectDirEdit);

    return page;
}

QWizardPage* OnboardingWizard::createScrivenerPage()
{
    auto *page = new WizardPage(this);
    page->setTitle(i18n("Import Scrivener Project"));
    page->setSubTitle(i18n("Select the .scriv folder you wish to import."));

    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    auto *scrivLayout = new QHBoxLayout();
    m_scrivenerPathEdit = new QLineEdit(page);
    auto *browseBtn = new QPushButton(i18n("Browse..."), page);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Scrivener Project (.scriv)"), QDir::homePath());
        if (!dir.isEmpty()) m_scrivenerPathEdit->setText(dir);
    });
    scrivLayout->addWidget(m_scrivenerPathEdit);
    scrivLayout->addWidget(browseBtn);
    form->addRow(i18n("Scrivener Project:"), scrivLayout);

    auto *targetLayout = new QHBoxLayout();
    QLineEdit *targetEdit = new QLineEdit(page);
    targetEdit->setText(QDir::homePath() + QStringLiteral("/Documents/RPGProjects"));
    auto *targetBrowseBtn = new QPushButton(i18n("Browse..."), page);
    connect(targetBrowseBtn, &QPushButton::clicked, this, [this, targetEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Target Directory"), targetEdit->text());
        if (!dir.isEmpty()) targetEdit->setText(dir);
    });
    targetLayout->addWidget(targetEdit);
    targetLayout->addWidget(targetBrowseBtn);
    form->addRow(i18n("Import Into:"), targetLayout);

    layout->addLayout(form);
    layout->addStretch();

    page->registerField(QStringLiteral("scrivenerPath*"), m_scrivenerPathEdit);
    page->registerField(QStringLiteral("projectDir*"), targetEdit);

    return page;
}

QWizardPage* OnboardingWizard::createGitImportPage()
{
    auto *page = new WizardPage(this);
    page->setTitle(i18n("Import from Git"));
    page->setSubTitle(i18n("Enter the URL of the repository you want to clone."));

    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    m_gitUrlEdit = new QLineEdit(page);
    m_gitUrlEdit->setPlaceholderText(QStringLiteral("https://github.com/user/repo.git"));
    form->addRow(i18n("Repository URL:"), m_gitUrlEdit);

    auto *targetLayout = new QHBoxLayout();
    QLineEdit *targetEdit = new QLineEdit(page);
    targetEdit->setText(QDir::homePath() + QStringLiteral("/Documents/RPGProjects"));
    auto *targetBrowseBtn = new QPushButton(i18n("Browse..."), page);
    connect(targetBrowseBtn, &QPushButton::clicked, this, [this, targetEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, i18n("Select Target Directory"), targetEdit->text());
        if (!dir.isEmpty()) targetEdit->setText(dir);
    });
    targetLayout->addWidget(targetEdit);
    targetLayout->addWidget(targetBrowseBtn);
    form->addRow(i18n("Import Into:"), targetLayout);

    layout->addLayout(form);
    layout->addStretch();

    page->registerField(QStringLiteral("gitUrl*"), m_gitUrlEdit);
    page->registerField(QStringLiteral("projectDir*"), targetEdit);

    return page;
}

QWizardPage* OnboardingWizard::createAiPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("AI Writing Assistant"));
    page->setSubTitle(i18n("Configure your creative partner."));

    auto *layout = new QVBoxLayout(page);
    
    m_aiInstructions = new QLabel(page);
    m_aiInstructions->setWordWrap(true);
    m_aiInstructions->setOpenExternalLinks(true);
    layout->addWidget(m_aiInstructions);

    auto *form = new QFormLayout();
    m_aiProviderCombo = new QComboBox(page);
    m_aiProviderCombo->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama (Local)")});
    form->addRow(i18n("AI Provider:"), m_aiProviderCombo);

    m_aiKeyEdit = new QLineEdit(page);
    m_aiKeyEdit->setEchoMode(QLineEdit::Password);
    form->addRow(i18n("API Key:"), m_aiKeyEdit);

    m_aiModelCombo = new QComboBox(page);
    m_aiModelCombo->setEditable(true);
    m_aiModelCombo->setMinimumWidth(400); // 3x width approx
    form->addRow(i18n("Default Model:"), m_aiModelCombo);
    
    auto *testLayout = new QHBoxLayout();
    m_testAiBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")), i18n("Test Connection"), page);
    m_testStatusLabel = new QLabel(page);
    m_testStatusLabel->setWordWrap(true);
    testLayout->addWidget(m_testAiBtn);
    testLayout->addWidget(m_testStatusLabel, 1);
    form->addRow(QString(), testLayout);

    m_ollamaStatus = new QLabel(page);
    m_ollamaStatus->setWordWrap(true);
    m_ollamaStatus->hide();
    layout->addWidget(m_ollamaStatus);

    layout->addLayout(form);
    layout->addStretch();

    connect(m_aiProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OnboardingWizard::updateAiFields);
    connect(m_testAiBtn, &QPushButton::clicked, this, &OnboardingWizard::testAiConnection);
    
    updateAiFields();
    return page;
}

void OnboardingWizard::updateAiFields()
{
    QString providerText = m_aiProviderCombo->currentText();
    m_aiModelCombo->clear();
    m_ollamaStatus->hide();
    m_aiKeyEdit->setEnabled(true);
    m_testStatusLabel->clear();

    LLMProvider provider = static_cast<LLMProvider>(m_aiProviderCombo->currentIndex());

    if (provider == LLMProvider::OpenAI) {
        m_aiInstructions->setText(i18n(
            "<b>Steps to get your key:</b><br/>"
            "1. Visit <a href='https://platform.openai.com/api-keys'>OpenAI API Keys</a>.<br/>"
            "2. Sign in and click 'Create new secret key'.<br/>"
            "3. Copy and paste the key below.<br/><br/>"
            "<i>Tip: Select a vision-capable model from the list below for best experience.</i>"
        ));
    } else if (provider == LLMProvider::Anthropic) {
        m_aiInstructions->setText(i18n(
            "<b>Steps to get your key:</b><br/>"
            "1. Visit <a href='https://console.anthropic.com/settings/keys'>Anthropic Console</a>.<br/>"
            "2. Sign in and create a new key.<br/>"
            "3. Copy and paste the key below.<br/><br/>"
            "<i>Tip: Select a vision-capable model from the list below for best experience.</i>"
        ));
    } else {
        m_aiInstructions->setText(i18n(
            "<b>Ollama (Local AI)</b><br/>"
            "Ollama allows you to run powerful AI models locally on your own machine.<br/><br/>"
            "<i>Tip: For image analysis, install a vision-capable model via Ollama.</i>"
        ));
        m_aiKeyEdit->setEnabled(false);
        checkOllama();
    }

    // Fetch models asynchronously
    m_aiModelCombo->setPlaceholderText(i18n("Fetching models..."));
    LLMService::instance().fetchModels(provider, [this, provider](const QStringList &models) {
        m_aiModelCombo->setPlaceholderText(QString());
        m_aiModelCombo->addItems(models);
        
        if (models.isEmpty()) {
            m_aiModelCombo->setPlaceholderText(i18n("No models found — check connection or install models"));
        }
    });
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
        const QColor errorBg = KColorScheme(QPalette::Active, KColorScheme::Window).background(KColorScheme::NegativeBackground).color();
        m_ollamaStatus->setStyleSheet(QStringLiteral("color: %1; background-color: %2; padding: 10px; border-radius: 4px;")
            .arg(errorFg.name(), errorBg.name()));
        m_ollamaStatus->setText(i18n(
            "<b>Ollama not detected!</b><br/>"
            "To use local AI, you must install Ollama.<br/><br/>"
            "<b>Arch / CachyOS:</b> Run <code>sudo pacman -S ollama</code> then <code>systemctl enable --now ollama</code><br/>"
            "<b>Other:</b> Visit <a href='https://ollama.com'>ollama.com</a> to download."
        ));
    } else {
        m_ollamaStatus->show();
        const QColor okFg = KColorScheme(QPalette::Active, KColorScheme::Window).foreground(KColorScheme::PositiveText).color();
        const QColor okBg = KColorScheme(QPalette::Active, KColorScheme::Window).background(KColorScheme::PositiveBackground).color();
        m_ollamaStatus->setStyleSheet(QStringLiteral("color: %1; background-color: %2; padding: 10px; border-radius: 4px;")
            .arg(okFg.name(), okBg.name()));
        m_ollamaStatus->setText(i18n("Ollama is installed and ready to use."));
    }
}

QWizardPage* OnboardingWizard::createGithubPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("Cloud Backup & Sync"));
    page->setSubTitle(i18n("Connect your project to GitHub (Optional)."));

    auto *layout = new QVBoxLayout(page);
    auto *intro = new QLabel(i18n(
        "RPG Forge can automatically back up your project to GitHub. This ensures your work is safe "
        "and allows you to sync across multiple machines.\n\n"
        "To enable this, you need a Personal Access Token (PAT)."
    ), page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();
    m_githubTokenEdit = new QLineEdit(page);
    m_githubTokenEdit->setEchoMode(QLineEdit::Password);
    m_githubTokenEdit->setPlaceholderText(i18n("ghp_xxxxxxxxxxxx"));
    form->addRow(i18n("GitHub Token:"), m_githubTokenEdit);

    m_githubRepoEdit = new QLineEdit(page);
    form->addRow(i18n("Repository Name:"), m_githubRepoEdit);

    m_githubPrivateCheck = new QCheckBox(i18n("Make repository private"), page);
    m_githubPrivateCheck->setChecked(true);
    form->addRow(QString(), m_githubPrivateCheck);

    layout->addLayout(form);

    auto *link = new QLabel(page);
    link->setText(i18n("<a href='https://github.com/settings/tokens/new?description=RPGForge&scopes=repo'>Click here to generate a token on GitHub</a>"));
    link->setOpenExternalLinks(true);
    layout->addWidget(link);
    
    layout->addStretch();

    connect(m_projectNameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        QString repo = text.trimmed().toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        m_githubRepoEdit->setText(repo);
    });

    return page;
}

QWizardPage* OnboardingWizard::createFinishPage()
{
    auto *page = new QWizardPage(this);
    page->setTitle(i18n("Ready to Forge!"));
    page->setFinalPage(true);

    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(i18n(
        "Your workspace is ready. When you click Finish, RPG Forge will:\n\n"
        "1. Create your project directory and default structure.\n"
        "2. Configure your AI Assistant.\n"
        "3. Initialize your local and remote Git repository (if configured).\n\n"
        "Good luck with your story!"
    ), page);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();

    return page;
}
void OnboardingWizard::accept()
{
    QString baseDir = m_projectDirEdit->text();
    QString name = m_projectNameEdit->text();
    QString fullDir;

    if (m_importScrivenerRadio->isChecked()) {
        QString scrivPath = m_scrivenerPathEdit->text();
        if (name.isEmpty()) name = QFileInfo(scrivPath).baseName();
        fullDir = QDir(baseDir).absoluteFilePath(name);

        if (ProjectManager::instance().createProject(fullDir, name)) {
            ProjectTreeModel model;
            ScrivenerImporter importer;
            importer.import(scrivPath, fullDir, &model);
            ProjectManager::instance().setTree(model.projectData());
            ProjectManager::instance().saveProject();
        }
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
        if (ProjectManager::instance().createProject(fullDir, name)) {
            ProjectManager::instance().setupDefaultProject(fullDir, name);
        }
    }

    // 2. AI Config
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    LLMProvider provider = static_cast<LLMProvider>(m_aiProviderCombo->currentIndex());
    settings.setValue(QStringLiteral("llm/provider"), static_cast<int>(provider));
    
    QString model = m_aiModelCombo->currentText();
    if (provider == LLMProvider::OpenAI) {
        settings.setValue(QStringLiteral("llm/openai/model"), model);
        LLMService::instance().setApiKey(LLMProvider::OpenAI, m_aiKeyEdit->text());
    } else if (provider == LLMProvider::Anthropic) {
        settings.setValue(QStringLiteral("llm/anthropic/model"), model);
        LLMService::instance().setApiKey(LLMProvider::Anthropic, m_aiKeyEdit->text());
    } else {
        settings.setValue(QStringLiteral("llm/ollama/model"), model);
    }

    // 3. GitHub Config
    QString token = m_githubTokenEdit->text();
    if (!token.isEmpty()) {
        GitHubService::instance().setToken(token);
        QString repoName = m_githubRepoEdit->text();
        // Initialize Git locally first
        GitService::instance().initRepo(fullDir);
        // Create remote repo (async)
        GitHubService::instance().createRemoteRepo(repoName, m_githubPrivateCheck->isChecked());
    }

    settings.setValue(QStringLiteral("firstRunComplete"), true);
    
    // Automatically open the README after the wizard finishes
    QString readmePath = QDir(fullDir).absoluteFilePath(QStringLiteral("research/README.md"));
    if (QFile::exists(readmePath)) {
        auto *mainWin = qobject_cast<MainWindow*>(parent());
        if (mainWin) {
            mainWin->openFileFromUrl(QUrl::fromLocalFile(readmePath));
        }
    }

    QWizard::accept();
}

QString OnboardingWizard::projectName() const { return m_projectNameEdit->text(); }
QString OnboardingWizard::projectDir() const { return m_projectDirEdit->text(); }
LLMProvider OnboardingWizard::aiProvider() const { return static_cast<LLMProvider>(m_aiProviderCombo->currentIndex()); }
QString OnboardingWizard::aiKey() const { return m_aiKeyEdit->text(); }
QString OnboardingWizard::aiModel() const { return m_aiModelCombo->currentText(); }
QString OnboardingWizard::githubToken() const { return m_githubTokenEdit->text(); }
QString OnboardingWizard::githubRepo() const { return m_githubRepoEdit->text(); }
bool OnboardingWizard::isGithubPrivate() const { return m_githubPrivateCheck->isChecked(); }
