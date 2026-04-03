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

#include "visualdiffview.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KTextEditor/Editor>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Attribute>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <KLocalizedString>

#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>

// Kompare-like vibrant colors
static const QColor COLOR_MOD(255, 200, 200);   // Salmon
static const QColor COLOR_ADD(200, 255, 200);   // Green
static const QColor COLOR_DEL(200, 200, 255);   // Blue

class DiffConnector : public QWidget {
    public:
        DiffConnector(VisualDiffView *parent) : QWidget(parent), m_view(parent) {
            setFixedWidth(80);
            setMouseTracking(true);
        }
        
        void paintEvent(QPaintEvent *event) override {
            Q_UNUSED(event);
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            for (const auto &hunk : m_view->m_visibleHunks) {
                QColor baseColor;
                if (hunk.data.type == DiffHunk::Modified) baseColor = COLOR_MOD;
                else if (hunk.data.type == DiffHunk::Added) baseColor = COLOR_ADD;
                else baseColor = COLOR_DEL;

                int tl = hunk.leftRect.top();
                int bl = hunk.leftRect.bottom();
                int tr = hunk.rightRect.top();
                int br = hunk.rightRect.bottom();

                // Make bands flow better using Kompare's 40% control point logic
                auto makePath = [this](int y1, int y2) {
                    QPainterPath p;
                    p.moveTo(0, y1);
                    int o = width() * 0.4;
                    p.cubicTo(o, y1, width() - o, y2, width(), y2);
                    return p;
                };

                QPainterPath topPath = makePath(tl, tr);
                QPainterPath bottomPath = makePath(bl, br);

                QPainterPath band;
                band.addPath(topPath);
                band.lineTo(width(), br);
                band.addPath(bottomPath.toReversed());
                band.lineTo(0, tl);

                QColor fillColor = baseColor;
                fillColor.setAlpha(120);
                painter.setBrush(fillColor);
                painter.setPen(baseColor);
                painter.drawPath(band);
            }
        }

        void mousePressEvent(QMouseEvent *event) override {
            for (const auto &hunk : m_view->m_visibleHunks) {
                // Check if click was inside this band's Y range
                // (Simplified: just check vertical hit)
                int midYLeft = (hunk.leftRect.top() + hunk.leftRect.bottom()) / 2;
                int midYRight = (hunk.rightRect.top() + hunk.rightRect.bottom()) / 2;
                int midY = (midYLeft + midYRight) / 2;
                
                if (qAbs(event->pos().y() - midY) < 20) {
                    m_view->applyHunk(hunk.index);
                    break;
                }
            }
        }

    private:
        VisualDiffView *m_view;
};

VisualDiffView::VisualDiffView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

VisualDiffView::~VisualDiffView() = default;

void VisualDiffView::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(5, 5, 5, 5);
    
    auto *saveBtn = new QToolButton(this);
    saveBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    saveBtn->setText(i18n("Save Changes"));
    saveBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(saveBtn, &QToolButton::clicked, this, &VisualDiffView::saveChanges);
    toolbar->addWidget(saveBtn);

    auto *swapBtn = new QToolButton(this);
    swapBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    swapBtn->setText(i18n("Swap Direction"));
    swapBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(swapBtn, &QToolButton::clicked, this, &VisualDiffView::swapDiff);
    toolbar->addWidget(swapBtn);

    toolbar->addStretch();
    
    auto *reloadBtn = new QToolButton(this);
    reloadBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh-symbolic")));
    reloadBtn->setText(i18n("Reload Original"));
    reloadBtn->setToolTip(i18n("Discard applied changes and reload from disk/Git"));
    connect(reloadBtn, &QToolButton::clicked, this, [this]() {
        if (!m_filePath.isEmpty()) {
            setDiff(m_filePath, m_oldHash, m_newHash);
            Q_EMIT reloadRequested(m_filePath);
        } else if (!m_file1.isEmpty()) {
            setFiles(m_file1, m_file2);
        }
    });
    toolbar->addWidget(reloadBtn);

    layout->addLayout(toolbar);

    auto *editorLayout = new QHBoxLayout();
    editorLayout->setSpacing(0);

    auto *editor = KTextEditor::Editor::instance();

    // Old Version (Left)
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(i18n("Source Version"), this));
    m_oldDoc = editor->createDocument(this);
    m_oldDoc->setHighlightingMode(QStringLiteral("Markdown"));
    m_oldView = m_oldDoc->createView(this);
    m_oldView->setConfigValue(QStringLiteral("read-only"), true);
    leftLayout->addWidget(m_oldView);
    editorLayout->addWidget(leftWidget, 1);

    // Connector
    m_connector = new DiffConnector(this);
    editorLayout->addWidget(m_connector);

    // New Version (Right)
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(i18n("Target Version (Editable)"), this));
    m_newDoc = editor->createDocument(this);
    m_newDoc->setHighlightingMode(QStringLiteral("Markdown"));
    m_newView = m_newDoc->createView(this);
    rightLayout->addWidget(m_newView);
    editorLayout->addWidget(rightWidget, 1);

    layout->addLayout(editorLayout);

    connect(m_oldView->verticalScrollBar(), &QScrollBar::valueChanged, this, &VisualDiffView::syncScroll);
    connect(m_newView->verticalScrollBar(), &QScrollBar::valueChanged, this, &VisualDiffView::syncScroll);
}

