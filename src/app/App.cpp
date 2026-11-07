#include "app/App.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

#include <QStandardPaths>
#include <QtGlobal>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QFile* g_logFile = nullptr;
static QMutex g_logMutex;
static int g_logLinesSinceFlush = 0;
static const qint64 kMaxLogBytes = 5 * 1024 * 1024; // 5MB
static const int kMaxRotatedLogs = 5;

#ifdef Q_OS_WIN
static std::atomic_bool g_windowsEfsUnsupported{false};
static std::atomic_bool g_windowsEfsUnsupportedLogged{false};

static bool isPathEncryptedWindows(const QString& path)
{
    if (path.trimmed().isEmpty()) return false;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attrs = GetFileAttributesW(native.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_ENCRYPTED) != 0;
}

static bool protectPathWithWindowsEfs(const QString& path)
{
    if (g_windowsEfsUnsupported.load(std::memory_order_relaxed)) {
        return false;
    }

    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.trimmed().isEmpty()) return false;
    const QFileInfo fi(cleaned);
    if (!fi.exists()) return false;
    if (isPathEncryptedWindows(cleaned)) return true;

    const std::wstring native = QDir::toNativeSeparators(cleaned).toStdWString();
    if (EncryptFileW(native.c_str())) return true;

    const DWORD err = GetLastError();
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return false;
    case ERROR_ACCESS_DENIED:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_FUNCTION:
        g_windowsEfsUnsupported.store(true, std::memory_order_relaxed);
        if (!g_windowsEfsUnsupportedLogged.exchange(true, std::memory_order_relaxed)) {
            qWarning() << "EFS protection unavailable; disabling further EFS attempts for this session at" << cleaned << "error" << err;
        }
        return false;
    default:
        qWarning() << "Failed to protect path with EFS:" << cleaned << "error" << err;
        return false;
    }
}
#else
static bool protectPathWithWindowsEfs(const QString&) { return false; }
#endif

static void rotateLogsIfNeeded()
{
    if (!g_logFile) return;
    QFileInfo fi(*g_logFile);
    if (!fi.exists()) return;
    if (fi.size() < kMaxLogBytes) return;

    g_logFile->flush();
    g_logFile->close();

    const QString base = App::logFilePath();
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString rotated = base + "." + ts + ".log";
    QFile::rename(base, rotated);

    // Cleanup old rotations
    QDir dir(QFileInfo(base).absolutePath());
    const QString pattern = QFileInfo(base).fileName() + ".*.log";
    QStringList files = dir.entryList(QStringList() << pattern, QDir::Files, QDir::Time); // newest first
    for (int i = kMaxRotatedLogs; i < files.size(); ++i) {
        dir.remove(files.at(i));
    }

    g_logFile->setFileName(base);
    const bool opened = g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (!opened) {
        // If we can't open the log file, leave it closed; messageHandler will fallback to stderr.
        return;
    }
    g_logLinesSinceFlush = 0;
}

static bool runtimeDeepDebugEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("CS_DEEP_DEBUG") == 1;
    return enabled;
}


static bool isCarServiceMessage(const QMessageLogContext& ctx, const QString& msg)
{
    const QByteArray category = QByteArray(ctx.category ? ctx.category : "");
    return category.startsWith("carservice.")
        || msg.startsWith(QStringLiteral("STARTUP "))
        || msg.startsWith(QStringLiteral("PAGE_OPEN "))
        || msg.startsWith(QStringLiteral("PAGE_PREWARM "))
        || msg.startsWith(QStringLiteral("PERF "))
        || msg.startsWith(QStringLiteral("UPDATE "))
        || msg.startsWith(QStringLiteral("Startup core completed"))
        || msg.startsWith(QStringLiteral("Showing MainWindow"))
        || msg.startsWith(QStringLiteral("App aboutToQuit"));
}

static bool isKnownQtFloodMessage(const QString& msg)
{
    return msg.startsWith(QStringLiteral("Checking character "))
        || msg.startsWith(QStringLiteral("Setting visibility of "))
        || msg.startsWith(QStringLiteral("Marking "))
        || msg.startsWith(QStringLiteral("Drawing "))
        || msg.startsWith(QStringLiteral("Initializing focus chain"))
        || msg.startsWith(QStringLiteral("delete - id"))
        || msg.startsWith(QStringLiteral("Object name:"))
        || msg.startsWith(QStringLiteral("QWindowsFontEngineDirectWrite::~QWindowsFontEngineDirectWrite"))
        || msg.contains(QStringLiteral("QWidgetRepaintManager"))
        || msg.contains(QStringLiteral("QStackedWidget(0x"))
        || msg.contains(QStringLiteral("settings_right { background:"))
        || msg.contains(QStringLiteral("Emoji"))
        || msg.contains(QStringLiteral("grapheme"));
}

