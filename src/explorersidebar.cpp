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

#include "explorersidebar.h"
#include "projecttreepanel.h"
#include "outlinepanel.h"
#include "fileexplorer.h"

#include <KLocalizedString>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// Build a collapsible pane: a header strip with a toggle button + title,
/// then the body widget. Returns the pane widget; out-params give the
/// caller references to the toggle button and the body for wiring.
QWidget *buildCollapsiblePane(const QString &title, const QString &tooltip,
                               const QString &iconName,
                               QWidget *body, QToolButton **outToggle,
                               QWidget *parent)
{
    auto *pane = new QWidget(parent);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(pane);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    auto *toggle = new QToolButton(header);
    toggle->setCheckable(true);
    toggle->setAutoRaise(true);
    toggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toggle->setToolTip(tooltip);
    toggle->setIcon(QIcon::fromTheme(iconName, QIcon::fromTheme(QStringLiteral("folder"))));

    auto *titleLabel = new QLabel(title, header);

    headerLayout->addWidget(toggle);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    layout->addWidget(header);
    layout->addWidget(body, /*stretch=*/1);

    *outToggle = toggle;
    return pane;
}

} // namespace

ExplorerSidebar::ExplorerSidebar(ProjectTreePanel *projectTree,
                                 OutlinePanel *outlinePanel,
                                 FileExplorer *fileExplorer,
                                 QWidget *parent)
    : QWidget(parent),
      m_projectTree(projectTree),
      m_outlinePanel(outlinePanel),
      m_fileExplorer(fileExplorer)
{
    setupUi();
}

ExplorerSidebar::~ExplorerSidebar() = default;

void ExplorerSidebar::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_projectTree);

    m_outlinePane = buildCollapsiblePane(
        i18n("Document Outline"),
        i18n("Show or hide the document outline"),
        QStringLiteral("view-list-tree-symbolic"),
        m_outlinePanel, &m_outlineToggleBtn, this);
    m_splitter->addWidget(m_outlinePane);

    m_filesystemPane = buildCollapsiblePane(
        i18n("Filesystem"),
        i18n("Show or hide the filesystem browser"),
        QStringLiteral("folder-symbolic"),
        m_fileExplorer, &m_filesystemToggleBtn, this);
    m_splitter->addWidget(m_filesystemPane);

    // Project tree gets all the stretch; the two lower panes size to their
    // content (plus whatever the user gives them by dragging the splitter).
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 0);
    // Panes can still collapse fully via the header toggle without the
    // splitter's own collapse-drag, which is confusing next to our toggles.
    m_splitter->setChildrenCollapsible(false);

    layout->addWidget(m_splitter);

    connect(m_outlineToggleBtn, &QToolButton::toggled,
            this, &ExplorerSidebar::setOutlinePanelVisible);
    connect(m_filesystemToggleBtn, &QToolButton::toggled,
            this, &ExplorerSidebar::setFileExplorerVisible);

    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    const bool outlineVisible = settings.value(
        QStringLiteral("ui/outline_panel_visible"), true).toBool();
    const bool fsVisible = settings.value(
        QStringLiteral("ui/filesystem_browser_visible"), false).toBool();
    m_outlineExpandedHeight = settings.value(
        QStringLiteral("ui/outline_panel_height"), 0).toInt();
    m_filesystemExpandedHeight = settings.value(
        QStringLiteral("ui/filesystem_browser_height"), 0).toInt();

    m_outlineToggleBtn->setChecked(outlineVisible);
    m_outlinePanel->setVisible(outlineVisible);
    m_filesystemToggleBtn->setChecked(fsVisible);
    m_fileExplorer->setVisible(fsVisible);

    // User-dragged splitter saves per-pane expanded heights to QSettings so
    // the layout survives restart. Only save while a pane is actually
    // expanded — collapsed panes sit at their header height, which isn't
    // a useful "expanded" value.
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        const QList<int> sizes = m_splitter->sizes();
        if (sizes.size() < 3) return;
        QSettings s(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        if (m_outlinePanel && m_outlinePanel->isVisible() && m_outlinePane) {
            const int headerOnly = m_outlinePane->sizeHint().height();
            if (sizes[1] > headerOnly + 20) {
                m_outlineExpandedHeight = sizes[1];
                s.setValue(QStringLiteral("ui/outline_panel_height"), m_outlineExpandedHeight);
            }
        }
        if (m_fileExplorer && m_fileExplorer->isVisible() && m_filesystemPane) {
            const int headerOnly = m_filesystemPane->sizeHint().height();
            if (sizes[2] > headerOnly + 20) {
                m_filesystemExpandedHeight = sizes[2];
                s.setValue(QStringLiteral("ui/filesystem_browser_height"), m_filesystemExpandedHeight);
            }
        }
    });

    reflowSplitter();
}

void ExplorerSidebar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The splitter's height is 0 until the widget is first shown, so the
    // ctor's reflowSplitter() call is a no-op. Defer to the event loop so
    // Qt has finished initial layout before we force our persisted sizes.
    QTimer::singleShot(0, this, [this]() { reflowSplitter(); });
}

void ExplorerSidebar::setOutlinePanelVisible(bool visible)
{
    if (!m_outlinePanel) return;
    m_outlinePanel->setVisible(visible);
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("ui/outline_panel_visible"), visible);
    reflowSplitter();
}

void ExplorerSidebar::setFileExplorerVisible(bool visible)
{
    if (!m_fileExplorer) return;
    m_fileExplorer->setVisible(visible);
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("ui/filesystem_browser_visible"), visible);
    reflowSplitter();
}

void ExplorerSidebar::reflowSplitter()
{
    if (!m_splitter) return;

    // When a lower pane's body widget is hidden, its pane shrinks to the
    // header height. Explicitly push splitter sizes so the Project Tree
    // absorbs the freed space instead of stranding a gap between panes.
    const int total = m_splitter->orientation() == Qt::Vertical
        ? m_splitter->height()
        : m_splitter->width();
    if (total <= 0) return;

    auto paneSize = [](QWidget *pane, bool bodyVisible, int rememberedHeight,
                       int defaultHeight) -> int {
        if (!pane) return 0;
        if (!bodyVisible) {
            // Pane collapsed: only the header remains.
            return pane->sizeHint().height();
        }
        // Expanded: prefer the user's last-dragged height; fall back to the
        // default the first time a user expands a pane.
        return rememberedHeight > 0 ? rememberedHeight : defaultHeight;
    };

    const int outlineSize = paneSize(m_outlinePane,
        m_outlinePanel && m_outlinePanel->isVisible(),
        m_outlineExpandedHeight, 200);
    const int fsSize = paneSize(m_filesystemPane,
        m_fileExplorer && m_fileExplorer->isVisible(),
        m_filesystemExpandedHeight, 200);
    const int treeSize = qMax(100, total - outlineSize - fsSize);

    m_splitter->setSizes({treeSize, outlineSize, fsSize});
}
