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

#include "unsavedchangesdialog.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QIcon>
#include <QDateTime>
#include <QSpacerItem>

UnsavedChangesDialog::UnsavedChangesDialog(const QString &currentBranch,
                                             const QString &targetBranch,
                                             QWidget *parent)
    : QDialog(parent)
{
    setupUi(currentBranch, targetBranch);
}

void UnsavedChangesDialog::setupUi(const QString &currentBranch,
                                     const QString &targetBranch)
{
    setWindowTitle(i18n("Switching Exploration"));
    setFixedWidth(440);

    auto *mainLayout = new QVBoxLayout(this);

    // Title
    auto *titleLabel = new QLabel(
        QStringLiteral("<b>%1</b>").arg(
            i18n("Switching to \"%1\"", targetBranch.toHtmlEscaped())),
        this);
    mainLayout->addWidget(titleLabel);

    // Subtitle
    auto *subtitleLabel = new QLabel(
        i18n("You have unsaved changes on \"%1\".\nWhat would you like to do?",
             currentBranch),
        this);
    auto subtitleFont = subtitleLabel->font();
    subtitleFont.setPointSize(subtitleFont.pointSize() - 1);
    subtitleLabel->setFont(subtitleFont);
    mainLayout->addWidget(subtitleLabel);

    mainLayout->addSpacing(12);

    // --- Card 1: Save & Switch ---
    auto *saveCard = new QFrame(this);
    saveCard->setFrameShape(QFrame::StyledPanel);
    saveCard->setFrameShadow(QFrame::Raised);
    saveCard->setCursor(Qt::PointingHandCursor);
    saveCard->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px solid palette(mid); border-radius: 6px; padding: 8px; }"));

    auto *saveLayout = new QHBoxLayout(saveCard);

    auto *saveIconLabel = new QLabel(saveCard);
    saveIconLabel->setPixmap(
        QIcon::fromTheme(QStringLiteral("document-save")).pixmap(32, 32));
    saveLayout->addWidget(saveIconLabel, 0, Qt::AlignTop);

    auto *saveTextLayout = new QVBoxLayout();
    auto *saveTitleLabel = new QLabel(
        QStringLiteral("<b>%1</b>").arg(
            i18n("Save to \"%1\"", currentBranch.toHtmlEscaped())),
        saveCard);
    saveTextLayout->addWidget(saveTitleLabel);

    auto *saveDescLabel = new QLabel(
        i18n("Creates a Milestone with your changes."), saveCard);
    auto descFont = saveDescLabel->font();
    descFont.setPointSize(descFont.pointSize() - 1);
    saveDescLabel->setFont(descFont);
    saveDescLabel->setStyleSheet(QStringLiteral("color: gray; border: none;"));
    saveTextLayout->addWidget(saveDescLabel);

    m_messageEdit = new QLineEdit(saveCard);
    m_messageEdit->setPlaceholderText(i18n("Milestone description (optional)"));
    saveTextLayout->addWidget(m_messageEdit);

    saveLayout->addLayout(saveTextLayout, 1);

    auto *saveBtn = new QPushButton(i18n("Save && Switch"), saveCard);
    saveLayout->addWidget(saveBtn, 0, Qt::AlignTop);

    mainLayout->addWidget(saveCard);

    connect(saveBtn, &QPushButton::clicked, this, [this, currentBranch] {
        QString msg = m_messageEdit->text().trimmed();
        if (msg.isEmpty())
            msg = i18n("Quick save before switching");
        Q_EMIT saveRequested(msg);
        accept();
    });

    mainLayout->addSpacing(8);

    // --- Card 2: Park & Switch ---
    auto *parkCard = new QFrame(this);
    parkCard->setFrameShape(QFrame::StyledPanel);
    parkCard->setFrameShadow(QFrame::Raised);
    parkCard->setCursor(Qt::PointingHandCursor);
    parkCard->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px solid palette(mid); border-radius: 6px; padding: 8px; }"));

    auto *parkLayout = new QHBoxLayout(parkCard);

    auto *parkIconLabel = new QLabel(parkCard);
    parkIconLabel->setPixmap(
        QIcon::fromTheme(QStringLiteral("document-save-all")).pixmap(32, 32));
    parkLayout->addWidget(parkIconLabel, 0, Qt::AlignTop);

    auto *parkTextLayout = new QVBoxLayout();
    auto *parkTitleLabel = new QLabel(
        QStringLiteral("<b>%1</b>").arg(i18n("Park Changes")), parkCard);
    parkTextLayout->addWidget(parkTitleLabel);

    auto *parkDescLabel = new QLabel(
        i18n("Keeps your edits in a holding area — you can pick them back up "
             "any time from the Explorations panel."),
        parkCard);
    parkDescLabel->setWordWrap(true);
    auto parkDescFont = parkDescLabel->font();
    parkDescFont.setPointSize(parkDescFont.pointSize() - 1);
    parkDescLabel->setFont(parkDescFont);
    parkDescLabel->setStyleSheet(QStringLiteral("color: gray; border: none;"));
    parkTextLayout->addWidget(parkDescLabel);

    parkLayout->addLayout(parkTextLayout, 1);

    auto *parkBtn = new QPushButton(i18n("Park && Switch"), parkCard);
    parkLayout->addWidget(parkBtn, 0, Qt::AlignTop);

    mainLayout->addWidget(parkCard);

    connect(parkBtn, &QPushButton::clicked, this, [this, currentBranch] {
        const QString msg = i18n("Parked on '%1' — %2",
            currentBranch,
            QDateTime::currentDateTime().toString(QStringLiteral("MMM d, hh:mm")));
        Q_EMIT parkRequested(msg);
        accept();
    });

    mainLayout->addSpacing(12);

    // --- Cancel button, right-aligned ---
    auto *cancelLayout = new QHBoxLayout();
    cancelLayout->addStretch();
    auto *cancelBtn = new QPushButton(i18n("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    cancelLayout->addWidget(cancelBtn);
    mainLayout->addLayout(cancelLayout);
}
