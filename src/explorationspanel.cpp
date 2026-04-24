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

#include "explorationspanel.h"
#include "explorationsgraphview.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

ExplorationsPanel::ExplorationsPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ExplorationsPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Toolbar row
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(4, 2, 4, 2);
    toolbar->setSpacing(4);

    auto *newBtn = new QToolButton(this);
    newBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-branch")));
    newBtn->setText(i18n("New Exploration"));
    newBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    newBtn->setAutoRaise(true);
    connect(newBtn, &QToolButton::clicked, this, &ExplorationsPanel::onNewExploration);
    toolbar->addWidget(newBtn);

    auto *refreshBtn = new QToolButton(this);
    refreshBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    refreshBtn->setToolTip(i18n("Refresh"));
    refreshBtn->setAutoRaise(true);
    connect(refreshBtn, &QToolButton::clicked, this, &ExplorationsPanel::refresh);
    toolbar->addWidget(refreshBtn);

    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // Graph view (scrollable via its own wheel handling)
    m_graphView = new ExplorationGraphView(this);
    m_graphView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusProxy(m_graphView);
    mainLayout->addWidget(m_graphView);

    // Forward signals from graph view
    connect(m_graphView, &ExplorationGraphView::switchRequested,
            this, &ExplorationsPanel::switchRequested);
    connect(m_graphView, &ExplorationGraphView::integrateRequested,
            this, &ExplorationsPanel::integrateRequested);
    connect(m_graphView, &ExplorationGraphView::createLandmarkRequested,
            this, &ExplorationsPanel::createLandmarkRequested);

    // Stash footer frame
    m_stashFrame = new QFrame(this);
    m_stashFrame->setFrameShape(QFrame::StyledPanel);
    m_stashFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_stashLayout = new QVBoxLayout(m_stashFrame);
    m_stashLayout->setContentsMargins(4, 4, 4, 4);
    m_stashLayout->setSpacing(4);

    // Header row within stash frame
    auto *stashHeaderRow = new QHBoxLayout();
    stashHeaderRow->setSpacing(4);

    m_stashToggle = new QToolButton(m_stashFrame);
    m_stashToggle->setArrowType(Qt::DownArrow);
    m_stashToggle->setAutoRaise(true);
    m_stashToggle->setFixedSize(20, 20);
    connect(m_stashToggle, &QToolButton::clicked, this, [this]() {
        m_stashExpanded = !m_stashExpanded;
        m_stashToggle->setArrowType(m_stashExpanded ? Qt::DownArrow : Qt::RightArrow);
        // Show/hide all stash entry widgets (skip header row at index 0)
        for (int i = 1; i < m_stashLayout->count(); ++i) {
            auto *item = m_stashLayout->itemAt(i);
            if (item && item->widget())
                item->widget()->setVisible(m_stashExpanded);
            else if (item && item->layout()) {
                // QHBoxLayout entries wrapped in a widget
                for (int j = 0; j < item->layout()->count(); ++j) {
                    auto *w = item->layout()->itemAt(j)->widget();
                    if (w) w->setVisible(m_stashExpanded);
                }
            }
        }
    });
    stashHeaderRow->addWidget(m_stashToggle);

    m_stashHeader = new QLabel(i18n("Parked Changes (0)"), m_stashFrame);
    QFont headerFont = m_stashHeader->font();
    headerFont.setBold(true);
    m_stashHeader->setFont(headerFont);
    stashHeaderRow->addWidget(m_stashHeader);
    stashHeaderRow->addStretch();

    m_stashLayout->addLayout(stashHeaderRow);

    mainLayout->addWidget(m_stashFrame);

    // Initially hidden until we know there are stashes
    m_stashFrame->setVisible(false);
}

void ExplorationsPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    m_graphView->setRepoPath(path);

    const QString branch = GitService::instance().currentBranch(path);
    m_graphView->setCurrentBranch(branch);

    refresh();
}

void ExplorationsPanel::refresh()
{
    m_graphView->refresh();
    refreshStashList();
}

void ExplorationsPanel::refreshStashList()
{
    const QString path = m_rootPath;
    if (path.isEmpty()) return;

    QtConcurrent::run([path] { return GitService::instance().listStashes(path); })
        .then(this, [this](QList<StashEntry> stashes) {
            // Remove all stash entry widgets (everything after the header row at index 0)
            while (m_stashLayout->count() > 1) {
                auto *item = m_stashLayout->takeAt(1);
                if (item->widget()) {
                    delete item->widget();
                } else if (item->layout()) {
                    while (item->layout()->count() > 0) {
                        auto *child = item->layout()->takeAt(0);
                        delete child->widget();
                        delete child;
                    }
                    delete item->layout();
                }
                delete item;
            }

            for (const StashEntry &entry : stashes) {
                buildStashEntry(m_stashLayout, entry);
            }

            m_stashHeader->setText(i18n("Parked Changes (%1)", stashes.size()));
            m_stashFrame->setVisible(!stashes.isEmpty());
        });
}

