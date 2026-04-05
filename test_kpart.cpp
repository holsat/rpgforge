#include <QApplication>
#include <QMainWindow>
#include <KPluginFactory>
#include <KParts/ReadWritePart>
#include <QUrl>
#include <QVBoxLayout>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QMainWindow win;
    
    auto result = KPluginFactory::instantiatePlugin<KParts::ReadWritePart>(
        KPluginMetaData(QStringLiteral("kf6/parts/komparepart")), &win);
        
    if (result) {
        auto part = result.plugin;
        win.setCentralWidget(part->widget());
        if (argc > 1) {
            part->openUrl(QUrl::fromLocalFile(QString::fromUtf8(argv[1])));
        }
    } else {
        qWarning("Failed to load komparepart");
    }
    
    win.show();
    return 0; // We just want to see if it compiles and links
}
