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
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>

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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);

    // Toolbar
    auto *toolbar = new QHBoxLayout();
    
    m_startBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Start"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &SimulationPanel::onStartClicked);
    toolbar->addWidget(m_startBtn);

    m_stopBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-stop")), i18n("Stop"), this);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &SimulationPanel::onStopClicked);
    toolbar->addWidget(m_stopBtn);

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

    toolbar->addStretch();
    layout->addLayout(toolbar);

    // Tabs
    auto *tabs = new QTabWidget(this);
    
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    tabs->addTab(m_logEdit, i18n("Live Log"));

    m_stateTree = new QTreeWidget(this);
    m_stateTree->setHeaderLabels({i18n("Path"), i18n("Value")});
    m_stateTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tabs->addTab(m_stateTree, i18n("World State"));

    layout->addWidget(tabs);
}

void SimulationPanel::onStartClicked()
{
    SimulationManager &mgr = SimulationManager::instance();
    mgr.setMaxTurns(m_turnsSpin->value());
    mgr.setBatchMode(m_batchCheck->isChecked());
    mgr.setRunCount(m_batchCheck->isChecked() ? m_batchSpin->value() : 1);
    
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
    m_logEdit->append(message);
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
    m_logEdit->append(i18n("Total Runs: %1").arg(results.totalRuns));
    
    // Basic analysis: Average turns
    double avgTurns = 0;
    for (const auto &run : results.runs) avgTurns += run.turns;
    avgTurns /= results.runs.size();
    
    m_logEdit->append(i18n("Average Duration: %1 turns").arg(QString::number(avgTurns, 'f', 1)));
    
    // In a future step, we'll add character mortality/utility metrics by parsing finalStates
}
