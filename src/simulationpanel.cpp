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

#include "simulationpanel.h"
#include "projectmanager.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QTabWidget>
#include <QHeaderView>
#include <QApplication>
#include <KColorScheme>
#include <QLineEdit>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>

SimulationPanel::SimulationPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    SimulationManager &mgr = SimulationManager::instance();
    connect(&mgr, &SimulationManager::logMessage, this, &SimulationPanel::onLogMessage);
    connect(&mgr, &SimulationManager::simulationStarted, this, &SimulationPanel::onSimulationStarted);
    connect(&mgr, &SimulationManager::simulationStopped, this, &SimulationPanel::onSimulationStopped);
    connect(&mgr, &SimulationManager::batchFinished, this, &SimulationPanel::onBatchFinished);
    connect(mgr.state(), &SimulationState::stateChanged, this, &SimulationPanel::onStateChanged);
}

SimulationPanel::~SimulationPanel() = default;

void SimulationPanel::setupUi()
{
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);

    // Toolbar 1: Controls
    auto *toolbar = new QHBoxLayout();
    
    m_startBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Start"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &SimulationPanel::onStartClicked);
    toolbar->addWidget(m_startBtn);

    m_stopBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-stop")), i18n("Stop"), this);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &SimulationPanel::onStopClicked);
    toolbar->addWidget(m_stopBtn);

    auto *saveBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QString(), this);
    saveBtn->setToolTip(i18n("Save current world state to JSON"));
    saveBtn->setAccessibleName(i18n("Save world state"));
    connect(saveBtn, &QPushButton::clicked, this, &SimulationPanel::onSaveResult);
    toolbar->addWidget(saveBtn);

    toolbar->addSpacing(10);
    
    toolbar->addWidget(new QLabel(i18n("Max Turns:"), this));
    m_turnsSpin = new QSpinBox(this);
    m_turnsSpin->setRange(1, 500);
    m_turnsSpin->setValue(20);
    toolbar->addWidget(m_turnsSpin);

    m_batchCheck = new QCheckBox(i18n("Batch"), this);
    toolbar->addWidget(m_batchCheck);

    m_batchSpin = new QSpinBox(this);
    m_batchSpin->setRange(1, 1000);
    m_batchSpin->setValue(10);
    m_batchSpin->setEnabled(false);
    toolbar->addWidget(m_batchSpin);
    connect(m_batchCheck, &QCheckBox::toggled, m_batchSpin, &QSpinBox::setEnabled);

    toolbar->addSpacing(10);
    toolbar->addWidget(new QLabel(i18n("Aggression:"), this));
    m_aggressionSlider = new QSlider(Qt::Horizontal, this);
    m_aggressionSlider->setRange(1, 5);
    m_aggressionSlider->setValue(3);
    m_aggressionSlider->setFixedWidth(100);
    m_aggressionSlider->setToolTip(i18n("1: Narrative/Casual, 5: Tactical/Munchkin"));
    toolbar->addWidget(m_aggressionSlider);

    toolbar->addStretch();
    layout->addLayout(toolbar);

    // Toolbar 2: Scenario
    auto *scenarioLayout = new QHBoxLayout();
    scenarioLayout->addWidget(new QLabel(i18n("Scenario:"), this));
    m_scenarioEdit = new QLineEdit(this);
    m_scenarioEdit->setPlaceholderText(i18n("Drag markdown scenario here..."));
    scenarioLayout->addWidget(m_scenarioEdit);
    
    auto *loadScenarioBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), QString(), this);
    loadScenarioBtn->setToolTip(i18n("Select scenario file"));
    loadScenarioBtn->setAccessibleName(i18n("Load scenario file"));
    connect(loadScenarioBtn, &QPushButton::clicked, this, &SimulationPanel::onLoadScenario);
    scenarioLayout->addWidget(loadScenarioBtn);
    
    layout->addLayout(scenarioLayout);

    // Tabs
    auto *tabs = new QTabWidget(this);
    
    // Log Tab
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    tabs->addTab(m_logEdit, i18n("Live Log"));

    // Actors Tab
    auto *actorTab = new QWidget(this);
    auto *actorLayout = new QVBoxLayout(actorTab);
    
    m_actorList = new QListWidget(this);
    actorLayout->addWidget(m_actorList);
    
    auto *actorBtns = new QHBoxLayout();
    auto *addActorBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add..."), this);
    auto *removeActorBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18n("Remove"), this);
    connect(addActorBtn, &QPushButton::clicked, this, &SimulationPanel::onAddActor);
    connect(removeActorBtn, &QPushButton::clicked, this, &SimulationPanel::onRemoveActor);
    actorBtns->addWidget(addActorBtn);
    actorBtns->addWidget(removeActorBtn);
    actorLayout->addLayout(actorBtns);
    
    tabs->addTab(actorTab, i18n("Participants"));

    // State Tab
    m_stateTree = new QTreeWidget(this);
    m_stateTree->setHeaderLabels({i18n("Path"), i18n("Value")});
    m_stateTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tabs->addTab(m_stateTree, i18n("World State"));

    layout->addWidget(tabs);
}