void VisualDiffView::syncScroll()
{
    auto *senderBar = qobject_cast<QScrollBar*>(sender());
    if (!senderBar) return;

    QScrollBar *otherBar = (senderBar == m_oldView->verticalScrollBar()) 
        ? m_newView->verticalScrollBar() 
        : m_oldView->verticalScrollBar();
    
    otherBar->blockSignals(true);
    otherBar->setValue(senderBar->value());
    otherBar->blockSignals(false);

    updateConnectors();
}

void VisualDiffView::updateConnectors()
{
    m_visibleHunks.clear();
    
    for (int i = 0; i < m_hunks.count(); ++i) {
        const auto &hunk = m_hunks[i];
        
        QPoint oldTop = m_oldView->cursorToCoordinate(KTextEditor::Cursor(hunk.oldStart > 0 ? hunk.oldStart - 1 : 0, 0));
        QPoint oldBottom = m_oldView->cursorToCoordinate(KTextEditor::Cursor(hunk.oldStart + hunk.oldLines > 0 ? hunk.oldStart + hunk.oldLines - 1 : 0, 0));
        
        QPoint newTop = m_newView->cursorToCoordinate(KTextEditor::Cursor(hunk.newStart > 0 ? hunk.newStart - 1 : 0, 0));
        QPoint newBottom = m_newView->cursorToCoordinate(KTextEditor::Cursor(hunk.newStart + hunk.newLines > 0 ? hunk.newStart + hunk.newLines - 1 : 0, 0));

        // Use actual coordinate differences for line height
        int lh = 20; 
        QPoint nextLine = m_oldView->cursorToCoordinate(KTextEditor::Cursor(hunk.oldStart, 0));
        if (nextLine.y() > oldTop.y()) lh = nextLine.y() - oldTop.y();

        if (hunk.oldLines > 0) oldBottom.setY(oldBottom.y() + lh);
        else oldBottom.setY(oldTop.y() + lh); // Insertion point height

        if (hunk.newLines > 0) newBottom.setY(newBottom.y() + lh);
        else newBottom.setY(newTop.y() + lh);

        HunkInfo info;
        info.index = i;
        info.leftRect = QRect(0, oldTop.y(), 0, oldBottom.y() - oldTop.y());
        info.rightRect = QRect(0, newTop.y(), 0, newBottom.y() - newTop.y());
        info.data = hunk;
        
        m_visibleHunks.append(info);
    }
    m_connector->update();
}

void VisualDiffView::setDiff(const QString &filePath, const QString &oldHash, const QString &newHash)
{
    m_filePath = filePath;
    m_oldHash = oldHash;
    m_newHash = newHash;
    m_file1.clear();
    m_file2.clear();

    QString tempOld = QDir::tempPath() + QStringLiteral("/rpgforge_diff_old.md");
    auto oldFuture = GitService::instance().extractVersion(filePath, oldHash, tempOld);
    
    oldFuture.then(this, [this, tempOld, filePath, oldHash, newHash](bool success) {
        if (!success) return;
        m_oldDoc->openUrl(QUrl::fromLocalFile(tempOld));

        if (newHash.isEmpty()) {
            // Compare with working copy
            m_newDoc->openUrl(QUrl::fromLocalFile(filePath));
            highlightDiff();
        } else {
            QString tempNew = QDir::tempPath() + QStringLiteral("/rpgforge_diff_new.md");
            GitService::instance().extractVersion(filePath, newHash, tempNew).then(this, [this, tempNew](bool success) {
                if (success) {
                    m_newDoc->openUrl(QUrl::fromLocalFile(tempNew));
                    highlightDiff();
                }
            });
        }
    });
}

