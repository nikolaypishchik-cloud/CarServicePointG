#pragma once

#include "app/update/UpdateService.h"

#include <QString>

class QByteArray;
class QWidget;

namespace UpdateServiceSupport {

QString escapeWinPath(QString s);
QString trimReleaseTag(QString s);
QString normalizeVersion(const QString& version);
int compareVersions(const QString& left, const QString& right);
bool isWritableDirectory(const QString& dirPath);
bool prefersInstallerPackage();
bool looksLikeInstallerAsset(const QString& lowName);
bool looksLikeChecksumAsset(const QString& lowName);
QString chooseChecksumAssetName(const QString& packageAssetName, const QStringList& checksumNames);
QString extractExpectedSha256(const QString& checksumText, const QString& assetName);
QString extractExpectedSha256(const QString& checksumText, const QString& assetName, const QString& checksumAssetName);
QString computeFileSha256(const QString& filePath, QString* errorOut);
UpdateService::ReleaseInfo parseReleaseInfo(const QByteArray& bytes, QString* errorOut);
bool prepareAndLaunchUpdaterImpl(QWidget* parent, const QString& downloadedFile, const QString& expectedVersion, QString* errorOut);
bool prepareAndLaunchUpdaterImpl(QWidget* parent, const QString& downloadedFile, const QString& expectedVersion, const QString& runId, QString* errorOut);
void checkPreviousUpdateResult(QWidget* parent);
bool runUpdateFlow(QWidget* parent, bool silentIfNoUpdates);

} // namespace UpdateServiceSupport
