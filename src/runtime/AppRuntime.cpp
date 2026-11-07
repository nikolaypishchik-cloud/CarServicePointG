#include "runtime/AppRuntime.h"

#include "app/App.h"

QString AppRuntime::dataDir() { return App::dataDir(); }
QString AppRuntime::logsDir() { return App::logsDir(); }
QString AppRuntime::logFilePath() { return App::logFilePath(); }
QString AppRuntime::dbFilePath() { return App::dbFilePath(); }
QString AppRuntime::activeDbFilePath() { return App::activeDbFilePath(); }
QString AppRuntime::legacyDbFilePath() { return App::legacyDbFilePath(); }
QString AppRuntime::archiveDbFilePath() { return App::archiveDbFilePath(); }
QString AppRuntime::registryDbFilePath() { return App::registryDbFilePath(); }
QString AppRuntime::securityDir() { return App::securityDir(); }
QString AppRuntime::sessionDir() { return App::sessionDir(); }
QString AppRuntime::backupsDir() { return App::backupsDir(); }
QString AppRuntime::backupTempDir() { return App::backupTempDir(); }
QString AppRuntime::recoveryDir() { return App::recoveryDir(); }
QString AppRuntime::recoveryTempDir() { return App::recoveryTempDir(); }
QString AppRuntime::sealedActiveDbFilePath() { return App::sealedActiveDbFilePath(); }
QString AppRuntime::sealedLegacyDbFilePath() { return App::sealedLegacyDbFilePath(); }
QString AppRuntime::sealedArchiveDbFilePath() { return App::sealedArchiveDbFilePath(); }
QString AppRuntime::sealedRegistryDbFilePath() { return App::sealedRegistryDbFilePath(); }
QString AppRuntime::masterKeyFilePath() { return App::masterKeyFilePath(); }
QString AppRuntime::pendingLegacyImportFilePath() { return App::pendingLegacyImportFilePath(); }
void AppRuntime::ensureDataAtRestProtection() { App::ensureDataAtRestProtection(); }
bool AppRuntime::protectPathAtRest(const QString& path) { return App::protectPathAtRest(path); }