void SimulationPanel::startSimulation()
{
    onStartClicked();
}

void SimulationPanel::onStartClicked()
{
    SimulationManager &mgr = SimulationManager::instance();
    mgr.setMaxTurns(m_turnsSpin->value());
    mgr.setBatchMode(m_batchCheck->isChecked());
    mgr.setRunCount(m_batchCheck->isChecked() ? m_batchSpin->value() : 1);
    mgr.setTacticalAggression(m_aggressionSlider->value());
    
    // Load Scenario
    if (!m_scenarioEdit->text().isEmpty()) {
        QFile file(m_scenarioEdit->text());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mgr.setScenario(QString::fromUtf8(file.readAll()));
        }
    } else {
        mgr.setScenario(i18n("A standard combat encounter."));
    }

    // Load Actors
    mgr.clearActors();
    for (int i = 0; i < m_actorList->count(); ++i) {
        QListWidgetItem *item = m_actorList->item(i);
        QString path = item->data(Qt::UserRole).toString();
        
        auto *actor = new SimulationActor(item->text(), &mgr);
        
        // Try to parse sheet as JSON
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.isObject()) {
                actor->setSheet(doc.object());
            } else {
                // Fallback: Treat raw text as context
                actor->setSheet(QJsonObject{{QStringLiteral("raw_context"), QString::fromUtf8(data)}});
            }
        }
        
        mgr.addActor(actor);
    }

    m_logEdit->clear();
    mgr.start();
}

void SimulationPanel::onStopClicked()
{
    SimulationManager::instance().stop();
}

void SimulationPanel::onSimulationStarted()
{
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
}

