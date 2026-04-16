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

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QIcon>

#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include <QFile>
#include <QTextStream>
#include <QDateTime>

// Custom message handler to log to file and console
static QTextStream *logStream = nullptr;
static QFile *logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Also print to original console
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    
    if (!logStream) return;

    QString level;
    switch (type) {
        case QtDebugMsg:    level = QStringLiteral("DEBUG"); break;
        case QtInfoMsg:     level = QStringLiteral("INFO"); break;
        case QtWarningMsg:  level = QStringLiteral("WARN"); break;
        case QtCriticalMsg: level = QStringLiteral("CRITICAL"); break;
        case QtFatalMsg:    level = QStringLiteral("FATAL"); break;
    }
    
    QString formattedMessage = QStringLiteral("%1 [%2] %3 (%4:%5, %6)\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), 
             level, 
             msg,
             context.file ? context.file : "unknown", 
             QString::number(context.line), 
             context.function ? context.function : "unknown");

    *logStream << formattedMessage;
    logStream->flush();
}


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Setup logging
    QString logFileName = QStringLiteral("rpgforge_debug.log");
    logFile = new QFile(logFileName);
    if (logFile->exists()) {
        QString backupName = QStringLiteral("%1.%2.bak").arg(logFileName, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
        logFile->rename(backupName);
    }
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        logStream = new QTextStream(logFile);
        qInstallMessageHandler(messageHandler);
    } else {
        qWarning() << "Could not open log file for writing:" << logFileName;
    }

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

    return app.exec();
}