static bool shouldSuppressRuntimeFlood(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (runtimeDeepDebugEnabled()) return false;
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) return false;

    if (isKnownQtFloodMessage(msg)) return true;

    const bool carServiceMessage = isCarServiceMessage(ctx, msg);
    if (type == QtDebugMsg) {
        return !carServiceMessage;
    }
    if (type == QtInfoMsg) {
        return !carServiceMessage;
    }
    return false;
}

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);

    if (shouldSuppressRuntimeFlood(type, ctx, msg)) {
        return;
    }

    QString level;
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }

    const QString line = QString("%1 [%2] %3\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
            .arg(level)
            .arg(msg);

    {
        QMutexLocker locker(&g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            rotateLogsIfNeeded();
            g_logFile->write(line.toUtf8());

            // Flush policy:
            // - always flush WARN/ERROR/FATAL
            // - otherwise flush every N lines to reduce IO pressure
            ++g_logLinesSinceFlush;
            const bool important = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
            if (important || g_logLinesSinceFlush >= 50) {
                g_logFile->flush();
                g_logLinesSinceFlush = 0;
            }
        }
    }

    const bool startupLine = msg.startsWith(QStringLiteral("STARTUP "))
        || msg.startsWith(QStringLiteral("Startup core completed"))
        || msg.startsWith(QStringLiteral("Startup failed"));
    const bool warnOrWorse = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
    const bool criticalOrFatal = (type == QtCriticalMsg || type == QtFatalMsg);

    // Keep startup diagnostics visible in the console even after the file logger
    // is installed, but avoid routing ordinary warnings to stderr because
    // Windows PowerShell 5.x can surface native stderr as NativeCommandError and
    // abort the run script before the real exit code is checked.
    const bool loggerActive = (g_logFile && g_logFile->isOpen());
    if ((startupLine && !loggerActive) || criticalOrFatal) {
        FILE* mirror = criticalOrFatal ? stderr : stdout;
        std::fwrite(line.toUtf8().constData(), 1, static_cast<size_t>(line.toUtf8().size()), mirror);
        std::fflush(mirror);
    } else if (warnOrWorse && !loggerActive) {
        std::fwrite(line.toUtf8().constData(), 1, static_cast<size_t>(line.toUtf8().size()), stdout);
        std::fflush(stdout);
    } else {
        // Optional full console mirroring for local debugging.
        const QByteArray env = qgetenv("CS_LOG_STDERR");
        if (!env.isEmpty() && env != "0") {
            fprintf(stderr, "%s", line.toUtf8().constData());
        }
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

QString App::appName() { return QStringLiteral("CarServicePointG"); }
QString App::orgName() { return QStringLiteral("CarServicePointG"); }

QString App::dataDir()
{
    // Instrumented Qt tests only: never redirect a normal desktop session.
    const QString testRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    if(QStandardPaths::isTestModeEnabled() && !testRoot.isEmpty()) {
        const QFileInfo root(testRoot);
        if(!root.isAbsolute() || !root.isDir())qFatal("Invalid isolated test data root");
        const QString name=QCoreApplication::applicationName();
        if(!name.startsWith(QStringLiteral("Safety-")) || name.contains('/') || name.contains('\\'))
            qFatal("Isolated data root requires a unique Safety test identity");
        const QString path=QDir(root.canonicalFilePath()).filePath(name);
        if(!QDir().mkpath(path))qFatal("Cannot create isolated test profile");
        return path;
    }
    // Prefer Roaming AppData on Windows. Qt maps AppDataLocation accordingly.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QLatin1String("/.carservicepointg");
    }

    // Make path stable and without spaces at tail
    QDir d(base);
    d.mkpath(".");
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::logsDir()
{
    QDir d(dataDir());
    d.mkpath("logs");
    d.cd("logs");
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::logFilePath()
{
    return QDir(logsDir()).filePath("app.log");
}

QString App::dbFilePath()
{
    return activeDbFilePath();
}

QString App::sessionDir()
{
    // Runtime DBs must survive abnormal termination so the next startup can
    // recover the most recent committed state instead of silently rolling back
    // to an older sealed snapshot. Keep runtime artifacts in app data rather
    // than the OS temp directory, and protect the directory at rest when the
    // platform supports it.
    QDir d(dataDir());
    d.mkpath(QStringLiteral("runtime"));
    d.cd(QStringLiteral("runtime"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::backupsDir()
{
    QDir d(dataDir());
    d.mkpath(QStringLiteral("backups"));
    d.cd(QStringLiteral("backups"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::backupTempDir()
{
    QDir d(backupsDir());
    d.mkpath(QStringLiteral("temp"));
    d.cd(QStringLiteral("temp"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::recoveryDir()
{
    QDir d(dataDir());
    d.mkpath(QStringLiteral("recovery"));
    d.cd(QStringLiteral("recovery"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::recoveryTempDir()
{
    QDir d(recoveryDir());
    d.mkpath(QStringLiteral("temp"));
    d.cd(QStringLiteral("temp"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::securityDir()
{
    QDir d(dataDir());
    d.mkpath(QStringLiteral("secure"));
    d.cd(QStringLiteral("secure"));
    const QString dirPath = d.absolutePath();
    App::protectPathAtRest(dirPath);
    return dirPath;
}

QString App::activeDbFilePath()
{
    QDir d(sessionDir());
    return d.filePath("CarService_workspace.db");
}

QString App::legacyDbFilePath()
{
    QDir d(sessionDir());
    return d.filePath("CarServicePointG.db");
}

QString App::archiveDbFilePath()
{
    QDir d(sessionDir());
    return d.filePath("CarService_archive_legacy.db");
}

QString App::registryDbFilePath()
{
    QDir d(sessionDir());
    return d.filePath("CarService_registry.db");
}

QString App::sealedActiveDbFilePath()
{
    QDir d(securityDir());
    return d.filePath("CarService_workspace.db.enc");
}

QString App::sealedLegacyDbFilePath()
{
    QDir d(securityDir());
    return d.filePath("CarServicePointG.db.enc");
}

QString App::sealedArchiveDbFilePath()
{
    QDir d(securityDir());
    return d.filePath("CarService_archive_legacy.db.enc");
}

QString App::sealedRegistryDbFilePath()
{
    QDir d(securityDir());
    return d.filePath("CarService_registry.db.enc");
}

QString App::masterKeyFilePath()
{
    QDir d(securityDir());
    return d.filePath("master.key.dpapi");
}

QString App::pendingLegacyImportFilePath()
{
    QDir d(securityDir());
    return d.filePath("pending_legacy_import.db");
}

void App::installLogHandler()
{
    // Resolve paths before the custom handler is installed so that any
    // best-effort filesystem protection warnings cannot recurse back into the
    // new logger while it is still being initialized.
    const QString resolvedLogPath = logFilePath();
    const QString resolvedDataPath = dataDir();

    {
        QMutexLocker locker(&g_logMutex);
        if (!g_logFile) {
            g_logFile = new QFile(resolvedLogPath);
        } else {
            g_logFile->setFileName(resolvedLogPath);
        }
        if (!g_logFile->isOpen()) {
            const bool opened = g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
            if (!opened) {
                // Keep the handler installable even without a writable file so
                // startup diagnostics still reach the console.
                g_logFile->close();
            }
        }
        if (g_logFile->isOpen()) {
            rotateLogsIfNeeded();
        }
    }

    qInstallMessageHandler(messageHandler);
    qInfo() << "Logging started:" << resolvedLogPath;
    qInfo() << "Data dir:" << resolvedDataPath;
}



void App::shutdownLogHandler()
{
    qInfo() << "Logging shutdown requested";

    qInstallMessageHandler(nullptr);

    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        if (g_logFile->isOpen()) {
            g_logFile->flush();
            g_logFile->close();
        }
        delete g_logFile;
        g_logFile = nullptr;
    }
    g_logLinesSinceFlush = 0;
}


void App::ensureDataAtRestProtection()
{
    protectPathAtRest(dataDir());
    protectPathAtRest(logsDir());
    protectPathAtRest(securityDir());
    protectPathAtRest(sessionDir());
    protectPathAtRest(backupsDir());
    protectPathAtRest(backupTempDir());
    protectPathAtRest(recoveryDir());
    protectPathAtRest(recoveryTempDir());
    protectPathAtRest(sealedActiveDbFilePath());
    protectPathAtRest(sealedLegacyDbFilePath());
    protectPathAtRest(sealedArchiveDbFilePath());
    protectPathAtRest(sealedRegistryDbFilePath());
    protectPathAtRest(masterKeyFilePath());
}

bool App::protectPathAtRest(const QString& path)
{
    return protectPathWithWindowsEfs(path);
}
