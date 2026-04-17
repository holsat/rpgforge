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
#include "mcpservice.h"

#ifdef RPGFORGE_DBUS_TESTING
#include "rpgforgedbus.h"
#include <QDBusConnection>
#include <QDBusError>
#endif

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QIcon>

#include <QWebEngineProfile>
#include <QWebEngineSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Disable and clear WebEngine cache for debugging and safety
    QWebEngineProfile::defaultProfile()->setHttpCacheType(QWebEngineProfile::NoCache);
    QWebEngineProfile::defaultProfile()->clearHttpCache();

    QCoreApplication::setApplicationName(QStringLiteral("rpgforge"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kde.org"));

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
    
    QCommandLineOption mcpOption(QStringList() << QStringLiteral("mcp"), i18n("Start in Model Context Protocol (MCP) server mode."));
    parser.addOption(mcpOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Initialize cmark-gfm extensions (must happen once before any parsing)
    MarkdownParser::init();

    if (parser.isSet(mcpOption)) {
        // MCP Mode: Headless
        MCPService mcp;
        mcp.start();
        return app.exec();
    }

    auto *window = new MainWindow();
    window->show();

#ifdef RPGFORGE_DBUS_TESTING
    // Register the DBus adaptor so automated test tools can drive the app
    // without AT-SPI/ydotool. The adaptor is parented to the MainWindow so
    // Qt handles teardown automatically. Only compiled into debug builds —
    // see the RPGFORGE_DBUS_TESTING CMake option.
    new RpgForgeDBus(window);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "DBus session bus not available; skipping DBus registration";
    } else {
        if (!bus.registerService(QStringLiteral("org.kde.rpgforge"))) {
            qWarning() << "Could not register DBus service:" << bus.lastError().message();
        }
        if (!bus.registerObject(QStringLiteral("/org/kde/rpgforge/MainWindow"), window)) {
            qWarning() << "Could not register DBus object:" << bus.lastError().message();
        }
    }
#endif

    return app.exec();
}