void ExplorationsPanel::buildStashEntry(QVBoxLayout *layout, const StashEntry &entry)
{
    auto *row = new QWidget(m_stashFrame);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    // Prefer a themed icon over an inline emoji — emoji fonts are
    // inconsistent across Linux distros and can render as tofu.
    QIcon icon = QIcon::fromTheme(QStringLiteral("package"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("folder-temp"));
    if (!icon.isNull()) {
        auto *iconLabel = new QLabel(row);
        iconLabel->setPixmap(icon.pixmap(16, 16));
        rowLayout->addWidget(iconLabel);
    }

    QString labelText;
    if (!entry.onBranch.isEmpty()) {
        const QString dateStr = entry.date.toString(QStringLiteral("MMM d, hh:mm"));
        labelText = QStringLiteral("%1 \u2014 %2").arg(entry.onBranch, dateStr);
    } else {
        QString msg = entry.message;
        if (msg.length() > 40)
            msg = msg.left(37) + QStringLiteral("\u2026");
        labelText = msg;
    }

    auto *label = new QLabel(labelText, row);
    label->setTextFormat(Qt::PlainText);
    rowLayout->addWidget(label);
    rowLayout->addStretch();

    auto *restoreBtn = new QPushButton(i18n("Restore"), row);
    restoreBtn->setFixedHeight(24);
    auto *discardBtn = new QPushButton(i18n("Discard"), row);
    discardBtn->setFixedHeight(24);

    // Disable both buttons the moment either is clicked. Stash indices shift
    // after any drop/apply, so a second click on the stale row would target
    // the wrong stash. The next refresh() rebuilds the list with fresh indices.
    auto disableRow = [restoreBtn, discardBtn]() {
        restoreBtn->setEnabled(false);
        discardBtn->setEnabled(false);
    };

    connect(restoreBtn, &QPushButton::clicked, this, [this, idx = entry.index, disableRow]() {
        disableRow();
        onStashApply(idx);
    });
    rowLayout->addWidget(restoreBtn);

    connect(discardBtn, &QPushButton::clicked, this, [this, idx = entry.index, disableRow]() {
        disableRow();
        onStashDrop(idx);
    });
    rowLayout->addWidget(discardBtn);

    row->setVisible(m_stashExpanded);
    layout->addWidget(row);
}

void ExplorationsPanel::onNewExploration()
{
    bool ok = false;
    QString name = QInputDialog::getText(this,
        i18n("New Exploration"),
        i18n("Exploration name:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    name = name.trimmed();

    // Validate the branch name: reject empty, slashes, backslashes, spaces,
    // leading dash, and ".." traversal attempts.
    if (name.isEmpty()
        || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\'))
        || name.contains(QLatin1Char(' '))
        || name.startsWith(QLatin1Char('-'))
        || name.contains(QStringLiteral(".."))) {
        KMessageBox::error(this,
            i18n("Invalid exploration name. Avoid spaces, slashes, and special characters."),
            i18n("Invalid Name"));
        return;
    }

    const QString path = m_rootPath;
    QtConcurrent::run([path, name] {
            return GitService::instance().createExploration(path, name);
        })
        .then(this, [this, name](bool created) {
            if (!created) {
                KMessageBox::error(this,
                    i18n("Could not create exploration \"%1\".", name),
                    i18n("Creation Failed"));
                return;
            }
            m_graphView->setCurrentBranch(name);
            refresh();
        });
}

void ExplorationsPanel::onStashApply(int stashIndex)
{
    GitService::instance().applyStash(m_rootPath, stashIndex)
        .then(this, [this](bool ok) {
            if (ok)
                refresh();
        });
}

void ExplorationsPanel::onStashDrop(int stashIndex)
{
    const auto result = QMessageBox::warning(this,
        i18n("Discard Parked Changes?"),
        i18n("These parked changes will be permanently deleted. This cannot be undone."),
        QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (result != QMessageBox::Discard)
        return;

    GitService::instance().dropStash(m_rootPath, stashIndex)
        .then(this, [this](bool) {
            refresh();
        });
}