void VisualDiffView::setFiles(const QString &file1, const QString &file2)
{
    m_file1 = file1;
    m_file2 = file2;
    m_filePath.clear();
    
    m_oldDoc->openUrl(QUrl::fromLocalFile(file1));
    m_newDoc->openUrl(QUrl::fromLocalFile(file2));
    highlightDiff();
}

void VisualDiffView::highlightDiff()
{
    auto future = m_filePath.isEmpty() 
        ? GitService::instance().computeFileDiff(m_oldDoc->url().toLocalFile(), m_newDoc->url().toLocalFile())
        : GitService::instance().computeDiff(m_filePath, m_oldHash, m_newHash);

    future.then(this, [this](const QList<DiffHunk> &hunks) {
        m_hunks = hunks;
        clearHighlights();

        for (const auto &hunk : m_hunks) {
            QColor baseColor;
            if (hunk.type == DiffHunk::Modified) baseColor = COLOR_MOD;
            else if (hunk.type == DiffHunk::Added) baseColor = COLOR_ADD;
            else baseColor = COLOR_DEL;

            KTextEditor::Attribute::Ptr attr(new KTextEditor::Attribute());
            attr->setBackground(baseColor);
            // In KF6, we use setBackgroundFillWhitespace(true) to span the whole line width
            attr->setBackgroundFillWhitespace(true);

            if (hunk.oldLines > 0) {
                auto *range = m_oldDoc->newMovingRange(KTextEditor::Range(hunk.oldStart - 1, 0, hunk.oldStart + hunk.oldLines - 1, 0));
                range->setAttribute(attr);
                m_highlights.append(range);
            }
            if (hunk.newLines > 0) {
                auto *range = m_newDoc->newMovingRange(KTextEditor::Range(hunk.newStart - 1, 0, hunk.newStart + hunk.newLines - 1, 0));
                range->setAttribute(attr);
                m_highlights.append(range);
            }
        }
        updateConnectors();
    });
}

void VisualDiffView::clearHighlights()
{
    qDeleteAll(m_highlights);
    m_highlights.clear();
}

void VisualDiffView::applyHunk(int index)
{
    if (index < 0 || index >= m_hunks.size()) return;
    const auto &hunk = m_hunks[index];

    KTextEditor::Document::EditingTransaction transaction(m_newDoc);
    
    if (hunk.newLines > 0) {
        m_newDoc->removeText(KTextEditor::Range(hunk.newStart - 1, 0, hunk.newStart + hunk.newLines - 1, 0));
    }
    
    QString fullText;
    for (const auto &line : hunk.lines) {
        if (line.type == DiffLine::Deleted) { 
            fullText += line.content;
        }
    }
    
    if (!fullText.isEmpty()) {
        m_newDoc->insertText(KTextEditor::Cursor(hunk.newStart - 1, 0), fullText);
    }
    
    QTimer::singleShot(100, this, [this]() {
        highlightDiff();
    });
}

void VisualDiffView::swapDiff()
{
    m_swapped = !m_swapped;
    
    // Swap the data roles
    if (!m_filePath.isEmpty()) {
        // Swap hashes
        QString oldH = m_oldHash;
        m_oldHash = m_newHash;
        m_newHash = oldH;
        setDiff(m_filePath, m_oldHash, m_newHash);
    } else {
        // Swap file paths
        QString f1 = m_file1;
        m_file1 = m_file2;
        m_file2 = f1;
        setFiles(m_file1, m_file2);
    }
}

void VisualDiffView::saveChanges()
{
    QString targetPath;
    if (!m_filePath.isEmpty()) {
        if (m_newHash.isEmpty()) {
            targetPath = m_filePath;
        } else {
            targetPath = m_newDoc->url().toLocalFile();
        }
    } else {
        targetPath = m_file2;
    }

    if (!targetPath.isEmpty()) {
        m_newDoc->saveAs(QUrl::fromLocalFile(targetPath));
        Q_EMIT saveRequested(targetPath);
    }
}
