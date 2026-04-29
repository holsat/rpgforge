/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

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
#include <QThreadPool>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QIcon>

#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

// Self-pipe for trapping SIGTERM/SIGINT. The signal handler is restricted to
// async-signal-safe functions (write(2)); the actual quit work happens on
// the main thread through a QSocketNotifier wired to the read end of the
// pipe. Without this, kill on the process bypasses both the QThreadPool
// drain and LibrarianDatabase::close()'s WAL checkpoint, which has been
// observed to corrupt .rpgforge.db. See:
// https://doc.qt.io/qt-6/unix-signals.html
static int s_sigFd[2] = { -1, -1 };

static void unixSignalHandler(int)
{
    char c = 1;
    ::write(s_sigFd[0], &c, sizeof(c));
}

// Custom message handler to log to file and console
static QTextStream *logStream = nullptr;
static QFile *logFile = nullptr;
// QTextStream::operator<< and fprintf on shared FILE* are NOT thread-
// safe. The handler is called from whichever thread emitted the
// qDebug/qWarning, so background workers (LibrarianService,
// KnowledgeBase embedding tasks, QtConcurrent runs) race the main
// thread. This mutex serialises writes; a single lock covers both the
// stderr mirror and the file stream.
static QMutex logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
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

    QMutexLocker lock(&logMutex);

    // Console mirror (inside the lock so stdout/stderr stay interleaved
    // in a sane order across threads).
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());

    if (!logStream) return;
    *logStream << formattedMessage;
    // Flushing on every debug/info call serializes high-frequency logging
    // (preview renders, LLM dispatches, status refreshes) behind a syscall
    // and drops perceptible stutter into text editing. Flush only for
    // Warning/Critical/Fatal so crash-adjacent lines still hit disk.
    if (type >= QtWarningMsg) {
        logStream->flush();
    }
}


int main(int argc, char *argv[])
{
    // Route QtQuick's scene graph through the software rasterizer. The Mesa/
    // gallium GL path segfaults in QSGBatchRenderer when QWebEngineView renders
    // its first frame on this family of GPUs; software rendering sidesteps
    // libgallium entirely. Must be set before QApplication. Chromium's own GPU
    // process is left alone so image decoding keeps working normally.
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    QApplication app(argc, argv);

    // Trap SIGTERM/SIGINT and route them through QApplication::closeAllWindows()
    // so the canonical shutdown ceremony in MainWindow::closeEvent runs (doc
    // save → service pause → thread-pool drain → librarian WAL checkpoint).
    // With quitOnLastWindowClosed=true (Qt default) the last window's close
    // triggers app.exec() to return naturally. The handler itself only does
    // async-signal-safe work; the QSocketNotifier on the read end dispatches
    // closeAllWindows() on the main thread. See bugfix-registry entry
    // "2026-04-28 — Unified graceful-shutdown path (UI + signal)".
    // SOCK_CLOEXEC keeps the signal-pipe fds out of any subprocess we spawn
    // (Pandoc, Git, etc.). Without it, a child can inherit the pipe ends
    // and silently keep them open after the parent's notifier has closed
    // its view of them.
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s_sigFd) != 0) {
        qWarning("Could not create signal socketpair; SIGTERM will not be handled gracefully");
    } else {
        auto *sn = new QSocketNotifier(s_sigFd[1], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, [sn]() {
            sn->setEnabled(false);
            char tmp;
            ::read(s_sigFd[1], &tmp, sizeof(tmp));
            QApplication::closeAllWindows();
        });
        struct sigaction sa{};
        sa.sa_handler = unixSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGTERM, &sa, nullptr);
        ::sigaction(SIGINT,  &sa, nullptr);
    }

    // Setup logging. Historically this wrote to the current working
    // directory, which fails silently when the app is launched from a
    // desktop launcher (cwd=/) or any non-writable directory. Now we
    // resolve an absolute path in user data, create the directory, and
    // print the final path to stderr so it's always discoverable.
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    const QString logFileName = QDir(logDir).absoluteFilePath(QStringLiteral("rpgforge_debug.log"));

    logFile = new QFile(logFileName);
    if (logFile->exists()) {
        const QString backupName = QStringLiteral("%1.%2.bak").arg(
            logFileName,
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
        logFile->rename(backupName);
        logFile->setFileName(logFileName);
    }
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        logStream = new QTextStream(logFile);
        qInstallMessageHandler(messageHandler);
        // First line after installing the handler so it's captured in-file.
        qInfo().noquote() << "RPG Forge log initialised at" << logFileName;
        // And a stderr copy that's visible even if the user never opens
        // the file: one line, on every startup, plus a startup timestamp.
        fprintf(stderr, "RPG Forge: logging to %s\n", logFileName.toLocal8Bit().constData());
    } else {
        fprintf(stderr, "RPG Forge: COULD NOT OPEN LOG FILE %s — error: %s\n",
                logFileName.toLocal8Bit().constData(),
                logFile->errorString().toLocal8Bit().constData());
        delete logFile;
        logFile = nullptr;
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
    // Self-delete on close so MainWindow's destructor (and therefore
    // ~LibrarianService, ~KnowledgeBase, etc.) actually runs after
    // closeEvent accepts. Qt schedules the delete via a deferred-delete
    // event during the same exec loop, so destruction completes before
    // app.exec() returns.
    window->setAttribute(Qt::WA_DeleteOnClose);
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

    const int rc = app.exec();

    // Shutdown coordination: drain the thread pool so any in-flight DB
    // operations (LibrarianService::processQueue, KnowledgeBase worker
    // tasks, ProjectManager::triggerWordCountUpdate, etc.) complete
    // while qApp is still alive. Without this, worker threads race the
    // QCoreApplication destruction and emit "QSqlDatabase requires a
    // QCoreApplication" warnings (or crash) when they next touch the DB.
    // waitForDone returns once the pool is idle; the optional timeout
    // caps wait in case a task is genuinely stuck.
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(5000);

    return rc;
}
