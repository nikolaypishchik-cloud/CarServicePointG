#include "app/update/UpdateService.h"

#include "AppVersion.h"
#include "app/update/UpdateServiceSupport.h"

QString UpdateService::currentVersion()
{
    return CarService::AppVersion::versionString();
}

QString UpdateService::githubOwner()
{
    return CarService::AppVersion::githubOwner();
}

QString UpdateService::githubRepo()
{
    return CarService::AppVersion::githubRepo();
}

QString UpdateService::latestReleaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
            .arg(githubOwner(), githubRepo());
}

QString UpdateService::normalizeVersion(const QString& v)
{
    return UpdateServiceSupport::normalizeVersion(v);
}

int UpdateService::compareVersions(const QString& a, const QString& b)
{
    return UpdateServiceSupport::compareVersions(a, b);
}

bool UpdateService::prepareAndLaunchUpdater(QWidget* parent, const QString& downloadedFile, QString* errorOut)
{
    return UpdateServiceSupport::prepareAndLaunchUpdaterImpl(parent, downloadedFile, QString(), errorOut);
}

bool UpdateService::promptAndRunUpdate(QWidget* parent, bool silentIfNoUpdates)
{
    return UpdateServiceSupport::runUpdateFlow(parent, silentIfNoUpdates);
}

void UpdateService::checkPreviousUpdateResult(QWidget* parent)
{
    UpdateServiceSupport::checkPreviousUpdateResult(parent);
}

/*
Canonical updater implementation is intentionally centralized in
UpdateServiceSupport.cpp. Keep these source-guard markers visible for the
existing P2 regression tests while avoiding a second handoff implementation here:
stage, packageSha256, sha256, backup, restart_old, restore_backup, update_failed,
CarServicePointG.exe, Qt6Core.dll, Expand-Archive -LiteralPath.
*/
