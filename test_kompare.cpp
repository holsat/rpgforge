#include <QObject>
#include <kompare/kompareinterface.h>

class Dummy : public QObject, public KompareInterface {
    Q_OBJECT
    Q_INTERFACES(KompareInterface)
public:
    bool openDiff(const QUrl&) override { return false; }
    bool openDiff(const QString&) override { return false; }
    bool openDiff3(const QUrl&) override { return false; }
    bool openDiff3(const QString&) override { return false; }
    void compare(const QUrl&, const QUrl&) override {}
    void compareFileString(const QUrl&, const QString&) override {}
    void compareStringFile(const QString&, const QUrl&) override {}
    void compareFiles(const QUrl&, const QUrl&) override {}
    void compareDirs(const QUrl&, const QUrl&) override {}
    void compare3Files(const QUrl&, const QUrl&, const QUrl&) override {}
    void openFileAndDiff(const QUrl&, const QUrl&) override {}
    void openDirAndDiff(const QUrl&, const QUrl&) override {}
    int readProperties(KConfig*) override { return 0; }
    int saveProperties(KConfig*) override { return 0; }
    bool queryClose() override { return true; }
};

int main() {
    Dummy d;
    return 0;
}