void SimulationPanel::onSimulationStopped()
{
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void SimulationPanel::onLogMessage(const QString &message)
{
    KColorScheme scheme(qApp->palette().currentColorGroup(), KColorScheme::View);
    QString systemColor = scheme.foreground(KColorScheme::InactiveText).color().name();
    QString arbiterColor = scheme.foreground(KColorScheme::LinkText).color().name();
    QString decisionColor = scheme.foreground(KColorScheme::NeutralText).color().name();

    // Simple HTML styling for the log
    QString formatted = message;
    if (message.startsWith(QLatin1String("---"))) {
        formatted = QStringLiteral("<b style='color: %1;'>%2</b>").arg(systemColor, message);
    } else if (message.startsWith(QLatin1String("Arbiter:"))) {
        formatted = QStringLiteral("<span style='color: %1;'><i>%2</i></span>").arg(arbiterColor, message);
    } else if (message.startsWith(QLatin1String("Griot:"))) {
        formatted = QStringLiteral("<span style='font-family: serif; font-size: 14px;'>%1</span>").arg(message.mid(6));
    } else if (message.contains(QLatin1String("decides to:"))) {
        formatted = QStringLiteral("<b style='color: %1;'>%2</b>").arg(decisionColor, message);
    }

    m_logEdit->append(formatted);
}

void SimulationPanel::onStateChanged()
{
    updateStateTree();
}

void SimulationPanel::updateStateTree()
{
    m_stateTree->clear();
    QJsonObject state = SimulationManager::instance().state()->state();

    std::function<void(const QJsonObject&, QTreeWidgetItem*)> fillTree;
    fillTree = [&](const QJsonObject &obj, QTreeWidgetItem *parent) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            auto *item = new QTreeWidgetItem(parent);
            item->setText(0, it.key());
            if (it.value().isObject()) {
                fillTree(it.value().toObject(), item);
            } else {
                item->setText(1, QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact).mid(1).chopped(1)));
            }
        }
    };

    fillTree(state, m_stateTree->invisibleRootItem());
    m_stateTree->expandAll();
}

void SimulationPanel::onBatchFinished(const BatchResult &results)
{
    showAnalytics(results);
}

void SimulationPanel::showAnalytics(const BatchResult &results)
{
    m_logEdit->append(QStringLiteral("\n=== BATCH ANALYTICS ==="));
    m_logEdit->append(i18n("Total Runs: %1", results.totalRuns));
    
    // Basic analysis: Average turns
    double avgTurns = 0;
    for (const auto &run : results.runs) avgTurns += run.turns;
    avgTurns /= results.runs.size();
    
    m_logEdit->append(i18n("Average Duration: %1 turns", QString::number(avgTurns, 'f', 1)));
    
    // In a future step, we'll add character mortality/utility metrics by parsing finalStates
}

void SimulationPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SimulationPanel::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".md")) || path.endsWith(QStringLiteral(".json")) || path.endsWith(QStringLiteral(".yaml"))) {
            // Decide if it's a character or scenario
            // For now, if dropped on the scenario line edit, it's a scenario
            if (m_scenarioEdit->geometry().contains(m_scenarioEdit->mapFromParent(event->position().toPoint()))) {
                m_scenarioEdit->setText(path);
            } else {
                addActorFromFile(path);
            }
        }
    }
}

void SimulationPanel::onLoadScenario()
{
    QString path = QFileDialog::getOpenFileName(this, i18n("Select Scenario"), QString(), i18n("Markdown Files (*.md)"));
    if (!path.isEmpty()) {
        m_scenarioEdit->setText(path);
    }
}

void SimulationPanel::onAddActor()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, i18n("Select Actors"), QString(), i18n("Character Files (*.md *.json *.yaml)"));
    for (const QString &path : paths) {
        addActorFromFile(path);
    }
}

void SimulationPanel::onRemoveActor()
{
    delete m_actorList->currentItem();
}

void SimulationPanel::onSaveResult()
{
    QString projectPath = ProjectManager::instance().projectPath();
    if (projectPath.isEmpty()) return;

    QString simDir = QDir(projectPath).absoluteFilePath(QStringLiteral("simulations"));
    QDir().mkpath(simDir);

    QString defaultName = QStringLiteral("sim_%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString path = QFileDialog::getSaveFileName(this, i18n("Save Simulation State"), 
        QDir(simDir).absoluteFilePath(defaultName),
        i18n("JSON Files (*.json)"));

    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QJsonObject state = SimulationManager::instance().state()->state();
            file.write(QJsonDocument(state).toJson());
            file.close();
        }
    }
}

void SimulationPanel::addActorFromFile(const QString &path)
{
    QFileInfo fi(path);
    auto *item = new QListWidgetItem(fi.fileName(), m_actorList);
    item->setData(Qt::UserRole, path);
    item->setIcon(QIcon::fromTheme(QStringLiteral("user-identity")));
}

