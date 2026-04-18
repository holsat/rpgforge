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
#include "gitservice.h"
#include "kompareinterface.h"

#include <KPluginFactory>
#include <KLocalizedString>
#include <KActionCollection>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QToolBar>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>

VisualDiffView::VisualDiffView(QWidget *parent)
    : QWidget(parent)
{
    m_scratchDir = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/rpgforge_diff_XXXXXX"));
    // The three-way conflict temps keep fixed names — they're semantically
    // distinct (ancestor / ours / theirs), not real project files.
    if (m_scratchDir->isValid()) {
        m_tempAncestor = m_scratchDir->filePath(QStringLiteral("ancestor.md"));
        m_tempOurs     = m_scratchDir->filePath(QStringLiteral("ours.md"));
        m_tempTheirs   = m_scratchDir->filePath(QStringLiteral("theirs.md"));
    }
    setupUi();
}

VisualDiffView::~VisualDiffView()
{
    // m_scratchDir's destructor removes the whole directory and its contents.
}

void VisualDiffView::allocateScratchPaths(const QString &oldSourceBasename,
                                           const QString &newSourceBasename,
                                           const QString &oldLabel,
                                           const QString &newLabel)
{
    if (!m_scratchDir || !m_scratchDir->isValid()) {
        m_tempOld = QDir::tempPath() + QStringLiteral("/rpgforge_diff_old.md");
        m_tempNew = QDir::tempPath() + QStringLiteral("/rpgforge_diff_new.md");
        return;
    }

    // If the two source basenames differ, use them verbatim — that's the
    // common case (comparing two distinct project files like Chapter_1.md
    // vs Chapter_1_Conflicted_Copy.md) and Kompare will show each side's
    // real filename in its header.
    if (!oldSourceBasename.isEmpty()
        && !newSourceBasename.isEmpty()
        && oldSourceBasename != newSourceBasename) {
        m_tempOld = m_scratchDir->filePath(oldSourceBasename);
        m_tempNew = m_scratchDir->filePath(newSourceBasename);
        return;
    }

    // Same source file, two versions (e.g. working copy vs commit). Prefix
    // the basename with a label so the user can tell which side is which.
    const QString base = !oldSourceBasename.isEmpty() ? oldSourceBasename : newSourceBasename;
    const QString stem = QFileInfo(base).completeBaseName();
    const QString suffix = QFileInfo(base).suffix();
    const QString extPart = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    m_tempOld = m_scratchDir->filePath(
        QStringLiteral("%1 (%2)%3").arg(stem, oldLabel, extPart));
    m_tempNew = m_scratchDir->filePath(
        QStringLiteral("%1 (%2)%3").arg(stem, newLabel, extPart));
}

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
    connect(saveBtn, &QToolButton::clicked, this, [this]() {
        if (m_part) {
            m_part->save();
            Q_EMIT saveRequested(m_file2);
        }
    });
    toolbar->addWidget(saveBtn);

    auto *swapBtn = new QToolButton(this);
    swapBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    swapBtn->setText(i18n("Swap Direction"));
    swapBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(swapBtn, &QToolButton::clicked, this, &VisualDiffView::swapDiff);
    toolbar->addWidget(swapBtn);

    auto *closeBtn = new QToolButton(this);
    closeBtn->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeBtn->setText(i18n("Close View"));
    closeBtn->setToolTip(i18n("Close the diff view and return to the editor"));
    closeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(closeBtn, &QToolButton::clicked, this, &VisualDiffView::closeRequested);
    toolbar->addWidget(closeBtn);

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

    loadKomparePart();

    // Secondary toolbar dedicated to the Kompare KPart's own diff actions
    // (Previous/Next Difference, Apply/Unapply, etc.). The main window's
    // toolbar merges these via KXMLGUI but they fall off the right edge at
    // typical window widths; this keeps them permanently visible.
    m_kompareToolbar = new QToolBar(this);
    m_kompareToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_kompareToolbar->setMovable(false);
    m_kompareToolbar->setFloatable(false);
    layout->addWidget(m_kompareToolbar);
    populateKompareToolbar();

    if (m_part && m_part->widget()) {
        layout->addWidget(m_part->widget(), 1);
    } else {
        auto *errorLabel = new QLabel(i18n("Could not load Kompare plugin. Ensure 'kompare' is installed."), this);
        errorLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(errorLabel, 1);
    }
}

