#include "app/AcceptanceProfile.h"
#include "app/App.h"
#include "app/SingleInstanceCoordinator.h"
#include "core/services/startup/StartupDatabaseFacade.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdlib>

QString AcceptanceProfile::create(QString* error)
{
    auto fail = [&](const QString& reason) { *error = reason; return QString{}; };
    QDir executable(QCoreApplication::applicationDirPath());
    if (executable.dirName() == "dist") {
        if (!executable.cdUp() || !executable.cd("build-single-instance-audit"))
            return fail("Acceptance build directory is unavailable");
    } else {
        while (executable.dirName() != "build-single-instance-audit") {
            if (executable.isRoot() || !executable.cdUp()) return fail("Executable is outside the project audit directory");
        }
    }
    if (executable.dirName() != "build-single-instance-audit" || !executable.exists("CMakeCache.txt"))
        return fail("Isolated launch requires the project build or dist executable");
    const auto base = QDir(executable.canonicalPath()).filePath("isolated-runs");
    if (!QDir().mkpath(base) || QFileInfo(base).canonicalFilePath() != base)
        return fail("Cannot create a private run in the project audit directory");
    QTemporaryDir run(QDir(base).filePath("run-XXXXXX"));
    if (!run.isValid()) return fail(run.errorString());
    const QString root = QFileInfo(run.path()).canonicalFilePath();
    if (root.isEmpty()) return fail("Cannot resolve the acceptance run directory");
#ifdef Q_OS_WIN
    const bool rootSet = _wputenv_s(L"CS_TEST_DATA_ROOT",reinterpret_cast<const wchar_t*>(root.utf16())) == 0;
#else
    const bool rootSet = qputenv("CS_TEST_DATA_ROOT",root.toUtf8());
#endif
    if (!rootSet || !qputenv("CS_ELIT_DISABLE_STARTUP","1"))
        return fail("Cannot configure the isolated profile environment");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceAcceptance");
    QCoreApplication::setApplicationName("Safety-" + QFileInfo(root).fileName());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root);
    const auto profile = QDir(root).filePath(QCoreApplication::applicationName());
    const auto settings = QDir::cleanPath(QSettings().fileName());
    if (!settings.startsWith(root + '/') || App::dataDir() != profile)
        return fail("Profile data or settings resolve outside the isolated run");
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",9));
    run.setAutoRemove(false);
    return root;
}

bool AcceptanceProfile::configureServices(const QString& root, QString* error)
{
    QDir run(root);
    for (const auto& key : {"settings.booking.sync.enabled","settings.cloudsync.enabled","settings.updates.auto"}) {
        if (!StartupDatabaseFacade::setSetting(QString(key),QStringLiteral("false"))) {
            *error = "Cannot disable external service: " + QString(key);
            return false;
        }
    }
    QSettings settings;
    settings.setValue("EmailSync/enabled",false);
    settings.sync();
    if (settings.status()!=QSettings::NoError) { *error = "Cannot write isolated settings"; return false; }
    for (const auto& name : {"pdf_dir","invoices_dir","reports_dir","order_photos_dir"}) {
        const auto key = QStringLiteral("settings.int.directories.") + name;
        if (!run.mkpath(name) || !StartupDatabaseFacade::setSetting(key,run.filePath(name))) {
            *error = "Cannot configure isolated document directory: " + QString(name);
            return false;
        }
    }
    const QJsonObject evidence{
        {"pid",QCoreApplication::applicationPid()}, {"executable",QCoreApplication::applicationFilePath()},
        {"profile",App::dataDir()}, {"workspace",App::activeDbFilePath()}, {"registry",App::registryDbFilePath()},
        {"settings",settings.fileName()}, {"instance",CarService::SingleInstanceCoordinator::defaultServerName()}
    };
    QSaveFile proof(run.filePath("isolation.json"));
    const auto bytes = QJsonDocument(evidence).toJson();
    if (!proof.open(QIODevice::WriteOnly) || proof.write(bytes) != bytes.size() || !proof.commit()) {
        *error = proof.errorString();
        return false;
    }
    return true;
}
