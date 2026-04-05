#include <QApplication>
#include <QMainWindow>
#include <KPluginFactory>
#include <KParts/ReadWritePart>
#include <QUrl>
#include "src/kompareinterface.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    auto result = KPluginFactory::instantiatePlugin<KParts::ReadWritePart>(
        KPluginMetaData(QStringLiteral("kf6/parts/komparepart")), nullptr);
    if (result) {
        auto part = result.plugin;
        auto k = qobject_cast<KompareInterface*>(part);
        if (k) k->compareFiles(QUrl(), QUrl());
    }
    return 0;
}