void VisualDiffView::populateKompareToolbar()
{
    if (!m_kompareToolbar) return;
    m_kompareToolbar->clear();
    if (!m_part) {
        m_kompareToolbar->hide();
        return;
    }

    KActionCollection *ac = m_part->actionCollection();
    if (!ac) {
        m_kompareToolbar->hide();
        return;
    }

    // Kompare's action names differ across versions; rather than guessing,
    // pull every action the KPart's collection exposes and add any that
    // look diff-related. Filter out separators, invisible actions, and the
    // catch-all file-management actions that would duplicate the host's.
    const QList<QAction*> allActions = ac->actions();
    bool addedAny = false;
    for (QAction *action : allActions) {
        if (!action) continue;
        if (action->isSeparator()) continue;
        if (!action->isVisible()) continue;
        // Skip file open/close actions — those belong to the host window.
        const QString objName = action->objectName();
        if (objName == QLatin1String("file_open")
            || objName == QLatin1String("file_close")
            || objName == QLatin1String("file_save_as")
            || objName == QLatin1String("file_quit")) {
            continue;
        }
        m_kompareToolbar->addAction(action);
        addedAny = true;
    }

    m_kompareToolbar->setVisible(addedAny);
    if (!addedAny) {
        qDebug() << "VisualDiffView: Kompare KPart action collection is empty at this time;"
                 << "will retry on next show.";
    }
}

void VisualDiffView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The Kompare KPart's action collection may populate lazily — e.g. only
    // after the part has been added as a KXMLGUI client, which MainWindow
    // does when the diff view becomes active. Re-populate on show to catch
    // those actions without requiring a manual rebuild step.
    if (m_kompareToolbar && m_kompareToolbar->actions().isEmpty()) {
        populateKompareToolbar();
    }
}

void VisualDiffView::loadKomparePart()
{
    auto result = KPluginFactory::instantiatePlugin<KParts::ReadWritePart>(
        KPluginMetaData(QStringLiteral("kf6/parts/komparepart")), this);

    if (result) {
        m_part = result.plugin;
        m_kompareInterface = qobject_cast<KompareInterface*>(m_part);
    }
}

