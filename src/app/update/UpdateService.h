#pragma once

#include <QObject>
#include <QString>

class QWidget;

class UpdateService : public QObject
{
    Q_OBJECT
public:
    struct ReleaseInfo {
        bool ok = false;
        QString currentVersion;
        QString latestVersion;
        QString title;
        QString notes;
        QString assetName;
        QString downloadUrl;
        QString installerAssetName;
        QString installerDownloadUrl;
        QString portableAssetName;
        QString portableDownloadUrl;
        QString checksumAssetName;
        QString checksumDownloadUrl;
        QString expectedSha256;
        QString assetDigestSha256;
        qint64 expectedDownloadSize = -1;
        QString assetUpdatedAt;
        bool sameVersionHotfix = false;
        bool prerelease = false;
        bool draft = false;
        QString apiUrl;
        QString htmlUrl;
        QString error;
    };

    static QString currentVersion();
    static QString githubOwner();
    static QString githubRepo();
    static QString latestReleaseApiUrl();

    static bool promptAndRunUpdate(QWidget* parent, bool silentIfNoUpdates);
    static void checkPreviousUpdateResult(QWidget* parent);

private:
    static int compareVersions(const QString& a, const QString& b);
    static QString normalizeVersion(const QString& v);
    static bool prepareAndLaunchUpdater(QWidget* parent, const QString& downloadedFile, QString* errorOut);
};
