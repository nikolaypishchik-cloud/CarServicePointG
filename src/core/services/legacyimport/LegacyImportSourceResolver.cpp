#include "core/services/legacyimport/LegacyImportSourceResolver.h"

#include "core/io/SafeFileOps.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUuid>
#include <QThread>

namespace {
bool isUiThread()
{
    return QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}

QStringList legacyTempAllowedRoots(const QString& path)
{
    const QString clean = SafeFileOps::cleanPath(path);
    const QString tempRoot = SafeFileOps::cleanPath(QDir::tempPath());
    return clean.isEmpty() ? QStringList{tempRoot} : QStringList{tempRoot, QFileInfo(clean).absolutePath()};
}

QString findNamedFileRecursively(const QString& rootPath, const QStringList& preferredNames)
{
    if (rootPath.trimmed().isEmpty()) return QString();
    for (const QString& name : preferredNames) {
        QDirIterator it(rootPath, QStringList{name}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) return QFileInfo(it.next()).absoluteFilePath();
    }
    return QString();
}

bool extractZipArchive(const QString& archivePath, const QString& destinationPath, QString* errorText)
{
    Q_UNUSED(destinationPath);
    // TZ-100 runtime acceptance: legacy archive extraction cannot run synchronously.
    // The legacy importer must schedule extraction through an async worker before calling this resolver.
    if (errorText) {
        *errorText = QStringLiteral("Розпакування legacy архіву вимкнено у синхронному runtime-шляху; використайте асинхронний імпорт.");
    }
    qWarning() << "LegacyImportSourceResolver: synchronous archive extraction disabled" << archivePath
               << "uiThread=" << isUiThread();
    return false;
}

bool fillBundleFromRoot(const QString& sourcePath, const QString& sourceKind, const QString& rootPath, bool ownsRoot,
                        LegacyImportBundle* bundle, QString* errorText)
{
    if (!bundle) {
        if (errorText) *errorText = QStringLiteral("Не передано об’єкт bundle для legacy імпорту");
        return false;
    }

    const QString primaryDb = findNamedFileRecursively(rootPath, {
        QStringLiteral("service_center.db"),
        QStringLiteral("service_center.sqlite"),
        QStringLiteral("service_center.sqlite3"),
        QStringLiteral("_db_snapshot_tmp.db")
    });
    if (primaryDb.trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("У вибраному legacy джерелі не знайдено service_center.db або _db_snapshot_tmp.db");
        return false;
    }

    bundle->sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    bundle->sourceKind = sourceKind;
    bundle->workingRootPath = QFileInfo(rootPath).absoluteFilePath();
    bundle->primaryDbPath = QFileInfo(primaryDb).absoluteFilePath();
    bundle->snapshotDbPath = findNamedFileRecursively(rootPath, {QStringLiteral("_db_snapshot_tmp.db")});
    bundle->settingsJsonPath = findNamedFileRecursively(rootPath, {QStringLiteral("settings.json")});
    bundle->uiStateJsonPath = findNamedFileRecursively(rootPath, {QStringLiteral("ui_state.json")});
    bundle->appLogPath = findNamedFileRecursively(rootPath, {QStringLiteral("app.log")});
    bundle->ownsWorkingRoot = ownsRoot;
    return true;
}
}

bool LegacyImportSourceResolver::resolve(const QString& sourcePath, LegacyImportBundle* bundle, QString* errorText)
{
    if (!bundle) {
        if (errorText) *errorText = QStringLiteral("Не передано об’єкт bundle для legacy імпорту");
        return false;
    }
    *bundle = LegacyImportBundle();

    const QFileInfo fi(sourcePath);
    if (!fi.exists()) {
        if (errorText) *errorText = QStringLiteral("Вибране legacy джерело не знайдено");
        return false;
    }

    if (fi.isDir()) {
        return fillBundleFromRoot(sourcePath, QStringLiteral("directory_bundle"), fi.absoluteFilePath(), false, bundle, errorText);
    }

    const QString suffix = fi.suffix().trimmed().toLower();
    if (suffix == QStringLiteral("db") || suffix == QStringLiteral("sqlite") || suffix == QStringLiteral("sqlite3")) {
        bundle->sourcePath = fi.absoluteFilePath();
        bundle->sourceKind = QStringLiteral("sqlite_db");
        bundle->workingRootPath = fi.absolutePath();
        bundle->primaryDbPath = fi.absoluteFilePath();
        bundle->snapshotDbPath = findNamedFileRecursively(fi.absolutePath(), {QStringLiteral("_db_snapshot_tmp.db")});
        bundle->settingsJsonPath = findNamedFileRecursively(fi.absolutePath(), {QStringLiteral("settings.json")});
        bundle->uiStateJsonPath = findNamedFileRecursively(fi.absolutePath(), {QStringLiteral("ui_state.json")});
        bundle->appLogPath = findNamedFileRecursively(fi.absolutePath(), {QStringLiteral("app.log")});
        return true;
    }

    if (suffix == QStringLiteral("zip")) {
        const QString tempRoot = QDir(QDir::tempPath()).filePath(
            QStringLiteral("carservice_legacy_import_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(tempRoot);
        QString extractError;
        if (!extractZipArchive(fi.absoluteFilePath(), tempRoot, &extractError)) {
            SafeFileOps::safeRemoveRecursively(tempRoot, legacyTempAllowedRoots(tempRoot), {QStringLiteral("carservice_legacy_import_")});
            if (errorText) {
                *errorText = QStringLiteral("Не вдалося розпакувати legacy архів: %1").arg(extractError);
            }
            return false;
        }
        return fillBundleFromRoot(sourcePath, QStringLiteral("zip_bundle"), tempRoot, true, bundle, errorText);
    }

    if (errorText) *errorText = QStringLiteral("Непідтримуваний тип legacy джерела. Потрібен .db, .sqlite, .sqlite3 або .zip");
    return false;
}

void LegacyImportSourceResolver::cleanup(LegacyImportBundle* bundle)
{
    if (!bundle) return;
    if (bundle->ownsWorkingRoot && !bundle->workingRootPath.trimmed().isEmpty()) {
        SafeFileOps::safeRemoveRecursively(bundle->workingRootPath, legacyTempAllowedRoots(bundle->workingRootPath), {QStringLiteral("carservice_legacy_import_")});
    }
    *bundle = LegacyImportBundle();
}