void VisualDiffView::setDiff(const QString &filePath, const QString &oldHash, const QString &newHash)
{
    m_filePath = filePath;
    m_oldHash = oldHash;
    m_newHash = newHash;
    m_file1.clear();
    m_file2.clear();

    if (!m_kompareInterface) return;

    // Same source file, two versions: label the temp files with the short
    // commit hash (or "working copy") so Kompare's header disambiguates them.
    const QString base = QFileInfo(filePath).fileName();
    const QString oldLabel = oldHash.left(7);
    const QString newLabel = newHash.isEmpty() ? i18n("working copy") : newHash.left(7);
    allocateScratchPaths(base, base, oldLabel, newLabel);

    auto oldFuture = GitService::instance().extractVersion(filePath, oldHash, m_tempOld);

    oldFuture.then(this, [this, filePath, newHash](bool success) {
        if (!success) return;

        if (newHash.isEmpty()) {
            // Compare with working copy (which is editable)
            // We copy the working copy to m_tempNew so kompare doesn't accidentally
            // save halfway directly into the project without us intercepting it
            if (QFile::exists(m_tempNew)) QFile::remove(m_tempNew);
            QFile::copy(filePath, m_tempNew);
            QFile(m_tempNew).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

            m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
        } else {
            GitService::instance().extractVersion(filePath, newHash, m_tempNew).then(this, [this](bool success) {
                if (success) {
                    m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
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

    if (!m_kompareInterface) return;

    // Two distinct project files — preserve their real basenames so the
    // Kompare header shows them verbatim (e.g. "Chapter_1.md" on one side,
    // "Chapter_1_Conflicted_Copy.md" on the other).
    const QString base1 = QFileInfo(file1).fileName();
    const QString base2 = QFileInfo(file2).fileName();
    allocateScratchPaths(base1, base2, i18n("original"), i18n("changed"));

    if (QFile::exists(m_tempOld)) QFile::remove(m_tempOld);
    if (QFile::exists(m_tempNew)) QFile::remove(m_tempNew);
    QFile::copy(file1, m_tempOld);
    QFile::copy(file2, m_tempNew);
    QFile(m_tempOld).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile(m_tempNew).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    m_kompareInterface->compareFiles(QUrl::fromLocalFile(m_tempOld), QUrl::fromLocalFile(m_tempNew));
}

void VisualDiffView::swapDiff()
{
    m_swapped = !m_swapped;
    
    if (!m_filePath.isEmpty()) {
        QString oldH = m_oldHash;
        m_oldHash = m_newHash;
        m_newHash = oldH;
        setDiff(m_filePath, m_oldHash, m_newHash);
    } else {
        QString f1 = m_file1;
        m_file1 = m_file2;
        m_file2 = f1;
        setFiles(m_file1, m_file2);
    }
}

void VisualDiffView::setConflict(const QString &repoPath,
                                  const QString &filePath,
                                  const QString &ancestorHash,
                                  const QString &oursHash,
                                  const QString &theirsHash)
{
    // filePath from the index is repo-relative; resolve to absolute for display/save-back.
    const QString absoluteFilePath = QDir(repoPath).absoluteFilePath(filePath);
    m_filePath = absoluteFilePath;
    m_oldHash.clear();
    m_newHash.clear();
    m_file1.clear();
    m_file2.clear();

    if (!m_kompareInterface) return;

    // The hashes here are BLOB OIDs from the index (stages 1/2/3), not commit OIDs.
    // Use extractBlob which resolves a blob by its OID directly.
    GitService::instance().extractBlob(repoPath, ancestorHash, m_tempAncestor)
        .then(this, [this, repoPath, oursHash, theirsHash](bool ok) {
            if (!ok) return;
            GitService::instance().extractBlob(repoPath, oursHash, m_tempOurs)
                .then(this, [this, repoPath, theirsHash](bool ok2) {
                    if (!ok2) return;
                    GitService::instance().extractBlob(repoPath, theirsHash, m_tempTheirs)
                        .then(this, [this](bool ok3) {
                            if (!ok3) return;
                            // Ancestor is read-only
                            QFile(m_tempAncestor).setPermissions(QFileDevice::ReadOwner);
                            // Ours is editable
                            QFile(m_tempOurs).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                            // Theirs is read-only
                            QFile(m_tempTheirs).setPermissions(QFileDevice::ReadOwner);
                            m_kompareInterface->compare3Files(
                                QUrl::fromLocalFile(m_tempAncestor),
                                QUrl::fromLocalFile(m_tempOurs),
                                QUrl::fromLocalFile(m_tempTheirs));
                        });
                });
        });
}

void VisualDiffView::saveChanges()
{
    if (!m_part) return;
    
    // Trigger Kompare's own save functionality first (saves applied hunks to m_tempNew)
    m_part->save();

    QString targetPath;
    if (!m_filePath.isEmpty()) {
        if (m_newHash.isEmpty()) {
            targetPath = m_filePath;
        } else {
            // Can't overwrite a commit in git, but maybe we let them save somewhere?
            // Actually, if m_newHash is not empty, it's history vs history, so read-only
            QMessageBox::information(this, i18n("Read Only"), i18n("Cannot save changes when comparing two historical commits."));
            return;
        }
    } else {
        targetPath = m_file2;
    }

    if (!targetPath.isEmpty()) {
        if (QFile::exists(targetPath)) QFile::remove(targetPath);
        QFile::copy(m_tempNew, targetPath);
        Q_EMIT saveRequested(targetPath);
    }
}
