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

#include "mainwindow.h"
#include "markdownparser.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include <QWebEngineProfile>
#include <QWebEngineSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Disable and clear WebEngine cache for debugging and safety
    QWebEngineProfile::defaultProfile()->setHttpCacheType(QWebEngineProfile::NoCache);
    QWebEngineProfile::defaultProfile()->clearHttpCache();

    KLocalizedString::setApplicationDomain("rpgforge");

    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("rpgforge"),
        QIcon(QStringLiteral(":/icons/org.kde.rpgforge.svg"))));

    KAboutData aboutData(
        QStringLiteral("rpgforge"),
        i18n("RPG Forge"),
        QStringLiteral("0.1.0"),
        i18n("An IDE for RPG game designers"),
        KAboutLicense::GPL_V3,
        i18n("(c) 2026"));
    aboutData.setProgramLogo(QIcon(QStringLiteral(":/icons/org.kde.rpgforge.svg")));
    aboutData.setDesktopFileName(QStringLiteral("org.kde.rpgforge"));

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
