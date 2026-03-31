#include "gitpanel.h"

#include <QLabel>
#include <QVBoxLayout>

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_statusLabel = new QLabel(QStringLiteral("Git integration coming in Phase 6."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_statusLabel);
    layout->addStretch();
}

GitPanel::~GitPanel() = default;

void GitPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    // TODO: show git status summary here in Phase 6
}
