#include "mainwindow.h"
#include "markdownparser.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("rpgforge"),
        QIcon(QStringLiteral(":/icons/rpgforge.svg"))));

    KAboutData aboutData(
        QStringLiteral("rpgforge"),
        i18n("RPG Forge"),
        QStringLiteral("0.1.0"),
        i18n("An IDE for RPG game designers"),
        KAboutLicense::GPL_V3,
        i18n("(c) 2026"));
    aboutData.setProgramLogo(QIcon(QStringLiteral(":/icons/rpgforge.svg")));

    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Initialize cmark-gfm extensions (must happen once before any parsing)
    MarkdownParser::init();

    auto *window = new MainWindow();
    window->show();

    return app.exec();
}
