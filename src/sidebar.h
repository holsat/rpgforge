#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>
#include <QVector>

class KMultiTabBar;
class QStackedWidget;

// Kate-style left sidebar: a narrow vertical button bar that toggles
// associated panels. Only one panel is visible at a time.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);
    ~Sidebar() override;

    // Add a panel with an icon and label. Returns the tab ID.
    int addPanel(const QIcon &icon, const QString &text, QWidget *panel);

    // Show a specific panel by ID, or hide if already shown
    void togglePanel(int id);

    // Get a panel widget by ID
    QWidget *panel(int id) const;

    // Get the currently visible panel ID (-1 if none)
    int currentPanel() const { return m_currentId; }

    // Show a panel by ID (without toggling)
    void showPanel(int id);

Q_SIGNALS:
    void panelVisibilityChanged(int id, bool visible);

private:
    KMultiTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    int m_currentId = -1;
    int m_nextId = 0;
};

#endif // SIDEBAR_H
