#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>

class QLabel;

// Placeholder panel for Git/Versioning functionality (Phase 6).
// Shows basic status for now.
class GitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GitPanel(QWidget *parent = nullptr);
    ~GitPanel() override;

    void setRootPath(const QString &path);

private:
    QLabel *m_statusLabel = nullptr;
    QString m_rootPath;
};

#endif // GITPANEL_H
