#include <QScreen>
#include "ui/tabs/history/HistoryPage.h"
#include <QLineEdit>
#include "ui/common/dialogs/HistoricalVersionsDialog.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/legacyimport/LegacyImportPolicySupport.h"
#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include <QtTest/QtTest>
#include <QDataStream>
#include <QSaveFile>
#include "core/io/SecureSnapshotRestore.h"
#include <future>
#include <QSemaphore>
#include <QTimeZone>
#include "core/crm/OnlineBookingService.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "core/crm/CrmClientService.h"
#include "core/cloudsync/CloudSyncSchemaService.h"
#include "core/services/OrderNumberService.h"
#include <chrono>
#include <QProcess>
#include <QProcessEnvironment>
#include <QApplication>
#include <cstdlib>
#include "core/services/storage/MigrationRestoreJournal.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include <QCryptographicHash>
#include <thread>
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/security/EncryptedStorageLocalSupport.h"
#include "infrastructure/security/EncryptedStorage.h"
#include <QJsonDocument>
#include <QJsonArray>
#include "core/services/legacyimport/LegacyImportJournalSupport.h"
#include "core/services/legacyimport/LegacyImportTypes.h"
#include "core/services/legacyimport/LegacyImportSafetySupport.h"
#include "core/services/LegacyMigrationService.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "runtime/AppRuntime.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableView>
#include <QTemporaryDir>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QUrl>
#include <QStandardPaths>
#include <QSettings>
#include <QPdfWriter>
#include <QPainter>

#include "core/services/ReportsDataLoader.h"
#include "core/services/orders/OrderDialogPrintQueryService.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/repository/PartRepository.h"
#include "core/repository/ServiceRepository.h"
#include "core/services/orders/PartMutationService.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include "ui/tabs/reports/ReportsInteractionSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbBootstrapEnsureSchema.h"
#include "core/services/orders/OrderFinancialService.h"
#include <QDateEdit>
#include "core/services/ActivityAuditService.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include <QScopeGuard>
#include <cstdio>
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/OrderDialogSubmitSupport.h"
#include "ui/main/MainWindow.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include <QPushButton>
#include <QTableWidget>
#include <QMessageBox>
#include <QTimer>
#include "app/App.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "runtime/RuntimeJobDispatcher.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <QTableView>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QSpinBox>
#include <QNetworkProxy>
#include "core/services/OrderWorkspaceService.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/orders/OrderSnapshotService.h"
#include "infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteExecutionSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteFinalizeSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbArchiveHydrationSupport.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include <QComboBox>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

namespace {
QString isolatedSafetyName(const QString& prefix, const QString& root)
{
    // Keep distinct profile identities without recursively embedding a previous
    // application's temporary-directory name in Windows INI lock-file paths.
    return prefix + QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

bool setIsolatedTestRoot(const QString& value)
{
#ifdef Q_OS_WIN
    // qputenv converts narrow bytes through the Windows code page, corrupting
    // UTF-8 paths in a Cyrillic workspace. Update both wide CRT/OS environment.
    return _wputenv_s(L"CS_TEST_DATA_ROOT", reinterpret_cast<const wchar_t*>(value.utf16())) == 0;
#else
    return qputenv("CS_TEST_DATA_ROOT", value.toUtf8());
#endif
}
}

class ReportsOrdersTests final : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        // QtTest also calls cleanup after an assertion returns early. Never let
        // a deliberately minimal SQL fixture become the next slot's runtime.
        Database::close();
    }
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",9));
        // Offscreen Qt does not discover Windows fonts automatically.
        for(const auto& name:QStringList{"times.ttf","timesbd.ttf","timesi.ttf","timesbi.ttf","arial.ttf","arialbd.ttf"}) {
            const auto path=qEnvironmentVariable("WINDIR")+"/Fonts/"+name;
            if(QFile::exists(path))QVERIFY(QFontDatabase::addApplicationFont(path)>=0);
        }
    }
    void unicodeTestRootEnvironmentRoundTrip();
    void periodSnapshotReadFailures();
    void outsourceSnapshotReadFailures();
    void callerOwnedConnectionScope();
    void reportsFilterUsesExplicitSource();
    void orderListAndPrintData();
    void invoiceLinkOpensAttachedFile();
    void partsPdfLayout();
    void annualOrderUiPdfVisibleIdentity();
    void newOrderFromSpareStock();
    void explicitDiscountPersistenceAndArchive();
    void annualAllocationProcessCrash_data();
    void annualAllocationProcessCrash();
    void annualAllocationRollbackAndSources();
    void deletedHistoricalOrderKeepsIdentity();
    void annualAllocationConcurrency_data();
    void annualAllocationConcurrency();
    void crmBackfillPreservesRecordedOwnerIdentity();
    void annualBookingConversionIsIdempotent();
    void annualBookingRoutingAtomic_data();
    void annualBookingRoutingAtomic();
    void annualBookingCallerOwnedContext_data();
    void annualBookingCallerOwnedContext();
    void annualWorkspaceCreationIsPersistent();
    void profileRegistryLifecycle_data();
    void profileRegistryLifecycle();
    void registryRebuildProcess_data();
    void registryRebuildProcess();
    void registryMirrorTransactionOwnership_data();
    void registryMirrorTransactionOwnership();
    void registryProjectionTransactionOwnership();
    void registryRebuildAtomicity_data();
    void registryRebuildAtomicity();
    void archiveFileImport_data();
    void archiveFileImport();
    void detailIdentityRefusal_data();
    void detailIdentityRefusal();
    void detailIdentityClosedRead();
    void archiveUidPublication_data();
    void archiveUidPublication();
    void archiveShardIdentityPreparation_data();
    void archiveShardIdentityPreparation();
    void partRegistryUidOwnership_data();
    void partRegistryUidOwnership();
    void startupRegistryBulkProcess_data();
    void startupRegistryBulkProcess();
    void startupRegistryPublicationFailure_data();
    void startupRegistryPublicationFailure();
    void startupRegistryIdentityPreflight_data();
    void startupRegistryIdentityPreflight();
    void childDateOwnerMismatch_data();
    void childDateOwnerMismatch();
    void childMutationRegistryIntent_data();
    void childMutationRegistryIntent();
    void workspaceRegistryIntentDurability();
    void workspaceRegistryIntentProcess_data();
    void workspaceRegistryIntentProcess();
    void externalRegistryRefreshAtomic_data();
    void externalRegistryRefreshAtomic();
    void workspaceRoutingIdentityValidation_data();
    void workspaceRoutingIdentityValidation();
    void workspaceMandatoryLinksAtomic_data();
    void workspaceMandatoryLinksAtomic();
    void existingAnnualIdentitySurvivesEdit();
    void identityRepairRollsBackOnIndexFailure();
    void identityConflictsMustNotRenumberHistory();
    void bootstrapIdentityConflictsStopMigration();
    void missingIdentityYearRequiresValidDate();
    void conflictingChildIdentityStopsMigration();
    void conflictingCacheIdentityStopsMigration();
    void archivedCacheChildrenKeepParentIdentity();
    void archiveCacheImportIsAtomic();
    void archiveChildrenHaveIdentityAtInsert();
    void archiveOutsourceKeepsServiceLink();
    void archivePartUidTransfer_data();
    void archivePartUidTransfer();
    void archivePartsKeepServiceLink();
    void archiveCacheRebuildIsAtomic();
    void archiveCacheRefreshIsAtomic();
    void archiveImportKeepsDistinctOrderIdentities();
    void canonicalRouterParentSyncFailure();
    void canonicalSettingsSnapshotAndRestore();
    void canonicalCompletionSettingsFailure();
    void canonicalSourceSchemaReadFailure();
    void sealingRejectsLiveConnections_data();
    void sealingRejectsLiveConnections();
    void deferredEditorKeepsProfile();
    void migrationIsolatedSchema();
    void migrationOperationExclusion();
    void archiveBootstrapOwnership_data();
    void archiveBootstrapOwnership();
    void migrationPublicationCrashRecovery_data();
    void migrationPublicationCrashRecovery();
    void migrationReservesArchivedOrderKeys();
    void migrationServiceRoundTrip_data();
    void workspaceSchemaPreparationRollback();
    void migrationServiceRoundTrip();
    void migrationJournalRejectsCorruption();
    void migrationProfileSettings_data();
    void migrationProfileSettings();
    void migrationCrashWalSnapshot_data();
    void migrationCrashWalSnapshot();
    void migrationRestoreIntent_data();
    void migrationRestoreIntent();
    void migrationShardSnapshot_data();
    void migrationShardSnapshot();
    void migrationExternalStorageSnapshot_data();
    void migrationExternalStorageSnapshot();
#ifdef Q_OS_WIN
    void secureSnapshotRestore_data();
    void secureSnapshotRestore();
    void migrationWindowsMetadata_data();
    void migrationWindowsMetadata();
#endif
    void migrationProfileInventoryRejectsDamage_data();
    void migrationProfileInventoryRejectsDamage();
    void migrationCompleteProfileSnapshot_data();
    void migrationCompleteProfileSnapshot();
    void migrationRuntimeSnapshot_data();
    void migrationRuntimeSnapshot();
    void migrationSnapshotRejectsDamage_data();
    void migrationSnapshotRejectsDamage();
    void canonicalCorrespondence_data();
    void canonicalCorrespondence();
    void canonicalCorrespondenceLegacyKeys();
    void canonicalCleanupFailure_data();
    void canonicalCleanupFailure();
    void canonicalRegistryBatchFailure_data();
    void canonicalRegistryBatchFailure();
    void canonicalFinalizationColorFailure_data();
    void canonicalFinalizationColorFailure();
    void routerCacheHistoricalYear_data();
    void routerCacheHistoricalYear();
    void canonicalRouterParentIdentity_data();
    void canonicalRouterParentIdentity();
    void canonicalRouterChildUidFailure_data();
    void canonicalRouterChildUidFailure();
    void canonicalRouterColumnFailure_data();
    void canonicalRouterColumnFailure();
    void canonicalRouterNormalizationFailure_data();
    void canonicalRouterNormalizationFailure();
    void canonicalRouterIndexFailure_data();
    void canonicalRouterIndexFailure();
    void canonicalRegistrySchemaFailure_data();
    void canonicalRegistrySchemaFailure();
    void canonicalWorkspaceSchemaFailure_data();
    void canonicalWorkspaceSchemaFailure();
    void canonicalSupportNormalizationIsAtomic_data();
    void canonicalSupportNormalizationIsAtomic();
    void canonicalRewritePreservesOrderYear_data();
    void canonicalRewritePreservesOrderYear();
    void canonicalRewritePreservesOutsourceIdentity_data();
    void canonicalRewritePreservesOutsourceIdentity();
    void canonicalRewritePreservesChildIdentity_data();
    void canonicalRewritePreservesChildIdentity();
public slots:
    void captureUrl(const QUrl& url) { openedUrl = url; }
private:
    QUrl openedUrl;
};


void ReportsOrdersTests::sealingRejectsLiveConnections_data()
{
    QTest::addColumn<QString>("state");
    for (const auto& value : QStringList{"idle", "reader", "writer"})
        QTest::newRow(qPrintable(value)) << value;
}

void ReportsOrdersTests::sealingRejectsLiveConnections()
{
    QFETCH(QString, state);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath("sealing.db");
    const QString connection = "sealing-test-" + state;
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    auto cleanup = qScopeGuard([&] {
        db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection);
    });
    db.setDatabaseName(path);
    QVERIFY(db.open());
    {
        QSqlQuery q(db);
        QVERIFY(q.exec("PRAGMA journal_mode=WAL"));
        QVERIFY(q.exec("PRAGMA wal_autocheckpoint=0"));
        QVERIFY(q.exec("CREATE TABLE evidence(value INTEGER)"));
        QVERIFY(q.exec("INSERT INTO evidence VALUES(101)"));
        if (state != "idle") {
            QVERIFY(db.transaction());
            QVERIFY(q.exec(state == "writer" ? "INSERT INTO evidence VALUES(202)" : "SELECT * FROM evidence"));
            q.finish();
        }
        auto readBytes = [](const QString& file) {
            QFile f(file); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const auto before = readBytes(path + "-wal");
        QVERIFY(!before.isEmpty());
        QString error;
        QVERIFY(!EncryptedStorage::prepareSqliteForSealing(path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(path + "-wal"), before);
        if (state != "idle") QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT SUM(value) FROM evidence"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 101);
    }
    db.close();
    QString error;
    QVERIFY2(EncryptedStorage::prepareSqliteForSealing(path, &error), qPrintable(error));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("PRAGMA integrity_check"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("ok"));
    QVERIFY(q.exec("SELECT SUM(value) FROM evidence"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 101);
}

void ReportsOrdersTests::unicodeTestRootEnvironmentRoundTrip()
{
    QTemporaryDir base; QVERIFY(base.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] { setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest); });
    const auto root = QDir(base.path()).filePath(QString::fromUtf8("профіль_БД"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(setIsolatedTestRoot(root));
    QCOMPARE(qEnvironmentVariable("CS_TEST_DATA_ROOT"), root);
    QCoreApplication::setApplicationName("Safety-Unicode-Root"); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(QFileInfo(root).canonicalFilePath()));
}

void ReportsOrdersTests::migrationProfileSettings_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"restore", "corrupt", "wrong-identity"}) QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::migrationProfileSettings()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName = QCoreApplication::applicationName();
    const auto oldOrganization = QCoreApplication::organizationName(); const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Settings-", root.path()));
        QSettings settings; settings.setFallbacksEnabled(false); settings.clear(); settings.sync();
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrganization); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setOrganizationName("Safety-Settings-Tests");
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Settings-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    const QByteArray protectedBytes = QByteArray::fromHex("0080ff1a0012");
    { QSettings settings; settings.setFallbacksEnabled(false);
      QVERIFY(settings.fileName().contains("Safety-Settings-"));
      settings.setValue("protected/opaque", protectedBytes); settings.setValue("ui/name", QString::fromUtf8("Майстерня"));
      settings.setValue("ui/count", 42); settings.sync(); QCOMPARE(settings.status(), QSettings::NoError); }
    LegacyImportSafetySnapshot snapshot; QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot, &error), qPrintable(error));
    const auto backup = QDir(snapshot.snapshotDirPath).filePath("profile_settings.json");
    QFile data(backup); QVERIFY(data.open(QIODevice::ReadOnly)); const auto protectedFile = data.readAll(); data.close();
    QVERIFY(!protectedFile.contains("protected/opaque")); QVERIFY(!protectedFile.contains("ui/name"));
    { QSettings settings; settings.setValue("protected/opaque", QByteArray("changed")); settings.remove("ui/name");
      settings.setValue("new/setting", "failed import"); settings.sync(); }
    if (fault == "corrupt") { QFile file(backup); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("{"), qint64(1)); file.close(); }
    if (fault == "wrong-identity") QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Other-", root.path()));
    if (fault != "restore") {
        QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot, &error));
        QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Settings-", root.path()));
        QCOMPARE(QSettings().value("protected/opaque").toByteArray(), QByteArray("changed")); return;
    }
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot, &error), qPrintable(error));
        QSettings settings; settings.setFallbacksEnabled(false);
        QCOMPARE(settings.value("protected/opaque").toByteArray(), protectedBytes);
        QCOMPARE(settings.value("ui/name").toString(), QString::fromUtf8("Майстерня"));
        QCOMPARE(settings.value("ui/count").toInt(), 42); QVERIFY(!settings.contains("new/setting"));
    }
}

void ReportsOrdersTests::migrationCrashWalSnapshot_data()
{
    QTest::addColumn<bool>("shard");
    QTest::newRow("runtime") << false;
    QTest::newRow("archive-shard") << true;
}

void ReportsOrdersTests::migrationCrashWalSnapshot()
{
    QFETCH(bool, shard);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] { setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest); });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Wal-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    QProcess child; auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CS_SAFETY_FIXTURE_APP_NAME", QCoreApplication::applicationName());
        environment.insert("CS_SAFETY_FIXTURE_ORGANIZATION", QCoreApplication::organizationName());
        environment.insert("CS_SAFETY_FIXTURE_DOMAIN", QCoreApplication::organizationDomain());
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {"--wal-crash-fixture"});
    QVERIFY(child.waitForStarted(5000)); QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(), 0);
    const auto source = QDir(AppRuntime::dataDir()).filePath("crash_wal_source.db");
    const auto shardDir = QDir(AppRuntime::dataDir()).filePath("archive_shards");
    if (shard) QVERIFY(QDir().mkpath(shardDir));
    const auto target = shard ? QDir(shardDir).filePath("orders_000001_001000.db") : AppRuntime::activeDbFilePath();
    auto read = [](const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QByteArray(); return f.readAll(); };
    QVERIFY(QFileInfo(source + "-wal").size() > 32);
    const auto wal = read(source + "-wal");
    QVERIFY(QFile::copy(source, target)); QVERIFY(QFile::copy(source + "-wal", target + "-wal"));
    LegacyImportSafetySnapshot snapshot; QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot, &error), qPrintable(error));
    for (int retry = 0; retry < 2; ++retry) {
        if (QFileInfo::exists(target)) QVERIFY(QFile::remove(target));
        if (QFileInfo::exists(target + "-wal")) QVERIFY(QFile::remove(target + "-wal"));
        if (QFileInfo::exists(target + "-shm")) QVERIFY(QFile::remove(target + "-shm"));
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot, &error), qPrintable(error));
        QCOMPARE(read(target + "-wal"), wal);
        const QString connection = "crash-wal-recovery";
        { auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(target); QVERIFY(db.open());
          { QSqlQuery q(db); QVERIFY(q.exec("SELECT value FROM proof")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 83); q.finish();
            QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("ok")); }
          db.close(); }
        QSqlDatabase::removeDatabase(connection);
    }
}

void ReportsOrdersTests::migrationRestoreIntent_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"recover", "bad-marker", "changed-manifest", "other-snapshot", "directory-marker", "pending-snapshot", "process-crash", "stage-write-crash", "stage-promoted-crash"})
        QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::migrationRestoreIntent()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] { setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest); });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Intent-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    auto write = [](const QString& path, const QByteArray& bytes) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    };
    auto read = [](const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QByteArray(); return f.readAll(); };
    const auto active = AppRuntime::sealedActiveDbFilePath(); const auto key = AppRuntime::masterKeyFilePath();
    const auto runtime = AppRuntime::activeDbFilePath();
    QVERIFY(write(active, "original active")); QVERIFY(write(key, "original key"));
    {
        auto database=QSqlDatabase::addDatabase("QSQLITE","restore-intent-fixture");
        database.setDatabaseName(runtime);QVERIFY(database.open());
        { QSqlQuery query(database);QVERIFY(query.exec("CREATE TABLE proof(value TEXT)"));
          QVERIFY(query.exec("INSERT INTO proof VALUES('original runtime')")); }
        database.close();
    }
    QSqlDatabase::removeDatabase("restore-intent-fixture");
    const QByteArray originalRuntime=read(runtime);QVERIFY(!originalRuntime.isEmpty());
    const QString attachment=QDir(AppRuntime::dataDir()).filePath("attachments/nested/crash-proof.bin");
    QVERIFY(QDir().mkpath(QFileInfo(attachment).absolutePath()));
    const QByteArray originalAttachment=QByteArray::fromHex("00ff0102fe00");
    QVERIFY(write(attachment,originalAttachment));
    LegacyImportSafetySnapshot snapshot, another; QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot, &error), qPrintable(error));
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&another, &error), qPrintable(error));
    const auto manifest = QDir(snapshot.snapshotDirPath).filePath("manifest.json");
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(read(manifest), QCryptographicHash::Sha256).toHex());
    const auto marker = QDir(AppRuntime::securityDir()).filePath("migration_restore_pending.json");
    if (fault == "directory-marker") {
        QVERIFY(QDir().mkpath(marker));
        QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot, &error));
        QCOMPARE(read(key), QByteArray("original key")); return;
    }
    QVERIFY2(MigrationRestoreJournal::begin(snapshot.snapshotDirPath, hash, &error), qPrintable(error));
    QVERIFY(write(key, "failed import key")); QVERIFY(write(runtime, "failed import runtime"));
    QVERIFY(write(attachment,"failed import attachment"));
    // Model interruption after the first file has already returned to its old generation.
    QCOMPARE(read(active), QByteArray("original active"));
    if (fault == "process-crash" || fault.startsWith("stage-")) {
        QVERIFY(write(active, "failed import active"));
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("CS_SAFETY_FIXTURE_APP_NAME", QCoreApplication::applicationName());
        environment.insert("CS_SAFETY_FIXTURE_ORGANIZATION", QCoreApplication::organizationName());
        environment.insert("CS_SAFETY_FIXTURE_DOMAIN", QCoreApplication::organizationDomain());
        child.setProcessEnvironment(environment);
        QStringList arguments{fault=="process-crash"?"--snapshot-restore-crash-fixture":"--snapshot-stage-crash-fixture",snapshot.snapshotDirPath};
        if(fault.startsWith("stage-"))arguments<<fault;
        child.start(QCoreApplication::applicationFilePath(),arguments);
        QVERIFY(child.waitForStarted(5000)); QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(), 0);
        QCOMPARE(read(active),fault=="stage-write-crash"?QByteArray("failed import active"):QByteArray("original active"));
        QCOMPARE(read(key), QByteArray("failed import key")); QVERIFY(QFileInfo::exists(marker));
        const auto stages=QDir(AppRuntime::securityDir()).entryList({".migration_restore_*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot);
        if(fault=="stage-write-crash") {
            QCOMPARE(stages.size(),1);
            const auto payloads=QDir(QDir(AppRuntime::securityDir()).filePath(stages[0])).entryInfoList(QDir::Files|QDir::Hidden);
            QCOMPARE(payloads.size(),1);QCOMPARE(read(payloads[0].filePath()),QByteArray("original active").left(4));
#ifdef Q_OS_WIN
            const auto native=QDir::toNativeSeparators(payloads[0].filePath()).toStdWString();
            QVERIFY(GetFileAttributesW(native.c_str())&FILE_ATTRIBUTE_ENCRYPTED);
#endif
        }else QVERIFY(stages.isEmpty());
    }
    if (fault == "pending-snapshot") {
        LegacyImportSafetySnapshot rejected;
        QVERIFY(!LegacyImportSafetySupport::createSnapshot(&rejected, &error)); QVERIFY(!rejected.created);
    }
    if (fault == "other-snapshot") {
        const auto anotherManifest = QDir(another.snapshotDirPath).filePath("manifest.json");
        const auto anotherHash = QString::fromLatin1(QCryptographicHash::hash(read(anotherManifest), QCryptographicHash::Sha256).toHex());
        QVERIFY(!MigrationRestoreJournal::begin(another.snapshotDirPath, anotherHash, &error));
    }
    if (fault == "bad-marker") QVERIFY(write(marker, "{"));
    if (fault == "changed-manifest") QVERIFY(write(manifest, read(manifest) + " "));
    if (fault == "bad-marker" || fault == "changed-manifest") {
        QVERIFY(!RuntimeRecoveryCoordinator::recoverPendingSnapshotRestore(&error));
        QCOMPARE(read(key), QByteArray("failed import key")); QCOMPARE(read(runtime), QByteArray("failed import runtime"));
        QCOMPARE(read(attachment),QByteArray("failed import attachment"));
        QVERIFY(QFileInfo::exists(marker)); return;
    }
    QVERIFY2(RuntimeRecoveryCoordinator::recoverPendingSnapshotRestore(&error), qPrintable(error));
    QCOMPARE(read(active), QByteArray("original active")); QCOMPARE(read(key), QByteArray("original key"));
    QCOMPARE(read(runtime), originalRuntime);
    QCOMPARE(read(attachment), originalAttachment); QVERIFY(!QFileInfo::exists(marker));
    QVERIFY(QDir(AppRuntime::securityDir()).entryList({".migration_restore_*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    const auto originalFiles=QJsonDocument::fromJson(read(manifest)).object().value("files").toObject();QJsonObject actualMetadata;
    QVERIFY(NativeSnapshotMetadata::capture(active,&actualMetadata,&error));
    QCOMPARE(actualMetadata,originalFiles.value("CarService_workspace.db.enc").toObject().value("metadata").toObject());
    QVERIFY2(RuntimeRecoveryCoordinator::recoverPendingSnapshotRestore(&error), qPrintable(error));
}

void ReportsOrdersTests::migrationShardSnapshot_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"restore", "corrupt", "open", "worker", "directory", "missing-directory", "traversal"})
        QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::migrationShardSnapshot()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName);
        QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Shards-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    const auto shards = QDir(AppRuntime::dataDir()).filePath("archive_shards");
    QVERIFY(QDir().mkpath(shards));
    const auto oldShard = QDir(shards).filePath("orders_000001_001000.db");
    const auto newShard = QDir(shards).filePath("orders_001001_002000.db");
    auto write = [](const QString& path, const QByteArray& bytes) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    };
    auto read = [](const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QByteArray(); return f.readAll(); };
    QVERIFY(write(AppRuntime::sealedActiveDbFilePath(), "old sealed"));
    const QString connection = "migration-shard-snapshot";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto close = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(oldShard); QVERIFY(db.open());
    { QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE proof(value INTEGER)")); QVERIFY(q.exec("INSERT INTO proof VALUES(97)")); }
    if (fault != "open") db.close();
    LegacyImportSafetySnapshot snapshot; QString error;
    if (fault == "worker") {
        std::promise<bool> opened; std::promise<void> stop;
        auto stopped = stop.get_future();
        std::thread worker([&] {
            ScopedSqliteConnection connection(oldShard, "snapshot-worker");
            opened.set_value(connection.isOpen()); stopped.wait();
        });
        const auto joined = qScopeGuard([&] { stop.set_value(); worker.join(); });
        QVERIFY(opened.get_future().get());
        QVERIFY(!LegacyImportSafetySupport::createSnapshot(&snapshot, &error));
        QVERIFY(!snapshot.created); QVERIFY(!error.isEmpty()); return;
    }
    if (fault == "open") {
        QVERIFY(!LegacyImportSafetySupport::createSnapshot(&snapshot, &error));
        QVERIFY(!snapshot.created); QVERIFY(!error.isEmpty()); return;
    }
    const auto original = read(oldShard);
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot, &error), qPrintable(error));
    const auto backup = QDir(snapshot.snapshotDirPath).filePath("shards_orders_000001_001000.db");
    QVERIFY(QFileInfo::exists(backup));
    QVERIFY(QFile::remove(oldShard));
    if (fault == "missing-directory") QVERIFY(QDir().rmdir(shards));
    else QVERIFY(write(newShard, "new failed-import shard"));
    QVERIFY(write(AppRuntime::sealedActiveDbFilePath(), "changed sealed"));
    if (fault == "corrupt") QVERIFY(write(backup, "damaged"));
    if (fault == "directory") QVERIFY(QDir().mkpath(oldShard));
    if (fault == "traversal") {
        const auto manifest = QDir(snapshot.snapshotDirPath).filePath("manifest.json");
        auto document = QJsonDocument::fromJson(read(manifest));
        auto obj = document.object(); auto files = obj["files"].toObject();
        files.insert("shards_../outside.db", QJsonObject{{"present", false}, {"sha256", ""}});
        obj["files"] = files; QVERIFY(write(manifest, QJsonDocument(obj).toJson()));
    }
    if (fault != "restore" && fault != "missing-directory") {
        QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot, &error));
        QCOMPARE(read(AppRuntime::sealedActiveDbFilePath()), QByteArray("changed sealed"));
        QCOMPARE(read(newShard), QByteArray("new failed-import shard"));
        return;
    }
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot, &error), qPrintable(error));
        QCOMPARE(read(oldShard), original); QVERIFY(!QFileInfo::exists(newShard));
    }
    QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("SELECT value FROM proof")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 97); q.finish();
    QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("ok"));
}

void ReportsOrdersTests::migrationExternalStorageSnapshot_data()
{
    QTest::addColumn<QString>("kind");QTest::addColumn<bool>("removeRoot");
    for(const auto& kind:QStringList{"pdf_dir","invoices_dir","reports_dir","order_photos_dir"})
    {
        QTest::newRow(qPrintable(kind))<<kind<<false;
        QTest::newRow(qPrintable(kind+"-missing-root"))<<kind<<true;
    }
}

void ReportsOrdersTests::migrationExternalStorageSnapshot()
{
    QFETCH(QString,kind);QFETCH(bool,removeRoot);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const auto oldOrganization=QCoreApplication::organizationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{
        Database::close();QSettings settings;settings.setFallbacksEnabled(false);settings.clear();settings.sync();
        setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrganization);QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setOrganizationName("Safety-ExternalSnapshot-Tests");
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-ExternalSnapshot-",root.path()));
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));QVERIFY(Database::ensureSchema());
    const QString externalRoot=QDir(root.path()).filePath("external-"+kind);
    QVERIFY(QDir().mkpath(externalRoot));
    QVERIFY(!QDir::cleanPath(externalRoot).startsWith(QDir::cleanPath(AppRuntime::dataDir())+"/"));
    QVERIFY(Database::setSetting("settings.int.directories."+kind,externalRoot));
    QCOMPARE(QSettings().value("settings.int.directories."+kind).toString(),externalRoot);
    QSettings explicitSettings(QSettings().fileName(),QSettings().format());
    QCOMPARE(explicitSettings.value("settings.int.directories."+kind).toString(),externalRoot);
    Database::close();
    const QString filePath=QDir(externalRoot).filePath("original.bin"),newPath=QDir(externalRoot).filePath("created-by-failed-migration.bin");
    auto write=[](const QString& path,const QByteArray& bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto read=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    const QByteArray original=QByteArray::fromHex("010200feff0300");QVERIFY(write(filePath,original));
    LegacyImportSafetySnapshot snapshot;QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot,&error),qPrintable(error));
    for(int retry=0;retry<2;++retry) {
        QVERIFY(write(filePath,"modified by failed migration"));QVERIFY(write(newPath,"new artifact"));
        if(removeRoot) {
            QVERIFY(QFile::remove(filePath));QVERIFY(QFile::remove(newPath));
            QVERIFY(QDir().rmdir(externalRoot));QVERIFY(!QFileInfo::exists(externalRoot));
        }
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot,&error),qPrintable(error));
        QCOMPARE(read(filePath),original);QVERIFY(!QFileInfo::exists(newPath));
    }
}

#ifdef Q_OS_WIN
void ReportsOrdersTests::secureSnapshotRestore_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"success","inherited-acl","copy-failure","promotion-lock","readonly","metadata-failure","post-copy-corruption"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::secureSnapshotRestore()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    QVERIFY(SafeFileOps::isInsideRoot(root.path(),qEnvironmentVariable("CS_TEST_DATA_ROOT")));
    const QString source=QDir(root.path()).filePath("source.bin"),target=QDir(root.path()).filePath("target.bin");
    auto write=[](const QString& path,const QByteArray& bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto read=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    const QByteArray original=QByteArray::fromHex("00ff0123fe00");QVERIFY(write(source,original));QVERIFY(write(target,"old generation"));
    const auto sourceNative=QDir::toNativeSeparators(source).toStdWString(),targetNative=QDir::toNativeSeparators(target).toStdWString();
    const auto cleanup=qScopeGuard([&]{for(const auto& path:{sourceNative,targetNative}){const DWORD attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES)SetFileAttributesW(path.c_str(),attributes&~FILE_ATTRIBUTE_READONLY);}});
    if(fault=="readonly")for(const auto& path:{sourceNative,targetNative})QVERIFY(SetFileAttributesW(path.c_str(),GetFileAttributesW(path.c_str())|FILE_ATTRIBUTE_READONLY));
    QString error;QJsonObject metadata,before;
    QVERIFY2(NativeSnapshotMetadata::capture(source,&metadata,&error),qPrintable(error));QVERIFY(NativeSnapshotMetadata::capture(target,&before,&error));
    if(fault=="inherited-acl") {
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        const auto sddl=metadata.value("security").toString().toStdWString();
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr));
        const auto freeDescriptor=qScopeGuard([&]{LocalFree(descriptor);});
        const auto inheritance=SE_DACL_AUTO_INHERIT_REQ|SE_DACL_AUTO_INHERITED;
        QVERIFY(SetSecurityDescriptorControl(descriptor,inheritance,inheritance));
        QVERIFY(SetFileSecurityW(sourceNative.c_str(),DACL_SECURITY_INFORMATION,descriptor));
        QVERIFY2(NativeSnapshotMetadata::capture(source,&metadata,&error),qPrintable(error));
        QVERIFY(metadata.value("security").toString().section("D:",1).section('(',0,0).contains("AI"));
    }
    if(fault=="metadata-failure") {
        QString security=metadata.value("security").toString();const int group=security.indexOf("G:");QVERIFY(group>0);
        security="O:WD"+security.mid(group);metadata.insert("security",security);
    }
    HANDLE locked=INVALID_HANDLE_VALUE;
    if(fault=="promotion-lock"){locked=CreateFileW(targetNative.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);QVERIFY(locked!=INVALID_HANDLE_VALUE);}
    const auto unlock=qScopeGuard([&]{if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);});
    bool privateStage=false,encryptedPayload=false;
    auto copier=[&](const QString& from,const QString& to,const QString& expected,QString* copyError){
        const auto directory=QDir::toNativeSeparators(QFileInfo(to).absolutePath()).toStdWString();
        PSECURITY_DESCRIPTOR descriptor=nullptr;PSID owner=nullptr;PACL acl=nullptr;
        if(GetNamedSecurityInfoW(const_cast<wchar_t*>(directory.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor)!=ERROR_SUCCESS)return false;
        const auto freeDescriptor=qScopeGuard([&]{LocalFree(descriptor);});SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;
        bool allowed=acl && acl->AceCount==2 && GetSecurityDescriptorControl(descriptor,&control,&revision) && (control&SE_DACL_PROTECTED);
        for(DWORD i=0;allowed && i<acl->AceCount;++i){void* rawAce=nullptr;if(!GetAce(acl,i,&rawAce)){allowed=false;break;}
            const auto ace=static_cast<ACCESS_ALLOWED_ACE*>(rawAce);const PSID sid=&ace->SidStart;
            allowed=ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && ace->Mask==FILE_ALL_ACCESS
                && (EqualSid(owner,sid) || IsWellKnownSid(sid,WinLocalSystemSid))
                && (ace->Header.AceFlags&(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE))==(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE);}
        privateStage=allowed && (GetFileAttributesW(directory.c_str())&FILE_ATTRIBUTE_ENCRYPTED);
        if(!privateStage)return NativeSnapshotMetadata::fail(copyError,"Stage is not private and encrypted before write");
        const QByteArray bytes=read(from);QSaveFile file(to);
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())return false;
        const auto payload=QDir::toNativeSeparators(to).toStdWString();encryptedPayload=GetFileAttributesW(payload.c_str())&FILE_ATTRIBUTE_ENCRYPTED;
        if(fault=="copy-failure")return NativeSnapshotMetadata::fail(copyError,"Injected copy failure");
        const bool verified=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==expected;
        // Model staged content changing after the copying stream passed its hash check.
        if(fault=="post-copy-corruption" && !write(to,"corrupted after copy"))return false;
        return verified;
    };
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex());
    const bool restored=SecureSnapshotRestore::restoreFile(source,target,hash,metadata,copier,&error);
    QVERIFY(privateStage);QVERIFY(encryptedPayload);
    QVERIFY(QDir(root.path()).entryList({".migration_restore_*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    if(fault=="success" || fault=="readonly" || fault=="inherited-acl"){
        QVERIFY2(restored,qPrintable(error));QCOMPARE(read(target),original);QJsonObject actual;
        QVERIFY(NativeSnapshotMetadata::capture(target,&actual,&error));QCOMPARE(actual,metadata);
        QVERIFY2(SecureSnapshotRestore::restoreFile(source,target,hash,metadata,copier,&error),qPrintable(error));
    }else{
        QVERIFY(!restored);QVERIFY(!error.isEmpty());QCOMPARE(read(target),QByteArray("old generation"));QJsonObject actual;
        QVERIFY(NativeSnapshotMetadata::capture(target,&actual,&error));QCOMPARE(actual,before);
        if(locked!=INVALID_HANDLE_VALUE){CloseHandle(locked);locked=INVALID_HANDLE_VALUE;}
        QVERIFY(NativeSnapshotMetadata::capture(source,&metadata,&error));fault="success";
        QVERIFY2(SecureSnapshotRestore::restoreFile(source,target,hash,metadata,copier,&error),qPrintable(error));QCOMPARE(read(target),original);
    }
}

void ReportsOrdersTests::migrationWindowsMetadata_data()
{
    QTest::addColumn<QString>("kind");
    for(const auto& kind:QStringList{"file","empty-directory","sealed"})QTest::newRow(qPrintable(kind))<<kind;
}

void ReportsOrdersTests::migrationWindowsMetadata()
{
    QFETCH(QString,kind);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Metadata-",root.path()));
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    const QString profile=AppRuntime::dataDir();QVERIFY(profile.startsWith(root.path()+"/"));
    QVERIFY(QDir().mkpath(AppRuntime::securityDir()));
    const bool directory=kind=="empty-directory";
    const QString path=kind=="sealed"?AppRuntime::sealedActiveDbFilePath():QDir(profile).filePath("attachments/"+kind);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    auto write=[](const QString& target,const QByteArray& bytes){QFile f(target);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    const QByteArray bytes=QByteArray::fromHex("00ff0100fe");
    if(directory)QVERIFY(QDir().mkpath(path));else QVERIFY(write(path,bytes));
    const std::wstring native=QDir::toNativeSeparators(path).toStdWString();
    PSECURITY_DESCRIPTOR descriptor=nullptr;PSID owner=nullptr;
    QCOMPARE(GetNamedSecurityInfoW(const_cast<wchar_t*>(native.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&descriptor),DWORD(ERROR_SUCCESS));
    const auto freeDescriptor=qScopeGuard([&]{LocalFree(descriptor);});
    LPWSTR sid=nullptr;QVERIFY(ConvertSidToStringSidW(owner,&sid));
    const QString ownerSid=QString::fromWCharArray(sid);LocalFree(sid);
    const QString privateAcl="D:P(A;;FA;;;SY)(A;;FA;;;"+ownerSid+")";
    auto applyAcl=[&](const QString& text){PSECURITY_DESCRIPTOR sd=nullptr;
        const auto encoded=text.toStdWString();if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(encoded.c_str(),SDDL_REVISION_1,&sd,nullptr))return false;
        const bool ok=SetFileSecurityW(native.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);LocalFree(sd);return ok;};
    QVERIFY(applyAcl(privateAcl));QVERIFY(EncryptFileW(native.c_str()));
    const quint64 ticks=132223214450000000ULL;
    const FILETIME originalTime{DWORD(ticks),DWORD(ticks>>32)};
    HANDLE handle=CreateFileW(native.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    QVERIFY(handle!=INVALID_HANDLE_VALUE);const bool setTime=SetFileTime(handle,&originalTime,nullptr,&originalTime);CloseHandle(handle);QVERIFY(setTime);
    auto metadata=[&](){QJsonObject result;PSECURITY_DESCRIPTOR sd=nullptr;
        if(GetNamedSecurityInfoW(const_cast<wchar_t*>(native.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&sd)!=ERROR_SUCCESS)return result;
        LPWSTR text=nullptr;const bool converted=ConvertSecurityDescriptorToStringSecurityDescriptorW(sd,SDDL_REVISION_1,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&text,nullptr);
        if(converted){result.insert("security",QString::fromWCharArray(text));LocalFree(text);}LocalFree(sd);
        WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(native.c_str(),GetFileExInfoStandard,&info))return QJsonObject();
        result.insert("attributes",double(info.dwFileAttributes));
        result.insert("creation",QString::number((quint64(info.ftCreationTime.dwHighDateTime)<<32)|info.ftCreationTime.dwLowDateTime));
        result.insert("modified",QString::number((quint64(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime));return result;};
    const auto before=metadata();QCOMPARE(before.size(),4);QVERIFY(before.value("attributes").toInt()&FILE_ATTRIBUTE_ENCRYPTED);
    LegacyImportSafetySnapshot snapshot;QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot,&error),qPrintable(error));
    for(int retry=0;retry<2;++retry){
        QVERIFY(DecryptFileW(native.c_str(),0));QVERIFY(applyAcl("D:P(A;;FR;;;SY)(A;;FA;;;"+ownerSid+")"));
        if(!directory)QVERIFY(write(path,"changed generation"));
        QVERIFY(metadata()!=before);
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot,&error),qPrintable(error));
        QCOMPARE(metadata(),before);
        if(!directory){QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),bytes);}
    }
}
#endif

void ReportsOrdersTests::migrationProfileInventoryRejectsDamage_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"corrupt-file","missing-file","traversal","root","missing-parent","case-alias","target-type","missing-inventory","metadata-extra","metadata-platform","known-metadata","root-metadata"})
        QTest::newRow(qPrintable(fault))<<fault;
#ifdef Q_OS_WIN
    for(const auto& fault:QStringList{"metadata-security","metadata-time","metadata-zero-attributes"})QTest::newRow(qPrintable(fault))<<fault;
#endif
}

void ReportsOrdersTests::migrationProfileInventoryRejectsDamage()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-InventoryDamage-",root.path()));
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    const QString profile=AppRuntime::dataDir();QVERIFY(profile.startsWith(root.path()+"/"));
    QVERIFY(QDir().mkpath(AppRuntime::securityDir()));
    auto write=[](const QString& path,const QByteArray& bytes){if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto read=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    const QString rel="attachments/nested/proof.bin",file=QDir(profile).filePath(rel);
    const QString active=AppRuntime::sealedActiveDbFilePath(),extra=QDir(profile).filePath("failed-import.bin");
    const QString outside=QDir(root.path()).filePath("outside.bin");
    const QByteArray original=QByteArray::fromHex("00ff1200fe");
    QVERIFY(write(file,original));QVERIFY(write(active,"original sealed"));QVERIFY(write(outside,"outside sentinel"));
    LegacyImportSafetySnapshot snapshot;QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot,&error),qPrintable(error));
    const QString manifest=QDir(snapshot.snapshotDirPath).filePath("manifest.json");
    const QByteArray originalManifest=read(manifest);
    const QString backup=QDir(snapshot.snapshotDirPath).filePath("profile_tree/0/"+rel);
    QCOMPARE(read(backup),original);
    QVERIFY(write(active,"changed sealed"));QVERIFY(write(file,"changed attachment"));QVERIFY(write(extra,"new file"));
    auto document=QJsonDocument::fromJson(originalManifest).object();
    auto trees=document.value("profile_inventory").toArray();QVERIFY(!trees.isEmpty());
    auto tree=trees[0].toObject();auto entries=tree.value("entries").toObject();
    if(fault=="corrupt-file")QVERIFY(write(backup,"corrupt"));
    if(fault=="missing-file")QVERIFY(QFile::remove(backup));
    if(fault=="traversal")entries.insert("../outside.bin",entries.value(rel));
    if(fault=="root")tree.insert("root",root.path());
    if(fault=="missing-parent")entries.remove("attachments/nested");
    if(fault=="case-alias")entries.insert("Attachments/nested/proof.bin",entries.value(rel));
    if(fault=="target-type"){QVERIFY(QFile::remove(file));QVERIFY(QDir().mkpath(file));}
    auto fileEntry=entries.value(rel).toObject();auto metadata=fileEntry.value("metadata").toObject();
    if(fault=="metadata-extra")metadata.insert("unrecognized",true);
    if(fault=="metadata-platform")metadata.insert("platform","unknown");
    if(fault=="metadata-security")metadata.insert("security","not a security descriptor");
    if(fault=="metadata-time")metadata.insert("modified","18446744073709551615");
    if(fault=="metadata-zero-attributes")metadata.insert("attributes",0);
    fileEntry.insert("metadata",metadata);entries.insert(rel,fileEntry);
    if(fault=="root-metadata")tree.insert("metadata","invalid");
    if(fault=="known-metadata") {
        auto known=document.value("files").toObject();auto first=known.value("CarService_workspace.db.enc").toObject();
        first.insert("metadata","invalid");known.insert("CarService_workspace.db.enc",first);document.insert("files",known);
    }
    tree.insert("entries",entries);trees[0]=tree;document.insert("profile_inventory",trees);
    if(fault=="missing-inventory")document.remove("profile_inventory");
    QVERIFY(write(manifest,QJsonDocument(document).toJson()));
    QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot,&error));QVERIFY(!error.isEmpty());
    QCOMPARE(read(active),QByteArray("changed sealed"));QCOMPARE(read(extra),QByteArray("new file"));
    QCOMPARE(read(outside),QByteArray("outside sentinel"));
    QVERIFY(!QFileInfo::exists(QDir(AppRuntime::securityDir()).filePath("migration_restore_pending.json")));
    if(fault=="target-type"){QVERIFY(QFileInfo(file).isDir());QVERIFY(QDir().rmdir(file));}
    else QCOMPARE(read(file),QByteArray("changed attachment"));
    QVERIFY(write(manifest,originalManifest));QVERIFY(write(backup,original));
    for(int retry=0;retry<2;++retry){
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot,&error),qPrintable(error));
        QCOMPARE(read(active),QByteArray("original sealed"));QCOMPARE(read(file),original);
        QVERIFY(!QFileInfo::exists(extra));QCOMPARE(read(outside),QByteArray("outside sentinel"));
    }
}

void ReportsOrdersTests::migrationCompleteProfileSnapshot_data()
{
    QTest::addColumn<QString>("relativePath");
    for(const auto& path:QStringList{"attachments/2025/order-uid/photo.bin","invoices/2025/document.bin","logs/activity/archive.log","preferences/nested/options.bin","empty-directory/child"})
        QTest::newRow(qPrintable(path))<<path;
}

void ReportsOrdersTests::migrationCompleteProfileSnapshot()
{
    QFETCH(QString,relativePath);
    QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-CompleteProfile-",root.path()));
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    const QString dataRoot=AppRuntime::dataDir();
    QVERIFY(QDir::cleanPath(dataRoot).startsWith(QDir::cleanPath(root.path())+"/"));
    QVERIFY(QDir().mkpath(AppRuntime::securityDir()));
    const QString originalPath=QDir(dataRoot).filePath(relativePath);
    const QString newPath=QDir(dataRoot).filePath("failed-migration/new-file.bin");
    auto write=[](const QString& path,const QByteArray& bytes){if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto read=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll();};
    const QByteArray original=QByteArray::fromHex("000102ff10203000fe");
    const bool directory=relativePath.startsWith("empty-directory/");
    if(directory)QVERIFY(QDir().mkpath(originalPath));else QVERIFY(write(originalPath,original));
    LegacyImportSafetySnapshot snapshot;QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot,&error),qPrintable(error));
    QVERIFY(snapshot.created);
    for(int retry=0;retry<2;++retry) {
        if(directory)QVERIFY(QDir().rmdir(originalPath));else QVERIFY(write(originalPath,"changed by failed migration"));
        QVERIFY(write(newPath,"new failed migration file"));
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot,&error),qPrintable(error));
        if(directory)QVERIFY(QFileInfo(originalPath).isDir());else QCOMPARE(read(originalPath),original);
        QVERIFY(!QFileInfo::exists(newPath));
    }
}

void ReportsOrdersTests::migrationRuntimeSnapshot_data()
{
    QTest::addColumn<int>("index");
    QTest::addColumn<bool>("live");
    for (int i = 0; i < 4; ++i) {
        QTest::newRow(qPrintable(QString("closed-%1").arg(i))) << i << false;
        QTest::newRow(qPrintable(QString("open-%1").arg(i))) << i << true;
    }
}

void ReportsOrdersTests::migrationRuntimeSnapshot()
{
    QFETCH(int, index); QFETCH(bool, live);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Runtime-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    const QStringList paths{AppRuntime::activeDbFilePath(), AppRuntime::archiveDbFilePath(),
                            AppRuntime::legacyDbFilePath(), AppRuntime::registryDbFilePath()};
    for (const auto& path : paths) QVERIFY(path.startsWith(root.path()));
    auto write = [](const QString& path, const QByteArray& bytes) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    };
    auto read = [](const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return QByteArray(); return f.readAll(); };
    // An older encrypted copy must never hide the newer committed runtime.
    QVERIFY(write(AppRuntime::sealedActiveDbFilePath(), "older sealed generation"));
    const QString connection = "migration-runtime-snapshot";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto close = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(paths[index]); QVERIFY(db.open());
    { QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE proof(value INTEGER)"));
      QVERIFY(q.exec("INSERT INTO proof VALUES(73)")); }
    if (!live) db.close();
    LegacyImportSafetySnapshot snapshot; QString error;
    if (live) {
        QVERIFY(!LegacyImportSafetySupport::createSnapshot(&snapshot, &error));
        QVERIFY(!snapshot.created); QVERIFY(!error.isEmpty());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT value FROM proof")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 73);
        return;
    }
    const QByteArray original = read(paths[index]); QVERIFY(!original.isEmpty());
    // Preserve sidecar bytes and absence separately, without opening them as SQL.
    QVERIFY(write(paths[index] + "-wal", "saved wal bytes"));
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot, &error), qPrintable(error));
    const auto backup = QDir(snapshot.snapshotDirPath).filePath("runtime_" + QFileInfo(paths[index]).fileName());
    QVERIFY(write(backup, "damaged runtime backup"));
    QVERIFY(write(AppRuntime::sealedActiveDbFilePath(), "changed sealed generation"));
    QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot, &error));
    QCOMPARE(read(AppRuntime::sealedActiveDbFilePath()), QByteArray("changed sealed generation"));
    QCOMPARE(read(paths[index]), original);
    QVERIFY(write(backup, original));
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY(write(paths[index], "failed migration runtime"));
        QVERIFY(write(paths[index] + "-wal", "failed migration wal"));
        QVERIFY(write(paths[index] + "-shm", "new shm"));
        QVERIFY(write(paths[(index + 1) % 4], "new database"));
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot, &error), qPrintable(error));
        QCOMPARE(read(paths[index]), original);
        QCOMPARE(read(paths[index] + "-wal"), QByteArray("saved wal bytes"));
        QVERIFY(!QFileInfo::exists(paths[index] + "-shm"));
        QVERIFY(!QFileInfo::exists(paths[(index + 1) % 4]));
    }
    QVERIFY(QFile::remove(paths[index] + "-wal"));
    QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("SELECT value FROM proof")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 73); q.finish();
    QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("ok")); q.finish();
    QVERIFY(write(AppRuntime::sealedActiveDbFilePath(), "changed while runtime open"));
    QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot, &error));
    QCOMPARE(read(AppRuntime::sealedActiveDbFilePath()), QByteArray("changed while runtime open"));
    db.close();
    QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot, &error), qPrintable(error));
    QCOMPARE(read(AppRuntime::sealedActiveDbFilePath()), QByteArray("older sealed generation"));
}

void ReportsOrdersTests::migrationSnapshotRejectsDamage_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& value : QStringList{"missing", "corrupt", "manifest", "unexpected", "target", "last-target"})
        QTest::newRow(qPrintable(value)) << value;
}

void ReportsOrdersTests::migrationSnapshotRejectsDamage()
{
    QFETCH(QString, fault);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto oldName=QCoreApplication::applicationName();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Migration-", dir.path()));
    QVERIFY(setIsolatedTestRoot(dir.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::securityDir().startsWith(dir.path()));
    QVERIFY(QDir().mkpath(AppRuntime::securityDir()));
    const auto active=AppRuntime::sealedActiveDbFilePath();
    const auto key=AppRuntime::masterKeyFilePath();
    const auto registry=AppRuntime::sealedRegistryDbFilePath();
    auto write=[](const QString& path,const QByteArray& bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
    };
    auto read=[](const QString& path) {QFile file(path); if(!file.open(QIODevice::ReadOnly))return QByteArray();return file.readAll();};
    const auto keyBytes=QByteArray::fromHex("00ff120080");
    QVERIFY(write(active,"original database")); QVERIFY(write(key,keyBytes));
    LegacyImportSafetySnapshot snapshot; QString error;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&snapshot,&error),qPrintable(error));
    QVERIFY(snapshot.created);
    LegacyImportSafetySnapshot another;
    QVERIFY2(LegacyImportSafetySupport::createSnapshot(&another,&error),qPrintable(error));
    QVERIFY(snapshot.snapshotDirPath!=another.snapshotDirPath);
    QVERIFY(write(active,"changed database")); QVERIFY(write(key,"changed key"));
    QVERIFY(write(registry,"new registry"));
    const auto backupKey=QDir(snapshot.snapshotDirPath).filePath("master.key.dpapi");
    const auto manifest=QDir(snapshot.snapshotDirPath).filePath("manifest.json");
    const auto manifestBytes=read(manifest);
    const auto unexpected=QDir(snapshot.snapshotDirPath).filePath("CarService_registry.db.enc");
    if(fault=="missing") QVERIFY(QFile::remove(backupKey));
    if(fault=="corrupt") QVERIFY(write(backupKey,"corrupt"));
    if(fault=="manifest") QVERIFY(write(manifest,"{"));
    if(fault=="unexpected") QVERIFY(write(unexpected,"unexpected"));
    if(fault=="target") {QVERIFY(QFile::rename(active,active+".saved"));QVERIFY(QDir().mkpath(active));}
    if(fault=="last-target") {QVERIFY(QFile::rename(key,key+".saved"));QVERIFY(QDir().mkpath(key));}
    QVERIFY(!LegacyImportSafetySupport::restoreSnapshot(snapshot,&error));
    QVERIFY(!error.isEmpty());
    if(fault=="target") {
        QCOMPARE(read(active+".saved"),QByteArray("changed database"));
        QVERIFY(QDir().rmdir(active)); QVERIFY(QFile::rename(active+".saved",active));
    } else QCOMPARE(read(active),QByteArray("changed database"));
    if(fault=="last-target") {
        QCOMPARE(read(key+".saved"),QByteArray("changed key"));
        QVERIFY(QDir().rmdir(key)); QVERIFY(QFile::rename(key+".saved",key));
    }
    QCOMPARE(read(key),QByteArray("changed key"));
    QCOMPARE(read(registry),QByteArray("new registry"));
    QVERIFY(write(backupKey,keyBytes)); QVERIFY(write(manifest,manifestBytes));
    if(QFile::exists(unexpected))QVERIFY(QFile::remove(unexpected));
    for(int retry=0;retry<2;++retry) {
        QVERIFY2(LegacyImportSafetySupport::restoreSnapshot(snapshot,&error),qPrintable(error));
        QCOMPARE(read(active),QByteArray("original database")); QCOMPARE(read(key),keyBytes);
        QVERIFY(!QFile::exists(registry));
    }
}



void ReportsOrdersTests::deferredEditorKeepsProfile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldMode = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        Database::close();
        setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName);
        QStandardPaths::setTestModeEnabled(oldMode);
    });
    QVERIFY(setIsolatedTestRoot(root.path()));
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-EditorOrigin-",root.path()));
    const auto origin = AppRuntime::activeDbFilePath();
    OrderDialogSubmitSupport::scheduleOpenCreatedOrderEditor(nullptr,7);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-EditorDestination-",root.path()));
    const auto destination = AppRuntime::activeDbFilePath();
    QVERIFY(origin != destination && destination.startsWith(root.path()));
    QVERIFY(!QFileInfo::exists(destination));
    QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
    QVERIFY(!QFileInfo::exists(destination));
    for (auto* widget : QApplication::topLevelWidgets()) QVERIFY(!qobject_cast<OrderDialog*>(widget));
}

void ReportsOrdersTests::migrationIsolatedSchema()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldMode = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName);
        QStandardPaths::setTestModeEnabled(oldMode);
    });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Schema-",dir.path()));
    QVERIFY(setIsolatedTestRoot(dir.path()));
    QStandardPaths::setTestModeEnabled(true);
    const QString live = AppRuntime::activeDbFilePath();
    QVERIFY(live.startsWith(dir.path()) && !QFileInfo::exists(live));
    const QString name = "migration-private-workspace";
    auto db = QSqlDatabase::addDatabase("QSQLITE",name);
    const auto cleanup = qScopeGuard([&]{db.close(); db={}; QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(dir.filePath("generation.db"));
    QVERIFY(db.open());
    QVERIFY(ensureWorkspaceSchema(db));
    QVERIFY(db.tables().contains("work_order_parts"));
    QVERIFY(db.tables().contains("masters"));
    QVERIFY(!QFileInfo::exists(live));
}

void ReportsOrdersTests::archiveBootstrapOwnership_data()
{
    QTest::addColumn<bool>("hasExpense");
    QTest::newRow("empty-workspace") << false;
    QTest::newRow("expense-only-workspace") << true;
}

void ReportsOrdersTests::archiveBootstrapOwnership()
{
    QFETCH(bool, hasExpense);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        Database::close();
        setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName);
        QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Bootstrap-", root.path()));
    QVERIFY(setIsolatedTestRoot(root.path()));
    QStandardPaths::setTestModeEnabled(true);
    const auto archive = AppRuntime::archiveDbFilePath();
    QVERIFY(archive.startsWith(root.path() + '/'));
    QVERIFY(QDir().mkpath(QFileInfo(archive).absolutePath()));
    {
        const QString connection = "bootstrap-archive";
        auto source = QSqlDatabase::addDatabase("QSQLITE", connection);
        const auto close = qScopeGuard([&] { source.close(); source = {}; QSqlDatabase::removeDatabase(connection); });
        source.setDatabaseName(archive);
        QVERIFY(source.open());
        QSqlQuery query(source);
        QVERIFY(query.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,opened_at TEXT,order_year INTEGER,order_number INTEGER)"));
        QVERIFY(query.exec("INSERT INTO work_orders VALUES(7,'archive-bootstrap-order','2024-06-15',2024,7)"));
    }
    const QString connection = "bootstrap-target";
    auto target = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto close = qScopeGuard([&] { target.close(); target = {}; QSqlDatabase::removeDatabase(connection); });
    target.setDatabaseName(root.filePath("target.db"));
    QVERIFY(target.open());
    QVERIFY(ensureWorkspaceSchema(target));
    QSqlQuery query(target);
    if (hasExpense)
        QVERIFY(query.exec("INSERT INTO expenses(id,period_from,period_to,rent) VALUES(9,'2025-01-01','2025-01-31',23.75)"));
    QString error;
    for (int run = 0; run < 2; ++run) {
        QVERIFY2(rewriteLegacyToCanonicalWorkspaceIfNeeded(target, &error), qPrintable(error));
        QVERIFY(query.exec("SELECT COUNT(*) FROM work_orders WHERE order_uid='archive-bootstrap-order'"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), hasExpense ? 0 : 1);
        query.finish();
        QVERIFY(query.exec("SELECT COUNT(*) FROM expenses WHERE id=9 AND round(100*rent)=2375"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), hasExpense ? 1 : 0);
        query.finish();
    }
}

void ReportsOrdersTests::migrationOperationExclusion()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Exclusion-",root.path()));
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    using Operation=DataProtectionCoordinator::Operation;
    {
        QString detail;
        const auto runtime = AppRuntime::sessionDir();
        QVERIFY(!LegacyImportPolicySupport::validateSourceSelection(QDir(runtime).filePath("nested/service_center.db"),&detail));
        QVERIFY(!LegacyImportPolicySupport::validateSourceSelection(QDir(AppRuntime::securityDir()).filePath("nested/source.db"),&detail));
        QVERIFY(LegacyImportPolicySupport::validateSourceSelection(runtime+"-backup/service_center.db",&detail));
#ifdef Q_OS_WIN
        QVERIFY(!LegacyImportPolicySupport::validateSourceSelection(QDir::toNativeSeparators(runtime.toUpper()+"/source.db"),&detail));
#endif
    }
    for(const auto operation:{Operation::AutomaticBackup,Operation::ManualBackup,Operation::Restore,Operation::DbMigration}) {
        QString error;
        DataProtectionCoordinator::Scope held;
        std::thread holder([&]{held=DataProtectionCoordinator::tryEnter(operation,"migration exclusion fixture",&error);});holder.join();
        QVERIFY2(held.isActive(),qPrintable(error)); const auto id=held.operationId();
        bool ok=true;int progressCalls=0;LegacyMigrationResult result;
        std::thread worker([&]{ok=LegacyMigrationService::executeLegacyImport(root.filePath("missing.db"),&result,&error,
            [&](const LegacyImportProgressState&){++progressCalls;});}); worker.join();
        QVERIFY(!ok); QVERIFY2(error.startsWith("Data protection busy:"),qPrintable(error));
        QCOMPARE(progressCalls,0); QCOMPARE(DataProtectionCoordinator::currentOperationId(),id);
        QVERIFY(!QFileInfo::exists(QDir(AppRuntime::securityDir()).filePath("legacy_import_history.json")));
        held.release(); QVERIFY(!DataProtectionCoordinator::isBusy());
    }
    // Hold the migration operation through callbacks and release it on early failure.
    bool ok=true,callbackSeen=false,backupRejected=false;QString error;LegacyMigrationResult result;
    std::thread worker([&]{ok=LegacyMigrationService::executeLegacyImport(root.filePath("missing.db"),&result,&error,
        [&](const LegacyImportProgressState& state){
            if(state.stageKey!="execute_prepare")return;
            callbackSeen=true;
            std::thread contender([&]{QString busy;auto scope=DataProtectionCoordinator::tryEnter(Operation::ManualBackup,"contender",&busy);
                backupRejected=!scope.isActive() && busy.startsWith("Data protection busy:");});contender.join();
        });});worker.join();
    QVERIFY(!ok); QVERIFY(callbackSeen); QVERIFY(backupRejected); QVERIFY(!DataProtectionCoordinator::isBusy());
    DataProtectionCoordinator::Scope retry;
    std::thread finalAttempt([&]{retry=DataProtectionCoordinator::tryEnter(Operation::ManualBackup,"after failed migration",&error);});finalAttempt.join();
    QVERIFY2(retry.isActive(),qPrintable(error));retry.release();
    QVERIFY(!QFileInfo::exists(AppRuntime::activeDbFilePath()));
}

void ReportsOrdersTests::migrationPublicationCrashRecovery_data()
{
    QTest::addColumn<QString>("boundary");
    for(const auto& stage:QStringList{"execute_import","execute_reopen","execute_done"})
        QTest::newRow(qPrintable(stage))<<stage;
}

void ReportsOrdersTests::migrationPublicationCrashRecovery()
{
    QFETCH(QString,boundary);
    QTemporaryDir parent(QDir::temp().filePath("crash-XXXXXX"));QVERIFY(parent.isValid());
    const auto markerPath=parent.filePath("crash.json");
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("CS_TEST_MIGRATION_PARENT",parent.path());
    environment.insert("CS_TEST_MIGRATION_CRASH_MARKER",markerPath);
    environment.insert("CS_TEST_MIGRATION_CRASH_STAGE",boundary);
    QProcess child;child.setProcessEnvironment(environment);
    const auto childLogPath = parent.filePath("child-result.txt");
    child.start(QCoreApplication::applicationFilePath(),{"-o",childLogPath+",txt","migrationServiceRoundTrip:populated-retained-stores"});
    QVERIFY(child.waitForStarted());QVERIFY(child.waitForFinished(120000));
    if (child.exitCode()!=87) {
        QFile childLog(childLogPath);
        const auto report=childLog.open(QIODevice::ReadOnly) ? childLog.readAll() : QByteArray();
        QFAIL(qPrintable(QString("Expected crash exit 87, got %1: %2").arg(child.exitCode()).arg(QString::fromUtf8(report+child.readAllStandardError()))));
    }
    QFile marker(markerPath);QVERIFY(marker.open(QIODevice::ReadOnly));
    QJsonParseError parse;const auto evidence=QJsonDocument::fromJson(marker.readAll(),&parse).object();
    QCOMPARE(parse.error,QJsonParseError::NoError);
    const auto root=evidence.value("root").toString();QVERIFY(QFileInfo(root).canonicalFilePath().startsWith(parent.path()+"/"));
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");const auto oldName=QCoreApplication::applicationName();
    const auto oldOrg=QCoreApplication::organizationName(),oldDomain=QCoreApplication::organizationDomain();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto oldFormat=QSettings::defaultFormat();
    const auto cleanup=qScopeGuard([&]{Database::close();QSettings::setDefaultFormat(oldFormat);
        setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QCoreApplication::setOrganizationName(oldOrg);
        QCoreApplication::setOrganizationDomain(oldDomain);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root));QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(evidence.value("name").toString());
    QCoreApplication::setOrganizationName(evidence.value("organization").toString());
    QCoreApplication::setOrganizationDomain(evidence.value("domain").toString());
    // A previous test can select an INI backend in this process. Reopen the
    // same settings store the terminated child used for the captured snapshot.
    QVERIFY(evidence.contains("settingsFormat"));
    QSettings::setDefaultFormat(static_cast<QSettings::Format>(evidence.value("settingsFormat").toInt()));
    QCOMPARE(QSettings().fileName(),evidence.value("settingsFile").toString());
    const auto cleanFixtureSettings=qScopeGuard([]{QSettings settings;settings.clear();settings.sync();});
    QString error,pending;QVERIFY2(MigrationRestoreJournal::pending(&pending,&error),qPrintable(error));
    QCOMPARE(pending,evidence.value("snapshot").toString());
    QVERIFY2(RuntimeRecoveryCoordinator::recoverPendingSnapshotRestore(&error),qPrintable(error));
    QVERIFY2(MigrationRestoreJournal::pending(&pending,&error),qPrintable(error));QVERIFY(pending.isEmpty());
    const QMap<QString,QString> stores={{"archive",AppRuntime::sealedArchiveDbFilePath()},
        {"legacy",AppRuntime::sealedLegacyDbFilePath()},{"registry",AppRuntime::sealedRegistryDbFilePath()}};
    for(auto it=stores.cbegin();it!=stores.cend();++it) {
        QFile file(it.value());QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex()),
            evidence.value("hashes").toObject().value(it.key()).toString());
    }
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {QSqlQuery q(Database::db());QVERIFY(q.exec("SELECT order_uid FROM work_orders"));QVERIFY(q.next());
     QCOMPARE(q.value(0).toString(),QString("existing-target-order"));QVERIFY(!q.next());}
    LegacyMigrationResult result;bool ok=false;QEventLoop loop;
    std::thread retry([&]{ok=LegacyMigrationService::executeLegacyImport(evidence.value("source").toString(),&result,&error);
        Database::close();QMetaObject::invokeMethod(&loop,&QEventLoop::quit,Qt::QueuedConnection);});
    loop.exec();retry.join();QVERIFY2(ok,qPrintable(error));
    QCOMPARE(result.importedOrderCount,1);QCOMPARE(result.importedServiceCount,1);QCOMPARE(result.importedPartCount,1);
    QVERIFY2(MigrationRestoreJournal::pending(&pending,&error),qPrintable(error));QVERIFY(pending.isEmpty());
    QSqlQuery q(Database::db());QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);
}

void ReportsOrdersTests::migrationReservesArchivedOrderKeys()
{
    const QString name=QUuid::createUuid().toString();
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    for(const auto& sql:QStringList{
        "CREATE TABLE work_orders(number INTEGER PRIMARY KEY)",
        "INSERT INTO work_orders VALUES(7)",
        "CREATE TABLE order_registry(order_number INTEGER PRIMARY KEY,order_uid TEXT)",
        "INSERT INTO order_registry VALUES(7,'old-uid'),(40,'archive-only-uid')",
        "ATTACH DATABASE ':memory:' AS audited_source",
        "CREATE TABLE audited_source.work_orders(number INTEGER PRIMARY KEY)",
        "INSERT INTO audited_source.work_orders VALUES(7)"})
        QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
    QString error;
    MigrationOrderKeys::Map keys;
    QVERIFY2(MigrationOrderKeys::plan(db,"audited_source",&keys,&error),qPrintable(error));
    QCOMPARE(keys.size(),1);
    QCOMPARE(keys.value(7),qint64(41));
    QVERIFY(q.exec("DELETE FROM work_orders"));
    QVERIFY2(MigrationOrderKeys::plan(db,"audited_source",&keys,&error),qPrintable(error));
    QCOMPARE(keys.size(),1);
    QCOMPARE(keys.value(7),qint64(41));
    QVERIFY(q.exec("SELECT order_uid FROM order_registry WHERE order_number=7"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("old-uid"));q.finish();
    QVERIFY(q.exec("INSERT INTO order_registry VALUES(2147483647,'last-uid')"));
    QVERIFY(!MigrationOrderKeys::plan(db,"audited_source",&keys,&error));
    QVERIFY(error.contains("No free internal order number"));
    QVERIFY(q.exec("DROP TABLE order_registry"));
    QVERIFY(q.exec("CREATE TABLE order_registry(broken INTEGER)"));
    error.clear();
    QVERIFY(!MigrationOrderKeys::plan(db,"audited_source",&keys,&error));
    QVERIFY(!error.isEmpty());
}

void ReportsOrdersTests::workspaceSchemaPreparationRollback()
{
    const auto connection = QUuid::createUuid().toString();
    const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec("CREATE TABLE caller_marker(id INTEGER PRIMARY KEY)"));
    QVERIFY(query.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,"
                       "discount_mode TEXT GENERATED ALWAYS AS ('fixed') VIRTUAL)"));
    QVERIFY(db.transaction());
    QVERIFY(query.exec("INSERT INTO caller_marker VALUES(1)"));
    QVERIFY(!ensureWorkspaceSchema(db));
    QVERIFY(!db.tables().contains("masters"));
    QVERIFY(query.exec("SELECT id FROM caller_marker"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    query.finish();
    QVERIFY(query.exec("DROP TABLE work_orders"));
    QVERIFY(query.exec("CREATE TABLE additional_expenses(id INTEGER PRIMARY KEY)"));
    QVERIFY(!ensureWorkspaceSchema(db));
    QVERIFY(!db.tables().contains("work_order_service_attachments"));
    QVERIFY(!db.tables().contains("work_order_part_attachments"));
    QVERIFY(query.exec("DROP TABLE additional_expenses"));
    QVERIFY(ensureWorkspaceSchema(db));
    QVERIFY(db.tables().contains("masters"));
    QVERIFY(db.rollback());
    QVERIFY(!db.tables().contains("masters"));
    QVERIFY(query.exec("SELECT COUNT(*) FROM caller_marker"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

void ReportsOrdersTests::migrationServiceRoundTrip_data()
{
    QTest::addColumn<QString>("targetState");
    for(const auto& state:QStringList{"fresh", "initialized-closed", "initialized-live", "active-transaction", "restore-after-failure", "populated", "populated-key-collision", "populated-child-key-collision", "populated-child-only-collision", "populated-archive-routing", "populated-legacy-routing", "populated-legacy-conflict", "populated-historical-child-uid", "populated-historical-child-uid-refusal", "populated-historical-paid", "populated-historical-service-link", "populated-retained-stores", "populated-retained-rollback", "populated-repeat-open", "populated-conflicting-parent"})
        QTest::newRow(qPrintable(state))<<state;
}

void ReportsOrdersTests::migrationServiceRoundTrip()
{
    QFETCH(QString,targetState);
    const bool historicalConflict=targetState.startsWith("populated-historical-");
    const bool historicalRefusal=targetState=="populated-historical-child-uid-refusal";
    const bool historicalUidChange=targetState.startsWith("populated-historical-child-uid");
    const auto previousActor=ActivityAuditService::currentActor();
    const auto restoreActor=qScopeGuard([&]{if(historicalConflict)ActivityAuditService::setCurrentActor(previousActor);});
    const auto currentChildError=[&](const QString& phase) -> QString {
        const auto name=QUuid::createUuid().toString();
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);
        const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
        db.setDatabaseName(AppRuntime::activeDbFilePath());db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if(!db.open())return phase+": "+db.lastError().text();
        QSqlQuery q(db);
        if(!q.exec("SELECT part_uid,paid,service_id FROM work_order_parts WHERE order_uid='existing-target-order'") || !q.next())
            return phase+": no readable current child: "+q.lastError().text();
        const QString uid=historicalUidChange ? "current-target-part" : "existing-target-part";
        const auto paid=targetState=="populated-historical-paid" ? 1 : 0;
        const auto service=targetState=="populated-historical-service-link" ? 200 : 20;
        if(q.value(0).toString()!=uid || q.value(1).toInt()!=paid || q.value(2).toInt()!=service)
            return phase+QString(": got %1/%2/%3, expected %4/%5/%6").arg(q.value(0).toString()).arg(q.value(1).toInt()).arg(q.value(2).toInt()).arg(uid).arg(paid).arg(service);
        if(q.next() || q.lastError().isValid())return phase+": ambiguous current child";
        return {};
    };
    const bool legacyRouting=targetState=="populated-legacy-routing" || targetState=="populated-legacy-conflict";
    const bool archiveConflict=targetState=="populated-legacy-conflict";
    const bool archiveRouting=targetState=="populated-archive-routing" || legacyRouting;
    const bool retainedStores=targetState=="populated-retained-stores" || targetState=="populated-retained-rollback";
    QMap<QString,QByteArray> priorSealed;
    const bool childOnlyCollision=targetState=="populated-child-only-collision";
    const bool childCollision=targetState=="populated-child-key-collision" || targetState=="populated-repeat-open" || childOnlyCollision || retainedStores || archiveRouting || historicalConflict;
    const bool keyedSource=targetState=="populated-key-collision" || childCollision;
    const auto childParent=qEnvironmentVariable("CS_TEST_MIGRATION_PARENT");
    QTemporaryDir root(QDir(childParent.isEmpty()?QDir::tempPath():childParent).filePath("service-XXXXXX")); QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName=QCoreApplication::applicationName();
    const auto oldOrg=QCoreApplication::organizationName();
    const auto oldDomain=QCoreApplication::organizationDomain();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto cleanup=qScopeGuard([&]{
        Database::close(); QSettings().clear(); QSettings().sync();
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrg); QCoreApplication::setOrganizationDomain(oldDomain);
        QStandardPaths::setTestModeEnabled(oldTest);
    });
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Service-",root.path()));
    QCoreApplication::setOrganizationName("Safety-Migration-Service");
    QCoreApplication::setOrganizationDomain("safety.invalid");
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    QVERIFY(QDir().mkpath(root.filePath("source")));
    const auto sourcePath=root.filePath("source/service_center.db");
    {
        const QString name="service-roundtrip-source";
        auto db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(sourcePath);
        const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
        QVERIFY(db.open()); QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT,status TEXT,works_sum REAL,parts_sum REAL,master_id INTEGER)",
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL,title TEXT)",
            "CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,part_uid TEXT,name TEXT)",
            "CREATE TABLE masters(id INTEGER PRIMARY KEY,name TEXT)",
            "INSERT INTO work_orders VALUES(7,'migration-service-order',2024,3,'2024-06-15','closed',0,0,1)",
            "INSERT INTO masters VALUES(1,'Safety Master')"
        }) QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        if (keyedSource || targetState=="populated-conflicting-parent") {
            QVERIFY(q.exec("ALTER TABLE work_order_services ADD COLUMN service_uid TEXT"));
            QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN service_id INTEGER"));
            QVERIFY(q.exec("INSERT INTO work_order_services VALUES(20,7,'migration-service-order',1,0,0,'Source work','source-service-uid')"));
            QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(21,7,'migration-service-order','source-part-uid','Source part',20)"));
            if (targetState=="populated-conflicting-parent") {
                QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN work_order INTEGER"));
                QVERIFY(q.exec("UPDATE work_order_parts SET work_order=77"));
            }
        }
    }
    QFile source(sourcePath); QVERIFY(source.open(QIODevice::ReadOnly));
    const auto original=source.readAll();source.close();
    const bool populated = targetState.startsWith("populated");
    const int existingKey = (targetState=="populated" || childOnlyCollision) ? 17 : 7;
    QString setupError;
    if(targetState!="fresh") {
        QVERIFY2(Database::prepareRuntime(&setupError),qPrintable(setupError));
        QVERIFY2(Database::openPrepared(&setupError),qPrintable(setupError));
        QVERIFY2(Database::runStartupSchemaCheck(&setupError),qPrintable(setupError));
        if (targetState=="restore-after-failure" || targetState=="active-transaction") {
            auto owner = Database::db();
            if (targetState=="active-transaction") QVERIFY(owner.transaction());
            QSqlQuery sentinel(owner);
            QVERIFY(sentinel.exec("INSERT INTO settings(key,value) VALUES('migration.fixture','owner data')"));
        }
        if (populated) {
            QSqlQuery setup(Database::db());
            QVERIFY2(setup.exec(QString("INSERT INTO work_orders(number,order_uid,order_year,order_number,opened_at,status,plate,plate_norm,car,client,master_id) "
                "VALUES(%1,'existing-target-order',2023,3,'2023-06-15','open','','','Target car','Target client','m1')").arg(existingKey)),qPrintable(setup.lastError().text()));
            QVERIFY2(setup.exec(QString("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,name,qty,price,amount) "
                "VALUES(1001,'existing-target-part',%1,'existing-target-order','Target part',2,19.25,38.50)").arg(existingKey)),qPrintable(setup.lastError().text()));
            if(childCollision) {
                QVERIFY(setup.exec(QString("INSERT INTO work_order_services(id,service_uid,order_uid,order_number,title,qty,price,amount) VALUES(20,'target-service','existing-target-order',%1,'Target work',1,12.50,12.50)").arg(existingKey)));
                QVERIFY(setup.exec("UPDATE work_order_parts SET id=21,service_id=20 WHERE id=1001"));
            }
            QVERIFY(setup.exec("INSERT INTO settings(key,value) VALUES('migration.target.sentinel','existing setting')"));
        }
        if(targetState=="initialized-closed") Database::close();
    }
    if(historicalConflict) {
        {
            auto active=Database::db();QString detail;QSqlQuery baseline(active);
            QVERIFY(StaffAccountService::ensureSchema(active,&detail));QVERIFY(AccessControlService::ensureSchema(active,&detail));
            QVERIFY(baseline.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('history-owner','History owner','owner',1)"));
            ActivityAuditService::setCurrentActor({"history-owner","History owner","owner","test"});
            QVERIFY2(LegacyMutationLogSupport::logMutation(active,"order","delete",919,"removed-order","919","{}","{}",&detail),qPrintable(detail));
            QVERIFY(baseline.exec("ALTER TABLE parts_catalog ADD COLUMN part_id INTEGER"));
            QVERIFY(baseline.exec("INSERT INTO parts_catalog(id,part_number,name,price,part_id) VALUES(9000,'SYN-HISTORY','Historical catalogue',38.50,21)"));
            TransactionContext tx(active,"HistoricalChildrenBaseline");QVERIFY(tx.begin("test",&detail));
            QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(active,existingKey,&detail),qPrintable(detail));
            QVERIFY(tx.commit("test",&detail));
        }
        QVERIFY2(Database::replayPendingOrderRegistryRefresh(&setupError),qPrintable(setupError));
        Database::close();
        const auto historical=AppRuntime::archiveDbFilePath();
        QVERIFY(QFile::remove(historical) || !QFileInfo::exists(historical));
        QVERIFY(QFile::copy(AppRuntime::activeDbFilePath(),historical));
        QVERIFY2(EncryptedStorage::encryptPlainFileToSealed(historical,AppRuntime::sealedArchiveDbFilePath(),&setupError),qPrintable(setupError));
        {QFile sealed(AppRuntime::sealedArchiveDbFilePath());QVERIFY(sealed.open(QIODevice::ReadOnly));priorSealed.insert("archive",sealed.readAll());}
        QVERIFY2(Database::prepareRuntime(&setupError),qPrintable(setupError));
        QVERIFY2(Database::openPrepared(&setupError),qPrintable(setupError));
        {
            auto active=Database::db();QSqlQuery edit(active);
            if(historicalUidChange)
                QVERIFY(edit.exec("UPDATE work_order_parts SET part_uid='current-target-part' WHERE part_uid='existing-target-part'"));
            else if(targetState=="populated-historical-paid")
                QVERIFY(edit.exec("UPDATE work_order_parts SET paid=1 WHERE part_uid='existing-target-part'"));
            else {
                QVERIFY(edit.exec(QString("INSERT INTO work_order_services(id,service_uid,order_uid,order_number,title,qty,price,amount) VALUES(200,'other-current-service','existing-target-order',%1,'Other work',1,0,0)").arg(existingKey)));
                QVERIFY(edit.exec("UPDATE work_order_parts SET service_id=200 WHERE part_uid='existing-target-part'"));
            }
            QVERIFY(edit.exec("UPDATE parts_catalog SET name='Current catalogue',price=40 WHERE id=9000"));
            TransactionContext tx(active,"HistoricalChildrenCurrent");QVERIFY(tx.begin("test",&setupError));
            QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(active,existingKey,&setupError),qPrintable(setupError));
            QVERIFY(tx.commit("test",&setupError));
        }
        QVERIFY2(Database::replayPendingOrderRegistryRefresh(&setupError),qPrintable(setupError));
    }
    if(archiveRouting) {
        const auto clearActor=qScopeGuard([]{ActivityAuditService::clearCurrentActor();});
        {
        auto active=Database::db();QSqlQuery setup(active);
        QVERIFY(StaffAccountService::ensureSchema(active,&setupError));
        QVERIFY(AccessControlService::ensureSchema(active,&setupError));
        QVERIFY(setup.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('migration-owner','Owner','owner',1)"));
        ActivityAuditService::setCurrentActor({"migration-owner","Owner","owner","test"});
        QVERIFY(setup.exec("UPDATE work_orders SET status='closed',closed_at='2023-06-16' WHERE order_uid='existing-target-order'"));
        {TransactionContext tx(active,"MigrationArchiveBaseline");QVERIFY(tx.begin("test",&setupError));
         QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(active,existingKey,&setupError),qPrintable(setupError));
         QVERIFY(tx.commit("test",&setupError));}
        QVERIFY2(Database::replayPendingOrderRegistryRefresh(&setupError),qPrintable(setupError));
        QVERIFY2(OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(active,existingKey,&setupError),qPrintable(setupError));
        QVERIFY(setup.exec("SELECT COUNT(*) FROM work_orders"));QVERIFY(setup.next());QCOMPARE(setup.value(0).toInt(),0);setup.finish();
        QVERIFY(setup.exec("SELECT COUNT(*) FROM work_order_parts"));QVERIFY(setup.next());QCOMPARE(setup.value(0).toInt(),0);setup.finish();
        }
        // The order is genuinely archive-only before migration, not a hydrated
        // active duplicate. Exercise replacement of the primary archive too.
        const auto previous=archiveShardFilePathForOrder(existingKey);
        Database::close();
        const auto oldPath=legacyRouting ? AppRuntime::legacyDbFilePath() : AppRuntime::archiveDbFilePath();
        const auto sealedPath=legacyRouting ? AppRuntime::sealedLegacyDbFilePath() : AppRuntime::sealedArchiveDbFilePath();
        QVERIFY(QFile::remove(oldPath) || !QFileInfo::exists(oldPath));
        QVERIFY(QFile::copy(previous,oldPath));
        if(legacyRouting) {
            const QString connection="independent-legacy-fixture";
            {auto legacy=QSqlDatabase::addDatabase("QSQLITE",connection);legacy.setDatabaseName(oldPath);QVERIFY(legacy.open());QSqlQuery fixture(legacy);
             if(archiveConflict) {
                QVERIFY(fixture.exec("UPDATE work_order_parts SET amount=99.99"));
             } else {
                QVERIFY(fixture.exec("UPDATE work_orders SET number=40,order_uid='legacy-only-order',order_year=2022,opened_at='2022-06-15',closed_at='2022-06-16'"));
                QVERIFY(fixture.exec("UPDATE work_order_services SET id=200,order_number=40,order_uid='legacy-only-order',service_uid='legacy-only-service'"));
                QVERIFY(fixture.exec("UPDATE work_order_parts SET id=201,order_number=40,order_uid='legacy-only-order',part_uid='legacy-only-part',service_id=200"));
                QVERIFY(fixture.exec("UPDATE service_masters SET service_id=200 WHERE service_id=20"));
                QVERIFY(fixture.exec("ALTER TABLE work_order_parts ADD COLUMN historic_note BLOB"));
                QVERIFY(fixture.exec("UPDATE work_order_parts SET historic_note=X'AB00CD'"));
                QVERIFY(fixture.exec("CREATE TABLE historical_adjustments(id INTEGER PRIMARY KEY,order_uid TEXT,cents INTEGER,note BLOB)"));
                QVERIFY(fixture.exec("INSERT INTO historical_adjustments VALUES(1,'legacy-only-order',7250,X'0002FF')"));
             }
             legacy.close();}
            QSqlDatabase::removeDatabase(connection);
        }
        QVERIFY2(EncryptedStorage::encryptPlainFileToSealed(oldPath,sealedPath,&setupError),qPrintable(setupError));
        if(archiveConflict){QFile sealed(sealedPath);QVERIFY(sealed.open(QIODevice::ReadOnly));priorSealed.insert("legacy",sealed.readAll());}
    }
    if(retainedStores) {
        Database::close();
        const QMap<QString,QPair<QString,QString>> storePaths={
            {"archive",{AppRuntime::archiveDbFilePath(),AppRuntime::sealedArchiveDbFilePath()}},
            {"legacy",{AppRuntime::legacyDbFilePath(),AppRuntime::sealedLegacyDbFilePath()}},
            {"registry",{AppRuntime::registryDbFilePath(),AppRuntime::sealedRegistryDbFilePath()}}};
        for(auto it=storePaths.cbegin();it!=storePaths.cend();++it) {
            const auto path=it.value().first;
            if(!QFileInfo::exists(path))QVERIFY(QFile::copy(AppRuntime::activeDbFilePath(),path));
            const auto connection="retention-fixture-"+it.key();
            {auto store=QSqlDatabase::addDatabase("QSQLITE",connection);store.setDatabaseName(path);QVERIFY(store.open());
             QSqlQuery marker(store);QVERIFY(marker.exec("CREATE TABLE retention_sentinel(uid TEXT,cents INTEGER)"));
             marker.prepare("INSERT INTO retention_sentinel VALUES(?,?)");marker.addBindValue(it.key()+"-old-uid");marker.addBindValue(7250);
             QVERIFY(marker.exec());store.close();}
            QSqlDatabase::removeDatabase(connection);
            QString sealError;QVERIFY2(EncryptedStorage::encryptPlainFileToSealed(path,it.value().second,&sealError),qPrintable(sealError));
            QFile sealed(it.value().second);QVERIFY(sealed.open(QIODevice::ReadOnly));priorSealed.insert(it.key(),sealed.readAll());
        }
    }
    if(historicalConflict) {
        const auto issue=currentChildError("before service");QVERIFY2(issue.isEmpty(),qPrintable(issue));
    }
    QEventLoop ownerLoop;
    LegacyMigrationResult result; QString error; QStringList stages; bool ok=false;
    std::thread worker([&]{
        ok=LegacyMigrationService::executeLegacyImport(sourcePath,&result,&error,
            [&](const LegacyImportProgressState& progress){
                stages<<progress.stageKey;
                if(targetState=="populated-retained-stores" && !qEnvironmentVariable("CS_TEST_MIGRATION_CRASH_MARKER").isEmpty()
                    && progress.stageKey==qEnvironmentVariable("CS_TEST_MIGRATION_CRASH_STAGE")) {
                    QString pending,detail;
                    if(!MigrationRestoreJournal::pending(&pending,&detail) || pending.isEmpty())std::_Exit(91);
                    QJsonObject hashes;
                    for(auto it=priorSealed.cbegin();it!=priorSealed.cend();++it)
                        hashes.insert(it.key(),QString::fromLatin1(QCryptographicHash::hash(it.value(),QCryptographicHash::Sha256).toHex()));
                    const QJsonObject evidence{{"root",root.path()},{"name",QCoreApplication::applicationName()},
                        {"organization",QCoreApplication::organizationName()},{"domain",QCoreApplication::organizationDomain()},
                        {"settingsFormat",int(QSettings().format())},{"settingsFile",QSettings().fileName()},
                        {"snapshot",pending},{"source",sourcePath},{"hashes",hashes}};
                    QSaveFile marker(qEnvironmentVariable("CS_TEST_MIGRATION_CRASH_MARKER"));
                    const auto bytes=QJsonDocument(evidence).toJson();
                    if(!marker.open(QIODevice::WriteOnly) || marker.write(bytes)!=bytes.size() || !marker.commit())std::_Exit(92);
                    std::_Exit(87); // Kill the real service without stack unwinding or rollback.
                }
                if(targetState=="populated-retained-rollback" && progress.stageKey=="execute_reopen")
                    QFile::rename(sourcePath,sourcePath+".held");
                if ((targetState=="restore-after-failure" || historicalRefusal) && progress.stageKey=="execute_import")
                    QFile::rename(sourcePath,sourcePath+".held");
            });
        Database::close();
        QMetaObject::invokeMethod(&ownerLoop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    ownerLoop.exec();
    worker.join();
    if (targetState=="restore-after-failure" || targetState=="populated-retained-rollback" || historicalRefusal) QVERIFY(QFile::rename(sourcePath+".held",sourcePath));
    QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),original); source.close();
    if(historicalConflict && historicalRefusal) {
        for(int attempt=0;attempt<2;++attempt) {
            QVERIFY(!ok);QVERIFY(!error.isEmpty());
            QVERIFY(!stages.contains("execute_reopen"));
            {
                QFile history(QDir(AppRuntime::securityDir()).filePath("legacy_import_history.json"));QVERIFY(history.open(QIODevice::ReadOnly));
                const auto entries=QJsonDocument::fromJson(history.readAll()).array();QVERIFY(!entries.isEmpty());
                const auto failed=entries.last().toObject();QCOMPARE(failed.value("status").toString(),QString("failed"));
                QVERIFY(!failed.value("note").toString().isEmpty());
                const auto backup=failed.value("snapshot_dir").toString();QVERIFY(!backup.isEmpty());QVERIFY(QFileInfo::exists(QDir(backup).filePath("manifest.json")));
            }
            {QFile sealed(AppRuntime::sealedArchiveDbFilePath());QVERIFY(sealed.open(QIODevice::ReadOnly));QCOMPARE(sealed.readAll(),priorSealed.value("archive"));}
            {const auto issue=currentChildError("after refusal");QVERIFY2(issue.isEmpty(),qPrintable(issue));}
            Database::close();
            QVERIFY2(Database::prepareRuntime(&setupError),qPrintable(setupError));
            {const auto issue=currentChildError("after prepare");QVERIFY2(issue.isEmpty(),qPrintable(issue));}
            QVERIFY2(Database::openPrepared(&setupError),qPrintable(setupError));
            {const auto issue=currentChildError("after open");QVERIFY2(issue.isEmpty(),qPrintable(issue));}
            QVERIFY2(Database::runStartupMigrationCheck(&setupError),qPrintable(setupError));
            {const auto issue=currentChildError("after startup migration");QVERIFY2(issue.isEmpty(),qPrintable(issue));}
            QVERIFY2(Database::runStartupRegistryRefresh(&setupError),qPrintable(setupError));
            {const auto issue=currentChildError("after registry refresh");QVERIFY2(issue.isEmpty(),qPrintable(issue));}
            QVERIFY2(Database::ensureOrderAvailableInActiveByUid("existing-target-order",&setupError),qPrintable(setupError));
            {
                QSqlQuery current(Database::db());
                QVERIFY(current.exec("SELECT COUNT(*),ROUND(SUM(works_sum)*100),ROUND(SUM(parts_sum)*100) FROM work_orders"));
                QVERIFY(current.next());QCOMPARE(current.value(0).toInt(),1);QCOMPARE(current.value(1).toInt(),1250);QCOMPARE(current.value(2).toInt(),3850);current.finish();
                QVERIFY(current.exec("SELECT part_uid,paid,service_id,ROUND(amount*100) FROM work_order_parts WHERE order_uid='existing-target-order'"));
                QVERIFY(current.next());QCOMPARE(current.value(0).toString(),historicalUidChange ? QString("current-target-part") : QString("existing-target-part"));
                QCOMPARE(current.value(1).toInt(),targetState=="populated-historical-paid" ? 1 : 0);
                QCOMPARE(current.value(2).toInt(),targetState=="populated-historical-service-link" ? 200 : 20);
                QCOMPARE(current.value(3).toInt(),3850);QVERIFY(!current.next());current.finish();
            }
            {
                const QString connection="historical-child-original";
                {auto originalDb=QSqlDatabase::addDatabase("QSQLITE",connection);originalDb.setDatabaseName(AppRuntime::archiveDbFilePath());originalDb.setConnectOptions("QSQLITE_OPEN_READONLY");QVERIFY(originalDb.open());QSqlQuery prior(originalDb);
                 QVERIFY(prior.exec("SELECT part_uid,paid,service_id,ROUND(amount*100) FROM work_order_parts WHERE order_uid='existing-target-order'"));QVERIFY(prior.next());
                 QCOMPARE(prior.value(0).toString(),QString("existing-target-part"));QCOMPARE(prior.value(1).toInt(),0);QCOMPARE(prior.value(2).toInt(),20);QCOMPARE(prior.value(3).toInt(),3850);prior.finish();originalDb.close();}
                QSqlDatabase::removeDatabase(connection);
            }
            if(attempt==0) {
                stages.clear();error.clear();QEventLoop repeatLoop;
                std::thread repeat([&]{ok=LegacyMigrationService::executeLegacyImport(sourcePath,&result,&error,[&](const LegacyImportProgressState& progress){stages<<progress.stageKey;if(progress.stageKey=="execute_import")QFile::rename(sourcePath,sourcePath+".held");});Database::close();QMetaObject::invokeMethod(&repeatLoop,&QEventLoop::quit,Qt::QueuedConnection);});
                repeatLoop.exec();repeat.join();QVERIFY(QFile::rename(sourcePath+".held",sourcePath));
            }
        }
        return;
    }
    if(historicalConflict) {
        QVERIFY2(ok,qPrintable(error));
        QString historicalId;
        for(int boot=0;boot<2;++boot) {
            Database::close();QVERIFY2(Database::prepareRuntime(&setupError),qPrintable(setupError));QVERIFY2(Database::openPrepared(&setupError),qPrintable(setupError));
            QVERIFY2(Database::runStartupMigrationCheck(&setupError),qPrintable(setupError));QVERIFY2(Database::runStartupRegistryRefresh(&setupError),qPrintable(setupError));
            QVERIFY2(Database::ensureOrderAvailableInActiveByUid("existing-target-order",&setupError),qPrintable(setupError));
            QList<HistoricalVersionsService::Version> versions;QVERIFY2(HistoricalVersionsService::versions("existing-target-order",&versions,&setupError),qPrintable(setupError));
            historicalId.clear();for(const auto& version:versions)if(version.origin==AppRuntime::archiveDbFilePath())historicalId=version.id;QVERIFY(!historicalId.isEmpty());
            HistoricalVersionsService::Page old,current;
            QVERIFY2(HistoricalVersionsService::read("existing-target-order",historicalId,"work_order_parts",0,100,&old,&setupError,"existing-target-part"),qPrintable(setupError));QCOMPARE(old.rows.size(),1);
            auto value=[](const HistoricalVersionsService::Page& page,const QString& field){return page.rows[0].value(page.columns.indexOf(field));};
            QCOMPARE(value(old,"paid").toInt(),0);QCOMPARE(value(old,"service_id").toInt(),20);QCOMPARE(qRound(value(old,"amount").toDouble()*100),3850);
            const auto currentUid=historicalUidChange ? QString("current-target-part") : QString("existing-target-part");
            QVERIFY2(HistoricalVersionsService::read("existing-target-order","current","work_order_parts",0,100,&current,&setupError,currentUid),qPrintable(setupError));QCOMPARE(current.rows.size(),1);
            QCOMPARE(value(current,"paid").toInt(),targetState=="populated-historical-paid" ? 1 : 0);QCOMPARE(value(current,"service_id").toInt(),targetState=="populated-historical-service-link" ? 200 : 20);QCOMPARE(qRound(value(current,"amount").toDouble()*100),3850);
            if(historicalUidChange){HistoricalVersionsService::Page missing;QVERIFY(HistoricalVersionsService::read("existing-target-order","current","work_order_parts",0,100,&missing,&setupError,"existing-target-part"));QVERIFY(missing.rows.isEmpty());}
            HistoricalVersionsService::Page oldCatalog,currentCatalog;
            QVERIFY2(HistoricalVersionsService::read({},historicalId,"parts_catalog",0,100,&oldCatalog,&setupError),qPrintable(setupError));
            QVERIFY2(HistoricalVersionsService::read({},"current","parts_catalog",0,100,&currentCatalog,&setupError),qPrintable(setupError));
            auto catalogPrice=[](const HistoricalVersionsService::Page& page){for(const auto& row:page.rows)if(row.value(page.columns.indexOf("id")).toInt()==9000)return qRound(row.value(page.columns.indexOf("price")).toDouble()*100);return -1;};
            QCOMPARE(catalogPrice(oldCatalog),3850);QCOMPARE(catalogPrice(currentCatalog),4000);
            HistoricalVersionsService::Page locator;
            QVERIFY2(HistoricalVersionsService::read({},historicalId,"order_registry",0,5,&locator,&setupError,"existing-target-order"),qPrintable(setupError));
            QCOMPARE(locator.rows.size(),1);
            QCOMPARE(locator.rows.first().value(locator.columns.indexOf("order_uid")).toString(),QString("existing-target-order"));
            QVERIFY2(HistoricalVersionsService::read({},historicalId,"order_registry",0,5,&locator,&setupError,"nonexistent-order"),qPrintable(setupError));
            QVERIFY(locator.rows.isEmpty());
            HistoricalVersionsService::Page journal;
            QVERIFY2(HistoricalVersionsService::read({},historicalId,"legacy_mutation_log",0,100,&journal,&setupError),qPrintable(setupError));
            const int ownerCol=journal.columns.indexOf("order_uid"),actionCol=journal.columns.indexOf("action");
            QVERIFY(ownerCol>=0 && actionCol>=0);
            QVERIFY(std::any_of(journal.rows.cbegin(),journal.rows.cend(),[&](const auto& row){
                return row.at(ownerCol).toString()=="removed-order" && row.at(actionCol).toString()=="delete";
            }));
            {QSqlQuery sums(Database::db());QVERIFY(sums.exec("SELECT COUNT(*),ROUND(SUM(amount)*100) FROM work_order_parts WHERE order_uid='existing-target-order'"));QVERIFY(sums.next());QCOMPARE(sums.value(0).toInt(),1);QCOMPARE(sums.value(1).toInt(),3850);}
            if(boot==0) {
                std::unique_ptr<QDialog> viewer(HistoricalVersionsDialog::create(nullptr,"existing-target-order",&setupError));QVERIFY2(viewer,qPrintable(setupError));
                auto* sources=viewer->findChild<QComboBox*>("historySource");auto* table=viewer->findChild<QComboBox*>("historyTable");auto* rows=viewer->findChild<QTableWidget*>("historyRows");auto* uid=viewer->findChild<QLineEdit*>("historyRecordUid");auto* find=viewer->findChild<QPushButton*>("historyFind");QVERIFY(sources && table && rows && uid && find);
                sources->setCurrentIndex(sources->findData(historicalId));table->setCurrentIndex(table->findData("work_order_parts"));uid->setText("existing-target-part");find->click();QCOMPARE(rows->rowCount(),1);QCOMPARE(rows->editTriggers(),QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
                sources->setCurrentIndex(sources->findData("current"));table->setCurrentIndex(table->findData("work_order_parts"));uid->setText(currentUid);find->click();QCOMPARE(rows->rowCount(),1);
            }
        }
        {
            const auto actor=ActivityAuditService::currentActor();const auto restore=qScopeGuard([&]{ActivityAuditService::setCurrentActor(actor);});
            ActivityAuditService::setCurrentActor({"history-master","History master","master","test"});
            QList<HistoricalVersionsService::Version> denied;QString accessError;
            QVERIFY(!HistoricalVersionsService::versions("existing-target-order",&denied,&accessError));QVERIFY(denied.isEmpty());QVERIFY(!accessError.isEmpty());
            QVERIFY(!HistoricalVersionsService::versions({},&denied,&accessError));QVERIFY(denied.isEmpty());
            HistoricalVersionsService::Page journal;
            QVERIFY(!HistoricalVersionsService::read({},historicalId,"legacy_mutation_log",0,100,&journal,&accessError));
            QVERIFY(journal.rows.isEmpty());
            QVERIFY(!HistoricalVersionsService::read({},historicalId,"order_registry",0,5,&journal,&accessError,"existing-target-order"));
            QVERIFY(journal.rows.isEmpty());
        }
        QSqlQuery count(Database::db());QVERIFY(count.exec("SELECT COUNT(*) FROM historical_records"));QVERIFY(count.next());const auto historyCount=count.value(0).toLongLong();count.finish();
        stages.clear();error.clear();QEventLoop repeatLoop;
        std::thread repeat([&]{ok=LegacyMigrationService::executeLegacyImport(sourcePath,&result,&error,[&](const LegacyImportProgressState& progress){stages<<progress.stageKey;});Database::close();QMetaObject::invokeMethod(&repeatLoop,&QEventLoop::quit,Qt::QueuedConnection);});repeatLoop.exec();repeat.join();
        QVERIFY(!ok);QVERIFY(!stages.contains("execute_import"));QVERIFY(count.exec("SELECT COUNT(*) FROM historical_records"));QVERIFY(count.next());QCOMPARE(count.value(0).toLongLong(),historyCount);count.finish();
        return;
    }
    if(archiveConflict) {
        QVERIFY(!ok);QVERIFY2(error.contains("source and archive differ"),qPrintable(error));
        QVERIFY(!stages.contains("execute_reopen"));
        QFile sealed(AppRuntime::sealedLegacyDbFilePath());QVERIFY(sealed.open(QIODevice::ReadOnly));QCOMPARE(sealed.readAll(),priorSealed.value("legacy"));
        return;
    }
    if(targetState=="populated-retained-rollback") {
        QVERIFY(!ok);QVERIFY(!error.isEmpty());QVERIFY(stages.contains("execute_reopen"));
        const QMap<QString,QString> paths={{"archive",AppRuntime::sealedArchiveDbFilePath()},
            {"legacy",AppRuntime::sealedLegacyDbFilePath()},{"registry",AppRuntime::sealedRegistryDbFilePath()}};
        for(auto it=paths.cbegin();it!=paths.cend();++it){QFile file(it.value());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),priorSealed.value(it.key()));}
        return;
    }
    if (targetState=="populated-conflicting-parent") {
        QVERIFY(!ok); QVERIFY2(error.contains("Conflicting source parent references"),qPrintable(error));
        QVERIFY(!stages.contains("execute_reopen"));
        auto owner=QSqlDatabase::database(mainConnectionName(),false);
        QVERIFY(owner.isOpen()); QSqlQuery retained(owner);
        QVERIFY(retained.exec("SELECT order_uid FROM work_orders")); QVERIFY(retained.next());
        QCOMPARE(retained.value(0).toString(),QString("existing-target-order")); QVERIFY(!retained.next());
        QVERIFY(retained.exec("SELECT p.part_uid FROM work_order_parts p JOIN work_orders o ON o.number=p.order_number AND o.order_uid=p.order_uid"));
        QVERIFY(retained.next()); QCOMPARE(retained.value(0).toString(),QString("existing-target-part")); QVERIFY(!retained.next());
        return;
    }
    if (targetState=="active-transaction" || targetState=="restore-after-failure") {
        QVERIFY(!ok);
        QVERIFY2(!error.isEmpty(),qPrintable(error));
        auto owner = QSqlDatabase::database(mainConnectionName(),false);
        QVERIFY2(owner.isOpen(),qPrintable(error));
        QSqlQuery sentinel(owner);
        QVERIFY(sentinel.exec("SELECT value FROM settings WHERE key='migration.fixture'"));
        QVERIFY(sentinel.next()); QCOMPARE(sentinel.value(0).toString(),QString("owner data"));
        sentinel.finish();
        if (targetState=="active-transaction") {
            QVERIFY(!stages.contains("execute_import"));
            QVERIFY2(error.contains("active transaction"),qPrintable(error));
            QVERIFY(owner.rollback());
            QVERIFY(sentinel.exec("SELECT COUNT(*) FROM settings WHERE key='migration.fixture'"));
            QVERIFY(sentinel.next()); QCOMPARE(sentinel.value(0).toInt(),0);
        } else {
            QVERIFY(stages.contains("execute_import"));
            QVERIFY(!stages.contains("execute_reopen"));
        }
        return;
    }
    QVERIFY2(ok,qPrintable(error+" | stages="+stages.join(',')));
    if(legacyRouting) {
        const QString connection="legacy-materialized-evidence";
        {auto check=QSqlDatabase::addDatabase("QSQLITE",connection);check.setDatabaseName(AppRuntime::activeDbFilePath());check.setConnectOptions("QSQLITE_OPEN_READONLY");QVERIFY(check.open());QSqlQuery evidence(check);
         QVERIFY(evidence.exec("SELECT hex(historic_note) FROM work_order_parts WHERE part_uid='legacy-only-part'"));QVERIFY(evidence.next());QCOMPARE(evidence.value(0).toString(),QString("AB00CD"));evidence.finish();check.close();}
        QSqlDatabase::removeDatabase(connection);
    }
    if (targetState=="initialized-live") {
        QVERIFY(QSqlDatabase::contains(mainConnectionName()));
        auto owner = QSqlDatabase::database(mainConnectionName(),false);
        QVERIFY(owner.isOpen());
        QSqlQuery accessible(owner);
        QVERIFY(accessible.exec("SELECT order_uid FROM work_orders WHERE number=7"));
        QVERIFY(accessible.next());
        QCOMPARE(accessible.value(0).toString(),QString("migration-service-order"));
    }
    QVERIFY(stages.contains("execute_snapshot")); QVERIFY(stages.contains("execute_import"));
    QVERIFY(stages.contains("execute_reopen_ready"));
    QCOMPARE(result.sourceOrderCount,1); QCOMPARE(result.importedOrderCount,1);
    QCOMPARE(result.importedPartCount,keyedSource ? 1 : 0); QCOMPARE(result.importedMasterCount,1);
    QVERIFY(QFileInfo::exists(result.snapshotDirPath));
    QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),original); source.close();
    if (populated) {
        Database::close();
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error));
        QVERIFY2(Database::runStartupMigrationCheck(&error),qPrintable(error));
        if(archiveRouting) {
            QVERIFY2(Database::runStartupArchiveMaintenance(&error),qPrintable(error));
            QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
            for(const auto& uid:QStringList{"existing-target-order","migration-service-order"}) {
                QVERIFY2(Database::ensureOrderAvailableInActiveByUid(uid,&error),qPrintable(error));
                QVERIFY(Database::resolveOrderInternalNumberByUid(uid)>0);
            }
            QSqlQuery graph(Database::db());
            QVERIFY(graph.exec("SELECT p.part_uid,s.service_uid,ROUND(p.amount*100),ROUND(s.amount*100) FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id AND s.order_uid=p.order_uid WHERE p.order_uid='existing-target-order'"));
            QVERIFY(graph.next());QCOMPARE(graph.value(0).toString(),QString("existing-target-part"));
            QCOMPARE(graph.value(1).toString(),QString("target-service"));QCOMPARE(graph.value(2).toInt(),3850);QCOMPARE(graph.value(3).toInt(),1250);QVERIFY(!graph.next());graph.finish();
        }
        Database::close();
        if(archiveRouting) {
            // A second ordinary boot, with no migration/recovery invocation.
            QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
            QVERIFY2(Database::openPrepared(&error),qPrintable(error));
            QVERIFY2(Database::runStartupMigrationCheck(&error),qPrintable(error));
            QVERIFY2(Database::runStartupArchiveMaintenance(&error),qPrintable(error));
            QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
            QVERIFY2(Database::ensureOrderAvailableInActiveByUid("existing-target-order",&error),qPrintable(error));
            QVERIFY2(Database::ensureOrderAvailableInActiveByUid("migration-service-order",&error),qPrintable(error));
            if(legacyRouting) {
                QVERIFY2(Database::ensureOrderAvailableInActiveByUid("legacy-only-order",&error),qPrintable(error));
                QSqlQuery history(Database::db());
                QVERIFY(history.exec("SELECT p.part_uid,s.service_uid,ROUND(p.amount*100) FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id AND s.order_uid=p.order_uid WHERE p.order_uid='legacy-only-order'"));
                QVERIFY(history.next());QCOMPARE(history.value(0).toString(),QString("legacy-only-part"));QCOMPARE(history.value(1).toString(),QString("legacy-only-service"));QCOMPARE(history.value(2).toInt(),3850);history.finish();
                QList<HistoricalRecordStore::Source> sources;QVERIFY2(HistoricalRecordStore::sources(Database::db(),"legacy-only-order",&sources,&error),qPrintable(error));
                QString version;for(const auto& source:sources)if(source.origin==AppRuntime::legacyDbFilePath())version=source.id;QVERIFY(!version.isEmpty());
                QList<HistoricalRecordStore::Record> records;QVERIFY2(HistoricalRecordStore::read(Database::db(),version,"historical_adjustments","legacy-only-order",{},0,100,&records,&error),qPrintable(error));QCOMPARE(records.size(),1);
                QCOMPARE(records[0].values.value(records[0].columns.indexOf("cents")).toInt(),7250);QCOMPARE(records[0].values.value(records[0].columns.indexOf("note")).toByteArray(),QByteArray::fromHex("0002FF"));
                QVERIFY(history.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='historical_adjustments'"));QVERIFY(history.next());QCOMPARE(history.value(0).toInt(),0);
            }
            Database::close();
        }
    }
    const QString name="service-roundtrip-result";
    auto db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(AppRuntime::activeDbFilePath());
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    QVERIFY2(db.open(),qPrintable(db.lastError().text()));
    QSqlQuery q(db);
    if (populated) {
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),legacyRouting ? 3 : 2);
        QVERIFY(q.exec("SELECT order_year,order_number,car FROM work_orders WHERE order_uid='existing-target-order'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2023); QCOMPARE(q.value(1).toInt(),3);
        QCOMPARE(q.value(2).toString(),QString("Target car")); QVERIFY(!q.next());
        QVERIFY(q.exec("SELECT p.name,ROUND(p.amount*100) FROM work_order_parts p JOIN work_orders o ON o.number=p.order_number AND o.order_uid=p.order_uid "
                       "WHERE o.order_uid='existing-target-order' AND p.part_uid='existing-target-part'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("Target part")); QCOMPARE(q.value(1).toInt(),3850); QVERIFY(!q.next());
        QVERIFY(q.exec("SELECT value FROM settings WHERE key='migration.target.sentinel'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("existing setting"));
    }
    if (keyedSource) {
        QVERIFY(q.exec("SELECT number,order_year,order_number FROM work_orders WHERE order_uid='migration-service-order'"));
        QVERIFY(q.next()); const auto mapped=q.value(0).toInt();
        if(childOnlyCollision) QCOMPARE(mapped,7); else QVERIFY(mapped!=7);
        QCOMPARE(q.value(1).toInt(),2024); QCOMPARE(q.value(2).toInt(),3);
        QVERIFY(q.exec("SELECT p.order_number,s.order_number,s.order_uid,s.service_uid FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id "
                       "WHERE p.part_uid='source-part-uid' AND p.order_uid='migration-service-order'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),mapped); QCOMPARE(q.value(1).toInt(),mapped);
        QCOMPARE(q.value(2).toString(),QString("migration-service-order")); QCOMPARE(q.value(3).toString(),QString("source-service-uid")); QVERIFY(!q.next());
        QVERIFY(q.exec("SELECT source_payload,target_key FROM migration_record_map WHERE source_table='work_orders' AND target_order_uid='migration-service-order'"));
        QVERIFY(q.next()); QMap<QString,QVariant> originalRow,targetKey;
        QDataStream sourceStream(q.value(0).toByteArray()),targetStream(q.value(1).toByteArray());
        sourceStream.setVersion(QDataStream::Qt_6_0);targetStream.setVersion(QDataStream::Qt_6_0);
        sourceStream>>originalRow;targetStream>>targetKey;
        QCOMPARE(sourceStream.status(),QDataStream::Ok); QCOMPARE(targetStream.status(),QDataStream::Ok);
        QCOMPARE(originalRow.value("number").toInt(),7); QCOMPARE(targetKey.value("number").toInt(),mapped);
    }
    if(childCollision) {
        QVERIFY(q.exec("SELECT p.id,s.id,ROUND(s.amount*100),p.order_uid,s.service_uid FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.part_uid='existing-target-part'"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),21);QCOMPARE(q.value(1).toInt(),20);QCOMPARE(q.value(2).toInt(),1250);
        QCOMPARE(q.value(3).toString(),QString("existing-target-order"));QCOMPARE(q.value(4).toString(),QString("target-service"));QVERIFY(!q.next());
        QVERIFY(q.exec("SELECT p.id,s.id,ROUND(p.amount*100),ROUND(s.amount*100) FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.part_uid='source-part-uid'"));
        QVERIFY(q.next());QVERIFY(q.value(0).toInt()!=21);QVERIFY(q.value(1).toInt()!=20);
        QCOMPARE(q.value(2).toInt(),0);QCOMPARE(q.value(3).toInt(),0);QVERIFY(!q.next());
    }
    QVERIFY2(q.exec(populated ? "SELECT order_uid,order_year,order_number FROM work_orders WHERE order_uid='migration-service-order'" : "SELECT order_uid,order_year,order_number FROM work_orders"),qPrintable(q.lastError().text()));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("migration-service-order"));
    QCOMPARE(q.value(1).toInt(),2024); QCOMPARE(q.value(2).toInt(),3); QVERIFY(!q.next());
    if(retainedStores) {
        const QDir receipts(QDir(AppRuntime::securityDir()).filePath("migration_retained_stores"));
        QList<HistoricalRecordStore::Source> versions;
        QVERIFY2(HistoricalRecordStore::sources(Database::db(), {}, &versions, &error), qPrintable(error));
        int registryVersions = 0;
        for (const auto& version : versions) {
            if (version.origin != AppRuntime::registryDbFilePath()) continue;
            ++registryVersions;
            QList<HistoricalRecordStore::Record> rows;
            QVERIFY2(HistoricalRecordStore::read(Database::db(), version.id, "retention_sentinel", {}, {}, 0, 2, &rows, &error), qPrintable(error));
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first().values.at(0).toString(), QStringLiteral("registry-old-uid"));
            QCOMPARE(rows.first().values.at(1).toLongLong(), 7250);
        }
        QCOMPARE(registryVersions, 1);
        bool matched=false;
        for(const auto& dir:receipts.entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
            if(dir.startsWith('.'))continue;
            bool same=true;
            for(auto it=priorSealed.cbegin();it!=priorSealed.cend();++it) {
                QFile file(receipts.filePath(dir+"/"+it.key()+".enc"));
                if(!file.open(QIODevice::ReadOnly) || file.readAll()!=it.value()){same=false;break;}
            }
            if(!same)continue;
            matched=true;QTemporaryDir exported;QVERIFY(exported.isValid());
            for(auto it=priorSealed.cbegin();it!=priorSealed.cend();++it) {
                const auto plain=exported.filePath(it.key()+".db");
                QVERIFY2(EncryptedStorage::exportPlaintextDatabase(receipts.filePath(dir+"/"+it.key()+".enc"),plain,&error),qPrintable(error));
                const auto connection="retention-readback-"+it.key();
                {auto retained=QSqlDatabase::addDatabase("QSQLITE",connection);retained.setDatabaseName(plain);QVERIFY(retained.open());
                 QSqlQuery marker(retained);QVERIFY(marker.exec("SELECT uid,cents FROM retention_sentinel"));QVERIFY(marker.next());
                 QCOMPARE(marker.value(0).toString(),it.key()+"-old-uid");QCOMPARE(marker.value(1).toInt(),7250);QVERIFY(!marker.next());retained.close();}
                QSqlDatabase::removeDatabase(connection);
            }
        }
        QVERIFY2(matched,"Prior sealed stores have no verified retained generation");
    }
    if(targetState=="populated-repeat-open" || retainedStores) {
        q.finish();db.close();
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error));
        QVERIFY2(Database::runStartupMigrationCheck(&error),qPrintable(error));
        auto owner=Database::db();QVERIFY(owner.isOpen());
        const auto capture=[&](QByteArray* bytes)->bool {
            bytes->clear();QDataStream stream(bytes,QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_0);
            for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts",
                    "migration_record_map","migration_order_key_map","migration_child_key_map",
                    "migration_prepared_source","migration_archive_pin"}) {
                QSqlQuery rows(owner);
                if(!rows.exec("SELECT * FROM "+table+" ORDER BY rowid")) {error=rows.lastError().text();return false;}
                stream<<table;
                while(rows.next()) {
                    QVariantList values;
                    for(int i=0;i<rows.record().count();++i) values<<rows.value(i);
                    stream<<values;
                }
                if(rows.lastError().isValid()) {error=rows.lastError().text();return false;}
            }
            return stream.status()==QDataStream::Ok;
        };
        QByteArray before,after;QVERIFY2(capture(&before),qPrintable(error));
        QFile history(QDir(AppRuntime::securityDir()).filePath("legacy_import_history.json"));
        QVERIFY(history.open(QIODevice::ReadOnly));const auto historyBefore=history.readAll();history.close();
        QJsonParseError parseError;const auto historyDoc=QJsonDocument::fromJson(historyBefore,&parseError);
        QCOMPARE(parseError.error,QJsonParseError::NoError);QVERIFY(historyDoc.isArray());
        QCOMPARE(historyDoc.array().size(),1);
        QCOMPARE(historyDoc.array().first().toObject().value("status").toString(),QString("success"));
        QVERIFY(!historyDoc.array().first().toObject().value("fingerprint").toString().isEmpty());
        QSqlQuery identity(owner);QVERIFY(identity.exec("SELECT DISTINCT source_identity FROM migration_record_map"));
        QVERIFY(identity.next());QCOMPARE(identity.value(0).toString(),historyDoc.array().first().toObject().value("fingerprint").toString());
        QVERIFY(!identity.next());identity.finish();
        QFile importLog(result.importLogPath);QVERIFY(importLog.open(QIODevice::ReadOnly));
        const auto logBefore=importLog.readAll();importLog.close();
        QString repeatError;QStringList repeatStages;LegacyMigrationResult repeated;
        bool repeatedOk=true;QEventLoop repeatLoop;
        std::thread repeatWorker([&]{
            repeatedOk=LegacyMigrationService::executeLegacyImport(sourcePath,&repeated,&repeatError,
                [&](const LegacyImportProgressState& state){repeatStages<<state.stageKey;});
            Database::close();
            QMetaObject::invokeMethod(&repeatLoop,&QEventLoop::quit,Qt::QueuedConnection);
        });
        repeatLoop.exec();repeatWorker.join();
        QVERIFY(!repeatedOk);
        QVERIFY2(repeatError.contains(QString::fromUtf8("вже було успішно імпортоване")),qPrintable(repeatError));
        QVERIFY(!repeatStages.contains("execute_snapshot"));QVERIFY(!repeatStages.contains("execute_import"));
        QVERIFY(!repeatStages.contains("execute_reopen"));QVERIFY(owner.isOpen());
        QVERIFY2(capture(&after),qPrintable(error));QCOMPARE(after,before);
        QVERIFY(history.open(QIODevice::ReadOnly));QCOMPARE(history.readAll(),historyBefore);history.close();
        QVERIFY(importLog.open(QIODevice::ReadOnly));QCOMPARE(importLog.readAll(),logBefore);importLog.close();
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),original);source.close();
        QSqlQuery counts(owner);
        QVERIFY(counts.exec("SELECT (SELECT COUNT(*) FROM work_orders),(SELECT COUNT(*) FROM work_order_services),(SELECT COUNT(*) FROM work_order_parts),"
            "(SELECT ROUND(SUM(amount)*100) FROM work_order_parts),(SELECT ROUND(SUM(amount)*100) FROM work_order_services)"));
        QVERIFY(counts.next());QCOMPARE(counts.value(0).toInt(),2);QCOMPARE(counts.value(1).toInt(),2);
        QCOMPARE(counts.value(2).toInt(),2);QCOMPARE(counts.value(3).toInt(),3850);QCOMPARE(counts.value(4).toInt(),1250);
    }

}

void ReportsOrdersTests::migrationJournalRejectsCorruption()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto oldName=QCoreApplication::applicationName();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Migration-", dir.path()));
    QVERIFY(setIsolatedTestRoot(dir.path()));
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(AppRuntime::securityDir().startsWith(dir.path()));
    QVERIFY(QDir().mkpath(AppRuntime::securityDir()));
    const auto path=QDir(AppRuntime::securityDir()).filePath("legacy_import_history.json");
    LegacyImportBundle bundle; bundle.sourcePath="test-source";
    LegacyImportAuditReport audit; audit.sourceFingerprint="fingerprint";
    for(const auto& bytes:QList<QByteArray>{"{", "{}", "[1]", "[{}]", ""}) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size()); f.close();
        QString error;
        QVERIFY(!LegacyImportJournalSupport::ensureCanImportFingerprint("fingerprint",&error));
        QVERIFY(!error.isEmpty());
        LegacyImportRunContext context; context.runId="unchanged";
        QVERIFY(!LegacyImportJournalSupport::beginRun(bundle,audit,&context,&error));
        QCOMPARE(context.runId,QString("unchanged"));
        QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),bytes); f.close();
    }
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkpath(path)); // History path is a directory: never accept it as an empty history.
    QString error;
    QVERIFY(!LegacyImportJournalSupport::ensureCanImportFingerprint("fingerprint",&error));
    QVERIFY(QDir().rmdir(path));
    LegacyImportRunContext first,second;
    QVERIFY2(LegacyImportJournalSupport::beginRun(bundle,audit,&first,&error),qPrintable(error));
    QVERIFY2(LegacyImportJournalSupport::beginRun(bundle,audit,&second,&error),qPrintable(error));
    QVERIFY(first.runId!=second.runId);
    QFile history(path); QVERIFY(history.open(QIODevice::ReadOnly));
    const auto records=QJsonDocument::fromJson(history.readAll()).array();
    QCOMPARE(records.size(),2);
    history.close();
    QVERIFY2(LegacyImportJournalSupport::logInfo(first,"stage","started",&error),qPrintable(error));
    LegacyImportRunContext missing=first; missing.runId="missing-run";
    error.clear();
    QVERIFY(!LegacyImportJournalSupport::logInfo(missing,"stage","must fail",&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(history.open(QIODevice::ReadOnly)); const auto intact=history.readAll(); history.close();
    const auto snapshotPath=path+".saved";
    QVERIFY(QFile::rename(path,snapshotPath));
    QVERIFY(QDir().mkpath(path));
    LegacyImportSafetySnapshot snapshot;
    LegacyMigrationResult result;
    LegacyImportVerifyReport verification;
    error.clear(); QVERIFY(!LegacyImportJournalSupport::logInfo(first,"stage","begin",&error)); QVERIFY(!error.isEmpty());
    error.clear(); QVERIFY(!LegacyImportJournalSupport::attachSnapshot(first,snapshot,&error)); QVERIFY(!error.isEmpty());
    error.clear(); QVERIFY(!LegacyImportJournalSupport::finishSuccess(first,result,verification,&error)); QVERIFY(!error.isEmpty());
    error.clear(); QVERIFY(!LegacyImportJournalSupport::finishFailure(first,"stage","failed",&error)); QVERIFY(!error.isEmpty());
    QFile completionLog(first.logFilePath); QVERIFY(completionLog.open(QIODevice::ReadOnly));
    const auto failedCompletionLog=completionLog.readAll(); completionLog.close();
    QVERIFY(!failedCompletionLog.contains("FINISHED SUCCESSFULLY"));
    QVERIFY(failedCompletionLog.contains("COMPLETION PREPARED"));
    QVERIFY(QDir().rmdir(path));
    QVERIFY(QFile::rename(snapshotPath,path));
    QVERIFY(history.open(QIODevice::ReadOnly)); QCOMPARE(history.readAll(),intact); history.close();
    const auto savedLog=first.logFilePath+".saved";
    QVERIFY(QFile::rename(first.logFilePath,savedLog));
    QVERIFY(QDir().mkpath(first.logFilePath));
    error.clear(); QVERIFY(!LegacyImportJournalSupport::logInfo(first,"stage","log unavailable",&error)); QVERIFY(!error.isEmpty());
    error.clear(); QVERIFY(!LegacyImportJournalSupport::finishSuccess(first,result,verification,&error)); QVERIFY(!error.isEmpty());
    QVERIFY(history.open(QIODevice::ReadOnly)); QCOMPARE(history.readAll(),intact); history.close();
    QVERIFY(QDir().rmdir(first.logFilePath));
    QVERIFY(QFile::rename(savedLog,first.logFilePath));
    QVERIFY2(LegacyImportJournalSupport::finishSuccess(first,result,verification,&error),qPrintable(error));
    QVERIFY(!LegacyImportJournalSupport::ensureCanImportFingerprint("fingerprint",&error));
}

void ReportsOrdersTests::canonicalCorrespondence_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"none", "missing", "wrong-parent", "wrong-uid", "write-failure", "duplicate-source", "parent-identity", "source-mutated"})
        QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::canonicalCorrespondence()
{
    QFETCH(QString, fault);
    const QString connection = "canonical-correspondence";
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(directory.filePath("map.sqlite"));
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    QVERIFY(db.open());
    QSqlQuery q(db);
    const auto exec = [&](const QString& sql) { const bool ok=q.exec(sql); if(!ok)qWarning()<<q.lastError().text(); return ok; };
    QVERIFY(exec("ATTACH DATABASE ':memory:' AS legacy_rewrite"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.work_orders(number INTEGER,order_uid TEXT,extra BLOB)"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.work_order_parts(id INTEGER,work_order INTEGER,price TEXT)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.work_orders VALUES(7,'order-seven',x'0012ff0080')"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.work_order_parts VALUES(11,7,'602.01')"));
    QVERIFY(exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT)"));
    QVERIFY(exec("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,price TEXT)"));
    QVERIFY(exec("INSERT INTO work_orders VALUES(7,'order-seven'),(8,'other-order')"));
    QVERIFY(exec("INSERT INTO work_order_parts VALUES(11,7,'order-seven','602.01')"));
    CanonicalRewriteSourceSchema schema;
    QString error;
    QVERIFY2(loadCanonicalRewriteSourceSchema(db,"legacy_rewrite",&schema,&error),qPrintable(error));
    QVERIFY(db.transaction());
    QVERIFY2(recordCanonicalRewriteCorrespondence(db,"source-A","legacy_rewrite",schema,&error),qPrintable(error));
    QVERIFY(db.commit());
    QVERIFY(exec("SELECT COUNT(*) FROM migration_record_map")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2); q.finish();
    QVERIFY(exec("SELECT source_payload FROM migration_record_map WHERE source_table='work_orders'"));
    QVERIFY(q.next());
    QDataStream stream(q.value(0).toByteArray()); stream.setVersion(QDataStream::Qt_6_0);
    QMap<QString,QVariant> original; stream>>original;
    QCOMPARE(original.value("extra").toByteArray(),QByteArray::fromHex("0012ff0080")); q.finish();
    if(fault=="parent-identity") QVERIFY(exec("UPDATE work_orders SET order_uid='replaced' WHERE number=7"));
    if(fault=="source-mutated") QVERIFY(exec("UPDATE legacy_rewrite.work_order_parts SET price='999.99'"));
    if(fault=="missing") QVERIFY(exec("UPDATE work_order_parts SET id=12")); // Same row count is not sufficient.
    if(fault=="wrong-parent") QVERIFY(exec("UPDATE work_order_parts SET order_number=8"));
    if(fault=="wrong-uid") QVERIFY(exec("UPDATE work_order_parts SET order_uid='other-order'"));
    if(fault=="duplicate-source") QVERIFY(exec("INSERT INTO legacy_rewrite.work_order_parts SELECT * FROM legacy_rewrite.work_order_parts"));
    if(fault=="write-failure") QVERIFY(exec("CREATE TRIGGER reject_map BEFORE INSERT ON migration_record_map BEGIN SELECT RAISE(ABORT,'injected map write failure'); END"));
    QVERIFY(db.transaction());
    const bool result=recordCanonicalRewriteCorrespondence(db,fault=="source-mutated" ? "source-A" : "source-B","legacy_rewrite",schema,&error);
    QCOMPARE(result,fault=="none");
    if(!result) QVERIFY(!error.isEmpty());
    QVERIFY(db.rollback());
    QVERIFY(exec("SELECT COUNT(*) FROM migration_record_map WHERE source_identity='source-B'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    QVERIFY(exec("UPDATE work_orders SET order_uid='order-seven' WHERE number=7"));
    QVERIFY(exec("UPDATE legacy_rewrite.work_order_parts SET price='602.01'"));
    QVERIFY(exec("DROP TRIGGER IF EXISTS reject_map"));
    QVERIFY(exec("UPDATE work_order_parts SET id=11,order_number=7,order_uid='order-seven'"));
    QVERIFY(exec("DELETE FROM legacy_rewrite.work_order_parts WHERE rowid>1"));
    QVERIFY(db.transaction());
    QVERIFY2(recordCanonicalRewriteCorrespondence(db,"source-A","legacy_rewrite",schema,&error),qPrintable(error));
    QVERIFY2(recordCanonicalRewriteCorrespondence(db,"source-B","legacy_rewrite",schema,&error),qPrintable(error));
    QVERIFY(db.commit());
    QVERIFY(exec("DETACH DATABASE legacy_rewrite"));
    q.finish(); db.close(); QVERIFY(db.open()); // Correspondence survives process connection lifecycle.
    QVERIFY(exec("SELECT COUNT(*) FROM migration_record_map")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),4);
}

void ReportsOrdersTests::canonicalCorrespondenceLegacyKeys()
{
    const QString connection = "canonical-legacy-keys";
    auto db = QSqlDatabase::addDatabase("QSQLITE",connection);
    const auto cleanup=qScopeGuard([&] { db.close(); db={}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QSqlQuery q(db);
    const auto exec=[&](const QString& sql) { return q.exec(sql); };
    QVERIFY(exec("ATTACH DATABASE ':memory:' AS legacy_rewrite"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.work_orders(number INTEGER,order_uid TEXT)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.work_orders VALUES(7,'seven')"));
    QVERIFY(exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT)"));
    // Legacy rowid is retained even when IDs have holes; no MAX+1 reconstruction.
    for(const auto& table: QStringList{"work_order_services","work_order_parts","outsource_repairs"}) {
        QVERIFY(exec("CREATE TABLE legacy_rewrite."+table+"(work_order_id INTEGER,note TEXT)"));
        QVERIFY(exec("INSERT INTO legacy_rewrite."+table+"(rowid,work_order_id,note) VALUES(41,7,'original')"));
        QVERIFY(exec("CREATE TABLE "+table+"(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT)"));
    }
    for(const auto& table: QStringList{"parts_catalog","expenses"}) {
        QVERIFY(exec("CREATE TABLE legacy_rewrite."+table+"(note TEXT)"));
        QVERIFY(exec("INSERT INTO legacy_rewrite."+table+"(rowid,note) VALUES(51,'retained')"));
        QVERIFY(exec("CREATE TABLE "+table+"(id INTEGER PRIMARY KEY)"));
    }
    QVERIFY(exec("CREATE TABLE legacy_rewrite.masters(id TEXT,name TEXT)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.masters VALUES('m','Name')"));
    QVERIFY(exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.service_masters(service_id INTEGER,master_id TEXT,role TEXT)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.service_masters VALUES(41,'m',NULL),(41,'m','  helper  ')"));
    QVERIFY(exec("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT,role TEXT,PRIMARY KEY(service_id,master_id,role))"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.planning_calendar(day TEXT,note TEXT)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.planning_calendar VALUES('2026-01-01','Plan')"));
    QVERIFY(exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
    QVERIFY(exec("CREATE TABLE legacy_rewrite.settings(key TEXT,value BLOB)"));
    QVERIFY(exec("INSERT INTO legacy_rewrite.settings VALUES('protected',x'00ff01'),('db_canonical_rewrite_done','old')"));
    QVERIFY(exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value BLOB)"));
    CanonicalRewriteSourceSchema schema; QString error;
    QVERIFY2(loadCanonicalRewriteSourceSchema(db,"legacy_rewrite",&schema,&error),qPrintable(error));
    QVERIFY(db.transaction());
    QVERIFY2(importCanonicalRewritePrimaryTables(db,schema,&error),qPrintable(error));
    QVERIFY2(importCanonicalRewriteAncillaryTables(db,schema,&error),qPrintable(error));
    QVERIFY2(recordCanonicalRewriteCorrespondence(db,"legacy","legacy_rewrite",schema,&error),qPrintable(error));
    QVERIFY(exec("CREATE TRIGGER reject_map BEFORE INSERT ON migration_record_map WHEN NEW.source_table='settings' BEGIN SELECT RAISE(ABORT,'injected late map failure'); END"));
    QVERIFY(!recordCanonicalRewriteCorrespondence(db,"retry-source","legacy_rewrite",schema,&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(db.rollback());
    // The late correspondence failure must roll back ALL imported tables, not just the map.
    for(const auto& table: QStringList{"masters","work_orders","work_order_services","work_order_parts",
            "service_masters","outsource_repairs","parts_catalog","expenses","planning_calendar","settings"}) {
        QVERIFY(exec("SELECT COUNT(*) FROM "+table));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    }
    QVERIFY(exec("SELECT COUNT(*) FROM sqlite_master WHERE name IN ('migration_record_map','reject_map')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    error.clear();
    QVERIFY(db.transaction());
    QVERIFY2(importCanonicalRewritePrimaryTables(db,schema,&error),qPrintable(error));
    QVERIFY2(importCanonicalRewriteAncillaryTables(db,schema,&error),qPrintable(error));
    for(int repeat=0;repeat<3;++repeat)
        QVERIFY2(recordCanonicalRewriteCorrespondence(db,"legacy","legacy_rewrite",schema,&error),qPrintable(error));
    QVERIFY(db.commit());
    QVERIFY(exec("SELECT COUNT(*),COUNT(DISTINCT source_table) FROM migration_record_map"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),11); QCOMPARE(q.value(1).toInt(),10); q.finish();
    QVERIFY(exec("SELECT source_key,target_key,source_payload FROM migration_record_map WHERE source_table='service_masters'"));
    int verified=0;
    while(q.next()) {
        QMap<QString,QVariant> oldKey,newKey,payload;
        QDataStream a(q.value(0).toByteArray()),b(q.value(1).toByteArray()),c(q.value(2).toByteArray());
        a.setVersion(QDataStream::Qt_6_0); b.setVersion(QDataStream::Qt_6_0); c.setVersion(QDataStream::Qt_6_0);
        a>>oldKey; b>>newKey; c>>payload;
        QCOMPARE(oldKey.value("role"),payload.value("role"));
        QCOMPARE(newKey.value("role").toString(),oldKey.value("role").isNull()?QString("master"):QString("helper"));
        ++verified;
    }
    QCOMPARE(verified,2);
}

void ReportsOrdersTests::canonicalSourceSchemaReadFailure()
{
    const QString name = QStringLiteral("canonical_source_schema");
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(name); });
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());
    CanonicalRewriteSourceSchema schema;
    schema.orders.insert(QStringLiteral("sentinel"));
    QString error;
    QVERIFY(!loadCanonicalRewriteSourceSchema(db, QStringLiteral("not_attached"), &schema, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(schema.orders, QSet<QString>{QStringLiteral("sentinel")});
    QVERIFY(loadCanonicalRewriteSourceSchema(db, QStringLiteral("main"), &schema, &error));
    QVERIFY(schema.orders.isEmpty());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY, work_order INTEGER)")));
    QVERIFY(loadCanonicalRewriteSourceSchema(db, QStringLiteral("main"), &schema, &error));
    QCOMPARE(schema.orders, (QSet<QString>{QStringLiteral("number"), QStringLiteral("order_uid")}));
    QCOMPARE(schema.parts, (QSet<QString>{QStringLiteral("id"), QStringLiteral("work_order")}));
    QVERIFY(schema.services.isEmpty());
    QVERIFY(!loadCanonicalRewriteSourceSchema(db, QStringLiteral("main"), nullptr, &error));
    query.finish();
    db.close();
    QVERIFY(!loadCanonicalRewriteSourceSchema(db, QStringLiteral("main"), &schema, &error));
    QVERIFY(schema.orders.contains(QStringLiteral("order_uid")));
}

void ReportsOrdersTests::canonicalSettingsSnapshotAndRestore()
{
    const QString connection = "canonical-settings-snapshot";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QMap<QString, QVariant> snapshot{{"sentinel", "preserve"}};
    QString error;
    QVERIFY(!snapshotWorkspaceSettingsForCanonicalRewrite(db, &snapshot, &error));
    QCOMPARE(snapshot.value("sentinel").toString(), QString("preserve"));
    QVERIFY(!error.isEmpty());
    QVERIFY(!restoreCanonicalRewriteWorkspaceSettings(db, snapshot, &error));
    QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value BLOB)"));
    QVERIFY(snapshotWorkspaceSettingsForCanonicalRewrite(db, &snapshot, &error));
    QVERIFY(snapshot.isEmpty());
    const QByteArray protectedValue = QByteArray::fromHex("0012ff0080");
    QVERIFY(q.prepare("INSERT INTO settings VALUES(?,?)"));
    q.addBindValue("protected"); q.addBindValue(protectedValue); QVERIFY(q.exec());
    QVERIFY(q.exec("INSERT INTO settings VALUES('absolute','C:/Profile/Documents'),('db_canonical_rewrite_done','1')"));
    QVERIFY(snapshotWorkspaceSettingsForCanonicalRewrite(db, &snapshot, &error));
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot.value("protected").toByteArray(), protectedValue);
    QVERIFY(db.transaction());
    QVERIFY(q.exec("DELETE FROM settings"));
    QVERIFY(q.exec("CREATE TRIGGER stop_settings BEFORE INSERT ON settings WHEN NEW.key='protected' BEGIN SELECT RAISE(ABORT,'settings write failure'); END"));
    QVERIFY(!restoreCanonicalRewriteWorkspaceSettings(db, snapshot, &error));
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT COUNT(*) FROM settings")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    for (int retry=0; retry<2; ++retry) {
        QVERIFY(db.transaction());
        QVERIFY(q.exec("DELETE FROM settings"));
        QVERIFY2(restoreCanonicalRewriteWorkspaceSettings(db, snapshot, &error), qPrintable(error));
        QVERIFY(db.commit());
        QMap<QString, QVariant> restored;
        QVERIFY(snapshotWorkspaceSettingsForCanonicalRewrite(db, &restored, &error));
        QCOMPARE(restored, snapshot);
    }
}

void ReportsOrdersTests::canonicalCompletionSettingsFailure()
{
    const QString connection = "canonical-completion-settings";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value BLOB)"));
    const QMap<QString, QVariant> markers{
        {"db_canonical_rewrite_at", "2026-11-06T00:00:00Z"},
        {"db_canonical_rewrite_done", "1"},
        {"db_canonical_rewrite_fingerprint", "fingerprint"},
        {"db_canonical_rewrite_source", "C:/Test/source.db"}};
    for (auto it = markers.cbegin(); it != markers.cend(); ++it) {
        QVERIFY(q.exec("DELETE FROM settings"));
        QVERIFY(q.exec("INSERT INTO settings VALUES('sentinel','original')"));
        QVERIFY(q.exec("CREATE TRIGGER marker_failure BEFORE INSERT ON settings WHEN NEW.key='" + it.key() +
            "' BEGIN SELECT RAISE(ABORT,'injected marker failure'); END"));
        QVERIFY(db.transaction()); QString error;
        QVERIFY(!restoreCanonicalRewriteWorkspaceSettings(db, markers, &error));
        QVERIFY2(error.contains("injected marker failure"), qPrintable(error));
        QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT key,value FROM settings")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("sentinel")); QCOMPARE(q.value(1).toString(), QString("original"));
        QVERIFY(!q.next()); q.finish();
        QVERIFY(q.exec("DROP TRIGGER marker_failure"));
        for (int retry=0; retry<2; ++retry) {
            QVERIFY(db.transaction());
            QVERIFY2(restoreCanonicalRewriteWorkspaceSettings(db, markers, &error), qPrintable(error));
            QVERIFY(db.commit());
            QVERIFY(q.exec("SELECT key,value FROM settings WHERE key!='sentinel' ORDER BY key"));
            for (auto expected = markers.cbegin(); expected != markers.cend(); ++expected) {
                QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), expected.key()); QCOMPARE(q.value(1), expected.value());
            }
            QVERIFY(!q.next()); q.finish();
        }
    }
}

void ReportsOrdersTests::canonicalCleanupFailure_data()
{
    QTest::addColumn<QString>("table");
    for (const auto& name : QStringList{"order_registry", "workspace_order_state", "router_cache_service_masters",
             "router_cache_work_order_parts", "router_cache_work_order_services", "router_cache_work_orders"})
        QTest::newRow(qPrintable(name)) << name;
}

void ReportsOrdersTests::canonicalCleanupFailure()
{
    QFETCH(QString, table);
    const QString connection = "canonical-cleanup-failure";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    const QStringList tables{"service_masters", "work_order_parts", "work_order_services", "work_orders",
        "masters", "settings", "order_registry", "workspace_order_state", "router_cache_service_masters",
        "router_cache_work_order_parts", "router_cache_work_order_services", "router_cache_work_orders"};
    for (const auto& name : tables) {
        QVERIFY(q.exec("CREATE TABLE " + name + "(id INTEGER PRIMARY KEY)"));
        QVERIFY(q.exec("INSERT INTO " + name + " VALUES(1)"));
    }
    QVERIFY(q.exec("CREATE TRIGGER stop_cleanup BEFORE DELETE ON " + table +
        " BEGIN SELECT RAISE(ABORT,'injected cleanup failure'); END"));
    QVERIFY(db.transaction());
    QString error;
    const bool result = clearCanonicalRewriteWorkspaceTargets(db, &error);
    QVERIFY(db.rollback());
    QVERIFY(!result);
    QVERIFY2(error.contains("injected cleanup failure") && error.contains(table), qPrintable(error));
    for (const auto& name : tables) {
        QVERIFY(q.exec("SELECT COUNT(*) FROM " + name)); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1); q.finish();
    }
    QVERIFY(q.exec("DROP TRIGGER stop_cleanup"));
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY(db.transaction());
        QVERIFY2(clearCanonicalRewriteWorkspaceTargets(db, &error), qPrintable(error));
        QVERIFY(db.commit());
        for (const auto& name : tables) {
            QVERIFY(q.exec("SELECT COUNT(*) FROM " + name)); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); q.finish();
        }
    }
}

void ReportsOrdersTests::canonicalRegistryBatchFailure_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("second-write") << 0;
    QTest::newRow("prepare") << 1;
    QTest::newRow("scan") << 2;
}

void ReportsOrdersTests::canonicalRegistryBatchFailure()
{
    QFETCH(int, mode);
    const QString workspaceName = "canonical-registry-workspace";
    const QString registryName = "canonical-registry-target";
    auto workspace = QSqlDatabase::addDatabase("QSQLITE", workspaceName);
    auto registry = QSqlDatabase::addDatabase("QSQLITE", registryName);
    const auto cleanup = qScopeGuard([&] {
        workspace.close(); registry.close(); workspace = {}; registry = {};
        QSqlDatabase::removeDatabase(workspaceName); QSqlDatabase::removeDatabase(registryName);
    });
    workspace.setDatabaseName(":memory:"); registry.setDatabaseName(":memory:");
    QVERIFY(workspace.open()); QVERIFY(registry.open());
    QSqlQuery w(workspace), r(registry);
    QVERIFY(w.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,order_year INTEGER,status TEXT,opened_at TEXT,closed_at TEXT,plate TEXT,plate_norm TEXT,vin TEXT,car TEXT,client TEXT,master_id TEXT,master_name_closed TEXT,works_sum REAL,parts_sum REAL)"));
    QVERIFY(w.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,works_sum,parts_sum) VALUES(1,'uid-1',1,2025,'2025-01-01',100,20),(2,'uid-2',2,2025,'2025-01-02',200,30)"));
    QVERIFY(ensureRouterCacheSchema(workspace));
    // Existing cache entries avoid unrelated workspace-state writes in this test.
    QVERIFY2(w.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(1,'uid-1',1,2025,'2025-01-01','open','','','Car A','Client A'),(2,'uid-2',2,2025,'2025-01-02','open','','','Car B','Client B')"), qPrintable(w.lastError().text()));
    QVERIFY(r.exec("CREATE TABLE order_registry(order_number INTEGER PRIMARY KEY,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER,storage_kind TEXT,storage_path TEXT,source_db TEXT,imported_at TEXT,last_seen_at TEXT,status TEXT,opened_at TEXT,closed_at TEXT,plate TEXT,plate_norm TEXT,vin TEXT,car TEXT,client TEXT,master_id TEXT,master_name_closed TEXT,works_sum REAL,parts_sum REAL)"));
    QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,works_sum) VALUES(1,'original',999)"));
    if (mode == 0) QVERIFY(r.exec("CREATE TRIGGER fail_second BEFORE INSERT ON order_registry WHEN NEW.order_number=2 BEGIN SELECT RAISE(ABORT,'injected second write failure'); END"));
    if (mode == 1) QVERIFY(r.exec("ALTER TABLE order_registry RENAME TO saved_registry"));
    if (mode == 2) QVERIFY(w.exec("ALTER TABLE work_orders RENAME TO saved_orders"));
    QString error;
    QVERIFY(!upsertActiveOrdersIntoRegistry(workspace, registry, &error));
    QVERIFY2(error.contains(mode == 0 ? "injected second write failure" : mode == 1 ? "prepare failed" : "scan failed"), qPrintable(error));
    if (mode == 0) QVERIFY(r.exec("DROP TRIGGER fail_second"));
    if (mode == 1) QVERIFY(r.exec("ALTER TABLE saved_registry RENAME TO order_registry"));
    if (mode == 2) QVERIFY(w.exec("ALTER TABLE saved_orders RENAME TO work_orders"));
    QVERIFY(r.exec("SELECT order_uid,works_sum FROM order_registry"));
    QVERIFY(r.next()); QCOMPARE(r.value(0).toString(), QString("original")); QCOMPARE(r.value(1).toDouble(), 999.0); QVERIFY(!r.next()); r.finish();
    for (int retry = 0; retry < 2; ++retry) {
        error.clear();
        QVERIFY2(upsertActiveOrdersIntoRegistry(workspace, registry, &error), qPrintable(error));
        QVERIFY(r.exec("SELECT order_number,order_uid,visible_order_number,order_year,works_sum,parts_sum FROM order_registry ORDER BY order_number"));
        for (int id = 1; id <= 2; ++id) {
            QVERIFY(r.next()); QCOMPARE(r.value(0).toInt(), id); QCOMPARE(r.value(1).toString(), QString("uid-%1").arg(id));
            QCOMPARE(r.value(2).toInt(), id); QCOMPARE(r.value(3).toInt(), 2025);
            QCOMPARE(r.value(4).toDouble(), id * 100.0); QCOMPARE(r.value(5).toDouble(), id == 1 ? 20.0 : 30.0);
        }
        QVERIFY(!r.next()); r.finish();
    }
}

void ReportsOrdersTests::canonicalFinalizationColorFailure_data()
{
    QTest::addColumn<QString>("column");
    QTest::newRow("color-write") << QString("color");
    QTest::newRow("car-color-write") << QString("car_color");
}

void ReportsOrdersTests::canonicalFinalizationColorFailure()
{
    QFETCH(QString, column);
    const QString connection = "canonical-finalization-color";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,color TEXT,car_color TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(1,'','White'),(2,'Black','')"));
    QVERIFY(q.exec("CREATE TABLE masters(role TEXT)"));
    QVERIFY(q.exec("INSERT INTO masters VALUES('')"));
    // Stop before any registry/file access, including when testing unfixed code.
    QVERIFY(q.exec("CREATE TRIGGER downstream_stop BEFORE UPDATE ON masters BEGIN SELECT RAISE(ABORT,'downstream sentinel'); END"));
    QVERIFY(q.exec(QString("CREATE TRIGGER color_failure BEFORE UPDATE OF %1 ON work_orders BEGIN SELECT RAISE(ABORT,'injected color write failure'); END").arg(column)));
    QVERIFY(db.transaction());
    QString error;
    const bool result = finalizeCanonicalRewriteImportedWorkspace(db, "test", "test", &error);
    QVERIFY(db.rollback());
    QVERIFY(!result);
    QVERIFY2(error.contains("injected color write failure"), qPrintable(error));
    QVERIFY2(error.contains(column), qPrintable(error));
    QVERIFY(q.exec("SELECT color,car_color FROM work_orders ORDER BY number"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString()); QCOMPARE(q.value(1).toString(), QString("White"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("Black")); QCOMPARE(q.value(1).toString(), QString()); q.finish();
    QVERIFY(q.exec("DROP TRIGGER color_failure"));
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY(db.transaction());
        error.clear();
        QVERIFY(!finalizeCanonicalRewriteImportedWorkspace(db, "test", "test", &error));
        QVERIFY2(error.contains("downstream sentinel"), qPrintable(error));
        QVERIFY(q.exec("SELECT color,car_color FROM work_orders ORDER BY number"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("White")); QCOMPARE(q.value(1).toString(), QString("White"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("Black")); QCOMPARE(q.value(1).toString(), QString("Black")); q.finish();
        QVERIFY(db.rollback());
    }
}

void ReportsOrdersTests::canonicalRouterParentIdentity_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("legacy-no-uid-column") << 0;
    QTest::newRow("empty-parent-uid") << 1;
    QTest::newRow("existing-parent-uid") << 2;
}

void ReportsOrdersTests::canonicalRouterParentIdentity()
{
    QFETCH(int, mode);
    const QString connection = "canonical-router-parent-identity";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,opened_at,status,plate,plate_norm,car,client) VALUES(9,'2025-12-31','','','','','')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,title) VALUES(1,9,'Test work')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,order_number,name) VALUES(1,9,'Test part')"));
    QVERIFY(q.exec("INSERT INTO router_cache_outsource_repairs(id,order_number) VALUES(1,9)"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(9,'','2025-12-31')"));
    if (mode > 0) {
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN order_uid TEXT NOT NULL DEFAULT ''"));
        if (mode == 2) QVERIFY(q.exec("UPDATE work_orders SET order_uid='original-parent-uid'"));
    }
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QString error, firstUid;
    for (int retry = 0; retry < 3; ++retry) {
        QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
        QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=9"));
        QVERIFY(q.next()); const QString uid = q.value(0).toString(); QVERIFY(!uid.isEmpty()); q.finish();
        if (retry == 0) firstUid = uid;
        QCOMPARE(uid, firstUid);
        if (mode == 2) QCOMPARE(uid, QString("original-parent-uid"));
        for (const auto& name : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
            QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QVERIFY(!q.next()); q.finish();
        }
    }
}

void ReportsOrdersTests::canonicalRouterChildUidFailure_data()
{
    QTest::addColumn<QString>("table");
    for (const auto& table : {"router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"})
        QTest::newRow(table) << QString(table);
}

void ReportsOrdersTests::routerCacheHistoricalYear_data()
{
    QTest::addColumn<QString>("openedAt");
    QTest::addColumn<int>("sourceYear");
    QTest::addColumn<int>("expectedYear");
    for (int sourceYear : {0, 2031}) {
        for (const auto& date : {QStringLiteral("31.12.2025"), QStringLiteral("01.01.2026 08:30"),
                                 QStringLiteral("31.12.25/23.59"), QStringLiteral("2026-01-01T00:01:00"),
                                 QStringLiteral(" 31.12.1999 ")}) {
            const int expected = sourceYear > 0 ? sourceYear : (date.contains("1999") ? 1999 : (date.startsWith("31") ? 2025 : 2026));
            QTest::newRow(qPrintable(QString::number(sourceYear) + ":" + date)) << date << sourceYear << expected;
        }
    }
}

void ReportsOrdersTests::routerCacheHistoricalYear()
{
    QFETCH(QString, openedAt);
    QFETCH(int, sourceYear);
    QFETCH(int, expectedYear);
    const QString connection = "router-cache-year";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.prepare("INSERT INTO router_cache_work_orders(number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(9,?,?,'','','','','')"));
    q.addBindValue(sourceYear); q.addBindValue(openedAt); QVERIFY(q.exec());
    QString uid;
    for (int retry = 0; retry < 3; ++retry) {
        QVERIFY(ensureRouterCacheSchema(db));
        QVERIFY(q.exec("SELECT order_year,opened_at,order_uid,order_number FROM router_cache_work_orders WHERE number=9"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), expectedYear);
        QCOMPARE(q.value(1).toString(), openedAt);
        if (retry == 0) { uid = q.value(2).toString(); QVERIFY(!uid.isEmpty()); }
        QCOMPARE(q.value(2).toString(), uid); QCOMPARE(q.value(3).toInt(), 9);
        QVERIFY(!q.next()); q.finish();
    }
}

void ReportsOrdersTests::canonicalRouterParentSyncFailure()
{
    const QString table = "router_cache_work_orders";
    const QString connection = "canonical-router-parent-sync";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,opened_at,status,plate,plate_norm,car,client) VALUES(9,'2025-12-31','','','','','')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,title) VALUES(1,9,'Test work')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,order_number,name) VALUES(1,9,'Test part')"));
    QVERIFY(q.exec("INSERT INTO router_cache_outsource_repairs(id,order_number) VALUES(1,9)"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(9,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec("UPDATE router_cache_work_orders SET order_uid='cached-original' WHERE number=9"));
    QVERIFY(q.exec(QString("CREATE TRIGGER fail_child_uid BEFORE UPDATE OF order_uid ON %1 BEGIN SELECT RAISE(ABORT,'injected child UID failure'); END").arg(table)));
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    for (const auto& name : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString(name) == table ? QString("cached-original") : QString()); QVERIFY(!q.next()); q.finish();
    }
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("DROP TRIGGER fail_child_uid"));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); const QString uid = q.value(0).toString(); QVERIFY(!uid.isEmpty()); q.finish();
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
        for (const auto& name : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
            QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QVERIFY(!q.next()); q.finish();
        }
    }
}

void ReportsOrdersTests::canonicalRouterChildUidFailure()
{
    QFETCH(QString, table);
    const QString connection = "canonical-router-child-uid";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,opened_at,status,plate,plate_norm,car,client) VALUES(9,'2025-12-31','','','','','')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,title) VALUES(1,9,'Test work')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,order_number,name) VALUES(1,9,'Test part')"));
    QVERIFY(q.exec("INSERT INTO router_cache_outsource_repairs(id,order_number) VALUES(1,9)"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec(QString("CREATE TRIGGER fail_child_uid BEFORE UPDATE OF order_uid ON %1 BEGIN SELECT RAISE(ABORT,'injected child UID failure'); END").arg(table)));
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    for (const auto& name : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString()); QVERIFY(!q.next()); q.finish();
    }
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("DROP TRIGGER fail_child_uid"));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); const QString uid = q.value(0).toString(); QVERIFY(!uid.isEmpty()); q.finish();
    for (int retry = 0; retry < 2; ++retry) {
        QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
        for (const auto& name : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
            QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QVERIFY(!q.next()); q.finish();
        }
    }
}

void ReportsOrdersTests::canonicalRouterColumnFailure_data()
{
    QTest::addColumn<QString>("table");
    QTest::addColumn<QString>("column");
    QTest::newRow("parent-client") << QString("router_cache_work_orders") << QString("client_uid");
    QTest::newRow("service-note") << QString("router_cache_work_order_services") << QString("note");
    QTest::newRow("part-manufacturer") << QString("router_cache_work_order_parts") << QString("manufacturer");
}

void ReportsOrdersTests::canonicalRouterColumnFailure()
{
    QFETCH(QString, table); QFETCH(QString, column);
    const QString connection = "canonical-router-column";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    int maxColumns = 2000;
    QVERIFY(q.exec("PRAGMA compile_options"));
    while (q.next()) {
        const QString option = q.value(0).toString();
        if (option.startsWith("MAX_COLUMN=")) maxColumns = option.mid(11).toInt();
    }
    q.finish(); QVERIFY(maxColumns > 100);
    QStringList definitions;
    QVERIFY(q.exec(QString("PRAGMA table_info(%1)").arg(table)));
    while (q.next()) {
        const QString name = q.value(1).toString();
        if (name == column) continue;
        QString definition = name + " " + q.value(2).toString();
        if (q.value(5).toInt()) definition += " PRIMARY KEY";
        if (q.value(3).toInt()) definition += " NOT NULL";
        if (!q.value(4).isNull()) definition += " DEFAULT " + q.value(4).toString();
        definitions << definition;
    }
    q.finish();
    for (int i = definitions.size(); i < maxColumns; ++i)
        definitions << QString("padding_%1 INTEGER").arg(i);
    QVERIFY(q.exec(QString("DROP TABLE %1").arg(table)));
    QVERIFY2(q.exec(QString("CREATE TABLE %1(%2)").arg(table, definitions.join(','))), qPrintable(q.lastError().text()));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,opened_at,status,plate,plate_norm,car,client) VALUES(9,'2025-12-31','','','','','')"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toInt(), 0); QCOMPARE(q.value(2).toInt(), 0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec(QString("ALTER TABLE %1 DROP COLUMN padding_%2").arg(table).arg(maxColumns - 1)));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec(QString("SELECT %1 FROM %2 LIMIT 0").arg(column, table))); q.finish();
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
}

void ReportsOrdersTests::canonicalRouterNormalizationFailure_data()
{
    QTest::addColumn<QString>("boundary");
    for (const auto& name : {"order_number", "order_year", "order_uid"})
        QTest::newRow(name) << QString(name);
}

void ReportsOrdersTests::canonicalRouterNormalizationFailure()
{
    QFETCH(QString, boundary);
    const QString connection = "canonical-router-normalization";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY2(q.exec("INSERT INTO router_cache_work_orders(number,opened_at,status,plate,plate_norm,car,client) VALUES(9,'2025-12-31','','','','','')"), qPrintable(q.lastError().text()));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec(QString("CREATE TRIGGER fail_normalization BEFORE UPDATE OF %1 ON router_cache_work_orders BEGIN SELECT RAISE(ABORT,'injected cache normalization failure'); END").arg(boundary)));
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toInt(), 0); QCOMPARE(q.value(2).toInt(), 0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("DROP TRIGGER fail_normalization"));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); const QString uid = q.value(0).toString(); QVERIFY(!uid.isEmpty());
    QCOMPARE(q.value(1).toInt(), 9); QCOMPARE(q.value(2).toInt(), 2025); q.finish();
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=9"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid);
    QCOMPARE(q.value(1).toInt(), 9); QCOMPARE(q.value(2).toInt(), 2025); q.finish();
}

void ReportsOrdersTests::canonicalRouterIndexFailure_data()
{
    QTest::addColumn<QString>("boundary");
    for (const auto& value : {"idx_router_cache_orders_status",
         "idx_router_cache_orders_order_number",
         "idx_router_cache_orders_order_year",
         "idx_router_cache_orders_vehicle_year",
         "idx_router_cache_orders_uid_unique",
         "idx_router_cache_orders_year_number_unique",
         "idx_router_cache_services_order_number",
         "idx_router_cache_services_order_uid_id",
         "idx_router_cache_parts_order_number",
         "idx_router_cache_parts_order_uid_id",
         "idx_router_cache_parts_service_id",
         "idx_router_cache_outsource_order_number",
         "idx_router_cache_outsource_service_id",
         "idx_router_cache_sm_service"})
        QTest::newRow(value) << QString(value);
}

void ReportsOrdersTests::canonicalRouterIndexFailure()
{
    QFETCH(QString, boundary);
    const QString connection = "canonical-router-index-failure";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec("CREATE TABLE order_registry(order_number INTEGER PRIMARY KEY,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER,plate_norm TEXT,status TEXT,opened_at TEXT,closed_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO order_registry VALUES(17,'',0,0,'','','','')"));
    QString removeFault;
    if (boundary.startsWith("idx_")) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(dummy INTEGER)").arg(boundary)));
        removeFault = QString("DROP TABLE %1").arg(boundary);
    } else {
        QVERIFY(q.exec(QString("CREATE TRIGGER fail_registry BEFORE UPDATE OF %1 ON order_registry BEGIN SELECT RAISE(ABORT,'injected registry failure'); END").arg(boundary)));
        removeFault = "DROP TRIGGER fail_registry";
    }
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toInt(), 0); QCOMPARE(q.value(2).toInt(), 0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('order_registry')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 8); q.finish();
    QVERIFY(q.exec(removeFault));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,r.order_uid,w.order_year,r.order_year FROM work_orders w JOIN order_registry r ON r.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next());
    const QString uid = q.value(0).toString();
    QVERIFY(!uid.isEmpty()); QCOMPARE(q.value(1).toString(), uid);
    QCOMPARE(q.value(2).toInt(), 2025); QCOMPARE(q.value(3).toInt(), 2025); q.finish();
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,r.order_uid,r.order_year FROM work_orders w JOIN order_registry r ON r.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QCOMPARE(q.value(1).toString(), uid);
    QCOMPARE(q.value(2).toInt(), 2025); q.finish();
}

void ReportsOrdersTests::canonicalRegistrySchemaFailure_data()
{
    QTest::addColumn<QString>("boundary");
    for (const auto& value : {"order_uid", "visible_order_number", "order_year",
         "idx_local_registry_order_uid_unique_nonempty", "idx_local_registry_legacy_internal_order",
         "idx_local_registry_visible_year", "idx_local_registry_visible_lookup",
         "idx_local_registry_plate_norm", "idx_local_registry_status_opened", "idx_local_registry_closed_at"})
        QTest::newRow(value) << QString(value);
}

void ReportsOrdersTests::canonicalRegistrySchemaFailure()
{
    QFETCH(QString, boundary);
    const QString connection = "canonical-registry-failure";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec("CREATE TABLE order_registry(order_number INTEGER PRIMARY KEY,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER,plate_norm TEXT,status TEXT,opened_at TEXT,closed_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO order_registry VALUES(17,'',0,0,'','','','')"));
    QString removeFault;
    if (boundary.startsWith("idx_")) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(dummy INTEGER)").arg(boundary)));
        removeFault = QString("DROP TABLE %1").arg(boundary);
    } else {
        QVERIFY(q.exec(QString("CREATE TRIGGER fail_registry BEFORE UPDATE OF %1 ON order_registry BEGIN SELECT RAISE(ABORT,'injected registry failure'); END").arg(boundary)));
        removeFault = "DROP TRIGGER fail_registry";
    }
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toInt(), 0); QCOMPARE(q.value(2).toInt(), 0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('order_registry')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 8); q.finish();
    QVERIFY(q.exec(removeFault));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,r.order_uid,w.order_year,r.order_year FROM work_orders w JOIN order_registry r ON r.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next());
    const QString uid = q.value(0).toString();
    QVERIFY(!uid.isEmpty()); QCOMPARE(q.value(1).toString(), uid);
    QCOMPARE(q.value(2).toInt(), 2025); QCOMPARE(q.value(3).toInt(), 2025); q.finish();
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,r.order_uid,r.order_year FROM work_orders w JOIN order_registry r ON r.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QCOMPARE(q.value(1).toString(), uid);
    QCOMPARE(q.value(2).toInt(), 2025); q.finish();
}

void ReportsOrdersTests::canonicalWorkspaceSchemaFailure_data()
{
    QTest::addColumn<QString>("boundary");
    for (const auto& value : {"update", "unique-index", "origin-index"})
        QTest::newRow(value) << QString(value);
}

void ReportsOrdersTests::canonicalWorkspaceSchemaFailure()
{
    QFETCH(QString, boundary);
    const QString connection = "canonical-workspace-failure";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,status TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER)").arg(name)));
    QVERIFY(q.exec("CREATE TABLE workspace_order_state(order_number INTEGER PRIMARY KEY, origin TEXT, order_uid TEXT)"));
    QVERIFY(q.exec("INSERT INTO workspace_order_state VALUES(17,'native','')"));
    QString removeFault;
    if (boundary == "update") {
        QVERIFY(q.exec("CREATE TRIGGER fail_state BEFORE UPDATE ON workspace_order_state BEGIN SELECT RAISE(ABORT,'injected state failure'); END"));
        removeFault = "DROP TRIGGER fail_state";
    } else {
        const QString name = boundary == "unique-index" ? "idx_workspace_order_state_uid_unique" : "idx_workspace_order_state_origin";
        QVERIFY(q.exec(QString("CREATE TABLE %1(dummy INTEGER)").arg(name)));
        removeFault = QString("DROP TABLE %1").arg(name);
    }
    QString error;
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid FROM workspace_order_state WHERE order_number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString()); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM pragma_table_info('work_orders')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE name='settings'"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); q.finish();
    QVERIFY(q.exec(removeFault));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,s.order_uid FROM work_orders w JOIN workspace_order_state s ON s.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next());
    const QString parentUid = q.value(0).toString();
    QVERIFY(!parentUid.isEmpty());
    QCOMPARE(q.value(1).toString(), parentUid); q.finish();
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT w.order_uid,s.order_uid FROM work_orders w JOIN workspace_order_state s ON s.order_number=w.number WHERE w.number=17"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), parentUid);
    QCOMPARE(q.value(1).toString(), parentUid);
}

void ReportsOrdersTests::canonicalSupportNormalizationIsAtomic_data()
{
    QTest::addColumn<QString>("table");
    QTest::addColumn<QString>("column");
    for (const auto& pair : QList<QPair<QString, QString>>{
        {"work_orders", "status"}, {"work_orders", "order_uid"},
        {"work_orders", "order_number"}, {"work_orders", "order_year"},
        {"work_order_services", "order_uid"}, {"work_order_services", "paid"},
        {"work_order_parts", "order_uid"}, {"work_order_parts", "paid"},
        {"work_order_parts", "ordered_by_admin"},
        {"outsource_repairs", "order_uid"}, {"outsource_jobs", "order_uid"}})
        QTest::newRow(qPrintable(pair.first + "-" + pair.second)) << pair.first << pair.second;
}

void ReportsOrdersTests::canonicalSupportNormalizationIsAtomic()
{
    QFETCH(QString, table); QFETCH(QString, column);
    const QString connection = "canonical-support-atomic";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, status TEXT, opened_at TEXT)"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER, paid INTEGER, ordered_by_admin INTEGER)").arg(name)));
    QString error;
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("INSERT INTO work_orders(number,status,opened_at) VALUES(17,'','2025-12-31')"));
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"})
        QVERIFY(q.exec(QString("INSERT INTO %1(order_number) VALUES(17)").arg(name)));
    QVERIFY(q.exec(QString("CREATE TRIGGER injected_failure BEFORE UPDATE OF %1 ON %2 BEGIN SELECT RAISE(ABORT,'injected normalization failure'); END").arg(column, table)));
    QVERIFY(db.transaction());
    QVERIFY(q.exec("INSERT INTO settings(key,value) VALUES('outer-marker','keep')"));
    QVERIFY(!ensureCanonicalRewriteSupportSchema(db, &error));
    QVERIFY2(error.contains("injected normalization failure"), qPrintable(error));
    QVERIFY(q.exec("SELECT status,order_uid,order_number,order_year FROM work_orders WHERE number=17"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString()); QCOMPARE(q.value(1).toString(), QString());
    QCOMPARE(q.value(2).toInt(), 0); QCOMPARE(q.value(3).toInt(), 0); q.finish();
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"}) {
        QVERIFY(q.exec(QString("SELECT order_uid,paid,ordered_by_admin FROM %1").arg(name)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString());
        QVERIFY(q.value(1).isNull()); QVERIFY(q.value(2).isNull()); q.finish();
    }
    QVERIFY(q.exec("SELECT value FROM settings WHERE key='outer-marker'"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("keep")); q.finish();
    QVERIFY(q.exec("DROP TRIGGER injected_failure"));
    QVERIFY2(ensureCanonicalRewriteSupportSchema(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=17"));
    QVERIFY(q.next()); const auto uid = q.value(0).toString(); QVERIFY(!uid.isEmpty());
    QCOMPARE(q.value(1).toInt(), 17); QCOMPARE(q.value(2).toInt(), 2025); q.finish();
    for (const auto& name : {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs"}) {
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1").arg(name)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); q.finish();
    }
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT count(*) FROM settings WHERE key='outer-marker'"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); q.finish();
}

void ReportsOrdersTests::archiveCacheRefreshIsAtomic()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(dir.path()));
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-ArchiveRefresh");
    const QString connection="archive-refresh-atomic";
    auto db=QSqlDatabase::addDatabase("QSQLITE",connection);
    const auto cleanup=qScopeGuard([&]{db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(connection);});
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    const QStringList tables={"router_cache_service_masters","router_cache_outsource_repairs","router_cache_work_order_parts","router_cache_work_order_services","router_cache_work_orders"};
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(7,'stable-uid',1,2025,'2025-01-01','open','AA1234AA','AA1234AA','Test car','Test client')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_uid,order_number,title) VALUES(8,'stable-uid',7,'Confirmed service')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,order_uid,order_number,name,service_id) VALUES(9,'stable-uid',7,'Confirmed part',8)"));
    QVERIFY(q.exec("INSERT INTO router_cache_service_masters(service_id,master_id) VALUES(8,'test-master')"));
    QVERIFY(q.exec("INSERT INTO router_cache_outsource_repairs(id,order_uid,order_number,service_id) VALUES(10,'stable-uid',7,8)"));
    for (const auto& table : tables) {
        const QString active = table.mid(QString("router_cache_").size());
        QVERIFY(q.exec(QString("CREATE TABLE %1 AS SELECT * FROM %2").arg(active,table)));
        QVERIFY(q.exec(QString("CREATE TABLE before_%1 AS SELECT * FROM %1").arg(table)));
    }
    QVERIFY(q.exec("UPDATE work_orders SET client='New client'"));
    for (const auto& operation : {"DELETE", "INSERT"}) for (const auto& boundary : tables) {
        const QString injectedFailure = QString("injected %1 failure on %2").arg(operation, boundary);
        QVERIFY(q.exec(QString("CREATE TRIGGER fail_rebuild BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'%3'); END").arg(boundary,QString(operation),injectedFailure)));
        QString expectedFailure = injectedFailure;
        if (QString(operation) == "INSERT") {
            const QMap<QString, QString> seedNames = {
                {"router_cache_service_masters", "service_masters"},
                {"router_cache_outsource_repairs", "outsource"},
                {"router_cache_work_order_parts", "parts"},
                {"router_cache_work_order_services", "services"},
                {"router_cache_work_orders", "orders"}
            };
            QVERIFY(seedNames.contains(boundary));
            expectedFailure.prepend("Active " + seedNames.value(boundary) + " router-cache seed failed: ");
        }
        // Assert both exact diagnostics of this deliberate failure. Missing messages fail
        // the test; unrelated critical diagnostics remain visible to release acceptance.
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= archive-refresh-atomic  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION step_failed_rollback connection= archive-refresh-atomic  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "
            + QRegularExpression::escape(expectedFailure) + " Unable to fetch row$"));
        QVERIFY(!refreshArchiveIntoRouterCache(db));
        for (const auto& table : tables) {
            for (int direction = 0; direction < 2; ++direction) {
                const QString left = direction ? "before_"+table : table;
                const QString right = direction ? table : "before_"+table;
                QVERIFY(q.exec(QString("SELECT COUNT(*) FROM (SELECT * FROM %1 EXCEPT SELECT * FROM %2)").arg(left,right)));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
            }
        }
        QVERIFY(q.exec("DROP TRIGGER fail_rebuild"));
    }

    for(int repeat=0;repeat<2;++repeat) QVERIFY(refreshArchiveIntoRouterCache(db));
    QVERIFY(q.exec("SELECT client FROM router_cache_work_orders")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("New client")); QVERIFY(!q.next()); q.finish();
}

void ReportsOrdersTests::archiveCacheRebuildIsAtomic()
{
    const QString connection = QStringLiteral("archive-rebuild-atomic");
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    for (const auto& table : {"work_orders", "work_order_services", "work_order_parts"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(id INTEGER)").arg(table)));
    const QStringList tables = {"router_cache_service_masters", "router_cache_outsource_repairs",
        "router_cache_work_order_parts", "router_cache_work_order_services", "router_cache_work_orders"};
    for (const auto& table : tables) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(id INTEGER PRIMARY KEY, payload TEXT)").arg(table)));
        QVERIFY(q.exec(QString("INSERT INTO %1 VALUES(7,'confirmed data')").arg(table)));
    }
    // Every destructive boundary, followed by a failure in the first seed statement.
    for (int boundary = 0; boundary <= tables.size(); ++boundary) {
        if (boundary < tables.size())
            QVERIFY(q.exec(QString("CREATE TRIGGER fail_rebuild BEFORE DELETE ON %1 BEGIN SELECT RAISE(ABORT,'injected rebuild failure'); END").arg(tables[boundary])));
        QVERIFY(db.transaction());
        QVERIFY(q.exec("INSERT INTO work_orders VALUES(99)"));
        QString error;
        QVERIFY(!rebuildRouterCacheFromActiveWorkspaceData(db, &error));
        QVERIFY(!error.isEmpty());
        for (const auto& table : tables) {
            QVERIFY(q.exec(QString("SELECT id,payload FROM %1").arg(table)));
            QVERIFY2(q.next(), qPrintable(table));
            QCOMPARE(q.value(0).toInt(), 7);
            QCOMPARE(q.value(1).toString(), QString("confirmed data"));
            QVERIFY(!q.next()); q.finish();
        }
        // The inner rollback must preserve, and must not commit, the caller's work.
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1); q.finish();
        QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0); q.finish();
        if (boundary < tables.size()) QVERIFY(q.exec("DROP TRIGGER fail_rebuild"));
    }
    // Use the real cache schema to exercise every INSERT boundary, including the last one.
    for (const auto& table : tables) QVERIFY(q.exec(QString("DROP TABLE %1").arg(table)));
    for (const auto& table : {"work_orders", "work_order_services", "work_order_parts"})
        QVERIFY(q.exec(QString("DROP TABLE %1").arg(table)));
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(7,'stable-uid',1,2025,'2025-01-01','open','AA1234AA','AA1234AA','Test car','Test client')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_uid,order_number,title) VALUES(8,'stable-uid',7,'Confirmed service')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,order_uid,order_number,name,service_id) VALUES(9,'stable-uid',7,'Confirmed part',8)"));
    QVERIFY(q.exec("INSERT INTO router_cache_service_masters(service_id,master_id) VALUES(8,'test-master')"));
    QVERIFY(q.exec("INSERT INTO router_cache_outsource_repairs(id,order_uid,order_number,service_id) VALUES(10,'stable-uid',7,8)"));
    for (const auto& table : tables) {
        const QString active = table.mid(QString("router_cache_").size());
        QVERIFY(q.exec(QString("CREATE TABLE %1 AS SELECT * FROM %2").arg(active,table)));
        QVERIFY(q.exec(QString("CREATE TABLE before_%1 AS SELECT * FROM %1").arg(table)));
    }
    QVERIFY(q.exec("UPDATE work_orders SET client='New client'"));
    for (const auto& boundary : tables) {
        QVERIFY(q.exec(QString("CREATE TRIGGER fail_rebuild BEFORE INSERT ON %1 BEGIN SELECT RAISE(ABORT,'injected insert failure'); END").arg(boundary)));
        QString error;
        QVERIFY(!rebuildRouterCacheFromActiveWorkspaceData(db,&error));
        QVERIFY(error.contains("injected insert failure"));
        for (const auto& table : tables) {
            for (int direction = 0; direction < 2; ++direction) {
                const QString left = direction ? "before_"+table : table;
                const QString right = direction ? table : "before_"+table;
                QVERIFY(q.exec(QString("SELECT COUNT(*) FROM (SELECT * FROM %1 EXCEPT SELECT * FROM %2)").arg(left,right)));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
            }
        }
        QVERIFY(q.exec("DROP TRIGGER fail_rebuild"));
    }
    QString error;
    QVERIFY(db.transaction());
    QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT client FROM router_cache_work_orders")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("Test client")); q.finish();
    for (int repeat = 0; repeat < 2; ++repeat)
        QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT client FROM router_cache_work_orders")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("New client")); QVERIFY(!q.next()); q.finish();
}

void ReportsOrdersTests::archiveImportKeepsDistinctOrderIdentities()
{
    const QString connection = "archive-distinct-identities";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'active-first'),(101,'active-second')"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.work_orders VALUES(101,'active-second','2025-01-01'),(200,'archive-only','2026-01-01')"));
    const QStringList columns = QStringLiteral(
        "order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,vin,car_year,odometer,"
        "engine_type,engine_volume,fuel_type,car,client,client_phone,master_id,works_sum,parts_sum,discount_mode,"
        "discount,master_name_closed,car_model,car_brand,car_kind,color,car_color,owner_name").split(',');
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders(number INTEGER PRIMARY KEY," + columns.join(',') + ")"));
    for (const auto& table : {"router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER,order_uid TEXT)").arg(table)));
        QVERIFY(q.exec(QString("INSERT INTO %1 VALUES(101,''),(200,'')").arg(table)));
    }
    ArchiveRouterSourceSchema schema;
    schema.srcOrders = {"number", "order_uid", "opened_at"};
    QString error;
    for (int repeat = 0; repeat < 2; ++repeat) {
        QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
        for (const auto& table : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
            const QString key = QString(table) == "router_cache_work_orders" ? "number" : "order_number";
            QVERIFY(q.exec(QString("SELECT %1,order_uid FROM %2 ORDER BY %1").arg(key, table)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 101); QCOMPARE(q.value(1).toString(), QString("active-second"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 200); QCOMPARE(q.value(1).toString(), QString("archive-only"));
            QVERIFY(!q.next()); q.finish();
        }
    }
    // Both source schemas must preserve four-digit historical years.
    for (int withYearColumn = 0; withYearColumn < 2; ++withYearColumn) {
        if (withYearColumn) {
            QVERIFY(q.exec("ALTER TABLE legacy_router.work_orders ADD COLUMN order_year INTEGER DEFAULT 0"));
            schema.srcOrders.insert("order_year");
        }
        for (const auto& sample : QList<QPair<QString,int>>{
                 {"03.01.2025",2025}, {"31.12.1999 23:59",1999},
                 {"03.01.25/12.30",2025}, {"2026-01-01T00:01:00",2026}}) {
            q.prepare("UPDATE legacy_router.work_orders SET opened_at=? WHERE number=200");
            q.addBindValue(sample.first); QVERIFY(q.exec()); q.finish();
            QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
            QVERIFY(q.exec("SELECT order_year FROM router_cache_work_orders WHERE number=200"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), sample.second); q.finish();
        }
    }
    // Equal legacy numbers do not make two explicitly identified orders identical.
    QVERIFY(q.exec("UPDATE legacy_router.work_orders SET order_uid='archive-distinct' WHERE number=101"));
    for (int repeat = 0; repeat < 2; ++repeat) {
        QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
        for (const auto& table : {"router_cache_work_orders", "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs"}) {
            const QString key = QString(table) == "router_cache_work_orders" ? "number" : "order_number";
            QVERIFY(q.exec(QString("SELECT order_uid FROM %1 WHERE %2=101").arg(table, key)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("archive-distinct"));
            QVERIFY(!q.next()); q.finish();
        }
    }
    // Preserve compatibility for old archives with blank or absent UID columns.
    for (int missingColumn = 0; missingColumn < 2; ++missingColumn) {
        QVERIFY(q.exec("UPDATE legacy_router.work_orders SET order_uid='   '"));
        if (missingColumn) {
            QVERIFY(q.exec("ALTER TABLE legacy_router.work_orders DROP COLUMN order_uid"));
            schema.srcOrders.remove("order_uid");
        }
        QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
        QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_orders ORDER BY number"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("active-second"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("archive_200"));
        QVERIFY(!q.next()); q.finish();
    }
    QVERIFY(q.exec("SELECT order_uid FROM work_orders ORDER BY number"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("active-first"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("active-second"));
}

void ReportsOrdersTests::archiveChildrenHaveIdentityAtInsert()
{
    const QString connection = "archive-child-insert-identity";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QVERIFY(ensureRouterCacheSchema(db));
    QSqlQuery q(db);
    QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'first',1,2025,'2025-01-01','open','','','Car A','Client A'),(200,'second',1,2026,'2026-01-01','open','','','Car B','Client B')"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.work_order_services(id INTEGER,order_number INTEGER,title TEXT,qty REAL,price REAL)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.work_order_services VALUES(8,100,'Work A',1,10),(9,200,'Work B',2,20)"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.work_order_parts(id INTEGER,order_number INTEGER,name TEXT,qty REAL,price REAL)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.work_order_parts VALUES(10,100,'Part A',0.5,10),(11,200,'Part B',2,20)"));
    // Enforce the identity at INSERT, before any post-import UPDATE can conceal it.
    for (const auto& table : {"router_cache_work_order_services", "router_cache_work_order_parts"}) {
        QVERIFY(q.exec(QString("CREATE TRIGGER check_%1 BEFORE INSERT ON %1 WHEN NEW.order_uid IS NOT (SELECT order_uid FROM router_cache_work_orders WHERE number=NEW.order_number) BEGIN SELECT RAISE(ABORT,'incorrect child identity at insert'); END").arg(table)));
    }
    ArchiveRouterSourceSchema schema;
    schema.srcServices = {"id","order_number","title","qty","price"};
    schema.srcParts = {"id","order_number","name","qty","price"};
    QString error;
    for (int repeat=0; repeat<2; ++repeat) {
        QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
        for (const auto& table : {"router_cache_work_order_services", "router_cache_work_order_parts"}) {
            QVERIFY(q.exec(QString("SELECT order_number,order_uid,amount FROM %1 ORDER BY order_number").arg(table)));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),100); QCOMPARE(q.value(1).toString(),QString("first"));
            QCOMPARE(q.value(2).toDouble(),QString(table).endsWith("parts") ? 5.0 : 10.0);
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),200); QCOMPARE(q.value(1).toString(),QString("second"));
            QCOMPARE(q.value(2).toDouble(),40.0); QVERIFY(!q.next()); q.finish();
        }
    }
    // Historical aliases must retain the same parent-key semantics.
    for (const auto& alias : {"work_order", "work_order_id", "work_order_number"}) {
        QVERIFY(q.exec("DROP TABLE legacy_router.work_order_parts"));
        QVERIFY(q.exec(QString("CREATE TABLE legacy_router.work_order_parts(id INTEGER,%1 INTEGER,name TEXT,qty REAL,price REAL)").arg(alias)));
        QVERIFY(q.exec("INSERT INTO legacy_router.work_order_parts VALUES(12,200,'Alias part',1,12)"));
        schema.srcParts = {"id",QString(alias),"name","qty","price"};
        QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT order_number,order_uid,amount FROM router_cache_work_order_parts WHERE id=12"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),200); QCOMPARE(q.value(1).toString(),QString("second"));
        QCOMPARE(q.value(2).toDouble(),12.0); q.finish();
    }
}

void ReportsOrdersTests::archivePartUidTransfer_data()
{
    QTest::addColumn<QString>("phase");
    QTest::newRow("cached-upgrade-archive-service-uid-metadata") << QString("cached-upgrade-archive-service-uid-metadata");
    for(const auto& phase:QStringList{"physical-cache","workspace-cache","hydrate","hydrate-empty","hydrate-duplicate","hydrate-owner","hydrate-ignore","hydrate-root","hydrate-service-ignore","hydrate-header-ignore","hydrate-outer","hydrate-ignore-outer","hydrate-global-duplicate","hydrate-service-parent","direct-import","direct-empty","direct-parent","direct-late","direct-target-uid","direct-target-year","direct-target-number","direct-metadata","cached-workspace-metadata","cached-physical-metadata","cached-upgrade-metadata","cached-upgrade-unavailable-metadata","cached-upgrade-rollback-metadata","cached-upgrade-archive-metadata","cached-upgrade-mixed-metadata","cached-upgrade-archive-missing-metadata","cached-upgrade-archive-owner-metadata","cached-upgrade-archive-failure-metadata","cached-upgrade-outer-metadata"})QTest::newRow(qPrintable(phase))<<phase;
}

void ReportsOrdersTests::archivePartUidTransfer()
{
    QFETCH(QString,phase);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Part-Transfer-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('transfer-master','Master','master')"));
    const QString header="(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'transfer-order',7,2025,'2025-01-02','closed','','','Transfer car','Client','transfer-master')";
    if(phase=="workspace-cache" || phase.startsWith("direct-") || phase.startsWith("cached-")) {
        QVERIFY(q.exec("INSERT INTO work_orders"+header));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,name) VALUES(10,'source-part-a',100,'transfer-order','First archived'),(11,'source-part-b',100,'transfer-order','Second archived')"));
        if(phase.startsWith("direct-") || phase.startsWith("cached-")) {
            QVERIFY(q.exec("INSERT INTO work_order_services(id,service_uid,order_uid,order_number,title) VALUES(8,'source-service-uid','transfer-order',100,'Archived work')"));
            QVERIFY(q.exec("UPDATE work_order_parts SET service_id=8 WHERE order_number=100"));
            if(phase.endsWith("metadata")) {
                QVERIFY(q.exec(R"SQL(UPDATE work_order_parts SET installed_at='2025-01-04',manual_markup_override=1,markup_source='manual',price_input_mode='client',pricing_updated_at='2025-01-03T12:34:56',pricing_calc_version=7,status='installed',installed=1,installed_by='installer',request_uid='request-'||id,requested_at='2025-01-02T10:00:00',requested_by='Requester',requested_by_uid='requester-uid',ordered_by='Buyer',ordered_by_uid='buyer-uid',order_comment='Preserve comment',node_group='engine',component='filter',metadata_json='{"note":"preserve"}',created_at='2025-01-02T09:00:00',updated_at='2025-01-04T11:00:00',qty=0.5,price=17.25,amount=8.63,purchase_price=13.11,purchase_amount=6.56,markup_percent=31.58 WHERE order_number=100)SQL"));
            }
            auto partRows=[&]{QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);QSqlQuery rows(db);if(!rows.exec("SELECT * FROM work_order_parts WHERE order_number=100 ORDER BY part_uid"))return QByteArray("SQL_ERROR");while(rows.next()){const auto record=rows.record();for(int i=0;i<record.count();++i){const auto name=record.fieldName(i);if(name!="id" && name!="service_id")stream<<name<<rows.value(i);}}if(rows.lastError().isValid())return QByteArray("SQL_ERROR");return bytes;};
            const auto originalParts=partRows();QVERIFY(originalParts!="SQL_ERROR");
            if(phase=="direct-late") {
                QVERIFY2(Database::finalizeNewOrderWorkspaceState(100,&error),qPrintable(error));
                QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
            }
            const auto sourcePath=root.filePath("direct-source.db");
            QVERIFY(q.exec("VACUUM INTO "+sqlQuoteText(sourcePath)));
            if(phase!="direct-import" && !phase.endsWith("metadata")) {
                if(phase=="direct-late") {
                    QVERIFY(q.exec("CREATE TRIGGER reject_direct_part BEFORE INSERT ON work_order_parts WHEN NEW.name='Second archived' BEGIN SELECT RAISE(IGNORE); END"));
                    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
                    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= Archive part insert failed: *$"));
                } else if(phase.startsWith("direct-target-")) {
                    if(phase=="direct-target-uid") {
                        // Create a distinct existing target; immutable UID triggers must stay enabled.
                        QVERIFY(q.exec("DELETE FROM work_order_parts WHERE order_number=100"));
                        QVERIFY(q.exec("DELETE FROM work_order_services WHERE order_number=100"));
                        QVERIFY(q.exec("DELETE FROM work_orders WHERE number=100"));
                        QString targetHeader=header;targetHeader.replace("transfer-order","working-order");
                        QVERIFY(q.exec("INSERT INTO work_orders"+targetHeader));
                        QVERIFY(q.exec("INSERT INTO work_order_services(id,order_uid,order_number,title) VALUES(8,'working-order',100,'Existing work')"));
                        QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,name,service_id) VALUES(10,'source-part-a',100,'working-order','Existing first',8),(11,'source-part-b',100,'working-order','Existing second',8)"));
                    } else {
                        QVERIFY(q.exec(phase=="direct-target-year"?"UPDATE work_orders SET order_year=2024 WHERE number=100":"UPDATE work_orders SET order_number=8 WHERE number=100"));
                    }
                } else {
                    QVERIFY(q.exec("ATTACH DATABASE "+sqlQuoteText(sourcePath)+" AS fixture_source"));
                    QVERIFY(q.exec(phase=="direct-empty"?"UPDATE fixture_source.work_order_parts SET part_uid='' WHERE id=10":"UPDATE fixture_source.work_order_parts SET order_uid='other-order' WHERE id=10"));
                    QVERIFY(q.exec("DETACH DATABASE fixture_source"));
                }
                auto snapshot=[&]{QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts","masters","workspace_order_state","order_registry","router_cache_work_orders","router_cache_work_order_services","router_cache_work_order_parts","service_masters","outsource_repairs","outsource_jobs","work_order_part_attachments","work_order_service_attachments"}){QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
                const auto before=snapshot();QVERIFY(before!="SQL_ERROR");
                QVERIFY2(!importSingleArchivedOrderToActiveFromPath(sourcePath,100,&error),"Invalid archive identity must be refused before purging the working order");
                QVERIFY(!error.isEmpty());if(phase=="direct-late")QCOMPARE(error,QString("Archive part insert failed: "));QCOMPARE(snapshot(),before);
                return;
            }
            if(phase=="cached-workspace-metadata" || phase.startsWith("cached-upgrade-")) {
                QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
                if(phase.startsWith("cached-upgrade-")) {
                    for(const auto& name:QStringList{"status","installed","installed_by","request_uid","requested_at","requested_by","requested_by_uid","ordered_by","ordered_by_uid","order_comment","node_group","component","metadata_json","created_at","updated_at"})
                        QVERIFY(q.exec("ALTER TABLE router_cache_work_order_parts DROP COLUMN "+sqlQuoteIdent(name)));
                    QVERIFY(ensureRouterCacheSchema(db));
                    QVERIFY(!routerCacheContentReady(db));
                    // Repeated schema checks must not erase a persisted invalidation.
                    QVERIFY(ensureRouterCacheSchema(db));
                    QVERIFY(!routerCacheContentReady(db));
                    if(phase!="cached-upgrade-metadata" && phase!="cached-upgrade-outer-metadata" && !phase.startsWith("cached-upgrade-archive-") && phase!="cached-upgrade-mixed-metadata") {
                        auto cachedRows=[&]{QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:QStringList{"router_cache_work_orders","router_cache_work_order_services","router_cache_work_order_parts","router_cache_content_state"}){QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
                        if(phase=="cached-upgrade-unavailable-metadata") {
                            QVERIFY(q.exec("DELETE FROM work_order_parts WHERE order_number=100"));
                        } else {
                            QVERIFY(q.exec("CREATE TRIGGER fail_cache_upgrade BEFORE INSERT ON router_cache_work_order_parts WHEN NEW.part_uid='source-part-b' BEGIN SELECT RAISE(ABORT,'injected cache rebuild failure'); END"));
                        }
                        const auto before=cachedRows();QVERIFY(before!="SQL_ERROR");
                        const auto workingBefore=partRows();QVERIFY(workingBefore!="SQL_ERROR");
                        QVERIFY(!backfillRouterCacheFromActiveIfNeeded(db,&error));QVERIFY(!error.isEmpty());
                        QCOMPARE(cachedRows(),before);QCOMPARE(partRows(),workingBefore);
                        QVERIFY(!routerCacheContentReady(db));
                        QVERIFY(!hydrateCachedOrderToActive(db,"transfer-order",100,&error));QVERIFY(!error.isEmpty());
                        QCOMPARE(cachedRows(),before);QCOMPARE(partRows(),workingBefore);
                        return;
                    }
                    if(phase.startsWith("cached-upgrade-archive-") || phase=="cached-upgrade-mixed-metadata") {
                        const QString archivePath=AppRuntime::archiveDbFilePath();
                        QVERIFY(QDir::cleanPath(archivePath).startsWith(QDir::cleanPath(root.path())+"/"));
                        QVERIFY(QFileInfo(archivePath).absoluteFilePath()!=QFileInfo(db.databaseName()).absoluteFilePath());
                        for(const auto& connection:QSqlDatabase::connectionNames()) {
                            const auto handle=QSqlDatabase::database(connection,false);
                            QVERIFY(!handle.isOpen() || QFileInfo(handle.databaseName()).absoluteFilePath()!=QFileInfo(archivePath).absoluteFilePath());
                        }
                        QVERIFY(!QFileInfo::exists(archivePath+"-wal"));
                        QVERIFY(!QFileInfo::exists(archivePath) || QFile::remove(archivePath));
                        QVERIFY(QDir().mkpath(QFileInfo(archivePath).absolutePath()));
                        QVERIFY(QFile::copy(sourcePath,archivePath));
                        if(phase=="cached-upgrade-mixed-metadata") {
                            QVERIFY(q.exec("ATTACH DATABASE "+sqlQuoteText(archivePath)+" AS fixture_archive"));
                            QVERIFY(q.exec("UPDATE fixture_archive.work_order_parts SET order_comment='Stale archive comment',metadata_json='{}' WHERE order_number=100"));
                            QString archivedHeader=header;archivedHeader.replace("(100,'transfer-order',7,","(200,'archive-only',8,");
                            QVERIFY(q.exec("INSERT INTO fixture_archive.work_orders"+archivedHeader));
                            QVERIFY(q.exec("INSERT INTO fixture_archive.work_order_services(id,order_uid,order_number,title) VALUES(18,'archive-only',200,'Archive only work')"));
                            QVERIFY(q.exec("INSERT INTO fixture_archive.work_order_parts(id,part_uid,order_uid,order_number,name,service_id,order_comment) VALUES(20,'archive-only-part','archive-only',200,'Archive only part',18,'Archive only comment')"));
                            QVERIFY(q.exec("DETACH DATABASE fixture_archive"));
                        } else {
                            QVERIFY(q.exec("DELETE FROM work_order_parts WHERE order_number=100"));
                            QVERIFY(q.exec("DELETE FROM work_order_services WHERE order_number=100"));
                            QVERIFY(q.exec("DELETE FROM work_orders WHERE number=100"));
                        }
                        if(phase=="cached-upgrade-archive-missing-metadata" || phase=="cached-upgrade-archive-owner-metadata" || phase=="cached-upgrade-archive-failure-metadata" || phase=="cached-upgrade-archive-service-uid-metadata") {
                            QString expectedFailure;
                            if(phase=="cached-upgrade-archive-failure-metadata") {
                                QVERIFY(q.exec("CREATE TRIGGER fail_physical_recovery BEFORE INSERT ON router_cache_work_order_parts WHEN NEW.part_uid='source-part-b' BEGIN SELECT RAISE(ABORT,'injected physical rebuild failure'); END"));
                                expectedFailure="Legacy parts import to router cache failed: injected physical rebuild failure Unable to fetch row";
                            } else {
                                QVERIFY(q.exec("ATTACH DATABASE "+sqlQuoteText(archivePath)+" AS fixture_archive"));
                                if(phase=="cached-upgrade-archive-missing-metadata") {
                                    QVERIFY(q.exec("DELETE FROM fixture_archive.work_order_parts WHERE part_uid='source-part-b'"));
                                    expectedFailure="Router cache contains identities absent from authoritative sources";
                                } else if(phase=="cached-upgrade-archive-service-uid-metadata") {
                                    QVERIFY(q.exec("UPDATE router_cache_work_order_services SET service_uid='different-service-uid' WHERE id=8"));
                                    expectedFailure="Router cache contains identities absent from authoritative sources";
                                } else {
                                    QVERIFY(q.exec("UPDATE fixture_archive.work_order_parts SET order_uid='foreign-owner' WHERE part_uid='source-part-b'"));
                                    expectedFailure="Cache source has conflicting child ownership";
                                }
                                QVERIFY(q.exec("DETACH DATABASE fixture_archive"));
                            }
                            auto snapshot=[&]{QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:QStringList{"router_cache_work_orders","router_cache_work_order_services","router_cache_work_order_parts","router_cache_outsource_repairs","router_cache_service_masters","router_cache_content_state","work_orders","work_order_services","work_order_parts","masters","workspace_order_state","order_registry"}){QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
                            const auto before=snapshot();QVERIFY(before!="SQL_ERROR");
                            QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
                            QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "+QRegularExpression::escape(expectedFailure)+"$"));
                            QVERIFY(!refreshArchiveIntoRouterCache(db));
                            QCOMPARE(snapshot(),before);QVERIFY(!routerCacheContentReady(db));
                            QVERIFY(!hydrateCachedOrderToActive(db,"transfer-order",100,&error));QVERIFY(!error.isEmpty());
                            QCOMPARE(snapshot(),before);
                            if(phase!="cached-upgrade-archive-failure-metadata")return;
                            QVERIFY(q.exec("DROP TRIGGER fail_physical_recovery"));
                        }
                        QVERIFY2(refreshArchiveIntoRouterCache(db),"Existing physical archive must reconstruct the old cache before hydration");
                        if(phase=="cached-upgrade-mixed-metadata") {
                            QCOMPARE(partRows(),originalParts);
                            QVERIFY(q.exec("SELECT p.order_comment,s.order_uid,o.order_number FROM router_cache_work_order_parts p JOIN router_cache_work_order_services s ON s.id=p.service_id JOIN router_cache_work_orders o ON o.order_uid=p.order_uid WHERE p.part_uid='archive-only-part'"));
                            QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Archive only comment"));QCOMPARE(q.value(1).toString(),QString("archive-only"));QCOMPARE(q.value(2).toInt(),8);QVERIFY(!q.next());q.finish();
                        }
                    } else {
                        if(phase=="cached-upgrade-outer-metadata") {
                            QVERIFY(q.exec("SAVEPOINT outer_cache_upgrade"));
                            QVERIFY2(backfillRouterCacheFromActiveIfNeeded(db,&error),qPrintable(error));
                            QVERIFY(routerCacheContentReady(db));
                            QVERIFY(q.exec("ROLLBACK TO SAVEPOINT outer_cache_upgrade"));
                            QVERIFY(q.exec("RELEASE SAVEPOINT outer_cache_upgrade"));
                            QVERIFY(!routerCacheContentReady(db));
                            QVERIFY(q.exec("SELECT COUNT(*) FROM router_cache_work_order_parts WHERE order_comment=''"));
                            QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
                            QCOMPARE(partRows(),originalParts);
                        }
                        QVERIFY2(backfillRouterCacheFromActiveIfNeeded(db,&error),qPrintable(error));
                    }
                    QVERIFY(routerCacheContentReady(db));
                    if(phase=="cached-upgrade-archive-metadata") {
                        QVERIFY(q.exec("SELECT service_uid FROM router_work_order_services WHERE order_uid='transfer-order' AND id=8"));
                        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-service-uid"));q.finish();
                        QVERIFY(q.exec("SELECT part_uid FROM router_work_order_parts WHERE order_uid='transfer-order' AND id=10"));
                        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-a"));q.finish();
                    }
                }
            } else if(phase=="cached-physical-metadata") {
                QVERIFY(q.exec("INSERT INTO router_cache_work_orders"+header));
                QVERIFY(q.exec("ATTACH DATABASE "+sqlQuoteText(sourcePath)+" AS legacy_router"));
                ArchiveRouterSourceSchema schema;
                schema.srcParts=tableColumnsForDb(db,"work_order_parts","legacy_router");
                schema.srcServices=tableColumnsForDb(db,"work_order_services","legacy_router");
                QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
                QVERIFY(q.exec("DETACH DATABASE legacy_router"));
            }
            // Prepare an empty target explicitly; this fixture does not assume cascade deletion.
            QVERIFY(q.exec("DELETE FROM work_order_parts WHERE order_number=100"));
            QVERIFY(q.exec("DELETE FROM work_order_services WHERE order_number=100"));
            QVERIFY(q.exec("DELETE FROM work_orders WHERE number=100"));
            QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts WHERE order_number=100"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
            if(phase.startsWith("cached-")) {
                QVERIFY2(hydrateCachedOrderToActive(db,"transfer-order",100,&error),qPrintable(error));
            } else {
                QVERIFY2(importSingleArchivedOrderToActiveFromPath(sourcePath,100,&error),qPrintable(error));
            }
            QVERIFY(q.exec("SELECT p.part_uid,p.order_uid,s.order_uid FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.order_number=100 ORDER BY p.name"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-a"));QCOMPARE(q.value(1).toString(),QString("transfer-order"));QCOMPARE(q.value(2).toString(),QString("transfer-order"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-b"));QVERIFY(!q.next());q.finish();
            if(phase.endsWith("metadata"))QCOMPARE(partRows(),originalParts);
            return;
        }
        QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
    } else {
        QVERIFY(q.exec("INSERT INTO router_cache_work_orders"+header));
        if(phase=="physical-cache") {
            QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
            QVERIFY(q.exec("CREATE TABLE legacy_router.work_order_parts(id INTEGER,part_uid TEXT,order_uid TEXT,order_number INTEGER,name TEXT,qty REAL,price REAL)"));
            QVERIFY(q.exec("INSERT INTO legacy_router.work_order_parts VALUES(10,'source-part-a','transfer-order',100,'First archived',1,10),(11,'source-part-b','transfer-order',100,'Second archived',1,20)"));
            ArchiveRouterSourceSchema schema;schema.srcParts={"id","part_uid","order_uid","order_number","name","qty","price"};
            QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
        }
    }
    if(phase=="physical-cache" || phase=="workspace-cache") {
        QVERIFY2(tableColumnsForDb(db,"router_cache_work_order_parts").contains("part_uid"),"Cache schema must carry the source immutable part UID");
        QVERIFY(q.exec("SELECT part_uid,order_uid FROM router_cache_work_order_parts ORDER BY id"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-a"));QCOMPARE(q.value(1).toString(),QString("transfer-order"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-b"));QVERIFY(!q.next());return;
    }
    // Supply an already verified cache explicitly to isolate the hydration boundary.
    if(!tableColumnsForDb(db,"router_cache_work_order_parts").contains("part_uid"))QVERIFY(q.exec("ALTER TABLE router_cache_work_order_parts ADD COLUMN part_uid TEXT NOT NULL DEFAULT ''"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,order_uid,title) VALUES(8,100,'transfer-order','Archived work')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,part_uid,order_number,order_uid,name,service_id) VALUES(10,'source-part-a',100,'transfer-order','First archived',8),(11,'source-part-b',100,'transfer-order','Second archived',8)"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,name) VALUES(1,'unrelated-stock',0,'','Preserved stock')"));
    if(phase=="hydrate-global-duplicate")QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts(id,part_uid,order_number,order_uid,name) VALUES(12,'source-part-a',200,'other-order','Competing owner')"));
    if(phase=="hydrate-service-parent")QVERIFY(q.exec("UPDATE router_cache_work_order_services SET order_number=200 WHERE id=8"));
    if(phase=="hydrate-empty")QVERIFY(q.exec("UPDATE router_cache_work_order_parts SET part_uid='' WHERE id=11"));
    if(phase=="hydrate-duplicate")QVERIFY(q.exec("UPDATE router_cache_work_order_parts SET part_uid='source-part-a' WHERE id=11"));
    if(phase=="hydrate-owner")QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='source-part-a' WHERE id=1"));
    if(phase=="hydrate-ignore" || phase=="hydrate-ignore-outer")QVERIFY(q.exec("CREATE TRIGGER reject_hydrated_part BEFORE INSERT ON work_order_parts WHEN NEW.name='Second archived' BEGIN SELECT RAISE(IGNORE); END"));
    if(phase=="hydrate-service-ignore")QVERIFY(q.exec("CREATE TRIGGER reject_hydrated_service BEFORE INSERT ON work_order_services BEGIN SELECT RAISE(IGNORE); END"));
    if(phase=="hydrate-header-ignore")QVERIFY(q.exec("CREATE TRIGGER reject_hydrated_header BEFORE INSERT ON work_orders BEGIN SELECT RAISE(IGNORE); END"));
    auto snapshot=[&]{QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts","masters","router_cache_work_orders","router_cache_work_order_services","router_cache_work_order_parts"}){QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");
    const bool outer=phase=="hydrate-outer" || phase=="hydrate-ignore-outer";
    if(outer)QVERIFY(db.transaction());
    const bool ok=hydrateCachedOrderToActive(db,phase=="hydrate-root"?QString("other-order"):QString("transfer-order"),100,&error);
    if(phase!="hydrate" && phase!="hydrate-outer") {QVERIFY2(!ok,"Hydration must refuse unknown/conflicting immutable identity or ignored writes");QVERIFY(!error.isEmpty());QCOMPARE(snapshot(),before);if(outer){QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('outer-marker','Outer','master')"));QVERIFY(db.rollback());QCOMPARE(snapshot(),before);}return;}
    QVERIFY2(ok,qPrintable(error));
    QVERIFY(q.exec("SELECT p.part_uid,p.order_uid,p.id,s.order_uid FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.order_number=100 ORDER BY p.part_uid"));
    QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-a"));QCOMPARE(q.value(1).toString(),QString("transfer-order"));QVERIFY(q.value(2).toInt()!=10);QCOMPARE(q.value(3).toString(),QString("transfer-order"));
    QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("source-part-b"));QVERIFY(!q.next());q.finish();
    const auto hydrated=snapshot();QVERIFY2(hydrateCachedOrderToActive(db,"transfer-order",100,&error),qPrintable(error));QCOMPARE(snapshot(),hydrated);
    if(outer){QVERIFY(db.rollback());QCOMPARE(snapshot(),before);}
}

void ReportsOrdersTests::archivePartsKeepServiceLink()
{
    const QString connection = "archive-parts-service-link";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QVERIFY(ensureRouterCacheSchema(db));
    QSqlQuery q(db);
    QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'first',1,2025,'2025-01-01','open','','','Car A','Client A'),(200,'second',1,2026,'2026-01-01','open','','','Car B','Client B')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,order_uid,title) VALUES(8,100,'first','First work'),(9,200,'second','Second work')"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.work_order_parts(id INTEGER,work_order_id INTEGER,service_id INTEGER,name TEXT,qty REAL,price REAL)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.work_order_parts VALUES(10,100,8,'Part A',2,10),(11,200,9,'Part B',0.5,20),(12,100,0,'Unlinked',1,5)"));
    ArchiveRouterSourceSchema schema;
    schema.srcParts = {"id","work_order_id","service_id","name","qty","price"};
    QString error;
    for (int repeat=0; repeat<2; ++repeat) {
        QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT id,order_number,order_uid,service_id,amount FROM router_cache_work_order_parts ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),10); QCOMPARE(q.value(1).toInt(),100);
        QCOMPARE(q.value(2).toString(),QString("first")); QCOMPARE(q.value(3).toInt(),8); QCOMPARE(q.value(4).toDouble(),20.0);
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),11); QCOMPARE(q.value(1).toInt(),200);
        QCOMPARE(q.value(2).toString(),QString("second")); QCOMPARE(q.value(3).toInt(),9); QCOMPARE(q.value(4).toDouble(),10.0);
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),12); QCOMPARE(q.value(3).toInt(),0);
        QVERIFY(!q.next()); q.finish();
    }
    for (int invalidService : {9,999}) {
        QVERIFY(q.exec(QString("INSERT INTO legacy_router.work_order_parts VALUES(13,100,%1,'Conflicting',1,7)").arg(invalidService)));
        QVERIFY(!importArchiveSourceIntoRouterCache(db,schema,&error));
        QVERIFY2(error.contains(QString("order=100 service=%1").arg(invalidService)),qPrintable(error));
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount) FROM router_cache_work_order_parts"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),3); QCOMPARE(q.value(1).toDouble(),35.0); q.finish();
        QVERIFY(q.exec("SELECT service_id FROM legacy_router.work_order_parts WHERE id=13"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),invalidService); q.finish();
        QVERIFY(q.exec("DELETE FROM legacy_router.work_order_parts WHERE id=13"));
    }
    QVERIFY(q.exec("INSERT INTO legacy_router.work_order_parts VALUES(13,100,8,'Resolved',1,7)"));
    QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT COUNT(*),SUM(amount) FROM router_cache_work_order_parts"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),4); QCOMPARE(q.value(1).toDouble(),42.0);
}

void ReportsOrdersTests::archiveOutsourceKeepsServiceLink()
{
    const QString connection = "archive-outsource-link";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QVERIFY(ensureRouterCacheSchema(db));
    QSqlQuery q(db);
    QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'first',1,2025,'2025-01-01','open','','','Car A','Client A'),(200,'second',1,2026,'2026-01-01','open','','','Car B','Client B')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,order_uid,title) VALUES(8,100,'first','First work'),(9,200,'second','Second work')"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.outsource_repairs(id INTEGER,order_number INTEGER,service_id INTEGER)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.outsource_repairs VALUES(10,200,9),(11,100,8)"));
    ArchiveRouterSourceSchema schema;
    schema.srcOutsource = {"id", "order_number", "service_id"};
    QString error;
    for (int repeat=0; repeat<2; ++repeat) {
        QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
        QVERIFY(q.exec("SELECT id,order_number,order_uid,service_id FROM router_cache_outsource_repairs ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),10); QCOMPARE(q.value(1).toInt(),200);
        QCOMPARE(q.value(2).toString(),QString("second")); QCOMPARE(q.value(3).toInt(),9);
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),11); QCOMPARE(q.value(1).toInt(),100);
        QCOMPARE(q.value(2).toString(),QString("first")); QCOMPARE(q.value(3).toInt(),8);

        QVERIFY(!q.next()); q.finish();
    }
    QVERIFY(q.exec("INSERT INTO legacy_router.outsource_repairs VALUES(12,100,9)"));
    QVERIFY(!importArchiveSourceIntoRouterCache(db,schema,&error));
    QVERIFY(error.contains("order=100 service=9"));
    QVERIFY(q.exec("SELECT COUNT(*) FROM router_cache_outsource_repairs"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2); q.finish();
    QVERIFY(q.exec("SELECT service_id FROM legacy_router.outsource_repairs WHERE id=12"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),9); q.finish();
    // Explicit resolution followed by retry must migrate all three records.
    QVERIFY(q.exec("UPDATE legacy_router.outsource_repairs SET order_number=200 WHERE id=12"));
    QVERIFY2(importArchiveSourceIntoRouterCache(db,schema,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_number,service_id FROM router_cache_outsource_repairs WHERE id=12"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),200); QCOMPARE(q.value(1).toInt(),9);
}

void ReportsOrdersTests::archiveCacheImportIsAtomic()
{
    const QString connection = "archive-import-atomic";
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:"); QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS legacy_router"));
    QVERIFY(q.exec("CREATE TABLE legacy_router.masters(id TEXT, name TEXT)"));
    QVERIFY(q.exec("INSERT INTO legacy_router.masters VALUES('test-master','Migration test')"));
    QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY, name TEXT, role TEXT, admin_percent REAL, active INTEGER)"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders(number INTEGER PRIMARY KEY, order_uid TEXT)"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders VALUES(100,'archived-uid')"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_order_services(order_number INTEGER, order_uid TEXT)"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_order_services VALUES(100,'')"));
    ArchiveRouterSourceSchema schema;
    schema.srcMasters = {"id", "name"};
    QString error;
    // The late parts normalization fails after master import and service update.
    QVERIFY(!importArchiveSourceIntoRouterCache(db, schema, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT COUNT(*) FROM masters")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); q.finish();
    QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_order_services")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString()); q.finish();
    for (const auto& table : {"router_cache_work_order_parts", "router_cache_outsource_repairs"})
        QVERIFY(q.exec(QString("CREATE TABLE %1(order_number INTEGER, order_uid TEXT)").arg(table)));
    QVERIFY(db.transaction());
    QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT COUNT(*) FROM masters")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); q.finish();
    QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
    QVERIFY2(importArchiveSourceIntoRouterCache(db, schema, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT COUNT(*) FROM masters")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1); q.finish();
    QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_order_services")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("archived-uid"));
}

void ReportsOrdersTests::periodSnapshotReadFailures()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("period.sqlite");
    const auto before = QSqlDatabase::connectionNames();
    auto result = ReportsDataLoader::buildPeriodSnapshot(path, "2026-01-01", "2026-12-31");
    QVERIFY(result.rows.isEmpty());
    QVERIFY(result.stats.join(" ").contains(QString::fromUtf8("Помилка")));
    QVERIFY(!QFile::exists(path));
    QCOMPARE(QSqlDatabase::connectionNames(), before);

    const QString connection = "period-fixture-" + QUuid::createUuid().toString(QUuid::Id128);
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE unrelated(id INTEGER)"));
        result = ReportsDataLoader::buildPeriodSnapshot(path, "2026-01-01", "2026-12-31");
        QVERIFY(result.rows.isEmpty());
        QVERIFY(result.stats.join(" ").contains(QString::fromUtf8("Помилка")));
        QCOMPARE(result.stats.size(), 1);
        QVERIFY(query.exec("CREATE TABLE masters(id INTEGER PRIMARY KEY,name TEXT)"));
        QVERIFY(query.exec("CREATE TABLE router_work_orders(number INTEGER,order_number INTEGER,closed_at TEXT,opened_at TEXT,car TEXT,master_name_closed TEXT,master_id INTEGER,works_sum REAL,parts_sum REAL)"));
        result = ReportsDataLoader::buildPeriodSnapshot(path, "2026-01-01", "2026-12-31");
        QCOMPARE(result.stats.size(), 4);
        QVERIFY(result.rows.isEmpty());
        QVERIFY(!result.stats.join(" ").contains(QString::fromUtf8("Помилка")));
        QVERIFY(query.exec("INSERT INTO router_work_orders VALUES(31,7,'','2026-06-01','Test car','Test master',NULL,12.50,8.25)"));
        result = ReportsDataLoader::buildPeriodSnapshot(path, "2026-01-01", "2026-12-31");
        QCOMPARE(result.rows.size(), 1);
        QCOMPARE(result.rows.first().internalOrderNumber, 31);
        QCOMPARE(result.rows.first().orderNumber, QString("7"));
        QCOMPARE(result.stats.size(), 4);
    }
    QSqlDatabase::removeDatabase(connection);
    QCOMPARE(QSqlDatabase::connectionNames(), before);
}

void ReportsOrdersTests::outsourceSnapshotReadFailures()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("outsource.sqlite");
    const auto before = QSqlDatabase::connectionNames();
    const auto read = [&] {
        return ReportsDataLoader::buildOutsourceSnapshot(path, "2026-01-01", "2026-12-31");
    };
    auto snapshot = read();
    QVERIFY(!QFile::exists(path));
    QVERIFY(snapshot.rows.isEmpty());
    QVERIFY(snapshot.stats.join(" ").contains(QStringLiteral("Помилка")));
    QCOMPARE(QSqlDatabase::connectionNames(), before);
    const auto conn = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE router_work_orders(number INTEGER, order_number INTEGER)"));
        snapshot = read();
        QVERIFY(snapshot.rows.isEmpty());
        QCOMPARE(snapshot.stats.size(), 1);
        QVERIFY(snapshot.stats.front().contains(QStringLiteral("Помилка")));
        QVERIFY(q.exec("CREATE TABLE outsource_jobs(id INTEGER, service_id INTEGER, sent_date TEXT, return_date TEXT, company_name TEXT, order_number INTEGER, part_name TEXT, company_price REAL, client_price REAL)"));
        snapshot = read();
        QVERIFY(snapshot.rows.isEmpty());
        QCOMPARE(snapshot.stats.size(), 3);
        QVERIFY(q.exec("INSERT INTO router_work_orders VALUES (31,7)"));
        QVERIFY(q.exec("INSERT INTO outsource_jobs VALUES (1,4,'2026-06-01','','Company',31,'Part',20,35)"));
        snapshot = read();
        QCOMPARE(snapshot.rows.size(), 1);
        QCOMPARE(snapshot.rows.front().internalOrderNumber, 31);
        QCOMPARE(snapshot.rows.front().orderNumber, QString("7"));
        QCOMPARE(snapshot.stats.size(), 3);
    }
    QSqlDatabase::removeDatabase(conn);
    QCOMPARE(QSqlDatabase::connectionNames(), before);
}

void ReportsOrdersTests::identityRepairRollsBackOnIndexFailure()
{
    const QString connection = QStringLiteral("identity-repair-fault");
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT, order_year INTEGER, order_number INTEGER, opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'',2025,7,'2025-01-02')"));
    // A malformed legacy cache causes a late DDL failure after parent repair.
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders(number INTEGER PRIMARY KEY, order_uid TEXT)"));
    QString error;
    QVERIFY(!OrderIdentityService::ensureCanonicalIdentityIndexes(db, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=100"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toInt(), 2025);
    QCOMPARE(q.value(2).toInt(), 7);
    QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE type='index' AND name='idx_work_orders_uid_unique'"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    QVERIFY(q.exec("PRAGMA foreign_keys"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QVERIFY(q.exec("DROP TABLE router_cache_work_orders"));
    error.clear();
    QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=100"));
    QVERIFY(q.next());
    const auto uid = q.value(0).toString();
    QVERIFY(!uid.isEmpty());
    q.finish();
    QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=100"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), uid);
    q.finish();
    // A successful nested repair must not commit the caller's transaction.
    QVERIFY(db.transaction());
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(101,'',2025,8,'2025-01-03')"));
    QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db, &error), qPrintable(error));
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT count(*) FROM work_orders WHERE number=101"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void ReportsOrdersTests::identityConflictsMustNotRenumberHistory()
{
    const QString connection = "identity-conflict";
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT)"));
    for (const bool duplicateUid : {false, true}) {
        QVERIFY(q.exec("DELETE FROM work_orders"));
        QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'original',2025,7,'2025-01-02')"));
        QVERIFY(q.exec(duplicateUid
            ? "INSERT INTO work_orders VALUES(101,'original',2025,8,'2025-01-03')"
            : "INSERT INTO work_orders VALUES(101,'second',2025,7,'2025-01-03')"));
        QString error;
        QVERIFY(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=101"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString(duplicateUid ? "original" : "second"));
        QCOMPARE(q.value(1).toInt(),2025); QCOMPARE(q.value(2).toInt(),duplicateUid ? 8 : 7); q.finish();
    }
    // A collision can appear only after a missing legacy year is filled.
    QVERIFY(q.exec("DELETE FROM work_orders"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'original',2025,7,'2025-01-02')"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(101,'second',0,7,'2025-01-03')"));
    QString error;
    QVERIFY(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=101"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("second"));
    QCOMPARE(q.value(1).toInt(),0); QCOMPARE(q.value(2).toInt(),7); q.finish();
}

void ReportsOrdersTests::conflictingChildIdentityStopsMigration()
{
    const QString connection = "identity-child-conflict";
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'first',2025,7,'2025-01-02'),(101,'second',2026,7,'2026-01-02')"));
    const QStringList tables = {"work_order_services", "work_order_parts", "outsource_repairs", "outsource_jobs",
        "router_cache_work_order_services", "router_cache_work_order_parts", "router_cache_outsource_repairs", "router_cache_outsource_jobs"};
    for (const auto& table : tables) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT)").arg(table)));
        QVERIFY(q.exec(QString("INSERT INTO %1 VALUES(1,100,'second'),(2,101,'')").arg(table)));
        QString error;
        QVERIFY2(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(table));
        QVERIFY(error.contains(table));
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1 ORDER BY id").arg(table)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("second"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString()); q.finish();
        // Explicit fixture correction permits filling a missing link on retry.
        QVERIFY(q.exec(QString("UPDATE %1 SET order_uid='first' WHERE id=1").arg(table)));
        error.clear(); QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(error));
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1 ORDER BY id").arg(table)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("first"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("second")); q.finish();
        QVERIFY(q.exec(QString("DROP TABLE %1").arg(table)));
    }
}

void ReportsOrdersTests::conflictingCacheIdentityStopsMigration()
{
    const QString connection = "identity-cache-conflict";
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'first',2025,7,'2025-01-02'),(101,'second',2026,7,'2026-01-02')"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER)"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders VALUES(100,'different',2025,7),(101,'',2026,7)"));
    QString error;
    QVERIFY(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error));
    QVERIFY(error.contains("router_cache_work_orders"));
    QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_orders ORDER BY number"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("different"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString()); q.finish();
    QVERIFY(q.exec("UPDATE router_cache_work_orders SET order_uid='first' WHERE number=100"));
    error.clear(); QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=101"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("second"));
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2026);
}

void ReportsOrdersTests::archivedCacheChildrenKeepParentIdentity()
{
    const QString connection = "identity-archived-cache";
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(101,'active',2026,7,'2026-01-02')"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER)"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders VALUES(100,'archived',2025,7)"));
    const QStringList tables = {"router_cache_work_order_services", "router_cache_work_order_parts",
        "router_cache_outsource_repairs", "router_cache_outsource_jobs"};
    for (const auto& table : tables) {
        QVERIFY(q.exec(QString("CREATE TABLE %1(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT)").arg(table)));
        QVERIFY(q.exec(QString("INSERT INTO %1 VALUES(1,100,'active'),(2,100,'')").arg(table)));
        QString error;
        QVERIFY2(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(table));
        QVERIFY(error.contains(table));
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1 ORDER BY id").arg(table)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("active"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString()); q.finish();
        QVERIFY(q.exec(QString("UPDATE %1 SET order_uid='archived' WHERE id=1").arg(table)));
        error.clear(); QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(error));
        QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(error));
        QVERIFY(q.exec(QString("SELECT order_uid FROM %1 ORDER BY id").arg(table)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("archived"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("archived")); q.finish();
        QVERIFY(q.exec(QString("DROP TABLE %1").arg(table)));
    }
}

void ReportsOrdersTests::missingIdentityYearRequiresValidDate()
{
    const QString connection = "identity-invalid-date";
    auto cleanupConnection = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(":memory:"); QVERIFY(db.open()); QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_year INTEGER,order_number INTEGER,opened_at TEXT)"));
    for (const auto& date : QStringList{"", "unknown", "31.02.2025", "2025-02-29", "00.01.25"}) {
        QVERIFY(q.exec("DELETE FROM work_orders"));
        q.prepare("INSERT INTO work_orders VALUES(100,'',0,7,?)"); q.addBindValue(date); QVERIFY(q.exec());
        QString error;
        QVERIFY2(!OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error), qPrintable(date));
        QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString()); QCOMPARE(q.value(1).toInt(),0); QCOMPARE(q.value(2).toInt(),7); q.finish();
    }
    for (const auto& date : QStringList{"03.01.2025", "03.01.25/09.30", "2025-01-03T09:30:00"}) {
        QVERIFY(q.exec("DELETE FROM work_orders"));
        q.prepare("INSERT INTO work_orders VALUES(100,'preserved',0,7,?)"); q.addBindValue(date); QVERIFY(q.exec());
        QString error; QVERIFY2(OrderIdentityService::ensureCanonicalIdentityIndexes(db,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("preserved")); QCOMPARE(q.value(1).toInt(),2025); QCOMPARE(q.value(2).toInt(),7); q.finish();
    }
}

void ReportsOrdersTests::bootstrapIdentityConflictsStopMigration()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceTests");
    QCoreApplication::setApplicationName("Safety-BootstrapIdentity-"+QUuid::createUuid().toString(QUuid::Id128));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    QVERIFY(Database::ensureSchema());
    auto db=Database::db(); QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name) VALUES('identity-master','Test master')"));
    QVERIFY(q.exec("PRAGMA foreign_keys=OFF"));
    QVERIFY(q.exec("DROP INDEX idx_work_orders_uid_unique"));
    QVERIFY(q.exec("DROP INDEX idx_work_orders_order_year_number_unique"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_year,order_number,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'original',2025,7,'2025-01-02','open','','','','','identity-master'),(101,'original',2025,8,'2025-01-03','open','','','','','identity-master')"));
    QVERIFY(!Database::ensureSchema());
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=101"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("original"));
    QCOMPARE(q.value(1).toInt(),2025); QCOMPARE(q.value(2).toInt(),8); q.finish();
    // Once the ambiguity is explicitly resolved, restart completes without renumbering.
    // The deliberately missing parent UID index makes this damaged fixture invalid for FK writes.
    QVERIFY(q.exec("PRAGMA foreign_keys=OFF"));
    QVERIFY2(q.exec("UPDATE work_orders SET order_uid='second',order_year=0,opened_at='03.01.2025' WHERE number=101"), qPrintable(q.lastError().text()));
    QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
    QVERIFY(Database::ensureSchema());
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=101"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("second"));
    QCOMPARE(q.value(1).toInt(),2025); QCOMPARE(q.value(2).toInt(),8);
}

void ReportsOrdersTests::annualAllocationProcessCrash_data()
{
    QTest::addColumn<bool>("committed");
    QTest::newRow("before-commit") << false; QTest::newRow("after-commit") << true;
}

void ReportsOrdersTests::annualAllocationProcessCrash()
{
    QFETCH(bool, committed);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-Annual-Process");
    QVERIFY(AppRuntime::dataDir().startsWith(root.path()));
    const auto path=QDir(AppRuntime::dataDir()).filePath("annual_process.db");
    { ScopedSqliteConnection setup(path,"annual-process-setup"); QVERIFY(setup.isOpen()); QSqlQuery q(setup.db());
      QVERIFY(q.exec("PRAGMA journal_mode=WAL"));
      QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT UNIQUE,order_number INTEGER,order_year INTEGER,UNIQUE(order_year,order_number))")); }
    QProcess child; auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("CS_SAFETY_FIXTURE_APP_NAME",QCoreApplication::applicationName());
    environment.insert("CS_SAFETY_FIXTURE_ORGANIZATION",QCoreApplication::organizationName());
    environment.insert("CS_SAFETY_FIXTURE_DOMAIN",QCoreApplication::organizationDomain());
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(),{"--annual-allocation-crash-fixture",committed?"after":"before"});
    QVERIFY(child.waitForStarted(5000)); QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(),0);
    ScopedSqliteConnection check(path,"annual-process-check"); QVERIFY(check.isOpen()); auto db=check.db(); QSqlQuery q(db);
    QVERIFY(q.exec("SELECT count(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),committed?1:0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE name IN ('annual_order_counters','order_creation_reservations')"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),committed?2:0); q.finish();
    QVERIFY(db.transaction()); QString error; OrderIdentityService::Allocation retry;
    const auto nextYear=QDateTime::fromString("2027-12-31T22:30:00Z",Qt::ISODate);
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"process-request",nextYear,0,&retry,&error),qPrintable(error));
    QCOMPARE(retry.replayed,committed); QCOMPARE(retry.year,committed?2027:2028); QCOMPARE(retry.visibleNumber,1); QCOMPARE(retry.internalNumber,1);
    if(!committed) {
        QVERIFY(q.prepare("INSERT INTO work_orders VALUES(?,?,?,?)")); q.addBindValue(retry.internalNumber); q.addBindValue(retry.orderUid);
        q.addBindValue(retry.visibleNumber); q.addBindValue(retry.year); QVERIFY(q.exec());
    }
    QVERIFY(db.commit());
    QVERIFY(q.exec("SELECT count(*) FROM work_orders w JOIN order_creation_reservations r ON r.order_uid=w.order_uid AND r.internal_number=w.number AND r.order_year=w.order_year AND r.order_number=w.order_number"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM annual_order_counters")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("ok"));
}

void ReportsOrdersTests::deletedHistoricalOrderKeepsIdentity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName();
    const bool oldMode = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        Database::close();
        setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName);
        QStandardPaths::setTestModeEnabled(oldMode);
    });
    QVERIFY(setIsolatedTestRoot(root.path()));
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Deleted-History-", root.path()));
    QString error;
    QVERIFY2(Database::prepareRuntime(&error), qPrintable(error));
    QVERIFY2(Database::openPrepared(&error), qPrintable(error));
    QVERIFY2(Database::runStartupSchemaCheck(&error), qPrintable(error));
    QVERIFY2(Database::runStartupRegistryRefresh(&error), qPrintable(error));
    {
        auto db = Database::db();
        QVERIFY(Database::ensureSchema());
        QSqlQuery q(db);
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,"
                        "plate,plate_norm,car,client,master_id) VALUES(91,'old-order-to-delete',42,2024,"
                        "'2024-11-01','open','AA1111AA','AA1111AA','Test car','Test client','m1')"), qPrintable(q.lastError().text()));
        QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db, {}, QDate(2024,11,2)), 43);
        QVERIFY(q.exec("CREATE TRIGGER retain_header BEFORE DELETE ON work_orders BEGIN SELECT RAISE(IGNORE); END"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= Confirmed order header was not deleted$"));
        QVERIFY(!Database::hardDeleteOrder(91, &error));
        QCOMPARE(error, QStringLiteral("Confirmed order header was not deleted"));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=91 AND order_uid='old-order-to-delete'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        q.finish();
        QVERIFY(q.exec("DROP TRIGGER retain_header"));
        error.clear();
        QSqlQuery registry(QSqlDatabase::database(dbRegistryConnectionName()));
        QVERIFY(registry.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(91,'different-owner',42,2024)"));
        QVERIFY(!Database::hardDeleteOrder(91,&error));
        QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=91"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("old-order-to-delete")); q.finish();
        QVERIFY(registry.exec("UPDATE order_registry SET order_uid='old-order-to-delete' WHERE order_number=91"));
        QVERIFY(registry.exec("INSERT INTO part_registry(entry_key,order_number,order_uid) VALUES('unverified-part',91,'different-owner')"));
        QVERIFY(!Database::hardDeleteOrder(91,&error));
        QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=91")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("old-order-to-delete")); q.finish();
        QVERIFY(registry.exec("UPDATE part_registry SET order_uid='old-order-to-delete' WHERE entry_key='unverified-part'"));
        QVERIFY(registry.exec("CREATE TRIGGER delay_publication BEFORE DELETE ON order_registry BEGIN SELECT RAISE(IGNORE); END"));
        QVERIFY(!Database::hardDeleteOrder(91,&error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=91")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
        QVERIFY(registry.exec("DROP TRIGGER delay_publication"));
        QVERIFY(q.exec("CREATE TRIGGER delay_ack BEFORE UPDATE OF pending ON order_deletion_publications "
                       "WHEN NEW.pending=0 BEGIN SELECT RAISE(IGNORE); END"));
        QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db, {}, QDate(2024,11,2)), 43);
    }
    Database::close();
    QVERIFY2(Database::openPrepared(&error), qPrintable(error));
    QVERIFY2(Database::runStartupSchemaCheck(&error),qPrintable(error));
    QVERIFY2(Database::runStartupMigrationCheck(&error),qPrintable(error));
    auto db = Database::db();
    QSqlQuery q(db);
    QVERIFY(!Database::runStartupRegistryRefresh(&error));
    QVERIFY2(error.contains("acknowledgement"),qPrintable(error));
    QVERIFY(q.exec("SELECT pending FROM order_deletion_publications WHERE internal_number=91"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("DROP TRIGGER delay_ack"));
    QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
    QSqlQuery projection(QSqlDatabase::database(dbRegistryConnectionName()));
    QVERIFY(projection.exec("SELECT COUNT(*) FROM order_registry WHERE order_number=91")); QVERIFY(projection.next());
    QCOMPARE(projection.value(0).toInt(),0); projection.finish();
    QVERIFY2(Database::hardDeleteOrder(91,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE order_uid='old-order-to-delete'"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    q.finish();
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db, {}, QDate(2024,11,2)), 43);
    OrderIdentityService::Allocation identity;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db, "new-after-deletion",
        QDateTime::fromString("2024-11-02T12:00:00Z", Qt::ISODate), 91, &identity, &error), qPrintable(error));
    QCOMPARE(identity.visibleNumber, 43);
    QVERIFY(identity.internalNumber > 91);
    QVERIFY(identity.orderUid != "old-order-to-delete");
}

void ReportsOrdersTests::annualAllocationRollbackAndSources()
{
    auto db = QSqlDatabase::addDatabase("QSQLITE", "annual-allocation"); db.setDatabaseName(":memory:"); QVERIFY(db.open());
    auto external = QSqlDatabase::addDatabase("QSQLITE", "annual-registry"); external.setDatabaseName(":memory:"); QVERIFY(external.open());
    const auto close = qScopeGuard([&] { db.close(); external.close(); db = {}; external = {};
        QSqlDatabase::removeDatabase("annual-allocation"); QSqlDatabase::removeDatabase("annual-registry"); });
    QSqlQuery q(db), reg(external);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,order_year INTEGER)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'active',7,2026)"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders AS SELECT * FROM work_orders WHERE 0"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders VALUES(500,'cache',4,2027)"));
    QVERIFY(q.exec("CREATE TABLE order_registry(order_number INTEGER,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER)"));
    QVERIFY(q.exec("INSERT INTO order_registry VALUES(800,'mirror',9,2027)"));
    QVERIFY(reg.exec("CREATE TABLE order_registry(order_number INTEGER,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER)"));
    QVERIFY(reg.exec("INSERT INTO order_registry VALUES(1000,'external',11,2027)"));
    const auto time = QDateTime::fromString("2026-12-31T22:30:00Z", Qt::ISODate);
    const auto resolver = [&] { return external; };
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2026,12,31)),8);
    QVERIFY(q.exec("UPDATE router_cache_work_orders SET order_number=17"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2027,1,1)),18);
    QVERIFY(q.exec("UPDATE router_cache_work_orders SET order_number=4"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,[]{return QSqlDatabase();},QDate(2027,1,1)),0);
    QVERIFY(reg.exec("ALTER TABLE order_registry RENAME COLUMN visible_order_number TO broken_visible"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2027,1,1)),0);
    QVERIFY(reg.exec("ALTER TABLE order_registry RENAME COLUMN broken_visible TO visible_order_number"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2027,1,1)),12);
    QVERIFY(reg.exec("UPDATE order_registry SET visible_order_number=2147483648"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2027,1,1)),0);
    QVERIFY(reg.exec("UPDATE order_registry SET visible_order_number=11"));
    QString error; OrderIdentityService::Allocation first;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"request-first",time,100,&first,&error,resolver),qPrintable(error));
    QCOMPARE(first.year,2027); QCOMPARE(first.visibleNumber,12); QCOMPARE(first.internalNumber,1001); QVERIFY(!first.replayed);
    QVERIFY(!QUuid(first.orderUid).isNull());
    OrderIdentityService::Allocation replay;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"request-first",time.addYears(1),9999,&replay,&error,resolver),qPrintable(error));
    QVERIFY(replay.replayed); QCOMPARE(replay.orderUid,first.orderUid); QCOMPARE(replay.internalNumber,1001);
    QCOMPARE(replay.visibleNumber,12); QCOMPARE(replay.year,2027);
    QVERIFY(q.exec("SELECT count(*) FROM annual_order_counters")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("CREATE TRIGGER fail_reservation BEFORE INSERT ON order_creation_reservations BEGIN SELECT RAISE(ABORT,'injected allocation failure'); END"));
    OrderIdentityService::Allocation unchanged; unchanged.orderUid="sentinel";
    QVERIFY(!OrderIdentityService::allocateAnnualIdentity(db,"request-fault",time,0,&unchanged,&error,resolver));
    QVERIFY(error.contains("injected allocation failure")); QCOMPARE(unchanged.orderUid,QString("sentinel"));
    QVERIFY(q.exec("SELECT last_number FROM annual_order_counters WHERE order_year=2027")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),12); q.finish();
    QVERIFY(q.exec("DROP TRIGGER fail_reservation"));
    OrderIdentityService::Allocation second;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"request-fault",time,0,&second,&error,resolver),qPrintable(error));
    QCOMPARE(second.visibleNumber,13); QCOMPARE(second.internalNumber,1002); QVERIFY(second.orderUid!=first.orderUid);
    QVERIFY(db.transaction()); OrderIdentityService::Allocation outer;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"request-outer",time,0,&outer,&error,resolver),qPrintable(error));
    QCOMPARE(outer.visibleNumber,14);
    QVERIFY(q.prepare("INSERT INTO work_orders VALUES(?,?,?,?)"));
    q.addBindValue(outer.internalNumber); q.addBindValue(outer.orderUid); q.addBindValue(outer.visibleNumber); q.addBindValue(outer.year); QVERIFY(q.exec());
    QVERIFY(db.rollback());
    QVERIFY(q.exec("SELECT count(*) FROM order_creation_reservations WHERE request_uid='request-outer'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM work_orders WHERE number=1003")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    QVERIFY(q.exec("SELECT last_number FROM annual_order_counters WHERE order_year=2027")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),13); q.finish();
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(2000,'',0,0)"));
    QVERIFY(!OrderIdentityService::allocateAnnualIdentity(db,"request-unresolved",time,0,&unchanged,&error,resolver));
    QVERIFY(error.contains("unresolved historical identity")); QCOMPARE(unchanged.orderUid,QString("sentinel"));
    QVERIFY(q.exec("DELETE FROM work_orders WHERE number=2000"));
    QVERIFY(q.exec("INSERT INTO order_creation_reservations VALUES('retired-source','historical-uid',15,2026,15,'2026-01-01')"));
    QCOMPARE(OrderIdentityService::nextVisibleOrderNumber(db,resolver,QDate(2026,2,1)),16);
    OrderIdentityService::Allocation afterRetirement;
    QVERIFY2(OrderIdentityService::allocateAnnualIdentity(db,"next-in-2026",
        QDateTime::fromString("2026-02-01T12:00:00Z",Qt::ISODate),15,&afterRetirement,&error,resolver),qPrintable(error));
    QCOMPARE(afterRetirement.visibleNumber,16);
    QVERIFY(afterRetirement.internalNumber>15);
    QVERIFY(q.exec("UPDATE annual_order_counters SET last_number=2147483647 WHERE order_year=2027"));
    QVERIFY(!OrderIdentityService::allocateAnnualIdentity(db,"request-overflow",time,0,&unchanged,&error,resolver));
    QVERIFY(error.contains("range exhausted")); QCOMPARE(unchanged.orderUid,QString("sentinel"));
    QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("ok"));
}

void ReportsOrdersTests::annualAllocationConcurrency_data()
{
    QTest::addColumn<bool>("sameRequest");
    QTest::newRow("different-requests") << false; QTest::newRow("same-request") << true;
}

void ReportsOrdersTests::annualAllocationConcurrency()
{
    QFETCH(bool, sameRequest);
    QTemporaryDir root; QVERIFY(root.isValid()); const auto path=QDir(root.path()).filePath("concurrent.db");
    { ScopedSqliteConnection setup(path,"annual-concurrency-setup"); QVERIFY(setup.isOpen()); QSqlQuery q(setup.db());
      QVERIFY(q.exec("PRAGMA journal_mode=WAL")); QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,order_year INTEGER)")); }
    struct Outcome { bool ok=false; QString error; OrderIdentityService::Allocation value; int busyRetries=0; };
    QSemaphore ready, go;
    const auto worker=[&](const QString& id) {
        Outcome out; ScopedSqliteConnection connection(path,"annual-concurrency-worker",&out.error);
        ready.release(); go.acquire(); if(!connection.isOpen()) return out;
        { QSqlQuery q(connection.db()); if(!q.exec("PRAGMA busy_timeout=5000")) {out.error=q.lastError().text(); return out;} }
        for(int attempt=0;attempt<50;++attempt) {
            out.ok=OrderIdentityService::allocateAnnualIdentity(connection.db(),id,
                QDateTime::fromString("2026-12-31T22:30:00Z",Qt::ISODate),0,&out.value,&out.error);
            if(out.ok || (!out.error.contains("locked",Qt::CaseInsensitive) && !out.error.contains("busy",Qt::CaseInsensitive))) break;
            ++out.busyRetries; std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return out;
    };
    auto a=std::async(std::launch::async,worker,QString("concurrent-a"));
    auto b=std::async(std::launch::async,worker,sameRequest?QString("concurrent-a"):QString("concurrent-b"));
    const auto unblock=qScopeGuard([&]{go.release(2);});
    QVERIFY(ready.tryAcquire(2,5000)); go.release(2);
    const auto first=a.get(),second=b.get();
    QVERIFY2(first.ok,qPrintable(first.error)); QVERIFY2(second.ok,qPrintable(second.error));
    QCOMPARE(first.value.year,2027); QCOMPARE(second.value.year,2027);
    if(sameRequest) {
        QCOMPARE(first.value.orderUid,second.value.orderUid); QCOMPARE(first.value.internalNumber,second.value.internalNumber);
        QCOMPARE(first.value.visibleNumber,1); QCOMPARE(second.value.visibleNumber,1);
        QVERIFY(first.value.replayed!=second.value.replayed);
    } else {
        QVERIFY(first.value.orderUid!=second.value.orderUid); QVERIFY(first.value.internalNumber!=second.value.internalNumber);
        QCOMPARE(qMin(first.value.visibleNumber,second.value.visibleNumber),1); QCOMPARE(qMax(first.value.visibleNumber,second.value.visibleNumber),2);
    }
    qInfo() << "Annual allocation SQLITE_BUSY retries" << first.busyRetries << second.busyRetries;
    ScopedSqliteConnection check(path,"annual-concurrency-check"); QVERIFY(check.isOpen()); QSqlQuery q(check.db());
    QVERIFY(q.exec("SELECT count(*),count(DISTINCT order_uid),count(DISTINCT internal_number),count(DISTINCT order_number) FROM order_creation_reservations"));
    QVERIFY(q.next()); for(int i=0;i<4;++i) QCOMPARE(q.value(i).toInt(),sameRequest?1:2); q.finish();
    QVERIFY(q.exec("PRAGMA integrity_check")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("ok"));
}

void ReportsOrdersTests::crmBackfillPreservesRecordedOwnerIdentity()
{
    auto db=QSqlDatabase::addDatabase("QSQLITE","crm-identity-ownership"); db.setDatabaseName(":memory:"); QVERIFY(db.open());
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase("crm-identity-ownership");});
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,client TEXT,client_phone TEXT,plate TEXT,vin TEXT,car TEXT,client_uid TEXT,vehicle_uid TEXT)"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders AS SELECT * FROM work_orders WHERE 0"));
    QString error; QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
    // Same name/phone is not authority to replace two already recorded owners.
    QVERIFY(q.exec("INSERT INTO crm_clients(client_uid,full_name,phone) VALUES('client-a','Shared label','0000000000'),('client-b','Shared label','0000000000')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,client,client_phone,plate,vin,car,client_uid,vehicle_uid) VALUES(100,'active-uid','Shared label','0000000000','','','','client-a','')"));
    QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,client,client_phone,plate,vin,car,client_uid,vehicle_uid) VALUES(100,'archive-uid','Shared label','0000000000','','','','client-b','')"));
    QVERIFY2(CrmClientService::backfillExistingData(db,nullptr,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,client_uid FROM work_orders WHERE number=100")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("active-uid")); QCOMPARE(q.value(1).toString(),QString("client-a")); q.finish();
    QVERIFY(q.exec("SELECT order_uid,client_uid FROM router_cache_work_orders WHERE number=100")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("archive-uid")); QCOMPARE(q.value(1).toString(),QString("client-b")); q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM crm_clients")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2); q.finish();
    QVERIFY(q.exec("INSERT INTO crm_vehicles(vehicle_uid,client_uid,plate) VALUES('vehicle-owned-b','client-b','')"));
    QVERIFY(q.exec("UPDATE work_orders SET client_uid='client-a',vehicle_uid='vehicle-owned-b' WHERE number=100"));
    QVERIFY(!CrmClientService::ensureFromWorkOrder(db,100,nullptr,nullptr,&error)); QVERIFY(error.contains("another client"));
    QVERIFY(q.exec("SELECT client_uid,vehicle_uid FROM work_orders WHERE number=100")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("client-a")); QCOMPARE(q.value(1).toString(),QString("vehicle-owned-b")); q.finish();
    QVERIFY(q.exec("UPDATE work_orders SET vehicle_uid='missing-vehicle' WHERE number=100"));
    QVERIFY(!CrmClientService::ensureFromWorkOrder(db,100,nullptr,nullptr,&error)); QVERIFY(error.contains("Recorded vehicle UID"));
    // A recorded vehicle provides an exact owner when only the client field is missing.
    QVERIFY(q.exec("UPDATE work_orders SET client_uid='',vehicle_uid='vehicle-owned-b' WHERE number=100"));
    QVERIFY2(CrmClientService::ensureFromWorkOrder(db,100,nullptr,nullptr,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT client_uid,vehicle_uid FROM work_orders WHERE number=100")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("client-b")); QCOMPARE(q.value(1).toString(),QString("vehicle-owned-b")); q.finish();
    QVERIFY(q.exec("UPDATE work_orders SET client_uid='missing-client',vehicle_uid='' WHERE number=100"));
    // Incremental backfill skips already-marked rows. Validate the changed row
    // through the same per-order path used on save, regardless of that marker.
    QVERIFY(!CrmClientService::ensureFromWorkOrder(db,100,nullptr,nullptr,&error)); QVERIFY(error.contains("Recorded client UID"));
    QVERIFY(q.exec("SELECT client_uid FROM work_orders WHERE number=100")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("missing-client"));
}

void ReportsOrdersTests::annualBookingRoutingAtomic_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault:QStringList{"state","mirror","intent"}) QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::annualBookingRoutingAtomic()
{
    QFETCH(QString,fault);

    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close(); ActivityAuditService::clearCurrentActor();
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Booking-Routing-",root.path()));
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error)); QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(CloudSyncSchemaService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('booking-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"booking-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('booking-master','Master','master')"));
    const int year=QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone(QByteArrayLiteral("Europe/Kyiv"))).date().year();
    QVERIFY(q.prepare("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,master_id,status,plate,plate_norm,car,client) VALUES(100,'booking-old',7,?,?,'booking-master','open','AA1111AA','AA1111AA','Old car','Old client')"));
    q.addBindValue(year); q.addBindValue(QDate::currentDate().toString(Qt::ISODate)); QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
    QVariantMap payload{{"appointment_uid","booking-request"},{"client_name","Booking test"},{"plate","AA1234AA"},
        {"phone","test-phone"},{"car_text","Test car"},{"service_reason","Diagnostic test"},{"desired_start_at","2035-01-01T10:00:00"},{"status","confirmed"}};
    QVERIFY2(OnlineBookingService::upsertRequest(db,payload,&error),qPrintable(error));

    const QStringList tables{"work_orders","work_order_services","annual_order_counters","order_creation_reservations","workspace_order_state","order_registry","crm_clients","crm_vehicles","activity_audit_events"};
    // Annual tables are created transactionally by the allocator on first use.
    // Absence is a distinct expected state, not an unchecked zero count.
    auto tableCount=[&](const QString& table,int* count) {
        QSqlQuery read(db);if(!read.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"))return false;
        read.addBindValue(table);if(!read.exec())return false;
        if(!read.next()) {*count=-1;return !read.lastError().isValid();}read.finish();
        if(!read.exec("SELECT count(*) FROM "+table) || !read.next())return false;*count=read.value(0).toInt();return true;
    };
    QMap<QString,int> before;
    for(const auto& table:tables) {int count=0;QVERIFY(tableCount(table,&count));before[table]=count;}
    const auto table=fault=="mirror"?QString("order_registry"):QString("workspace_order_state");
    const auto event=fault=="intent"?QString("UPDATE OF registry_revision"):QString("INSERT");
    const auto raised=fault=="intent"?QString("IGNORE"):QString("ABORT,'injected booking route failure'");
    QVERIFY(q.exec("CREATE TRIGGER reject_booking_route BEFORE "+event+" ON "+table+" BEGIN SELECT RAISE("+raised+"); END"));
    const QString expected=fault=="state"?QString("Failed to upsert workspace_order_state"):fault=="mirror"?QString("Failed to upsert local order_registry mirror"):QString("Workspace registry intent was not recorded");
    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "+QRegularExpression::escape(expected)+"$"));
    QString uid;error.clear();QVERIFY(!OnlineBookingService::createOrderFromRequest(db,"booking-request",&uid,&error));QCOMPARE(error,expected);QVERIFY(uid.isEmpty());
    for(const auto& table:tables) {int count=0;QVERIFY(tableCount(table,&count));QCOMPARE(count,before.value(table));}
    QVERIFY(q.exec("SELECT status FROM online_booking_requests WHERE appointment_uid='booking-request'"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("confirmed"));q.finish();
    QVERIFY(q.exec("DROP TRIGGER reject_booking_route"));
    QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-request",&uid,&error),qPrintable(error));QVERIFY(!uid.isEmpty());
    QVERIFY(q.prepare("SELECT w.order_number,s.registry_revision,s.published_registry_revision,r.order_uid FROM work_orders w JOIN workspace_order_state s ON s.order_uid=w.order_uid JOIN order_registry r ON r.order_uid=w.order_uid WHERE w.order_uid=?"));q.addBindValue(uid);QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),8);QVERIFY(q.value(1).toLongLong()>0);QCOMPARE(q.value(1).toLongLong(),q.value(2).toLongLong());QCOMPARE(q.value(3).toString(),uid);q.finish();
    const auto original=uid;QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-request",&uid,&error),qPrintable(error));QCOMPARE(uid,original);
    QVERIFY(q.exec("SELECT count(*) FROM work_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);
}

void ReportsOrdersTests::annualBookingCallerOwnedContext_data()
{
    QTest::addColumn<bool>("capture");QTest::addColumn<bool>("token");QTest::addColumn<bool>("rejectToken");
    QTest::newRow("disabled")<<false<<false<<false;
    QTest::newRow("enabled-cold-identity")<<true<<false<<false;
    QTest::newRow("plain-token-migration")<<true<<true<<false;
    QTest::newRow("ignored-token-migration")<<true<<true<<true;
}

void ReportsOrdersTests::annualBookingCallerOwnedContext()
{
    QFETCH(bool,capture);QFETCH(bool,token);QFETCH(bool,rejectToken);

    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close(); ActivityAuditService::clearCurrentActor();
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Booking-Caller-",root.path()));
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error)); QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    const auto suppliedPath=root.filePath("supplied-booking.db");
    {
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(CloudSyncSchemaService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('booking-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"booking-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('booking-master','Master','master')"));
    const int year=QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone(QByteArrayLiteral("Europe/Kyiv"))).date().year();
    QVERIFY(q.prepare("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,master_id,status,plate,plate_norm,car,client) VALUES(100,'booking-old',7,?,?,'booking-master','open','AA1111AA','AA1111AA','Old car','Old client')"));
    q.addBindValue(year); q.addBindValue(QDate::currentDate().toString(Qt::ISODate)); QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
    QVariantMap payload{{"appointment_uid","booking-request"},{"client_name","Booking test"},{"plate","AA1234AA"},
        {"phone","test-phone"},{"car_text","Test car"},{"service_reason","Diagnostic test"},{"desired_start_at","2035-01-01T10:00:00"},{"status","confirmed"}};
    QVERIFY2(OnlineBookingService::upsertRequest(db,payload,&error),qPrintable(error));

    q.finish();QSqlQuery clone(db);QVERIFY(clone.prepare("VACUUM INTO ?"));clone.addBindValue(suppliedPath);QVERIFY2(clone.exec(),qPrintable(clone.lastError().text()));
    }
    Database::close();QVERIFY(!QSqlDatabase::contains(currentConnectionName()));
    QFile original(AppRuntime::activeDbFilePath());QVERIFY(original.open(QIODevice::ReadOnly));const auto before=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256);original.close();
    ScopedSqliteConnection supplied(suppliedPath,"booking-caller-owned");QVERIFY(supplied.isOpen());
    QVERIFY(!DbThreadConnectionSupport::isRuntimeConnection(supplied.connectionName()));
    QSqlQuery config(supplied.db());
    QVERIFY(config.prepare("INSERT INTO settings(key,value) VALUES('settings.cloudsync.capture_outbox_while_disabled',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));config.addBindValue(capture && !token?1:0);QVERIFY(config.exec());config.finish();
    if(capture) {
        QVERIFY(config.exec("DELETE FROM sync_device_identity"));
        QVERIFY(config.exec("INSERT INTO settings(key,value) VALUES('settings.cloudsync.branch_uid','provided-branch') ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    }
    if(token) {
        QVERIFY(config.exec("INSERT INTO settings(key,value) VALUES('settings.cloudsync.api_token','synthetic-booking-token') ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        QVERIFY(config.exec("INSERT INTO settings(key,value) VALUES('settings.backend.offline','0') ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    }
    QString uid;
    if(rejectToken) {
        QVERIFY(config.exec("CREATE TRIGGER reject_scoped_token BEFORE UPDATE ON settings WHEN OLD.key='settings.cloudsync.api_token' BEGIN SELECT RAISE(IGNORE); END"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= "+QRegularExpression::escape(supplied.connectionName())+"  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= "+QRegularExpression::escape(supplied.connectionName())+"  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= Caller-owned token migration did not persist$"));
        error.clear();QVERIFY(!OnlineBookingService::createOrderFromRequest(supplied.db(),"booking-request",&uid,&error));QCOMPARE(error,QString("Caller-owned token migration did not persist"));QVERIFY(uid.isEmpty());
        QVERIFY(config.exec("SELECT count(*) FROM work_orders"));QVERIFY(config.next());QCOMPARE(config.value(0).toInt(),1);config.finish();
        QVERIFY(config.exec("SELECT count(*) FROM sync_outbox"));QVERIFY(config.next());QCOMPARE(config.value(0).toInt(),0);config.finish();
        QVERIFY(!QSqlDatabase::contains(currentConnectionName()));
        QVERIFY(config.exec("DROP TRIGGER reject_scoped_token"));
    }
    QVERIFY2(OnlineBookingService::createOrderFromRequest(supplied.db(),"booking-request",&uid,&error),qPrintable(error));QVERIFY(!uid.isEmpty());
    if(token) {
        QVERIFY(config.exec("SELECT value FROM settings WHERE key='settings.cloudsync.api_token'"));QVERIFY(config.next());const auto protectedToken=config.value(0).toString();config.finish();
        QVERIFY(protectedToken.startsWith("dpapi:v1:"));QString tokenError;QCOMPARE(CloudSyncCredentials::unprotectToken(protectedToken,&tokenError),QString("synthetic-booking-token"));QVERIFY(tokenError.isEmpty());
    }
    QSqlQuery read(supplied.db());QVERIFY(read.prepare("SELECT count(*) FROM work_orders WHERE order_uid=?"));read.addBindValue(uid);QVERIFY(read.exec());QVERIFY(read.next());QCOMPARE(read.value(0).toInt(),1);read.finish();
    QVERIFY(read.prepare("SELECT count(*) FROM workspace_order_state WHERE order_uid=?"));read.addBindValue(uid);QVERIFY(read.exec());QVERIFY(read.next());QCOMPARE(read.value(0).toInt(),0);read.finish();
    QVERIFY(read.prepare("SELECT count(*) FROM sync_outbox WHERE entity_type='work_order' AND entity_uid=?"));read.addBindValue(uid);QVERIFY(read.exec());QVERIFY(read.next());if(capture)QVERIFY(read.value(0).toInt()>0);else QCOMPARE(read.value(0).toInt(),0);read.finish();
    if(capture) {QVERIFY(read.exec("SELECT branch_uid FROM sync_device_identity WHERE id=1"));QVERIFY(read.next());QCOMPARE(read.value(0).toString(),QString("provided-branch"));read.finish();}
    QVERIFY(!QSqlDatabase::contains(currentConnectionName()));QVERIFY(!DbThreadConnectionSupport::isRuntimeConnection(supplied.connectionName()));
    QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256),before);original.close();
}

void ReportsOrdersTests::annualBookingConversionIsIdempotent()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"); const auto oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close(); ActivityAuditService::clearCurrentActor();
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-Annual-Booking");
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error)); QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(CloudSyncSchemaService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('booking-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"booking-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('booking-master','Master','master')"));
    const int year=QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone(QByteArrayLiteral("Europe/Kyiv"))).date().year();
    QVERIFY(q.prepare("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,master_id,status,plate,plate_norm,car,client) VALUES(100,'booking-old',7,?,?,'booking-master','open','AA1111AA','AA1111AA','Old car','Old client')"));
    q.addBindValue(year); q.addBindValue(QDate::currentDate().toString(Qt::ISODate)); QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
    QVariantMap payload{{"appointment_uid","booking-request"},{"client_name","Booking test"},{"plate","AA1234AA"},
        {"phone","test-phone"},{"car_text","Test car"},{"service_reason","Diagnostic test"},{"desired_start_at","2035-01-01T10:00:00"},{"status","confirmed"}};
    QVERIFY2(OnlineBookingService::upsertRequest(db,payload,&error),qPrintable(error));
    QString uid; QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-request",&uid,&error),qPrintable(error)); QVERIFY(!uid.isEmpty());
    QVERIFY(q.prepare("SELECT number,order_number,order_year,opened_at FROM work_orders WHERE order_uid=?")); q.addBindValue(uid); QVERIFY(q.exec());
    QVERIFY(q.next()); const int internal=q.value(0).toInt(); QVERIFY(internal>100); QCOMPARE(q.value(1).toInt(),8); QCOMPARE(q.value(2).toInt(),year);
    QCOMPARE(QDateTime::fromString(q.value(3).toString(),Qt::ISODate).date().year(),year); q.finish();
    QVERIFY(q.prepare("SELECT b.client_uid,w.client_uid,b.vehicle_uid,w.vehicle_uid FROM online_booking_requests b JOIN work_orders w ON w.order_uid=? WHERE b.appointment_uid='booking-request'"));
    q.addBindValue(uid); QVERIFY(q.exec()); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),q.value(1).toString()); QCOMPARE(q.value(2).toString(),q.value(3).toString()); q.finish();
    QVERIFY(q.prepare("SELECT s.order_uid,s.registry_revision,s.published_registry_revision,r.order_uid FROM workspace_order_state s JOIN order_registry r ON r.order_number=s.order_number WHERE s.order_number=?"));
    q.addBindValue(internal);QVERIFY(q.exec());QVERIFY2(q.next(),"Booking confirmation must commit local routing and a durable publication intent");
    QCOMPARE(q.value(0).toString(),uid);QCOMPARE(q.value(3).toString(),uid);QVERIFY(q.value(1).toLongLong()>0);QCOMPARE(q.value(1).toLongLong(),q.value(2).toLongLong());q.finish();
    const auto originalUid=uid;
    // A repeated website payload omits internal CRM IDs and must not clear them.
    QVERIFY2(OnlineBookingService::upsertRequest(db,payload,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT client_uid,vehicle_uid FROM online_booking_requests WHERE appointment_uid='booking-request'"));
    QVERIFY(q.next()); QVERIFY(!q.value(0).toString().isEmpty()); QVERIFY(!q.value(1).toString().isEmpty()); q.finish();
    QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-request",&uid,&error),qPrintable(error)); QCOMPARE(uid,originalUid);
    QVERIFY(q.exec("SELECT count(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2); q.finish();
    QVERIFY(q.prepare("SELECT count(*) FROM work_order_services WHERE order_uid=?")); q.addBindValue(uid); QVERIFY(q.exec()); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("SELECT status FROM online_booking_requests WHERE appointment_uid='booking-request'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("order_created")); q.finish();
    QVERIFY(q.prepare("SELECT count(*) FROM activity_audit_events WHERE entity_type='work_order' AND entity_id=? AND order_number=? AND action='create'"));
    q.addBindValue(QString::number(internal)); q.addBindValue(internal); QVERIFY(q.exec()); QVERIFY(q.next()); QVERIFY(q.value(0).toInt()>=1); q.finish();
    QCOMPARE(OrderNumberService::nextVisibleOrderNumber(),9);
    QVERIFY(ensureRegistrySchemaFile());
    QSqlQuery archived(QSqlDatabase::database(dbRegistryConnectionName()));
    archived.prepare("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(801,'external-booking-owner',33,?)");
    archived.addBindValue(year);
    QVERIFY2(archived.exec(),qPrintable(archived.lastError().text()));
    QCOMPARE(OrderNumberService::nextVisibleOrderNumber(),34);
    payload["appointment_uid"]="booking-after-external";
    QVERIFY2(OnlineBookingService::upsertRequest(db,payload,&error),qPrintable(error));
    QString nextUid;
    QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-after-external",&nextUid,&error),qPrintable(error));
    QVERIFY(q.prepare("SELECT number,order_number FROM work_orders WHERE order_uid=?"));
    q.addBindValue(nextUid); QVERIFY(q.exec()); QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(),802); QCOMPARE(q.value(1).toInt(),34); q.finish();
    QVERIFY2(OnlineBookingService::createOrderFromRequest(db,"booking-after-external",&uid,&error),qPrintable(error));
    QCOMPARE(uid,nextUid);
    QCOMPARE(OrderNumberService::nextVisibleOrderNumber(),35);
}

void ReportsOrdersTests::annualWorkspaceCreationIsPersistent()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot = qEnvironmentVariable("CS_TEST_DATA_ROOT");
    const auto oldName = QCoreApplication::applicationName(); const bool oldTest = QStandardPaths::isTestModeEnabled();
    const auto restore = qScopeGuard([&] {
        Database::close(); ActivityAuditService::clearCurrentActor(); qApp->setProperty("csDisableOrderSaveBackup", false);
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-Annual-Creation"); qApp->setProperty("csDisableOrderSaveBackup", true);
    QString error; QVERIFY2(Database::prepareRuntime(&error), qPrintable(error));
    QVERIFY2(Database::openPrepared(&error), qPrintable(error));
    auto db = Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db, &error)); QVERIFY(AccessControlService::ensureSchema(db, &error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('annual-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"annual-owner", "Owner", "owner", "test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('annual-master','Master','master')"));
    OrderWorkspaceInput input; input.number = 100; input.visibleNumber = 999;
    input.status = "open"; input.car = "Test car"; input.client = "Test client"; input.plate = "AA1234AA";
    input.masterId = "annual-master"; input.discountMode = "uah";
    input.createdAt = QDateTime::fromString("2026-12-31T21:30:00Z", Qt::ISODate); input.openedAt = input.createdAt;
    int first = 0; QVERIFY2(OrderWorkspaceService::saveEditableOrder(input, &first, &error), qPrintable(error));
    QCOMPARE(first, 100);
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); const auto uid = q.value(0).toString(); QVERIFY(!uid.isEmpty());
    QCOMPARE(q.value(1).toInt(), 1); QCOMPARE(q.value(2).toInt(), 2026); q.finish();
    QVERIFY(!TransactionContext::hasActiveTransaction(db));
    error.clear(); QVERIFY(!Database::finalizeOrderWorkspaceInTransaction(db,first,&error));
    QCOMPARE(error,QString("Workspace finalization requires an owned transaction"));
    // Model metadata missing after a historical partial finalization; replay must
    // fail while repair is blocked, then repair the same UID without a new number.
    QVERIFY(q.exec("DELETE FROM workspace_order_state WHERE order_number=100"));
    QVERIFY(q.exec("DELETE FROM order_registry WHERE order_number=100"));
    QVERIFY(q.exec("CREATE TRIGGER reject_replay_state BEFORE INSERT ON workspace_order_state BEGIN SELECT RAISE(ABORT,'injected replay state failure'); END"));
    // A lost response retried after Kyiv midnight must not create or reidentify the order.
    input.number = 0; input.createdAt = QDateTime::fromString("2026-12-31T22:30:00Z", Qt::ISODate);
    int retried = 0;
    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
    QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= Failed to upsert workspace_order_state$"));
    QVERIFY(!OrderWorkspaceService::saveEditableOrder(input,&retried,&error));
    QCOMPARE(error,QString("Failed to upsert workspace_order_state"));
    QVERIFY(q.exec("SELECT count(*) FROM order_creation_reservations")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    QVERIFY(q.exec("DROP TRIGGER reject_replay_state"));
    QVERIFY2(OrderWorkspaceService::saveEditableOrder(input, &retried, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT s.order_uid,r.order_uid FROM workspace_order_state s JOIN order_registry r ON r.order_number=s.order_number WHERE s.order_number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid); QCOMPARE(q.value(1).toString(),uid); q.finish();
    QCOMPARE(retried, first);
    QVERIFY(q.exec("SELECT count(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1); q.finish();
    OrderWorkspaceInput next = input; next.creationRequestUid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    next.visibleNumber = 500; int second = 0;
    QVERIFY2(OrderWorkspaceService::saveEditableOrder(next, &second, &error), qPrintable(error)); QVERIFY(second > first);
    QVERIFY(q.exec(QString("SELECT order_number,order_year FROM work_orders WHERE number=%1").arg(second)));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1); QCOMPARE(q.value(1).toInt(), 2027); q.finish();
    // The allocation ledger survives a deleted order and the next confirmation gets 2.
    QVERIFY(q.exec(QString("DELETE FROM work_orders WHERE number=%1").arg(second)));
    next.creationRequestUid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int third = 0; QVERIFY2(OrderWorkspaceService::saveEditableOrder(next, &third, &error), qPrintable(error));
    QVERIFY(third > second);
    QVERIFY(q.exec(QString("SELECT order_number,order_year FROM work_orders WHERE number=%1").arg(third)));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2); QCOMPARE(q.value(1).toInt(), 2027); q.finish();
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), uid); QCOMPARE(q.value(1).toInt(), 1); QCOMPARE(q.value(2).toInt(), 2026); q.finish();
    QCOMPARE(OrderNumberService::nextVisibleOrderNumber(),2);
    QVERIFY(q.prepare("SELECT count(*) FROM activity_audit_events WHERE entity_type='work_order' AND entity_id=? AND order_number=? AND action='create'"));
    q.addBindValue(QString::number(third)); q.addBindValue(third); QVERIFY(q.exec()); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    OrderDialog form(nullptr,-1);
    auto* visibleNumber=form.findChild<QSpinBox*>("orderVisibleNumber"); QVERIFY(visibleNumber);
    QVERIFY(visibleNumber->isReadOnly()); QCOMPARE(visibleNumber->buttonSymbols(),QAbstractSpinBox::NoButtons);
    QCOMPARE(visibleNumber->value(),2); QVERIFY(visibleNumber->maximum()>=10000000);
    auto* post=form.findChild<QComboBox*>("newOrderPost"); QVERIFY(post); QCOMPARE(post->currentData().toInt(),0);
    if (const auto output=qEnvironmentVariable("CS_NEW_ORDER_FORM_OUTPUT"); !output.isEmpty()) {
        form.show(); QTest::qWait(100);
        QCOMPARE(form.size(), QSize(944, 565));
        const QPoint offset = form.frameGeometry().center() - form.screen()->availableGeometry().center();
        QVERIFY(qAbs(offset.x()) <= 1 && qAbs(offset.y()) <= 1);
        QVERIFY(form.grab().save(output));
    }
    post->setCurrentIndex(post->findData(2));
    const auto draft=form.serializeDraftState();
    QCOMPARE(draft.value("preferredPost").toInt(),2);
    const auto requestUid=draft["creationRequestUid"].toString(); QVERIFY(!requestUid.isEmpty());
    QCOMPARE(form.serializeDraftState()["creationRequestUid"].toString(),requestUid);
    OrderDialog recovered(nullptr,-1); QVERIFY(recovered.serializeDraftState()["creationRequestUid"].toString()!=requestUid);
    QVERIFY2(recovered.restoreDraftState(draft,&error),qPrintable(error));
    QCOMPARE(recovered.serializeDraftState()["creationRequestUid"].toString(),requestUid);
    QCOMPARE(recovered.serializeDraftState()["preferredPost"].toInt(),2);
}

void ReportsOrdersTests::profileRegistryLifecycle_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"close","active-owner","live-path-change","closed-path-change","reconnect-same-path","already-removed"}) QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::profileRegistryLifecycle()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {
        Database::close(); ActivityAuditService::clearCurrentActor(); qApp->setProperty("csDisableOrderSaveBackup",false);
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Life-",root.path())); qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('atomic-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"atomic-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('atomic-master','Original master','master')"));
    QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
    QVERIFY(ensureRegistrySchemaFile());
    const auto mainName=db.connectionName(), registryName=dbRegistryConnectionName();
    const auto oldMainPath=QFileInfo(db.databaseName()).absoluteFilePath();
    const auto oldRegistryPath=QFileInfo(QSqlDatabase::database(registryName,false).databaseName()).absoluteFilePath();
    if (fault=="active-owner") {
        auto registry=QSqlDatabase::database(registryName,false);
        TransactionContext owner(registry,"RegistryLifecycleOwner"); QVERIFY(owner.begin("test transaction",&error));
        Database::close();
        const bool mainPreserved=QSqlDatabase::contains(mainName) && QSqlDatabase::database(mainName,false).isOpen();
        const bool registryPreserved=registry.isOpen() && TransactionContext::hasActiveTransaction(registry);
        QVERIFY(owner.rollback(QString(),&error));
        QVERIFY2(mainPreserved && registryPreserved,"Closing a profile must defer while its registry transaction is active");
        return;
    }
    if (fault=="reconnect-same-path") {
        db.close();QVERIFY2(Database::ensureConnectionAlive(&error),qPrintable(error));
        auto reopened=QSqlDatabase::database(mainName,false);QSqlQuery check(reopened);
        QVERIFY(check.exec("SELECT count(*) FROM masters WHERE id='atomic-master'"));QVERIFY(check.next());QCOMPARE(check.value(0).toInt(),1);check.finish();
        QVERIFY(check.exec("PRAGMA busy_timeout"));QVERIFY(check.next());QCOMPARE(check.value(0).toInt(),5000);check.finish();
        QVERIFY(QSqlDatabase::database(registryName,false).isOpen());
    }
    if (fault=="closed-path-change") {
        db.close();
        {auto registry=QSqlDatabase::database(registryName,false); registry.close();}
        QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Rebind-",root.path()));
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::ensureConnectionAlive(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error)); QVERIFY(Database::ensureSchema()); QVERIFY(ensureRegistrySchemaFile());
        QCOMPARE(QFileInfo(QSqlDatabase::database(mainName,false).databaseName()).absoluteFilePath(),QFileInfo(AppRuntime::activeDbFilePath()).absoluteFilePath());
        QCOMPARE(QFileInfo(QSqlDatabase::database(registryName,false).databaseName()).absoluteFilePath(),QFileInfo(AppRuntime::registryDbFilePath()).absoluteFilePath());
    }
    if (fault=="live-path-change") {
        QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Next-",root.path()));
        error.clear(); QVERIFY(!Database::prepareRuntime(&error)); QVERIFY(!error.isEmpty());
        error.clear();
        const bool opened=Database::openPrepared(&error);
        QVERIFY2(!opened,"An open connection from another profile must not be acknowledged as this profile");
        QVERIFY(!error.isEmpty());
        QVERIFY(!ensureRegistrySchemaFile());
        QCOMPARE(QFileInfo(QSqlDatabase::database(mainName,false).databaseName()).absoluteFilePath(),oldMainPath);
        QCOMPARE(QFileInfo(QSqlDatabase::database(registryName,false).databaseName()).absoluteFilePath(),oldRegistryPath);
    }
    q.finish(); q=QSqlQuery(); db=QSqlDatabase();
    if (fault=="already-removed") {QSqlDatabase::removeDatabase(mainName);QSqlDatabase::removeDatabase(registryName);}
    Database::close();
    QVERIFY(!DbThreadConnectionSupport::isRuntimeConnection(mainName));QVERIFY(!DbThreadConnectionSupport::isRuntimeConnection(registryName));
    QVERIFY(!QSqlDatabase::contains(mainName));
    QVERIFY2(!QSqlDatabase::contains(registryName),"Database::close must release the profile's current-thread registry too");
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-New-",root.path()));
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error)); QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    QVERIFY(Database::ensureSchema()); QVERIFY(ensureRegistrySchemaFile());
    QCOMPARE(QFileInfo(QSqlDatabase::database(registryName,false).databaseName()).absoluteFilePath(),QFileInfo(AppRuntime::registryDbFilePath()).absoluteFilePath());
}

void ReportsOrdersTests::registryRebuildProcess_data()
{
    QTest::addColumn<QString>("boundary");
    QTest::newRow("after-registry")<<QString("after-registry");
    QTest::newRow("after-source")<<QString("after-source");
}

void ReportsOrdersTests::registryRebuildProcess()
{
    QFETCH(QString,boundary);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Rebuild-Process-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('bulk-master','Master','master')"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'bulk-first',7,2025,'2025-01-02','open','','','Original car','Client','bulk-master'),(101,'bulk-second',8,2025,'2025-01-02','open','','','Original car','Client','bulk-master')"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,100,'bulk-first','2025-01-02','Original part',1,0,0),(2,101,'bulk-second','2025-01-02','Original part',1,0,0)"));
        {TransactionContext tx(db,"BulkProcessBaseline");QVERIFY(tx.begin("test",&error));for(int n:{100,101})QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,n,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
        QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE work_orders SET car='Rebuilt car'"));QVERIFY(q.exec("UPDATE work_order_parts SET name='Rebuilt part'"));
        QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
        QVERIFY(r.exec("UPDATE registry_meta SET value='force-cold' WHERE key='runtime_active_stamp'"));
        QVERIFY2(rebuildRegistryDerivedState(db,&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE order_registry SET car='Prior local mirror'"));
        QVERIFY(r.exec("UPDATE order_registry SET car='Prior external projection'"));
        QVERIFY(r.exec("UPDATE part_registry SET name='Prior external part'"));
    }
    Database::close();
    QProcess child;auto env=QProcessEnvironment::systemEnvironment();
    env.insert("CS_TEST_DATA_ROOT",root.path());env.insert("CS_SAFETY_FIXTURE_APP_NAME",QCoreApplication::applicationName());
    env.insert("CS_SAFETY_FIXTURE_ORGANIZATION",QCoreApplication::organizationName());env.insert("CS_SAFETY_FIXTURE_DOMAIN",QCoreApplication::organizationDomain());
    child.setProcessEnvironment(env);child.start(QCoreApplication::applicationFilePath(),{"--registry-rebuild-crash-fixture",boundary});
    QVERIFY(child.waitForStarted(5000));QVERIFY(child.waitForFinished(15000));
    const auto diagnostics=child.readAllStandardError()+child.readAllStandardOutput();
    QCOMPARE(child.exitStatus(),QProcess::NormalExit);QVERIFY2(child.exitCode()==0,diagnostics.constData());
    if(boundary=="after-registry") {
        QCOMPARE(diagnostics.count("QCRITICAL|"),1);QCOMPARE(diagnostics.count("commit_failed_rollback"),1);
        QVERIFY(diagnostics.contains("FOREIGN KEY constraint failed"));QVERIFY(diagnostics.contains("rollbackOk=true"));QVERIFY(diagnostics.contains("RegistryRebuildSource"));
    } else QVERIFY2(!diagnostics.contains("QCRITICAL|"),diagnostics.constData());
    QVERIFY2(!diagnostics.contains("QFATAL|") && !diagnostics.contains("removeDatabase"),diagnostics.constData());
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QSqlQuery q(db);
    auto registry=QSqlDatabase::addDatabase("QSQLITE",dbRegistryConnectionName());registry.setDatabaseName(AppRuntime::registryDbFilePath());
    DbThreadConnectionSupport::registerRuntimeConnection(dbRegistryConnectionName(),AppRuntime::registryDbFilePath());
    QVERIFY2(registry.open(),qPrintable(registry.lastError().text()));QSqlQuery r(registry);
    QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='registry_rebuild_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("1"));r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE car='Rebuilt car'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE name='Rebuilt part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
    QVERIFY(q.exec("SELECT count(*) FROM workspace_order_state WHERE registry_revision=1 AND published_registry_revision=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM order_registry WHERE car='Rebuilt car'"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),boundary=="after-source"?2:0);q.finish();
    if(boundary=="after-registry")QVERIFY(q.exec("DROP TRIGGER fail_rebuild_ack"));
    else QVERIFY(r.exec("DROP TRIGGER fail_rebuild_completion"));
    for(int repeat=0;repeat<2;++repeat) {
        QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
        QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE car='Rebuilt car' AND order_year=2025 AND ((order_number=100 AND order_uid='bulk-first' AND visible_order_number=7) OR (order_number=101 AND order_uid='bulk-second' AND visible_order_number=8))"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
        QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE name='Rebuilt part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
        QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='registry_rebuild_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("0"));r.finish();
        QVERIFY(q.exec("SELECT count(*) FROM workspace_order_state WHERE registry_revision=1 AND published_registry_revision=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
    }
}

void ReportsOrdersTests::registryMirrorTransactionOwnership_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","foreign-uid","local-ignore"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::registryMirrorTransactionOwnership()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Mirror-Owner-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto main=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(main));QSqlQuery q(main);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('import-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'native-owner',1,2026,'2026-01-02','open','','','Current native car','Client','import-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(3,'native-part',100,'native-owner','2026-01-02','Current native part',1,0,0)"));
    {TransactionContext tx(main,"ArchiveFileBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(main,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    if(fault=="local-ignore")QVERIFY(q.exec("CREATE TRIGGER reject_owned_mirror BEFORE UPDATE ON order_registry BEGIN SELECT RAISE(IGNORE); END"));
    auto snapshot=[](QSqlDatabase database,const QStringList& tables){QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:tables){QSqlQuery rows(database);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
    const QStringList localTables{"work_orders","work_order_parts","order_registry","workspace_order_state"};
    const QStringList externalTables{"order_registry","part_registry"};
    const auto mainBefore=snapshot(main,localTables),externalBefore=snapshot(registry,externalTables),stateBefore=snapshot(main,{"workspace_order_state"});
    QVERIFY(mainBefore!="SQL_ERROR");QVERIFY(externalBefore!="SQL_ERROR");QVERIFY(stateBefore!="SQL_ERROR");
    QVERIFY(!syncRegistryProjectionInOwnedTransaction(main,&error));QVERIFY(!error.isEmpty());
    TransactionContext sourceTx(main,"MirrorSource"),targetTx(registry,"MirrorTarget");
    QVERIFY(sourceTx.begin("test",&error));QVERIFY(targetTx.begin("test",&error));
    QVERIFY(r.exec("UPDATE order_registry SET car='Uncommitted candidate' WHERE order_number=100"));
    if(fault=="foreign-uid")QVERIFY(r.exec("UPDATE order_registry SET order_uid='foreign-owner' WHERE order_number=100"));
    error.clear();const bool ok=syncRegistryProjectionInOwnedTransaction(main,&error);
    if(fault=="valid") {
        QVERIFY2(ok,qPrintable(error));
        QVERIFY(q.exec("SELECT car FROM order_registry WHERE order_uid='native-owner'"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Uncommitted candidate"));q.finish();
        QCOMPARE(snapshot(main,{"workspace_order_state"}),stateBefore);
    } else {QVERIFY(!ok);QVERIFY(!error.isEmpty());QCOMPARE(snapshot(main,localTables),mainBefore);}
    QVERIFY(sourceTx.rollback(QString(),&error));QVERIFY(targetTx.rollback(QString(),&error));
    QCOMPARE(snapshot(main,localTables),mainBefore);QCOMPARE(snapshot(registry,externalTables),externalBefore);
}

void ReportsOrdersTests::registryProjectionTransactionOwnership()
{
    QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Projection-Owner-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto main=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(main));QSqlQuery q(main);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('import-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'native-owner',1,2026,'2026-01-02','open','','','Current native car','Client','import-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(3,'native-part',100,'native-owner','2026-01-02','Current native part',1,0,0)"));
    {TransactionContext tx(main,"ArchiveFileBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(main,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    {TransactionContext tx(main,"ProjectionPending");QVERIFY(tx.begin("test",&error));
     QVERIFY(q.exec("UPDATE work_orders SET car='Pending car' WHERE number=100"));
     QVERIFY(q.exec("UPDATE work_order_parts SET name='Pending part' WHERE id=3"));
     QVERIFY(Database::finalizeOrderWorkspaceInTransaction(main,100,&error));QVERIFY(tx.commit("test",&error));}
    auto snapshot=[](QSqlDatabase database,const QStringList& tables){QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:tables){QSqlQuery rows(database);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
    const QStringList localTables{"work_orders","work_order_parts","order_registry","workspace_order_state"};
    const QStringList externalTables{"order_registry","part_registry"};
    const auto mainBefore=snapshot(main,localTables),externalBefore=snapshot(registry,externalTables);
    QVERIFY(mainBefore!="SQL_ERROR");QVERIFY(externalBefore!="SQL_ERROR");
    QVERIFY(!publishCommittedOrderRegistryProjection(main,100,&error));QVERIFY(!error.isEmpty());
    QCOMPARE(snapshot(registry,externalTables),externalBefore);
    for(const bool commit: {false,true}) {
        TransactionContext sourceTx(main,"ProjectionSource"),targetTx(registry,"ProjectionTarget");
        QVERIFY(sourceTx.begin("test",&error));
        QVERIFY(!publishCommittedOrderRegistryProjection(main,100,&error));
        QVERIFY(targetTx.begin("test",&error));
        QVERIFY2(publishCommittedOrderRegistryProjection(main,100,&error),qPrintable(error));
        QCOMPARE(snapshot(main,localTables),mainBefore);
        QVERIFY(r.exec("SELECT car FROM order_registry WHERE order_uid='native-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Pending car"));r.finish();
        QVERIFY(r.exec("SELECT name FROM part_registry WHERE part_uid='native-part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Pending part"));r.finish();
        if(commit)QVERIFY(targetTx.commit("test",&error));else QVERIFY(targetTx.rollback(QString(),&error));
        QVERIFY(sourceTx.rollback(QString(),&error));
        if(!commit)QCOMPARE(snapshot(registry,externalTables),externalBefore);
        QCOMPARE(snapshot(main,localTables),mainBefore);
    }
    QVERIFY(q.exec("SELECT registry_revision>published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QVERIFY(q.value(0).toBool());q.finish();
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    QVERIFY(q.exec("SELECT registry_revision=published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QVERIFY(q.value(0).toBool());
}

void ReportsOrdersTests::registryRebuildAtomicity_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","source-parent","foreign-header","header-ignore","part-ignore","local-ignore","active-ignore","missing-source","unresolved-projection","cross-file","pending-recovery","hydrated-copy","hydrated-cross-file","hydrated-wrong-source","hydrated-missing-source-order","archive-path-alias","hydrated-path-alias","alias-normalize-ignore"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::registryRebuildAtomicity()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Rebuild-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto main=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(main));QSqlQuery q(main);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('import-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'native-owner',1,2026,'2026-01-02','open','','','Current native car','Client','import-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(3,'native-part',100,'native-owner','2026-01-02','Current native part',1,0,0)"));
    {TransactionContext tx(main,"ArchiveFileBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(main,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    const auto path=archiveShardFilePathForOrder(200);QVERIFY(ensureArchiveShardSchemaByPath(path));ScopedSqliteConnection source(path,"archive_file_fixture",&error);QVERIFY2(source.isOpen(),qPrintable(error));auto physical=source.db();QSqlQuery a(physical);
    QVERIFY(a.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(200,'archive-first',7,2025,'2025-01-02','closed','','','Archived car','Client'),(201,'archive-second',7,2024,'2024-01-02','closed','','','Older car','Client')"));
    QVERIFY(a.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name) VALUES(1,'archive-a',200,'archive-first','2025-01-02','First archived'),(2,'archive-b',200,'archive-first','2025-01-02','Second archived'),(4,'archive-c',201,'archive-second','2024-01-02','Older archived')"));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    QVERIFY2(importArchivedRegistryFromDbPath(path,&error),qPrintable(error));
    QVERIFY2(syncLocalRegistryMirrorFromRegistryFile(main,&error),qPrintable(error));
    if(fault=="source-parent")QVERIFY(a.exec("UPDATE work_order_parts SET order_uid='archive-second' WHERE id=2"));
    if(fault=="foreign-header")QVERIFY(r.exec("UPDATE order_registry SET order_uid='unknown-recorded-owner' WHERE order_number=200"));
    if(fault=="header-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_rebuild BEFORE INSERT ON order_registry WHEN NEW.order_number=201 BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="part-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_rebuild BEFORE INSERT ON part_registry WHEN NEW.part_uid='archive-b' BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="local-ignore")QVERIFY(q.exec("CREATE TRIGGER reject_rebuild_local BEFORE UPDATE ON order_registry WHEN NEW.order_number=201 BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="active-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_rebuild_active BEFORE INSERT ON part_registry WHEN NEW.part_uid='native-part' BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="missing-source") {QVERIFY(r.prepare("UPDATE order_registry SET storage_path=? WHERE order_number=200"));r.addBindValue(QDir(root.path()).filePath("missing-archive.db"));QVERIFY(r.exec());}
    if(fault=="unresolved-projection") {QVERIFY(r.prepare("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year,storage_kind,storage_path) VALUES(300,'unresolved-archive-owner',9,2023,'archive',?)"));r.addBindValue(path);QVERIFY(r.exec());}
    if(fault=="cross-file") {const auto second=QDir(QFileInfo(path).absolutePath()).filePath("orders_duplicate.db");QVERIFY(a.exec("VACUUM INTO "+sqlQuoteText(second)));}
    if(fault=="pending-recovery") {QVERIFY(r.exec("INSERT INTO registry_meta(key,value) VALUES('registry_rebuild_pending','1') ON CONFLICT(key) DO UPDATE SET value='1'"));QVERIFY(q.exec("UPDATE order_registry SET car='Interrupted mirror' WHERE order_number=200"));}
    if(fault=="hydrated-copy" || fault=="hydrated-cross-file" || fault=="hydrated-wrong-source" || fault=="hydrated-missing-source-order" || fault=="hydrated-path-alias") {
        TransactionContext hydrate(main,"RebuildHydratedFixture");QVERIFY(hydrate.begin("test",&error));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(200,'archive-first',7,2025,'2025-01-02','closed','','','Edited hydrated car','Client','import-master')"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,'archive-a',200,'archive-first','2025-01-02','Edited hydrated part',1,0,0),(2,'archive-b',200,'archive-first','2025-01-02','Second archived',1,0,0)"));
        QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(main,200,&error),qPrintable(error));QVERIFY(hydrate.commit("test",&error));
        if(fault=="hydrated-cross-file" || fault=="hydrated-wrong-source") {
            const auto second=QDir(QFileInfo(path).absolutePath()).filePath("orders_hydrated_duplicate.db");QVERIFY(a.exec("VACUUM INTO "+sqlQuoteText(second)));
            ScopedSqliteConnection copy(second,"hydrated_duplicate_fixture",&error);QVERIFY(copy.isOpen());QSqlQuery trim(copy.db());
            QVERIFY(trim.exec("DELETE FROM work_order_parts WHERE order_number=201"));QVERIFY(trim.exec("DELETE FROM work_orders WHERE number=201"));
        }
    }
    if(fault=="hydrated-wrong-source" || fault=="hydrated-missing-source-order") {
        QVERIFY(a.exec("DELETE FROM work_order_parts WHERE order_number=200"));QVERIFY(a.exec("DELETE FROM work_orders WHERE number=200"));
    }
    const auto aliasPath=QFileInfo(path).absolutePath()+"/./"+QFileInfo(path).fileName();
    if(fault=="archive-path-alias" || fault=="hydrated-path-alias" || fault=="alias-normalize-ignore") {
        QCOMPARE(QFileInfo(aliasPath).canonicalFilePath(),QFileInfo(path).canonicalFilePath());
        QVERIFY(r.prepare("UPDATE order_registry SET storage_path=? WHERE order_number=200"));r.addBindValue(aliasPath);QVERIFY(r.exec());
        QVERIFY(r.prepare("UPDATE part_registry SET storage_path=? WHERE order_number=200"));r.addBindValue(aliasPath);QVERIFY(r.exec());
        if(fault=="hydrated-path-alias") {
            QVERIFY(q.prepare("UPDATE workspace_order_state SET source_db=? WHERE order_number=200"));q.addBindValue(aliasPath);QVERIFY(q.exec());
        }
    }
    if(fault=="alias-normalize-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_alias_normalization BEFORE UPDATE OF storage_path ON part_registry WHEN NEW.storage_path<>OLD.storage_path BEGIN SELECT RAISE(IGNORE); END"));
    auto snapshot=[](QSqlDatabase database,const QStringList& tables){QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:tables){QSqlQuery rows(database);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
    const QStringList sourceTables{"work_orders","work_order_parts"};
    const QStringList localTables{"work_orders","work_order_parts","order_registry","workspace_order_state"};
    const QStringList externalTables{"order_registry","part_registry"};
    const auto sourceBefore=snapshot(physical,sourceTables),mainBefore=snapshot(main,localTables),externalBefore=snapshot(registry,externalTables);
    QVERIFY(sourceBefore!="SQL_ERROR");QVERIFY(mainBefore!="SQL_ERROR");QVERIFY(externalBefore!="SQL_ERROR");
    error.clear();const bool ok=fault=="pending-recovery"?Database::runStartupRegistryRefresh(&error):rebuildRegistryDerivedState(main,&error);
    if(fault=="valid" || fault=="pending-recovery" || fault=="hydrated-copy" || fault=="archive-path-alias" || fault=="hydrated-path-alias") {
        QVERIFY2(ok,qPrintable(error));
        QVERIFY2(rebuildRegistryDerivedState(main,&error),qPrintable(error));
        QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='registry_rebuild_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("0"));r.finish();
        QVERIFY(q.exec("SELECT car FROM order_registry WHERE order_number=200"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),(fault=="hydrated-copy" || fault=="hydrated-path-alias")?QString("Edited hydrated car"):QString("Archived car"));q.finish();
        QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE (order_uid='archive-first' AND part_uid IN ('archive-a','archive-b')) OR (order_uid='archive-second' AND part_uid='archive-c') OR (order_uid='native-owner' AND part_uid='native-part')"));
        QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),4);r.finish();
        if(fault=="hydrated-copy" || fault=="hydrated-path-alias") {
            QVERIFY(r.exec("SELECT car FROM order_registry WHERE order_uid='archive-first'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Edited hydrated car"));r.finish();
            QVERIFY(r.exec("SELECT name FROM part_registry WHERE order_uid='archive-first' AND part_uid='archive-a'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Edited hydrated part"));QVERIFY(!r.next());r.finish();
            QVERIFY(r.exec("SELECT storage_kind,storage_path,source_db FROM order_registry WHERE order_uid='archive-first'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("archive"));QCOMPARE(r.value(1).toString(),path);QCOMPARE(r.value(2).toString(),fault=="hydrated-path-alias"?aliasPath:path);r.finish();
            QVERIFY(r.exec("SELECT storage_kind,storage_path,source_db FROM part_registry WHERE part_uid='archive-a'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("archive"));QCOMPARE(r.value(1).toString(),path);QCOMPARE(r.value(2).toString(),fault=="hydrated-path-alias"?aliasPath:path);r.finish();
            QVERIFY(q.exec("SELECT origin,source_db,registry_revision=published_registry_revision FROM workspace_order_state WHERE order_uid='archive-first'"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("legacy_copy"));QCOMPARE(q.value(1).toString(),fault=="hydrated-path-alias"?aliasPath:path);QVERIFY(q.value(2).toBool());q.finish();
        }
        QVERIFY(r.exec("SELECT count(*) FROM part_registry"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),4);r.finish();
        QVERIFY(r.exec("SELECT count(*) FROM order_registry"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),3);r.finish();
    } else {
        QVERIFY2(!ok,"Global rebuild must refuse invalid identities and ignored publication writes");QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot(registry,externalTables),externalBefore);
        QCOMPARE(snapshot(main,localTables),mainBefore);
    }
    QCOMPARE(snapshot(physical,sourceTables),sourceBefore);
}

void ReportsOrdersTests::archiveFileImport_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","active-precedence","source-parent","source-empty","source-year","source-no-root","foreign-root","unknown-part","foreign-part","part-ignore","header-ignore","delete-ignore","missing-file","empty-path"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::archiveFileImport()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Archive-File-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto main=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(main));QSqlQuery q(main);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('import-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'native-owner',1,2026,'2026-01-02','open','','','Current native car','Client','import-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(3,'native-part',100,'native-owner','2026-01-02','Current native part',1,0,0)"));
    {TransactionContext tx(main,"ArchiveFileBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(main,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    const auto path=QDir(root.path()).filePath("physical-input.db");QVERIFY(ensureArchiveShardSchemaByPath(path));ScopedSqliteConnection source(path,"archive_file_fixture",&error);QVERIFY2(source.isOpen(),qPrintable(error));auto physical=source.db();QSqlQuery a(physical);
    QVERIFY(a.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(200,'archive-first',7,2025,'2025-01-02','closed','','','Archived car','Client'),(201,'archive-second',7,2024,'2024-01-02','closed','','','Older car','Client')"));
    QVERIFY(a.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name) VALUES(1,'archive-a',200,'archive-first','2025-01-02','First archived'),(2,'archive-b',200,'archive-first','2025-01-02','Second archived'),(4,'archive-c',201,'archive-second','2024-01-02','Older archived')"));
    if(fault=="active-precedence") {QVERIFY(a.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'native-owner',1,2026,'2026-01-02','closed','','','Old native car','Client')"));QVERIFY(a.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,name) VALUES(3,'native-part',100,'native-owner','Old native part')"));}
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year,storage_kind,storage_path,car) VALUES(300,'unrelated-owner',9,2023,'archive','kept-source','Keep unrelated')"));
    QVERIFY(r.exec("INSERT INTO part_registry(entry_key,order_number,part_ref,order_uid,part_uid,storage_kind,storage_path,name) VALUES('kept-part',300,9,'unrelated-owner','unrelated-part','archive','kept-source','Keep unrelated part')"));
    QVERIFY(r.prepare("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year,storage_kind,storage_path,car) VALUES(200,'archive-first',7,2025,'archive',?,'Previously archived')"));r.addBindValue(path);QVERIFY(r.exec());
    QVERIFY(r.prepare("INSERT INTO part_registry(entry_key,order_number,part_ref,order_uid,part_uid,storage_kind,storage_path,name) VALUES('old-200',200,1,'archive-first','archive-a','archive',?,'Previously projected')"));r.addBindValue(path);QVERIFY(r.exec());
    if(fault=="source-parent")QVERIFY(a.exec("UPDATE work_order_parts SET order_uid='archive-second' WHERE id=2"));
    if(fault=="source-empty")QVERIFY(a.exec("UPDATE work_order_parts SET part_uid='' WHERE id=2"));
    if(fault=="source-year")QVERIFY(a.exec("UPDATE work_orders SET order_year=0,opened_at='unresolved' WHERE number=200"));
    if(fault=="source-no-root")QVERIFY(a.exec("DROP TABLE work_orders"));
    if(fault=="foreign-root")QVERIFY(r.exec("UPDATE order_registry SET order_uid='foreign-root' WHERE order_number=200"));
    if(fault=="unknown-part")QVERIFY(r.exec("UPDATE part_registry SET order_uid='',part_uid='' WHERE order_number=200"));
    if(fault=="foreign-part")QVERIFY(r.exec("UPDATE part_registry SET part_uid='archive-c',part_ref=4 WHERE order_number=200"));
    if(fault=="part-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_file BEFORE INSERT ON part_registry WHEN NEW.name='Second archived' BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="header-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_file BEFORE INSERT ON order_registry WHEN NEW.order_number=201 BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="delete-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_file BEFORE DELETE ON part_registry WHEN OLD.order_number=200 BEGIN SELECT RAISE(IGNORE); END"));
    auto snapshot=[](QSqlDatabase database,const QStringList& tables){QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);for(const auto& table:tables){QSqlQuery rows(database);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}return bytes;};
    const auto mainBefore=snapshot(main,{"work_orders","work_order_parts"}),externalBefore=snapshot(registry,{"order_registry","part_registry"});
    const auto sourceTables=fault=="source-no-root"?QStringList{"work_order_parts"}:QStringList{"work_orders","work_order_parts"};const auto sourceBefore=snapshot(physical,sourceTables);
    QVERIFY(mainBefore!="SQL_ERROR");QVERIFY(externalBefore!="SQL_ERROR");QVERIFY(sourceBefore!="SQL_ERROR");
    const auto missing=QDir(root.path()).filePath("missing-input.db");const auto input=fault=="missing-file"?missing:fault=="empty-path"?QString():path;
    error.clear();const bool ok=importArchivedRegistryFromDbPath(input,&error);const bool valid=fault=="valid" || fault=="active-precedence";
    if(valid)QVERIFY2(ok,qPrintable(error));else {QVERIFY2(!ok,"Archive file import must refuse invalid identity or ignored writes");QVERIFY(!error.isEmpty());QCOMPARE(snapshot(registry,{"order_registry","part_registry"}),externalBefore);}
    QCOMPARE(snapshot(main,{"work_orders","work_order_parts"}),mainBefore);QCOMPARE(snapshot(physical,sourceTables),sourceBefore);QVERIFY(!QFileInfo::exists(missing));
    if(!valid)return;
    QVERIFY2(importArchivedRegistryFromDbPath(path,&error),qPrintable(error));
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE order_uid='archive-first' AND part_uid IN ('archive-a','archive-b')"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE order_uid='archive-second' AND part_uid='archive-c'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE visible_order_number=7 AND ((order_uid='archive-first' AND order_year=2025) OR (order_uid='archive-second' AND order_year=2024))"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE order_uid='native-owner' AND car='Current native car' AND storage_kind='active'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE part_uid='native-part' AND order_uid='native-owner' AND name='Current native part' AND storage_kind='active'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE part_uid='unrelated-part' AND order_uid='unrelated-owner' AND name='Keep unrelated part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),5);
}

void ReportsOrdersTests::detailIdentityRefusal_data()
{
    QTest::addColumn<QString>("fault");QTest::addColumn<QString>("entry");
    for(const auto& fault:QStringList{"root-empty","root-duplicate","annual-duplicate","year-empty","service-parent","part-parent","outsource-parent","outsource-repair-parent","cache-root","cache-service","cache-part"})
        for(const auto& entry:QStringList{"schema","repair","validate"})QTest::newRow(qPrintable(fault+":"+entry))<<fault<<entry;
    QTest::newRow("valid-sparse")<<QString("valid-sparse")<<QString("validate");
    QTest::newRow("foreign-keys-off")<<QString("fk-off")<<QString("repair");
    QTest::newRow("owned-transaction")<<QString("owned")<<QString("repair");
    QTest::newRow("sql-read-error")<<QString("sql-error")<<QString("validate");
    QTest::newRow("late-schema-error")<<QString("schema-index-error")<<QString("schema");
}

void ReportsOrdersTests::detailIdentityRefusal()
{
    QFETCH(QString,fault);QFETCH(QString,entry);
    QString error;ScopedSqliteConnection fixture(":memory:","detail_identity",&error);QVERIFY2(fixture.isOpen(),qPrintable(error));auto db=fixture.db();QSqlQuery q(db);
    QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
    QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT NOT NULL,order_number INTEGER,order_year INTEGER,opened_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO work_orders VALUES(100,'detail-first',7,2025,'2025-01-02'),(101,'detail-second',8,2025,'2025-01-02')"));
    for(const auto& table:QStringList{"work_order_services","work_order_parts","outsource_jobs","outsource_repairs","router_cache_work_order_services","router_cache_work_order_parts"})QVERIFY(q.exec("CREATE TABLE "+table+"(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT)"));
    QVERIFY(q.exec("CREATE TABLE router_cache_work_orders AS SELECT * FROM work_orders"));
    QVERIFY(q.exec("INSERT INTO work_order_services VALUES(1,100,'detail-first'),(2,101,'detail-second')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(1,100,'detail-first'),(2,101,'detail-second'),(3,0,'')"));
    QVERIFY2(OrderDetailIntegrityValidator::ensureCanonicalDetailSchema(db,&error),qPrintable(error));
    if(fault=="root-empty")QVERIFY(q.exec("UPDATE work_orders SET order_uid='' WHERE number=100"));
    if(fault=="root-duplicate") {QVERIFY(q.exec("DROP INDEX idx_work_orders_uid_unique"));QVERIFY(q.exec("UPDATE work_orders SET order_uid='detail-first' WHERE number=101"));}
    if(fault=="annual-duplicate")QVERIFY(q.exec("UPDATE work_orders SET order_number=7 WHERE number=101"));
    if(fault=="year-empty")QVERIFY(q.exec("UPDATE work_orders SET order_year=0,opened_at='unresolved' WHERE number=100"));
    if(fault=="service-parent")QVERIFY(q.exec("UPDATE work_order_services SET order_uid='detail-second' WHERE id=1"));
    if(fault=="part-parent")QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='detail-second' WHERE id=1"));
    if(fault=="outsource-parent")QVERIFY(q.exec("INSERT INTO outsource_jobs VALUES(1,100,'detail-second')"));
    if(fault=="outsource-repair-parent")QVERIFY(q.exec("INSERT INTO outsource_repairs VALUES(1,100,'detail-second')"));
    if(fault=="cache-root")QVERIFY(q.exec("UPDATE router_cache_work_orders SET order_uid='foreign-cache' WHERE number=100"));
    if(fault=="cache-service")QVERIFY(q.exec("INSERT INTO router_cache_work_order_services VALUES(1,100,'detail-second')"));
    if(fault=="cache-part")QVERIFY(q.exec("INSERT INTO router_cache_work_order_parts VALUES(1,100,'detail-second')"));
    if(fault=="valid-sparse") {QVERIFY(q.exec("DELETE FROM work_order_services WHERE id=2"));QVERIFY(q.exec("DELETE FROM work_order_parts WHERE id=2"));}
    if(fault=="sql-error") {QVERIFY(q.exec("DELETE FROM work_orders"));QVERIFY(q.exec("DELETE FROM work_order_services"));QVERIFY(q.exec("DELETE FROM router_cache_work_orders"));QVERIFY(q.exec("DROP TABLE work_order_parts"));}
    if(fault=="schema-index-error") {QVERIFY(q.exec("DROP INDEX idx_work_orders_uid_unique"));QVERIFY(q.exec("DROP INDEX idx_router_cache_work_orders_uid_unique"));QVERIFY(q.exec("CREATE TABLE idx_router_cache_work_orders_uid_unique(proof TEXT)"));}
    if(fault=="fk-off")QVERIFY(q.exec("PRAGMA foreign_keys=OFF"));
    QVERIFY(q.exec("PRAGMA foreign_keys"));QVERIFY(q.next());const int fkBefore=q.value(0).toInt();q.finish();
    TransactionContext owner(db,"DetailIdentityOwner");if(fault=="owned")QVERIFY(owner.begin("test",&error));
    auto snapshot=[&] {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        QSqlQuery schema(db);if(!schema.exec("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name"))return QByteArray("SQL_ERROR");while(schema.next())for(int i=0;i<4;++i)stream<<schema.value(i);schema.finish();
        QSqlQuery names(db);if(!names.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"))return QByteArray("SQL_ERROR");QStringList tables;while(names.next())tables<<names.value(0).toString();names.finish();
        for(const auto& table:tables) {QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)))return QByteArray("SQL_ERROR");QStringList columns;for(int i=0;i<rows.record().count();++i)columns<<sqlQuoteIdent(rows.record().fieldName(i));rows.finish();if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY "+columns.join(',')))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}
        return bytes;
    };
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");error.clear();
    const bool ok=entry=="schema"?OrderDetailIntegrityValidator::ensureCanonicalDetailSchema(db,&error):entry=="repair"?OrderDetailIntegrityValidator::repairCanonicalOrderUids(db,&error):OrderDetailIntegrityValidator::validateAfterImport(db,&error);
    const bool valid=fault=="valid-sparse" || fault=="fk-off" || fault=="owned";
    if(valid)QVERIFY2(ok,qPrintable(error));else {QVERIFY2(!ok,"Recorded identities/read errors must not be silently repaired or accepted");QVERIFY(!error.isEmpty());}
    if(fault=="sql-error") {const auto report=OrderDetailIntegrityValidator::analyze(db);QVERIFY(!report.ok);QVERIFY(!report.errorText.isEmpty());}
    QCOMPARE(snapshot(),before);QVERIFY(q.exec("PRAGMA foreign_keys"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),fkBefore);q.finish();
    if(fault=="owned") {QVERIFY(owner.isActive());QVERIFY(q.exec("UPDATE work_orders SET opened_at='2025-01-04' WHERE number=100"));QVERIFY(owner.rollback(QString(),&error));}
}

void ReportsOrdersTests::detailIdentityClosedRead()
{
    QTemporaryDir root;QVERIFY(root.isValid());const auto path=QDir(root.path()).filePath("must-not-be-created.db");
    const auto name=QString("detail_closed_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto cleanup=qScopeGuard([&]{SqliteConnectionUtils::safeCloseAndRemoveDatabase(name);});
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);DbThreadConnectionSupport::registerConnectionName(name,path);
    QString error;QVERIFY(!OrderDetailIntegrityValidator::validateAfterImport(db,&error));QVERIFY(!error.isEmpty());QVERIFY(!db.isOpen());QVERIFY(!QFileInfo::exists(path));
    const auto report=OrderDetailIntegrityValidator::analyze(db);QVERIFY(!report.ok);QVERIFY(!report.errorText.isEmpty());QVERIFY(!db.isOpen());QVERIFY(!QFileInfo::exists(path));
}

void ReportsOrdersTests::archiveUidPublication_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","source-empty","source-parent","source-year","external-header","external-part-parent","part-insert-ignore","header-ignore","part-delete-ignore"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::archiveUidPublication()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{ActivityAuditService::clearCurrentActor();Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Archive-Publish-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('archive-owner','Owner','owner',1)"));ActivityAuditService::setCurrentActor({"archive-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('archive-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'archived-owner',7,2025,'2025-01-02','2025-01-03','closed','','','Target car','Client','archive-master'),(101,'other-owner',8,2025,'2025-01-02','','open','','','Other car','Client','archive-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,'archived-first',100,'archived-owner','2025-01-02','First target',1,0,0),(2,'archived-second',100,'archived-owner','2025-01-02','Second target',1,0,0),(3,'other-part',101,'other-owner','2025-01-02','Other part',1,0,0),(4,'unassigned-part',0,'','2025-01-02','Stock part',1,0,0)"));
    {TransactionContext tx(db,"ArchivePublishBaseline");QVERIFY(tx.begin("test",&error));for(int n:{100,101})QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,n,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    if(fault=="source-year")QVERIFY(q.exec("UPDATE work_orders SET order_year=0,opened_at='unresolved' WHERE number=100"));
    if(fault=="source-empty")QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='' WHERE id=2"));
    if(fault=="source-parent")QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='other-owner' WHERE id=2"));
    if(fault=="external-header")QVERIFY(r.exec("UPDATE order_registry SET order_uid='foreign-owner' WHERE order_number=100"));
    if(fault=="external-part-parent")QVERIFY(r.exec("UPDATE part_registry SET order_uid='other-owner' WHERE part_uid='archived-second'"));
    if(fault=="part-insert-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_archive BEFORE INSERT ON part_registry WHEN NEW.name='Second target' BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="header-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_archive BEFORE UPDATE ON order_registry WHEN NEW.order_number=100 BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="part-delete-ignore")QVERIFY(r.exec("CREATE TRIGGER reject_archive BEFORE DELETE ON part_registry WHEN OLD.order_number=100 BEGIN SELECT RAISE(IGNORE); END"));
    auto snapshot=[](QSqlDatabase source,const QStringList& tables) {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        for(const auto& table:tables) {QSqlQuery rows(source);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY 1"))return QByteArray("SQL_ERROR");stream<<table;while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");}
        return bytes;
    };
    const auto sourceBefore=snapshot(db,{"work_orders","work_order_parts"}),externalBefore=snapshot(registry,{"order_registry","part_registry"});QVERIFY(sourceBefore!="SQL_ERROR");QVERIFY(externalBefore!="SQL_ERROR");
    error.clear();const bool ok=OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(db,100,&error);
    if(fault!="valid") {
        QVERIFY2(!ok,"Archive must refuse invalid UID or an ignored required write");QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot(db,{"work_orders","work_order_parts"}),sourceBefore);QCOMPARE(snapshot(registry,{"order_registry","part_registry"}),externalBefore);
        QVERIFY(q.exec("UPDATE work_orders SET order_year=2025,opened_at='2025-01-02' WHERE number=100"));
        QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='archived-second',order_uid='archived-owner' WHERE id=2"));
        QVERIFY(r.exec("UPDATE order_registry SET order_uid='archived-owner' WHERE order_number=100"));QVERIFY(r.exec("UPDATE part_registry SET order_uid='archived-owner' WHERE order_number=100"));
        if(fault.endsWith("ignore"))QVERIFY(r.exec("DROP TRIGGER reject_archive"));
        QVERIFY2(OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(db,100,&error),qPrintable(error));
    } else QVERIFY2(ok,qPrintable(error));
    const auto shard=archiveShardFilePathForOrder(100);ScopedSqliteConnection physical(shard,"archive_uid_physical",&error);QVERIFY2(physical.isOpen(),qPrintable(error));QSqlQuery archived(physical.db());
    QVERIFY(archived.exec("SELECT count(*) FROM work_order_parts WHERE order_uid='archived-owner' AND part_uid IN ('archived-first','archived-second')"));QVERIFY(archived.next());QCOMPARE(archived.value(0).toInt(),2);archived.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE order_uid='archived-owner' AND part_uid IN ('archived-first','archived-second') AND storage_kind='archive'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE order_number=100 AND order_uid='archived-owner' AND order_year=2025 AND visible_order_number=7 AND storage_kind='archive'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(q.exec("SELECT count(*) FROM work_orders WHERE number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM work_order_parts WHERE part_uid='other-part' AND order_uid='other-owner'"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
    QVERIFY(q.exec("SELECT count(*) FROM work_order_parts WHERE part_uid='unassigned-part' AND order_number=0 AND order_uid=''"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE part_uid='other-part' AND order_uid='other-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY2(OrderArchiveShardSqlSupport::pruneActiveClosedOrdersToArchive(db,&error),qPrintable(error));
    QVERIFY(r.exec("SELECT count(*) FROM part_registry"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),3);
}

void ReportsOrdersTests::archiveShardIdentityPreparation_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","part-parent","service-parent","header-empty","part-empty","ddl-error"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::archiveShardIdentityPreparation()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Archive-Identity-",root.path()));
    const auto path=QDir(root.path()).filePath("existing-shard.db");QVERIFY(ensureArchiveShardSchemaByPath(path));
    QString error;ScopedSqliteConnection fixture(path,"archive_identity_fixture",&error);QVERIFY2(fixture.isOpen(),qPrintable(error));auto db=fixture.db();QSqlQuery q(db);
    if(!tableColumnsForDb(db,"work_order_parts").contains("part_uid"))QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN part_uid TEXT NOT NULL DEFAULT ''"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'archive-first',7,2025,'2025-01-02','closed','','','Car','Client'),(101,'archive-second',8,2025,'2025-01-02','closed','','','Car','Client')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name) VALUES(1,100,'archive-first','archive-part','Part')"));
    QVERIFY(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,title) VALUES(10,100,'archive-first','Work')"));
    if(fault=="part-parent")QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='archive-second' WHERE id=1"));
    if(fault=="service-parent")QVERIFY(q.exec("UPDATE work_order_services SET order_uid='archive-second' WHERE id=10"));
    if(fault=="header-empty")QVERIFY(q.exec("UPDATE work_orders SET order_uid='' WHERE number=100"));
    if(fault=="part-empty")QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='' WHERE id=1"));
    if(fault=="ddl-error") {QVERIFY(q.exec("ALTER TABLE work_orders DROP COLUMN discount_mode"));QVERIFY(q.exec("DROP INDEX idx_arch_orders_uid_unique"));QVERIFY(q.exec("CREATE TABLE idx_arch_orders_uid_unique(proof TEXT)"));}
    auto snapshot=[&] {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        QSqlQuery schema(db);if(!schema.exec("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name"))return QByteArray("SQL_ERROR");
        while(schema.next())for(int i=0;i<4;++i)stream<<schema.value(i);schema.finish();
        QSqlQuery names(db);if(!names.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"))return QByteArray("SQL_ERROR");QStringList tables;while(names.next())tables<<names.value(0).toString();names.finish();
        for(const auto& table:tables) {
            QSqlQuery rows(db);if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)))return QByteArray("SQL_ERROR");QStringList columns;for(int i=0;i<rows.record().count();++i)columns<<sqlQuoteIdent(rows.record().fieldName(i));rows.finish();
            if(!rows.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY "+columns.join(',')))return QByteArray("SQL_ERROR");stream<<table;
            while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);if(rows.lastError().isValid())return QByteArray("SQL_ERROR");
        }
        return bytes;
    };
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");const bool ok=ensureArchiveShardSchemaByPath(path);const auto after=snapshot();QVERIFY(after!="SQL_ERROR");
    if(fault=="valid")QVERIFY(ok);else QVERIFY2(!ok,qPrintable(QString("Archive schema accepted invalid identity/DDL; changed=%1").arg(before!=after)));
    QCOMPARE(after,before);
    QCOMPARE(SqliteConnectionUtils::countOpenConnectionsForPath(path),1);
    // Explicit fixture-only reconciliation; product must not infer it.
    if(fault=="ddl-error")QVERIFY(q.exec("DROP TABLE idx_arch_orders_uid_unique"));
    QVERIFY(q.exec("UPDATE work_orders SET order_uid='archive-first' WHERE number=100"));QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='archive-first',part_uid='archive-part' WHERE id=1"));QVERIFY(q.exec("UPDATE work_order_services SET order_uid='archive-first' WHERE id=10"));
    QVERIFY(ensureArchiveShardSchemaByPath(path));QVERIFY(ensureArchiveShardSchemaByPath(path));
    QCOMPARE(SqliteConnectionUtils::countOpenConnectionsForPath(path),1);
    QVERIFY(q.exec("SELECT order_uid,part_uid FROM work_order_parts WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("archive-first"));QCOMPARE(q.value(1).toString(),QString("archive-part"));
}

void ReportsOrdersTests::partRegistryUidOwnership_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"valid","foreign-parent","legacy-empty","source-empty"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::partRegistryUidOwnership()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Part-UID-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('uid-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'uid-first',7,2025,'2025-01-02','open','','','Car','Client','uid-master'),(101,'uid-second',8,2025,'2025-01-02','open','','','Car','Client','uid-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,part_uid,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,'part-first',100,'uid-first','2025-01-02','First part',1,0,0),(2,'part-second',101,'uid-second','2025-01-02','Second part',1,0,0)"));
    {TransactionContext tx(db,"PartUidBaseline");QVERIFY(tx.begin("test",&error));for(int n:{100,101})QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,n,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
    QVERIFY2(r.exec("SELECT order_uid,part_uid FROM part_registry WHERE order_number=100"),qPrintable(r.lastError().text()));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("uid-first"));QCOMPARE(r.value(1).toString(),QString("part-first"));r.finish();
    if(fault=="foreign-parent")QVERIFY(r.exec("UPDATE part_registry SET order_uid='uid-second' WHERE part_uid='part-first'"));
    if(fault=="legacy-empty")QVERIFY(r.exec("UPDATE part_registry SET order_uid='',part_uid='' WHERE order_number=100"));
    if(fault=="source-empty")QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='' WHERE id=1"));
    auto snapshot=[&] {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        QSqlQuery rows(QSqlDatabase::database(dbRegistryConnectionName(),false));if(!rows.exec("SELECT * FROM part_registry ORDER BY entry_key"))return QByteArray("SQL_ERROR");
        while(rows.next())for(int i=0;i<rows.record().count();++i)stream<<rows.value(i);return bytes;
    };
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");error.clear();const bool ok=Database::refreshOrderRegistry(100,&error);
    if(fault!="valid") {QVERIFY(!ok);QVERIFY(!error.isEmpty());QCOMPARE(snapshot(),before);}
    else QVERIFY2(ok,qPrintable(error));
    // Repair only known fixture ownership; no numeric inference for real legacy rows.
    QVERIFY(q.exec("UPDATE work_order_parts SET part_uid='part-first' WHERE id=1"));
    QVERIFY(r.exec("UPDATE part_registry SET order_uid='uid-first',part_uid='part-first' WHERE order_number=100"));
    QVERIFY(q.exec("UPDATE work_order_parts SET name='Renamed part',part_number='Changed SKU' WHERE id=1"));
    QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE part_uid='part-first' AND order_uid='uid-first' AND name='Renamed part' AND part_number='Changed SKU'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE part_uid='part-second' AND order_uid='uid-second'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);
}

void ReportsOrdersTests::startupRegistryBulkProcess_data()
{
    QTest::addColumn<int>("blockedNumber");
    QTest::newRow("first-publication")<<100;
    QTest::newRow("partial-batch")<<101;
}

void ReportsOrdersTests::startupRegistryBulkProcess()
{
    QFETCH(int,blockedNumber);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Bulk-Process-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('bulk-master','Master','master')"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'bulk-first',7,2025,'2025-01-02','open','','','Original car','Client','bulk-master'),(101,'bulk-second',8,2025,'2025-01-02','open','','','Original car','Client','bulk-master')"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,100,'bulk-first','2025-01-02','Original part',1,0,0),(2,101,'bulk-second','2025-01-02','Original part',1,0,0)"));
        {TransactionContext tx(db,"BulkProcessBaseline");QVERIFY(tx.begin("test",&error));for(int n:{100,101})QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,n,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
        QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE work_orders SET car='Rebuilt car'"));QVERIFY(q.exec("UPDATE work_order_parts SET name='Rebuilt part'"));
        QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
        QVERIFY(r.exec("UPDATE registry_meta SET value='force-cold' WHERE key='runtime_active_stamp'"));
        QVERIFY(r.exec(QString("CREATE TRIGGER blocked_bulk BEFORE INSERT ON order_registry WHEN NEW.order_number=%1 BEGIN SELECT RAISE(IGNORE); END").arg(blockedNumber)));
    }
    Database::close();
    QProcess child;auto env=QProcessEnvironment::systemEnvironment();
    env.insert("CS_TEST_DATA_ROOT",root.path());env.insert("CS_SAFETY_FIXTURE_APP_NAME",QCoreApplication::applicationName());
    env.insert("CS_SAFETY_FIXTURE_ORGANIZATION",QCoreApplication::organizationName());env.insert("CS_SAFETY_FIXTURE_DOMAIN",QCoreApplication::organizationDomain());
    child.setProcessEnvironment(env);child.start(QCoreApplication::applicationFilePath(),{"--startup-bulk-crash-fixture"});
    QVERIFY(child.waitForStarted(5000));QVERIFY(child.waitForFinished(15000));
    const auto diagnostics=child.readAllStandardError()+child.readAllStandardOutput();
    QCOMPARE(child.exitStatus(),QProcess::NormalExit);QVERIFY2(child.exitCode()==0,diagnostics.constData());
    QVERIFY2(!diagnostics.contains("QCRITICAL|") && !diagnostics.contains("QFATAL|") && !diagnostics.contains("removeDatabase"),diagnostics.constData());
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QSqlQuery q(db);
    auto registry=QSqlDatabase::addDatabase("QSQLITE",dbRegistryConnectionName());registry.setDatabaseName(AppRuntime::registryDbFilePath());
    DbThreadConnectionSupport::registerRuntimeConnection(dbRegistryConnectionName(),AppRuntime::registryDbFilePath());
    QVERIFY2(registry.open(),qPrintable(registry.lastError().text()));QSqlQuery r(registry);
    QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='runtime_bulk_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("1"));r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE car='Rebuilt car'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),blockedNumber==101?1:0);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE name='Rebuilt part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),blockedNumber==101?1:0);r.finish();
    QVERIFY(q.exec("SELECT count(*) FROM workspace_order_state WHERE registry_revision=1 AND published_registry_revision=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
    QVERIFY(r.exec("DROP TRIGGER blocked_bulk"));
    for(int repeat=0;repeat<2;++repeat) {
        QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
        QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE car='Rebuilt car' AND order_year=2025 AND ((order_number=100 AND order_uid='bulk-first' AND visible_order_number=7) OR (order_number=101 AND order_uid='bulk-second' AND visible_order_number=8))"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
        QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE name='Rebuilt part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);r.finish();
        QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='runtime_bulk_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("0"));r.finish();
        QVERIFY(q.exec("SELECT count(*) FROM workspace_order_state WHERE registry_revision=1 AND published_registry_revision=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
    }
}

void ReportsOrdersTests::startupRegistryPublicationFailure_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"header-ignore","part-ignore","metadata-ignore","intent-ignore"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::startupRegistryPublicationFailure()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Startup-Pub-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('startup-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'startup-owner',7,2025,'2025-01-02','open','','','Original car','Client','startup-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,100,'startup-owner','2025-01-02','Original part',1,0,0)"));
    {TransactionContext tx(db,"StartupPublicationBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    QVERIFY(q.exec("UPDATE work_orders SET car='Updated car' WHERE number=100"));
    QVERIFY(q.exec("UPDATE work_order_parts SET name='Updated part' WHERE id=1"));
    QVERIFY(r.exec("UPDATE registry_meta SET value='force-cold' WHERE key='runtime_active_stamp'"));
    if(fault=="header-ignore")QVERIFY(r.exec("CREATE TRIGGER injected_startup_failure BEFORE INSERT ON order_registry BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="part-ignore")QVERIFY(r.exec("CREATE TRIGGER injected_startup_failure BEFORE INSERT ON part_registry BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="metadata-ignore")QVERIFY(r.exec("CREATE TRIGGER injected_startup_failure BEFORE INSERT ON registry_meta BEGIN SELECT RAISE(IGNORE); END"));
    if(fault=="intent-ignore") {
        QVERIFY(q.exec("DELETE FROM workspace_order_state WHERE order_number=100"));
        QVERIFY(q.exec("CREATE TRIGGER injected_startup_failure BEFORE UPDATE OF registry_revision ON workspace_order_state BEGIN SELECT RAISE(IGNORE); END"));
    }
    error.clear();const bool ok=Database::runStartupRegistryRefresh(&error);
    QVERIFY2(!ok,"Startup must not report success after a required publication write was ignored");QVERIFY(!error.isEmpty());
    if(fault!="metadata-ignore") {
        QVERIFY(r.exec("SELECT value FROM registry_meta WHERE key='runtime_bulk_pending'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("1"));r.finish();
    }
    if(fault=="header-ignore" || fault=="part-ignore") {
        // Even apparently healthy stamps/counts cannot hide an unfinished batch.
        for(const auto& entry:QList<QPair<QString,QString>>{{"runtime_active_stamp",AppRuntime::activeDbFilePath()},{"runtime_archive_stamp",AppRuntime::archiveDbFilePath()}}) {
            const QFileInfo fi(entry.second);const auto stamp=fi.exists()?QString("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toUTC().toString(Qt::ISODateWithMs)):QString("missing");
            QVERIFY(r.prepare("UPDATE registry_meta SET value=? WHERE key=?"));r.addBindValue(stamp);r.addBindValue(entry.first);QVERIFY(r.exec());
        }
        error.clear();QVERIFY(!Database::runStartupRegistryRefresh(&error));QVERIFY(!error.isEmpty());
    }
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number,car FROM work_orders WHERE number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("startup-owner"));QCOMPARE(q.value(1).toInt(),2025);QCOMPARE(q.value(2).toInt(),7);QCOMPARE(q.value(3).toString(),QString("Updated car"));q.finish();
    if(fault=="intent-ignore")QVERIFY(q.exec("DROP TRIGGER injected_startup_failure"));else QVERIFY(r.exec("DROP TRIGGER injected_startup_failure"));
    QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE order_uid='startup-owner' AND order_year=2025 AND visible_order_number=7 AND car='Updated car'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE order_number=100 AND name='Updated part'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
    QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),q.value(1).toLongLong());
}

void ReportsOrdersTests::startupRegistryIdentityPreflight_data()
{
    QTest::addColumn<QString>("fault");
    for(const auto& fault:QStringList{"external-uid-warm","external-uid-cold","external-year","external-number","annual-owner","active-orphan","local-uid","state-uid"})QTest::newRow(qPrintable(fault))<<fault;
}

void ReportsOrdersTests::startupRegistryIdentityPreflight()
{
    QFETCH(QString,fault);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Startup-ID-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('startup-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'startup-owner',7,2025,'2025-01-02','open','','','Car','Client','startup-master'),(101,'sibling-owner',8,2025,'2025-01-02','open','','','Sibling','Client','startup-master')"));
    {TransactionContext tx(db,"StartupIdentityBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,101,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);QSqlQuery r(registry);
    if(fault.startsWith("external-uid"))QVERIFY(r.exec("UPDATE order_registry SET order_uid='foreign-owner' WHERE order_number=100"));
    if(fault=="external-year")QVERIFY(r.exec("UPDATE order_registry SET order_year=2024 WHERE order_number=100"));
    if(fault=="external-number")QVERIFY(r.exec("UPDATE order_registry SET visible_order_number=6 WHERE order_number=100"));
    if(fault=="annual-owner") {
        QVERIFY(r.exec("DELETE FROM order_registry WHERE order_number=100"));
        QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(102,'foreign-owner',7,2025)"));
    }
    if(fault=="active-orphan")QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(102,'foreign-owner',9,2025)"));
    if(fault=="local-uid")QVERIFY(q.exec("UPDATE order_registry SET order_uid='foreign-owner' WHERE order_number=100"));
    if(fault=="state-uid")QVERIFY(q.exec("UPDATE workspace_order_state SET order_uid='foreign-owner' WHERE order_number=100"));
    if(fault.endsWith("-cold"))QVERIFY(r.exec("UPDATE registry_meta SET value='force-cold-path' WHERE key='runtime_active_stamp'"));
    auto snapshot=[&] {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        for(const auto& source:QList<QSqlDatabase>{db,registry}) {
            QSqlQuery schema(source);if(!schema.exec("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name"))return QByteArray("SQL_ERROR");
            while(schema.next())for(int i=0;i<4;++i)stream<<schema.value(i);schema.finish();
            QSqlQuery names(source);if(!names.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"))return QByteArray("SQL_ERROR");
            QStringList tables;while(names.next())tables<<names.value(0).toString();names.finish();
            for(const auto& table:tables) {
                QSqlQuery read(source);if(!read.exec("SELECT * FROM "+sqlQuoteIdent(table))){qWarning()<<table<<read.lastError().text();return QByteArray("SQL_ERROR");}
                QStringList columns;for(int i=0;i<read.record().count();++i)columns<<sqlQuoteIdent(read.record().fieldName(i));read.finish();
                if(!read.exec("SELECT * FROM "+sqlQuoteIdent(table)+" ORDER BY "+columns.join(","))){qWarning()<<table<<read.lastError().text();return QByteArray("SQL_ERROR");}
                stream<<table;while(read.next())for(int i=0;i<read.record().count();++i)stream<<read.value(i);
                if(read.lastError().isValid())return QByteArray("SQL_ERROR");
            }
        }
        return bytes;
    };
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");error.clear();
    const bool ok=Database::runStartupRegistryRefresh(&error);const auto after=snapshot();QVERIFY(after!="SQL_ERROR");
    QVERIFY2(!ok,qPrintable(QString("Startup accepted conflicting recorded identity; snapshots_changed=%1").arg(before!=after)));
    QVERIFY(!error.isEmpty());QCOMPARE(after,before);
    // Explicitly resolve only injected ambiguity, then retry original identities.
    QVERIFY(q.exec("UPDATE workspace_order_state SET order_uid='startup-owner' WHERE order_number=100"));
    QVERIFY(q.exec("UPDATE order_registry SET order_uid='startup-owner',visible_order_number=7,order_year=2025 WHERE order_number=100"));
    QVERIFY(r.exec("DELETE FROM order_registry WHERE order_number=102"));
    QVERIFY(r.exec("UPDATE order_registry SET order_uid='startup-owner',visible_order_number=7,order_year=2025 WHERE order_number=100"));
    QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
    QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_year,order_number FROM work_orders WHERE number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("startup-owner"));QCOMPARE(q.value(1).toInt(),2025);QCOMPARE(q.value(2).toInt(),7);q.finish();
    QVERIFY(r.exec("SELECT order_uid,order_year,visible_order_number FROM order_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("startup-owner"));QCOMPARE(r.value(1).toInt(),2025);QCOMPARE(r.value(2).toInt(),7);
}

void ReportsOrdersTests::childDateOwnerMismatch_data()
{
    QTest::addColumn<QString>("operation");
    for(const auto& operation:QStringList{"installed-number","delivery-uid","unassigned-delivery"})QTest::newRow(qPrintable(operation))<<operation;
}

void ReportsOrdersTests::childDateOwnerMismatch()
{
    QFETCH(QString,operation);QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Part-Owner-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('projection-master','Master','master')"));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'projection-owner',7,2025,'2025-01-02','open','','','Car','Client','projection-master'),(101,'other-owner',8,2025,'2025-01-02','open','','','Other car','Other client','projection-master')"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(1,100,'projection-owner','2025-01-02','Part',1,0,0)"));
    if(operation=="unassigned-delivery")QVERIFY(q.exec("UPDATE work_order_parts SET order_number=0,order_uid='' WHERE id=1"));
    {TransactionContext tx(db,"DateOwnershipBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
    QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
    if(operation=="installed-number")QVERIFY(q.exec("UPDATE work_order_parts SET order_number=101 WHERE id=1"));
    else if(operation=="delivery-uid")QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='other-owner' WHERE id=1"));
    auto snapshot=[&] {
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
        QSqlQuery read(db);if(!read.exec("SELECT p.order_number,p.order_uid,p.installed_at,p.delivery_date,s.registry_revision,s.published_registry_revision FROM work_order_parts p CROSS JOIN workspace_order_state s WHERE p.id=1 AND s.order_number=100") || !read.next())return QByteArray("SQL_ERROR");
        for(int i=0;i<6;++i)stream<<read.value(i);return bytes;
    };
    const auto before=snapshot();QVERIFY(before!="SQL_ERROR");
    error.clear();const bool ok=operation=="installed-number"?PartMutationService::updateInstalledAt(db,100,1,"2025-01-03",&error):PartMutationService::ensureDeliveryDateIfMissing(db,1,"2025-01-03",&error);
    if(operation=="unassigned-delivery") {
        QVERIFY2(ok,qPrintable(error));QVERIFY(q.exec("SELECT order_number,order_uid,delivery_date FROM work_order_parts WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);QCOMPARE(q.value(1).toString(),QString(""));QCOMPARE(q.value(2).toString(),QString("2025-01-03"));q.finish();
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toInt(),1);q.finish();
    } else {QVERIFY2(!ok,"A part with conflicting UID/internal parent must not be modified");QVERIFY(!error.isEmpty());QCOMPARE(snapshot(),before);}
    QVERIFY(q.exec("SELECT count(*) FROM work_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);q.finish();
    QVERIFY(q.exec("PRAGMA foreign_key_check"));QVERIFY(!q.next());
}

void ReportsOrdersTests::childMutationRegistryIntent_data()
{
    QTest::addColumn<QString>("operation");QTest::addColumn<bool>("rejectIntent");
    for(const auto& operation:QStringList{"part-insert","part-update","part-remove","service-insert","service-update","service-remove","installed","delivery"})QTest::newRow(qPrintable(operation))<<operation<<false;
    for(const auto& operation:QStringList{"part-insert","installed","delivery"})QTest::newRow(qPrintable(operation+"-ignored-intent"))<<operation<<true;
}

void ReportsOrdersTests::childMutationRegistryIntent()
{
    QFETCH(QString,operation);QFETCH(bool,rejectIntent);
    QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Child-Intent-",root.path()));
    QString error;qlonglong pendingRevision=0;
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('projection-master','Master','master')"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'projection-owner',7,2025,'2025-01-02','open','','','Car','Client','projection-master')"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,qty,price,amount,purchase_price,purchase_amount) VALUES(1,100,'projection-owner','2025-01-02','Original part',1,0,0,0,0)"));
        QVERIFY(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,title,qty,price,amount) VALUES(1,100,'projection-owner','Original service',1,0,0)"));
        {TransactionContext tx(db,"ChildIntentBaseline");QVERIFY(tx.begin("test",&error));QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));}
        QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
        QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
        QVERIFY(r.exec("CREATE TRIGGER reject_child_publish BEFORE UPDATE ON order_registry BEGIN SELECT RAISE(ABORT,'injected child publication failure'); END"));
        if(rejectIntent)QVERIFY(q.exec("CREATE TRIGGER ignore_child_intent BEFORE UPDATE OF registry_revision ON workspace_order_state BEGIN SELECT RAISE(IGNORE); END"));
        auto snapshot=[&] {
            QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
            for(const auto& table:QStringList{"work_orders","work_order_parts","work_order_services","service_masters","workspace_order_state","order_registry","sqlite_sequence"}) {
                QSqlQuery read(db);if(!read.exec("SELECT * FROM "+table+" ORDER BY 1"))return QByteArray("SQL_ERROR");
                stream<<table;while(read.next())for(int i=0;i<read.record().count();++i)stream<<read.value(i);
                if(read.lastError().isValid())return QByteArray("SQL_ERROR");
            }
            return bytes;
        };
        const auto before=snapshot();QVERIFY(before!="SQL_ERROR");
        auto mutate=[&] {
            if(operation.startsWith("part-")) {
                if(operation=="part-remove")return PartRepository::remove(db,100,1,&error);
                auto rows=PartRepository::listByOrderUid(db,"projection-owner");if(rows.size()!=1)return false;
                auto row=rows.first();row.name=operation=="part-insert"?"New part":"Updated part";
                int id=0;return operation=="part-insert"?PartRepository::insert(db,100,row,&id,&error):PartRepository::update(db,100,1,row,&error);
            }
            if(operation.startsWith("service-")) {
                if(operation=="service-remove")return ServiceRepository::remove(db,100,1,&error);
                WorkOrderServiceRow row;row.title="Updated service";row.masterId="projection-master";row.masterName="Master";
                int id=0;return operation=="service-insert"?ServiceRepository::insert(db,100,row,&id,&error):ServiceRepository::update(db,100,1,row,&error);
            }
            return operation=="installed"?PartMutationService::updateInstalledAt(db,100,1,"2025-01-03",&error):PartMutationService::ensureDeliveryDateIfMissing(db,1,"2025-01-03",&error);
        };
        TransactionContext owner(db,"ChildIntentMutation");QVERIFY(owner.begin("test",&error));
        if(rejectIntent) {
            error.clear();QVERIFY2(!mutate(),"An ignored durable intent must reject the child write");QVERIFY(!error.isEmpty());QCOMPARE(snapshot(),before);
            QVERIFY(q.exec("DROP TRIGGER ignore_child_intent"));
        }
        QVERIFY2(mutate(),qPrintable(error));
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());pendingRevision=q.value(0).toLongLong();QVERIFY2(pendingRevision>q.value(1).toLongLong(),"Child changes must durably queue publication in their own transaction");q.finish();
        QVERIFY2(owner.commit("test",&error),qPrintable(error));
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),pendingRevision);QVERIFY(q.value(0).toLongLong()>q.value(1).toLongLong());q.finish();
        QVERIFY(r.exec("SELECT count(*),max(name) FROM part_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);QCOMPARE(r.value(1).toString(),QString("Original part"));r.finish();
    }
    Database::close();QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));QVERIFY(ensureRegistrySchemaFile());
    QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));QVERIFY(r.exec("DROP TRIGGER reject_child_publish"));
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    auto db=Database::db();QSqlQuery q(db);QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),pendingRevision);QCOMPARE(q.value(1).toLongLong(),pendingRevision);q.finish();
    QVERIFY(r.exec("SELECT count(*) FROM part_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),operation=="part-insert"?2:operation=="part-remove"?0:1);r.finish();
    if(operation=="part-update") {QVERIFY(r.exec("SELECT name FROM part_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Updated part"));r.finish();}
    if(operation=="delivery") {QVERIFY(r.exec("SELECT delivery_date FROM part_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("2025-01-03"));r.finish();}
    if(operation=="installed") {QVERIFY(q.exec("SELECT installed_at FROM work_order_parts WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("2025-01-03"));q.finish();}
    QVERIFY(q.exec("PRAGMA foreign_key_check"));QVERIFY(!q.next());q.finish();
    if(operation=="installed" && !rejectIntent) {
        QVERIFY(r.exec("CREATE TABLE child_publication_count(value INTEGER NOT NULL)"));QVERIFY(r.exec("INSERT INTO child_publication_count VALUES(0)"));
        QVERIFY(r.exec("CREATE TRIGGER count_child_publication AFTER UPDATE ON order_registry BEGIN UPDATE child_publication_count SET value=value+1; END"));
        TransactionContext batch(db,"CoalescedChildIntent");QVERIFY(batch.begin("test",&error));
        QVERIFY2(PartMutationService::updateInstalledAt(db,100,1,"2025-01-04",&error),qPrintable(error));
        QVERIFY2(PartMutationService::updateInstalledAt(db,100,1,"2025-01-05",&error),qPrintable(error));
        QVERIFY(r.exec("SELECT value FROM child_publication_count"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),0);r.finish();
        QVERIFY2(batch.commit("test",&error),qPrintable(error));
        QVERIFY(r.exec("SELECT value FROM child_publication_count"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);r.finish();
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),pendingRevision+2);QCOMPARE(q.value(1).toLongLong(),pendingRevision+2);q.finish();
    }
}

void ReportsOrdersTests::workspaceRegistryIntentDurability()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {
        Database::close(); setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Intent-",root.path()));
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error)); QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('intent-master','Master','master')"));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'durable-owner',7,2025,'2025-01-02','open','','','Original car','Client','intent-master')"),qPrintable(q.lastError().text()));
        {
            TransactionContext tx(db,"RegistryIntentRollback"); QVERIFY(tx.begin("test",&error));
            QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));
            QVERIFY2(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"),qPrintable(q.lastError().text()));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toLongLong(),1); QCOMPARE(q.value(1).toLongLong(),0);q.finish();
            QVERIFY(tx.rollback(QString(),&error));
            QVERIFY(q.exec("SELECT count(*) FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
        }
        {
            TransactionContext tx(db,"RegistryIntentCommit");QVERIFY(tx.begin("test",&error));
            QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));
        }
        QVERIFY(ensureRegistrySchemaFile());QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
        QVERIFY(r.exec("CREATE TRIGGER reject_intent_publish BEFORE INSERT ON order_registry BEGIN SELECT RAISE(ABORT,'injected publication failure'); END"));
        error.clear();QVERIFY(!Database::refreshOrderRegistry(100,&error));QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),1);QCOMPARE(q.value(1).toLongLong(),0);q.finish();
        QVERIFY(r.exec("DROP TRIGGER reject_intent_publish"));
        QVERIFY(q.exec("CREATE TRIGGER ignore_publication_ack BEFORE UPDATE OF published_registry_revision ON workspace_order_state BEGIN SELECT RAISE(IGNORE); END"));
        error.clear();QVERIFY(!Database::refreshOrderRegistry(100,&error));QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),1);QCOMPARE(q.value(1).toLongLong(),0);q.finish();
        QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),0);r.finish();
        QVERIFY(q.exec("DROP TRIGGER ignore_publication_ack"));
        QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),1);QCOMPARE(q.value(1).toLongLong(),1);q.finish();
        QVERIFY(q.exec("CREATE TRIGGER ignore_registry_intent BEFORE UPDATE OF registry_revision ON workspace_order_state BEGIN SELECT RAISE(IGNORE); END"));
        {
            TransactionContext rejected(db,"RegistryIntentIgnored");QVERIFY(rejected.begin("test",&error));
            QVERIFY(q.exec("UPDATE work_orders SET car='Must roll back' WHERE number=100"));
            error.clear();QVERIFY(!Database::finalizeOrderWorkspaceInTransaction(db,100,&error));QVERIFY(!error.isEmpty());
            QVERIFY(rejected.rollback(QString(),&error));
        }
        QVERIFY(q.exec("SELECT car FROM work_orders WHERE number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Original car"));q.finish();
        QVERIFY(q.exec("DROP TRIGGER ignore_registry_intent"));
        TransactionContext tx(db,"RegistryIntentNextRevision");QVERIFY(tx.begin("test",&error));
        QVERIFY(q.exec("UPDATE work_orders SET car='Committed pending car' WHERE number=100"));
        QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));
    }
    Database::close();QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db();QSqlQuery q(db);
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),2);QCOMPARE(q.value(1).toLongLong(),1);q.finish();
        QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
        QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),2);QCOMPARE(q.value(1).toLongLong(),2);q.finish();
        QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));QVERIFY(r.exec("SELECT car FROM order_registry WHERE order_uid='durable-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),QString("Committed pending car"));
    }
}

void ReportsOrdersTests::workspaceRegistryIntentProcess_data()
{
    QTest::addColumn<QString>("phase");
    QTest::newRow("before-commit") << QString("before");
    QTest::newRow("after-commit-before-publication") << QString("after");
    QTest::newRow("external-commit-local-ack-failure") << QString("after-external");
}

void ReportsOrdersTests::workspaceRegistryIntentProcess()
{
    QFETCH(QString,phase);
    const bool committed=phase!=QString("before"),afterExternal=phase==QString("after-external");
    QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Registry-Crash-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('intent-master','Master','master')"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'durable-owner',7,2025,'2025-01-02','open','','','Original car','Client','intent-master')"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(100,'durable-owner','2025-01-02','First captured part',1,10,10),(100,'durable-owner','2025-01-02','Second captured part',1,20,20)"));
        TransactionContext tx(db,"RegistryCrashBaseline");QVERIFY(tx.begin("test",&error));
        QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));QVERIFY(tx.commit("test",&error));
        QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
    }
    Database::close();
    QProcess child;auto env=QProcessEnvironment::systemEnvironment();
    env.insert("CS_SAFETY_FIXTURE_APP_NAME",QCoreApplication::applicationName());
    env.insert("CS_SAFETY_FIXTURE_ORGANIZATION",QCoreApplication::organizationName());env.insert("CS_SAFETY_FIXTURE_DOMAIN",QCoreApplication::organizationDomain());
    child.setProcessEnvironment(env);child.start(QCoreApplication::applicationFilePath(),{"--registry-intent-crash-fixture",phase});
    QVERIFY(child.waitForStarted(5000));QVERIFY(child.waitForFinished(15000));QCOMPARE(child.exitCode(),0);
    const auto diagnostics=QString::fromUtf8(child.readAllStandardError());
    if(afterExternal) {
        QCOMPARE(diagnostics.count("QCRITICAL|"),1);
        QCOMPARE(diagnostics.count("commit_failed_rollback"),1);
        QVERIFY(diagnostics.contains("FOREIGN KEY constraint failed"));QVERIFY(diagnostics.contains("rollbackOk=true"));
    } else QVERIFY(!diagnostics.contains("QCRITICAL|"));
    QVERIFY(!diagnostics.contains("QFATAL|"));
    QVERIFY(!diagnostics.contains("DB_CONNECTION phase="));
    QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QSqlQuery q(db);
    QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),committed?2:1);QCOMPARE(q.value(1).toLongLong(),1);q.finish();
    QVERIFY(ensureRegistrySchemaFile());QSqlQuery r(QSqlDatabase::database(dbRegistryConnectionName(),false));
    QVERIFY(r.exec("SELECT car FROM order_registry WHERE order_uid='durable-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),afterExternal?QString("Process committed car"):QString("Original car"));r.finish();
    if(afterExternal) {
        QVERIFY(q.exec("DROP TRIGGER fail_local_registry_ack"));
        QVERIFY(q.exec("SELECT count(*) FROM intent_ack_failure"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
    }
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    QVERIFY(r.exec("SELECT car FROM order_registry WHERE order_uid='durable-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toString(),committed?QString("Process committed car"):QString("Original car"));r.finish();
    QVERIFY(q.exec("SELECT registry_revision,published_registry_revision FROM workspace_order_state WHERE order_number=100"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),committed?2:1);QCOMPARE(q.value(1).toLongLong(),committed?2:1);q.finish();
    QVERIFY2(Database::replayPendingOrderRegistryRefresh(&error),qPrintable(error));
    QVERIFY(r.exec("SELECT count(*),sum(CASE WHEN name='Process committed part' THEN 1 ELSE 0 END) FROM part_registry WHERE order_number=100"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),2);QCOMPARE(r.value(1).toInt(),committed?1:0);r.finish();
    QVERIFY(r.exec("SELECT count(*) FROM order_registry WHERE order_uid='durable-owner'"));QVERIFY(r.next());QCOMPARE(r.value(0).toInt(),1);
}

void ReportsOrdersTests::externalRegistryRefreshAtomic_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"uid","year","number","annual-owner","source-year","part-owner","part-delete","part-insert","registry-update","duplicate-uid","uncommitted","part-ignore","registry-ignore","part-delete-ignore"}) QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::externalRegistryRefreshAtomic()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {
        Database::close(); ActivityAuditService::clearCurrentActor(); qApp->setProperty("csDisableOrderSaveBackup",false);
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-External-",root.path())); qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('atomic-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"atomic-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('atomic-master','Original master','master')"));
    QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,master_id,plate,plate_norm,car,client) VALUES(100,'external-owner',7,2025,'2025-01-02','open','atomic-master','','','Current car','Test client')"));
    QVERIFY(q.exec("INSERT INTO workspace_order_state(order_number,order_uid,origin) VALUES(100,'external-owner','native')"));
    QVERIFY(q.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(100,'external-owner',7,2025)"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(order_number,order_uid,ordered_at,name,qty,price,amount) VALUES(100,'external-owner','2025-01-02','First current part',1,10,10),(100,'external-owner','2025-01-02','Second current part',1,20,20)"));
    QVERIFY(ensureRegistrySchemaFile()); QVERIFY(ensurePartRegistrySchemaFile());
    auto registry=QSqlDatabase::database(dbRegistryConnectionName()); QSqlQuery r(registry);
    QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year,car) VALUES(100,'external-owner',7,2025,'Previously published')"));
    QVERIFY(r.prepare("INSERT INTO part_registry(entry_key,order_number,part_ref,storage_kind,storage_path,name,order_uid,part_uid) VALUES('previous-part',100,999,'active',?,'Previously published part','external-owner','previous-part-uid')"));
    r.addBindValue(AppRuntime::activeDbFilePath()); QVERIFY2(r.exec(),qPrintable(r.lastError().text()));
    if (fault=="uid") QVERIFY(r.exec("UPDATE order_registry SET order_uid='foreign-owner' WHERE order_number=100"));
    if (fault=="year") QVERIFY(r.exec("UPDATE order_registry SET order_year=2024 WHERE order_number=100"));
    if (fault=="number") QVERIFY(r.exec("UPDATE order_registry SET visible_order_number=8 WHERE order_number=100"));
    if (fault=="annual-owner") QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(101,'foreign-owner',7,2025)"));
    if (fault=="source-year") QVERIFY(q.exec("UPDATE work_orders SET order_year=0,opened_at='unresolved' WHERE number=100"));
    if (fault=="part-owner") {
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,master_id,plate,plate_norm,car,client) VALUES(101,'foreign-owner',8,2025,'2025-01-02','open','atomic-master','','','Other car','Other client')"));
        QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='foreign-owner' WHERE name='Second current part'"));
    }
    if (fault=="part-delete") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE DELETE ON part_registry BEGIN SELECT RAISE(ABORT,'injected part delete failure'); END"));
    if (fault=="part-insert") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE INSERT ON part_registry WHEN NEW.name='Second current part' BEGIN SELECT RAISE(ABORT,'injected second part failure'); END"));
    if (fault=="registry-update") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE UPDATE ON order_registry BEGIN SELECT RAISE(ABORT,'injected registry update failure'); END"));
    if (fault=="part-ignore") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE INSERT ON part_registry WHEN NEW.name='Second current part' BEGIN SELECT RAISE(IGNORE); END"));
    if (fault=="registry-ignore") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE UPDATE ON order_registry BEGIN SELECT RAISE(IGNORE); END"));
    if (fault=="part-delete-ignore") QVERIFY(r.exec("CREATE TRIGGER reject_registry BEFORE DELETE ON part_registry BEGIN SELECT RAISE(IGNORE); END"));
    if (fault=="duplicate-uid") {
        QVERIFY(r.exec("DROP INDEX idx_registry_order_uid_unique"));
        QVERIFY(r.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(101,'external-owner',8,2025)"));
    }
    auto snapshot=[&] {
        QStringList values;
        for (const auto& table:QStringList{"order_registry","part_registry"}) {
            QSqlQuery read(registry);
            if (!read.exec("SELECT * FROM "+table+" ORDER BY 1")) {values<<"SQL_ERROR";continue;}
            while(read.next()) for(int i=0;i<read.record().count();++i) values<<read.value(i).toString();
        }
        return values;
    };
    const auto before=snapshot();
    TransactionContext outer(db,"RegistryUncommittedTest");
    if (fault=="uncommitted") {QVERIFY(outer.begin("test transaction",&error)); QVERIFY(q.exec("UPDATE work_orders SET car='Uncommitted car' WHERE number=100"));}
    error.clear(); const bool ok=Database::refreshOrderRegistry(100,&error);
    if (outer.isActive()) {QString rollbackError; QVERIFY2(outer.rollback(QString(),&rollbackError),qPrintable(rollbackError));}
    QVERIFY2(!ok,"Conflicting identity or failed publication must not be acknowledged");
    QVERIFY(!error.isEmpty()); QCOMPARE(snapshot(),before);
    // Explicitly resolve only the injected ambiguity/fault, then retry.
    QVERIFY(r.exec("DROP TRIGGER IF EXISTS reject_registry"));
    QVERIFY(r.exec("DELETE FROM order_registry WHERE order_number=101"));
    QVERIFY(r.exec("UPDATE order_registry SET order_uid='external-owner',visible_order_number=7,order_year=2025 WHERE order_number=100"));
    QVERIFY(q.exec("UPDATE work_orders SET order_year=2025,opened_at='2025-01-02' WHERE number=100"));
    QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='external-owner' WHERE order_number=100"));
    QVERIFY2(Database::refreshOrderRegistry(100,&error),qPrintable(error));
    QVERIFY(r.exec("SELECT order_uid,visible_order_number,order_year,car FROM order_registry WHERE order_number=100")); QVERIFY(r.next());
    QCOMPARE(r.value(0).toString(),QString("external-owner")); QCOMPARE(r.value(1).toInt(),7); QCOMPARE(r.value(2).toInt(),2025); QCOMPARE(r.value(3).toString(),QString("Current car")); r.finish();
    QVERIFY(r.exec("SELECT count(*),sum(CASE WHEN name='Previously published part' THEN 1 ELSE 0 END) FROM part_registry WHERE order_number=100")); QVERIFY(r.next());
    QCOMPARE(r.value(0).toInt(),2); QCOMPARE(r.value(1).toInt(),0); r.finish();
}

void ReportsOrdersTests::workspaceRoutingIdentityValidation_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"missing-year","missing-uid","missing-number","state-uid","registry-uid","registry-year","registry-number","foreign-cache","unrelated-state","unrelated-registry","published-state","published-registry"}) QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::workspaceRoutingIdentityValidation()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {
        Database::close(); ActivityAuditService::clearCurrentActor(); qApp->setProperty("csDisableOrderSaveBackup",false);
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-Routing-Identity"); qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('atomic-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"atomic-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('atomic-master','Original master','master')"));
    QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,master_id,plate,plate_norm,car,client) VALUES(100,'routing-owner',7,2025,'2025-01-02','open','atomic-master','','','Test car','Test client')"));
    QVERIFY(q.exec("INSERT INTO workspace_order_state(order_number,order_uid,origin) VALUES(100,'routing-owner','native')"));
    QVERIFY(q.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(100,'routing-owner',7,2025)"));
    if (fault=="missing-year") QVERIFY(q.exec("UPDATE work_orders SET order_year=0,opened_at='unresolved' WHERE number=100"));
    if (fault=="missing-uid") QVERIFY(q.exec("UPDATE work_orders SET order_uid='' WHERE number=100"));
    if (fault=="missing-number") QVERIFY(q.exec("UPDATE work_orders SET order_number=0 WHERE number=100"));
    if (fault=="state-uid") QVERIFY(q.exec("UPDATE workspace_order_state SET order_uid='another-owner' WHERE order_number=100"));
    if (fault=="registry-uid") QVERIFY(q.exec("UPDATE order_registry SET order_uid='another-owner' WHERE order_number=100"));
    if (fault=="registry-year") QVERIFY(q.exec("UPDATE order_registry SET order_year=2024 WHERE order_number=100"));
    if (fault=="registry-number") QVERIFY(q.exec("UPDATE order_registry SET visible_order_number=8 WHERE order_number=100"));
    if (fault=="foreign-cache") {
        QVERIFY(q.exec("DELETE FROM workspace_order_state WHERE order_number=100"));
        QVERIFY(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client) VALUES(100,'another-owner',42,2024,'2024-01-02','closed','','','Other car','Other client')"));
    }
    if (fault=="unrelated-state" || fault=="published-state") QVERIFY(q.exec("INSERT INTO workspace_order_state(order_number,order_uid,origin) VALUES(101,'','native')"));
    if (fault=="unrelated-registry" || fault=="published-registry") QVERIFY(q.exec("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year) VALUES(101,'',33,2024)"));
    if (fault.startsWith("published-")) {
        QVERIFY2(Database::finalizeNewOrderWorkspaceState(100,&error),qPrintable(error));
        QVERIFY(q.exec(fault=="published-state" ? "SELECT order_uid FROM workspace_order_state WHERE order_number=101" : "SELECT order_uid FROM order_registry WHERE order_number=101"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("")); q.finish();
        return;
    }
    auto snapshot = [&] {
        QStringList values;
        for (const auto& table : QStringList{"work_orders","workspace_order_state","order_registry"}) {
            QSqlQuery read(db);
            if (!read.exec("SELECT * FROM "+table+" ORDER BY 1")) { values << "SQL_ERROR"; continue; }
            while (read.next()) for (int i=0;i<read.record().count();++i) values << read.value(i).toString();
        }
        return values;
    };
    const auto before=snapshot();
    TransactionContext tx(db,"RoutingIdentityTest"); QVERIFY2(tx.begin("test transaction",&error),qPrintable(error));
    QString validationError;
    const bool ok=Database::finalizeOrderWorkspaceInTransaction(db,100,&validationError);
    if (fault=="foreign-cache" || fault=="unrelated-state" || fault=="unrelated-registry") {
        QVERIFY2(tx.commit("test commit",&error),qPrintable(error));
        QVERIFY2(ok,qPrintable(validationError));
        QVERIFY(q.exec("SELECT origin,order_uid FROM workspace_order_state WHERE order_number=100")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("native")); QCOMPARE(q.value(1).toString(),QString("routing-owner")); q.finish();
        if (fault=="foreign-cache") {
            QVERIFY(q.exec("SELECT order_uid FROM router_cache_work_orders WHERE number=100")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("another-owner")); q.finish();
        } else {
            QVERIFY(q.exec(fault=="unrelated-state" ? "SELECT order_uid FROM workspace_order_state WHERE order_number=101" : "SELECT order_uid FROM order_registry WHERE order_number=101")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("")); q.finish();
        }
        return;
    }
    QVERIFY2(tx.rollback(QString(),&error),qPrintable(error));
    QVERIFY2(!ok,"Ambiguous routing identity must be refused, never silently normalized");
    QVERIFY(!validationError.isEmpty()); QCOMPARE(snapshot(),before);
    // Explicitly repair only the injected source ambiguity, then retry unchanged identity.
    QVERIFY(q.exec("UPDATE work_orders SET order_uid='routing-owner',order_number=7,order_year=2025,opened_at='2025-01-02' WHERE number=100"));
    QVERIFY(q.exec("UPDATE workspace_order_state SET order_uid='routing-owner' WHERE order_number=100"));
    QVERIFY(q.exec("UPDATE order_registry SET order_uid='routing-owner',visible_order_number=7,order_year=2025 WHERE order_number=100"));
    TransactionContext retry(db,"RoutingIdentityRetry"); QVERIFY(retry.begin("test retry",&error));
    QVERIFY2(Database::finalizeOrderWorkspaceInTransaction(db,100,&error),qPrintable(error));
    QVERIFY2(retry.commit("test retry commit",&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=100")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(),QString("routing-owner")); QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025); q.finish();
}

void ReportsOrdersTests::workspaceMandatoryLinksAtomic_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& fault : QStringList{"crm", "close", "reopen", "state", "registry", "state-ignore", "registry-ignore", "header-ignore"}) QTest::newRow(qPrintable(fault)) << fault;
}

void ReportsOrdersTests::workspaceMandatoryLinksAtomic()
{
    QFETCH(QString, fault);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {
        Database::close(); ActivityAuditService::clearCurrentActor(); qApp->setProperty("csDisableOrderSaveBackup",false);
        setIsolatedTestRoot(oldRoot); QCoreApplication::setApplicationName(oldName); QStandardPaths::setTestModeEnabled(oldTest);
    });
    QVERIFY(setIsolatedTestRoot(root.path())); QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("Safety-Workspace-Atomic"); qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error; QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db(); QVERIFY(Database::ensureSchema()); QVERIFY(rebuildRouterViews(db)); QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error)); QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('atomic-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"atomic-owner","Owner","owner","test"});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('atomic-master','Original master','master')"));
    QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
    OrderWorkspaceInput input; input.car="Test car"; input.client="Test client"; input.plate="AA1234AA";
    input.masterId="atomic-master"; input.discountMode="uah"; input.status="open";
    int number=0; QString originalStatus;
    const bool stateFault=fault.startsWith("state"),registryFault=fault.startsWith("registry");
    const bool creating = fault=="crm" || stateFault || registryFault;
    if (creating) {
        const QString table = fault=="crm" ? "crm_clients" : (stateFault ? "workspace_order_state" : "order_registry");
        const QString raised=fault.endsWith("-ignore") ? "RAISE(IGNORE)" : "RAISE(ABORT,'injected mandatory failure')";
        QVERIFY2(q.exec(QString("CREATE TRIGGER reject_mandatory BEFORE INSERT ON %1 BEGIN SELECT %2; END").arg(table,raised)),qPrintable(q.lastError().text()));
    } else {
        input.status=fault=="reopen" ? "closed" : "open";
        QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error)); input.number=number;
        originalStatus=input.status;
        QVERIFY(q.prepare("INSERT INTO work_order_services(order_number,order_uid,title,qty,price,amount) SELECT number,order_uid,'Test service',1,10,10 FROM work_orders WHERE number=?"));
        q.addBindValue(number); QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
        const auto serviceId=q.lastInsertId();
        QVERIFY(q.prepare("INSERT INTO service_masters(service_id,master_id,share_percent,master_name_closed) VALUES(?,'atomic-master',1,?)"));
        q.addBindValue(serviceId); q.addBindValue(fault=="reopen" ? QString("Frozen master") : QString(""));
        QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
        input.status=fault=="reopen" ? "open" : "closed";
        if (fault=="header-ignore") QVERIFY(q.exec("CREATE TRIGGER reject_mandatory BEFORE UPDATE ON work_orders BEGIN SELECT RAISE(IGNORE); END"));
        else QVERIFY(q.exec("CREATE TRIGGER reject_mandatory BEFORE UPDATE ON service_masters BEGIN SELECT RAISE(ABORT,'injected snapshot failure'); END"));
    }
    QVERIFY(q.exec("SELECT count(*) FROM activity_audit_events")); QVERIFY(q.next()); const int auditBefore=q.value(0).toInt(); q.finish();
    const QString expectedFailure = fault=="header-ignore" ? QString("Order header write did not affect the expected record") : (fault=="crm" ? QString("injected mandatory failure") : (stateFault
        ? QString("Failed to upsert workspace_order_state") : (registryFault
        ? QString("Failed to upsert local order_registry mirror") : QString("injected snapshot failure"))));
    const QString expectedSqlSuffix = (stateFault || registryFault || fault=="header-ignore") ? QString("") : QString(" Unable to fetch row");
    // Assert these two exact diagnostics from this deliberately failed transaction.
    // Missing expected messages fail QtTest; unrelated critical errors remain visible.
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
        "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
        "^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "
        + QRegularExpression::escape(expectedFailure + expectedSqlSuffix) + "$"));
    error.clear();
    QVERIFY2(!OrderWorkspaceService::saveEditableOrder(input,&number,&error),"Mandatory CRM/snapshot failure must reject the whole save");
    QCOMPARE(error,expectedFailure + expectedSqlSuffix);
    QVERIFY(q.exec("SELECT count(*) FROM activity_audit_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),auditBefore); q.finish();
    if (creating) {
        QVERIFY(q.exec("SELECT count(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
        // An aborted first allocation may roll back the creation of ledger tables too.
        QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='order_creation_reservations'")); QVERIFY(q.next());
        const bool ledgerExists=q.value(0).toInt()!=0; q.finish();
        if (ledgerExists) { QVERIFY(q.exec("SELECT count(*) FROM order_creation_reservations")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish(); }
    } else {
        QVERIFY(q.prepare("SELECT status FROM work_orders WHERE number=?")); q.addBindValue(number); QVERIFY(q.exec()); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),originalStatus); q.finish();
        QVERIFY(q.exec("SELECT master_name_closed FROM service_masters")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),fault=="reopen" ? QString("Frozen master") : QString("")); q.finish();
    }
    QVERIFY(q.exec("DROP TRIGGER reject_mandatory"));
    QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
    QVERIFY(q.prepare("SELECT client_uid,vehicle_uid,order_number FROM work_orders WHERE number=?")); q.addBindValue(number);
    QVERIFY(q.exec()); QVERIFY(q.next()); QVERIFY(!q.value(0).toString().isEmpty()); QVERIFY(!q.value(1).toString().isEmpty());
    if (creating) QCOMPARE(q.value(2).toInt(),1); q.finish();
    QVERIFY(q.prepare("SELECT s.order_uid,r.order_uid FROM workspace_order_state s JOIN order_registry r ON r.order_number=s.order_number WHERE s.order_number=?"));
    q.addBindValue(number); QVERIFY(q.exec()); QVERIFY(q.next()); QVERIFY(!q.value(0).toString().isEmpty()); QCOMPARE(q.value(0).toString(),q.value(1).toString()); q.finish();
    if (!creating) {
        QVERIFY(q.exec("SELECT master_name_closed FROM service_masters")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),fault=="reopen" ? QString("") : QString("Original master")); q.finish();
        if (fault=="close") {
            QVERIFY(q.exec("UPDATE masters SET name='Renamed master' WHERE id='atomic-master'"));
            QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT master_name_closed FROM service_masters")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("Original master")); q.finish();
        }
    }
}

void ReportsOrdersTests::existingAnnualIdentitySurvivesEdit()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceTests");
    QCoreApplication::setApplicationName("Safety-AnnualIdentity-"+QUuid::createUuid().toString(QUuid::Id128));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('discount-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"discount-owner","Owner","owner","test"});
    const auto cleanup=qScopeGuard([]{ActivityAuditService::clearCurrentActor();qApp->setProperty("csDisableOrderSaveBackup",false);});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('m-discount','Master','master')"));
    OrderWorkspaceInput input;input.status="open";input.car="Audi A6";input.plate="AA 1234 AA";
    input.masterId="m-discount";input.client="Test";input.openedAt=QDateTime::currentDateTime();
    input.number=100; input.discountMode="uah";
    // Seed a historical canonical order before any confirmation. Mutating only
    // a newly confirmed header would conflict with its durable registry/ledger.
    QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) VALUES(100,'historical-annual-owner',7,2025,'2025-01-02','open','AA1234AA','AA1234AA','Audi A6','Test','m-discount')"));
    int number=0; QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
    QCOMPARE(number,100);
    QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); const QString uid=q.value(0).toString(); q.finish(); QVERIFY(!uid.isEmpty());
    input.visibleNumber=999; input.openedAt=QDateTime(QDate(2026,1,2),QTime(10,0));
    input.client="Edited client";
    QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year,client FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid);
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025);
    QCOMPARE(q.value(3).toString(),QString("Edited client")); q.finish();
    // Opening an existing profile must preserve its annual public identity.
    QVERIFY(Database::ensureSchema());
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid);
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025); q.finish();
    QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
    QVERIFY(ensureRouterCacheSchema(db));
    QVERIFY(Database::ensureSchema());
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM router_cache_work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid);
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025); q.finish();
    QVERIFY(ensureLocalRegistryMirrorSchema(db));
    QVERIFY(q.exec("INSERT OR REPLACE INTO order_registry(order_number,order_uid,visible_order_number,order_year) SELECT number,order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(ensureLocalRegistryMirrorSchema(db));
    QVERIFY(q.exec("SELECT visible_order_number,order_year FROM order_registry WHERE order_number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),7); QCOMPARE(q.value(1).toInt(),2025); q.finish();
    QVERIFY2(LegacyCanonicalAncillaryImportSupport::finalizeImportedAncillaryData(db,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid);
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025); q.finish();
    QVERIFY(q.exec("DELETE FROM work_orders WHERE number=100"));
    QVERIFY2(hydrateCachedOrderToActive(db,uid,100,&error),qPrintable(error));
    QVERIFY(q.exec("SELECT order_uid,order_number,order_year FROM work_orders WHERE number=100"));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),uid);
    QCOMPARE(q.value(1).toInt(),7); QCOMPARE(q.value(2).toInt(),2025); q.finish();
}

void ReportsOrdersTests::explicitDiscountPersistenceAndArchive()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceTests");
    QCoreApplication::setApplicationName("Safety-Discount-"+QUuid::createUuid().toString(QUuid::Id128));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    qApp->setProperty("csDisableOrderSaveBackup",true);
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
    QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));QSqlQuery q(db);
    QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('discount-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"discount-owner","Owner","owner","test"});
    const auto cleanup=qScopeGuard([]{ActivityAuditService::clearCurrentActor();qApp->setProperty("csDisableOrderSaveBackup",false);});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('m-discount','Master','master')"));
    OrderWorkspaceInput input;input.status="open";input.car="Audi A6";input.plate="AA 1234 AA";
    input.masterId="m-discount";input.client="Test";input.openedAt=QDateTime::currentDateTime();
    input.discount=50;input.discountMode="percent";input.worksSum=1000;
    int number=0;QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
    input.number=number;
    OrderSnapshotService::OrderSnapshot snapshot;
    QVERIFY2(OrderSnapshotService::loadByNumber(db,number,&snapshot,&error),qPrintable(error));
    QCOMPARE(snapshot.discount,50.0);QCOMPARE(snapshot.discountMode,QString("percent"));
    {
        OrderDialog dialog(nullptr,number);
        auto* mode=dialog.findChild<QComboBox*>("orderDiscountMode");QVERIFY(mode);
        QCOMPARE(mode->currentData().toString(),QString("percent"));
        QCOMPARE(dialog.serializeDraftState()["discount_mode"].toString(),QString("percent"));
        mode->setCurrentIndex(mode->findData("uah"));
        QCOMPARE(dialog.serializeDraftState()["discount_mode"].toString(),QString("uah"));
    }
    input.discount=101;QVERIFY(!OrderWorkspaceService::saveEditableOrder(input,&number,&error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(OrderSnapshotService::loadByNumber(db,number,&snapshot,&error),qPrintable(error));
    QCOMPARE(snapshot.discount,50.0);QCOMPARE(snapshot.discountMode,QString("percent"));
    input.discount=50;input.discountMode="uah";
    QVERIFY2(OrderWorkspaceService::saveEditableOrder(input,&number,&error),qPrintable(error));
    QVERIFY2(rebuildRouterCacheFromActiveWorkspaceData(db,&error),qPrintable(error));
    QVERIFY(ensureRouterCacheSchema(db));
    q.prepare("SELECT order_uid,discount_mode FROM router_cache_work_orders WHERE number=?");q.addBindValue(number);
    QVERIFY(q.exec());QVERIFY(q.next());const auto uid=q.value(0).toString();QCOMPARE(q.value(1).toString(),QString("uah"));q.finish();
    q.prepare("DELETE FROM work_orders WHERE number=?");q.addBindValue(number);QVERIFY(q.exec());
    QVERIFY2(hydrateCachedOrderToActive(db,uid,number,&error),qPrintable(error));
    QVERIFY2(OrderSnapshotService::loadByNumber(db,number,&snapshot,&error),qPrintable(error));
    QCOMPARE(snapshot.discount,50.0);QCOMPARE(snapshot.discountMode,QString("uah"));
}

void ReportsOrdersTests::newOrderFromSpareStock()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceTests");
    QCoreApplication::setApplicationName("Safety-SpareOrder-"+QUuid::createUuid().toString(QUuid::Id128));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    QString runtimeError;QVERIFY2(Database::prepareRuntime(&runtimeError),qPrintable(runtimeError));
    QVERIFY2(Database::openPrepared(&runtimeError),qPrintable(runtimeError));
    auto db=Database::db();
    Database::setSetting("settings.booking.sync.enabled",false);
    Database::setSetting("settings.cloudsync.enabled",false);
    QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));
    QString error;QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
    QSqlQuery q(db);
    QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('stock-owner','Owner','owner',1)"));
    ActivityAuditService::setCurrentActor({"stock-owner","Owner","owner","test"});
    const auto clearActor=qScopeGuard([]{ActivityAuditService::clearCurrentActor();});
    QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('stock-master','Stock Master','master')"));
    WorkshopSpareOrder spare;spare.name="Gasket";spare.article="OEM-500";spare.brand="Volvo";spare.model="V50";
    spare.plate="AE2365BP";spare.vin="YV1MW000000000001";spare.masterId="stock-master";spare.supplier="Avtonova";
    spare.orderedOn=QDate::currentDate();spare.quantity=1;spare.unitCost=500;
    QVERIFY(Database::setSetting("settings.int.directories.invoices_dir",dir.filePath("invoices")));
    QFile invoice(dir.filePath("stock-invoice.pdf")); QVERIFY(invoice.open(QIODevice::WriteOnly));
    invoice.write("%PDF-1.4 stock invoice"); invoice.close();
    spare.invoicePath=invoice.fileName(); spare.invoiceName="stock-invoice.pdf"; spare.document="STOCK-500";
    QVERIFY2(WorkshopSpareStockService::addOrder(db,spare,nullptr,&error,QDate::currentDate()),qPrintable(error));
    for(int pass=0;pass<2;++pass) {
        spare=WorkshopSpareStockService::orders(db,&error).first();
        OrderDialog dialog;dialog.prefillFromSpareStock(spare,1);
        const auto state=dialog.serializeDraftState();QCOMPARE(PlateUtils::normalizePlate(state.value("plate").toString()),PlateUtils::normalizePlate(spare.plate));
        QCOMPARE(state.value("vin").toString(),spare.vin);QCOMPARE(state.value("car").toString(),QString("VOLVO V50"));
        QCOMPARE(state.value("masterId").toString(),spare.masterId);
        for(auto* spin:dialog.findChildren<QSpinBox*>()) if(spin->maximum()==2000000) spin->setValue(123456);
        dialog.show();dialog.openWorkPartsTab();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.workPartsPageForRecovery(),10000);
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().issued,1.0);
        QVERIFY(q.exec("SELECT part_id FROM workshop_spare_movements WHERE operation='issue'")); QVERIFY(q.next());
        QString invoiceNumber;
        QCOMPARE(OrderAttachmentService::partAttachmentPath(db,q.value(0).toInt(),nullptr,&invoiceNumber),spare.invoicePath);
        QCOMPARE(invoiceNumber,spare.document);
        q.finish();
        if(pass==0) {
            QTimer::singleShot(100,[]{if(auto* msg=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))msg->done(QMessageBox::No);});
            QMetaObject::invokeMethod(&dialog,"reject",Qt::DirectConnection);
            QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().issued,0.0);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(RuntimeJob::Family::WorkPartsDialog).status==RuntimeJob::Status::Succeeded,10000);
            QTest::qWait(200);QMetaObject::invokeMethod(&dialog,"accept",Qt::DirectConnection);
            QTRY_COMPARE_WITH_TIMEOUT(dialog.result(),int(QDialog::Accepted),10000);
            QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().issued,1.0);
        }
    }
    MainWindow window;
    auto* ordersButton=window.findChild<QPushButton*>("sb_btn_orders"); QVERIFY(ordersButton);
    QCOMPARE(ordersButton->minimumHeight(),33+qRound(3.0*window.logicalDpiY()/25.4));
    ordersButton->resize(240,ordersButton->minimumHeight());
    const QImage buttonImage=ordersButton->grab().toImage();
    const QColor emerald=buttonImage.pixelColor(buttonImage.width()/3,buttonImage.height()*3/4);
    QVERIFY(emerald.red()>emerald.blue() && emerald.green()>emerald.blue()); // Yellow glossy orders button.
    const QString captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
    if(!captures.isEmpty()) QVERIFY(ordersButton->grab().save(QDir(captures).filePath("orders-emerald-button.png")));
    ordersButton->group()->setExclusive(false);
    ordersButton->setChecked(false);
    QVERIFY(!ordersButton->isChecked());
    const QImage inactive=ordersButton->grab().toImage();
    // A rectangular inactive button paints its corner; the active pill leaves it clear.
    QVERIFY(inactive.pixelColor(3,3)!=buttonImage.pixelColor(3,3));
    QCOMPARE(ordersButton->height(),33+qRound(3.0*window.logicalDpiY()/25.4));
    if(!captures.isEmpty()) QVERIFY(ordersButton->grab().save(QDir(captures).filePath("orders-emerald-inactive.png")));
}

void ReportsOrdersTests::callerOwnedConnectionScope()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Provided-",root.path()));
    const QString unrelatedRuntime=AppRuntime::activeDbFilePath();
    QVERIFY(!QFileInfo::exists(unrelatedRuntime));
    {
        auto invalid=QSqlDatabase::addDatabase("UNAVAILABLE_SCOPE_TEST_DRIVER",currentConnectionName());
        QVERIFY(!invalid.isValid());QString invalidError;
        QVERIFY(!Database::ensureConnectionAlive(&invalidError));QVERIFY(!invalidError.isEmpty());QVERIFY(!QFileInfo::exists(unrelatedRuntime));
        invalid=QSqlDatabase();QSqlDatabase::removeDatabase(currentConnectionName());
    }
    auto db=QSqlDatabase::addDatabase("QSQLITE",currentConnectionName());
    const QString suppliedPath=root.filePath("provided.db");db.setDatabaseName(suppliedPath);QVERIFY(db.open());
    {QSqlQuery q(db);QVERIFY(q.exec("CREATE TABLE probe(value TEXT)"));QVERIFY(q.exec("INSERT INTO probe VALUES('caller data')"));}
    QVERIFY(SqliteConnectionUtils::countOpenConnectionsForPath(suppliedPath)>=1);
    QString error;auto supplied=Database::db(&error);QVERIFY2(supplied.isValid(),qPrintable(error));
    QCOMPARE(QFileInfo(supplied.databaseName()).absoluteFilePath(),QFileInfo(suppliedPath).absoluteFilePath());
    QVERIFY(!Database::prepareRuntime(&error));QVERIFY(!Database::openPrepared(&error));
    QVERIFY(!QFileInfo::exists(unrelatedRuntime));supplied=QSqlDatabase();db.close();
    QVERIFY(!Database::ensureConnectionAlive(&error));QVERIFY(!error.isEmpty());QVERIFY(!QFileInfo::exists(unrelatedRuntime));
    QVERIFY(db.open());QVERIFY(Database::ensureConnectionAlive(&error));
    QSqlQuery q(db);QVERIFY(q.exec("SELECT value FROM probe"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("caller data"));
}

void ReportsOrdersTests::reportsFilterUsesExplicitSource()
{
    QTemporaryDir root;QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"),oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto restore=qScopeGuard([&] {Database::close();ActivityAuditService::clearCurrentActor();setIsolatedTestRoot(oldRoot);QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Report-",root.path()));
    const QString path=root.filePath("explicit-report.db"),connection="explicit-report-source";
    auto db=QSqlDatabase::addDatabase("QSQLITE",connection);
    const auto cleanup=qScopeGuard([&] {db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(connection);});
    db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);
    ActivityAuditService::setCurrentActor({"test-owner","Owner","owner","test"});
        const QStringList setup = {
            "CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT,role TEXT,active INTEGER)",
            "INSERT INTO staff_accounts VALUES('test-owner','','owner',1)",
            "CREATE TABLE access_roles(id TEXT PRIMARY KEY,active INTEGER)",
            "INSERT INTO access_roles VALUES('owner',1)",
            "CREATE TABLE masters(id TEXT, name TEXT)",
            "INSERT INTO masters VALUES('m1','Master')",
            "CREATE TABLE router_work_orders(number INTEGER, order_number INTEGER, order_uid TEXT, opened_at TEXT, closed_at TEXT, plate TEXT, car TEXT, client TEXT, master_id TEXT, master_name_closed TEXT)",
            "INSERT INTO router_work_orders VALUES(101,9,'uid-a','2026-09-01','', 'AA1234AA','Audi Q7','Client','m1','')",
            "INSERT INTO router_work_orders VALUES(102,10,'uid-b','2026-09-02',NULL,'','Volvo V90','Client','m1','')",
            "INSERT INTO router_work_orders VALUES(103,11,'uid-c','2026-08-01','2026-09-03','','Skoda','Client','m1','Archived Master')",
            "INSERT INTO router_work_orders VALUES(104,12,'uid-d','2026-08-01',NULL,'','','','m1','')",
            "CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY, order_number INTEGER, order_uid TEXT, part_number TEXT, manufacturer TEXT, name TEXT, store TEXT, qty REAL, price REAL, amount REAL, purchase_price REAL, purchase_amount REAL)",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "INSERT INTO work_order_parts VALUES(1,101,'uid-a','OEM-001','Brand','Filter','Store',2,150,300,100,200)",
            "INSERT INTO work_order_parts VALUES(2,101,'uid-a','OEM-002','Brand','Oil','Store',1.5,80,NULL,60,NULL)",
            "INSERT INTO work_order_parts VALUES(3,101,'wrong-uid','OTHER','','Wrong part','Store',1,9999,9999,9999,9999)",
            "INSERT INTO work_order_parts VALUES(4,103,'','LEGACY','','Legacy part','Store',1,40,40,20,20)",
            "ALTER TABLE work_order_parts ADD COLUMN ordered_at TEXT DEFAULT '2026-09-01'",
            "CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY, invoice_number TEXT, invoice_date TEXT, file_path TEXT)",
        };
    for(const auto& sql:setup) QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
    for(const auto& sql:QStringList{"ALTER TABLE router_work_orders ADD COLUMN status TEXT DEFAULT 'open'","ALTER TABLE router_work_orders ADD COLUMN order_year INTEGER DEFAULT 2026","ALTER TABLE router_work_orders ADD COLUMN vin TEXT DEFAULT ''","ALTER TABLE router_work_orders ADD COLUMN works_sum REAL DEFAULT 0","ALTER TABLE router_work_orders ADD COLUMN parts_sum REAL DEFAULT 0","CREATE VIEW router_order_locator AS SELECT * FROM router_work_orders","CREATE TABLE work_order_services(order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)"}) QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
    const QString unrelatedRuntime=AppRuntime::activeDbFilePath();
    QVERIFY(!QSqlDatabase::contains(currentConnectionName()));QVERIFY(!QFileInfo::exists(unrelatedRuntime));
    QVERIFY(HistoryQueryService::buildHistorySql(QSqlDatabase(),{QDate(2026,9,1),QDate(2026,9,30),QString("AA 1"),true}).isEmpty());
    QVERIFY(!QSqlDatabase::contains(currentConnectionName()));
    const auto found=ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30","AA 1");
    QVERIFY2(!QSqlDatabase::contains(currentConnectionName()),"An explicit-source report must not create a default runtime connection");
    QVERIFY(!QFileInfo::exists(unrelatedRuntime));
    QCOMPARE(found.rows.size(),1);QCOMPARE(found.rows.first().internalOrderNumber,101);
}

void ReportsOrdersTests::orderListAndPrintData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("orders.db"));
    const QString connection = QStringLiteral("carservice_main");
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        const auto clearActor = qScopeGuard([] { ActivityAuditService::clearCurrentActor(); });
        ActivityAuditService::setCurrentActor({"test-owner", "Owner", "owner", "test"});
        QSqlQuery q(db);
        const QStringList setup = {
            "CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT,role TEXT,active INTEGER)",
            "INSERT INTO staff_accounts VALUES('test-owner','','owner',1)",
            "CREATE TABLE access_roles(id TEXT PRIMARY KEY,active INTEGER)",
            "INSERT INTO access_roles VALUES('owner',1)",
            "CREATE TABLE masters(id TEXT, name TEXT)",
            "INSERT INTO masters VALUES('m1','Master')",
            "CREATE TABLE router_work_orders(number INTEGER, order_number INTEGER, order_uid TEXT, opened_at TEXT, closed_at TEXT, plate TEXT, car TEXT, client TEXT, master_id TEXT, master_name_closed TEXT)",
            "INSERT INTO router_work_orders VALUES(101,9,'uid-a','2026-09-01','', 'AA1234AA','Audi Q7','Client','m1','')",
            "INSERT INTO router_work_orders VALUES(102,10,'uid-b','2026-09-02',NULL,'','Volvo V90','Client','m1','')",
            "INSERT INTO router_work_orders VALUES(103,11,'uid-c','2026-08-01','2026-09-03','','Skoda','Client','m1','Archived Master')",
            "INSERT INTO router_work_orders VALUES(104,12,'uid-d','2026-08-01',NULL,'','','','m1','')",
            "CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY, order_number INTEGER, order_uid TEXT, part_number TEXT, manufacturer TEXT, name TEXT, store TEXT, qty REAL, price REAL, amount REAL, purchase_price REAL, purchase_amount REAL)",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "INSERT INTO work_order_parts VALUES(1,101,'uid-a','OEM-001','Brand','Filter','Store',2,150,300,100,200)",
            "INSERT INTO work_order_parts VALUES(2,101,'uid-a','OEM-002','Brand','Oil','Store',1.5,80,NULL,60,NULL)",
            "INSERT INTO work_order_parts VALUES(3,101,'wrong-uid','OTHER','','Wrong part','Store',1,9999,9999,9999,9999)",
            "INSERT INTO work_order_parts VALUES(4,103,'','LEGACY','','Legacy part','Store',1,40,40,20,20)",
            "ALTER TABLE work_order_parts ADD COLUMN ordered_at TEXT DEFAULT '2026-09-01'",
            "CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY, invoice_number TEXT, invoice_date TEXT, file_path TEXT)",
            "INSERT INTO work_order_part_attachments VALUES(1,'INV-2026-01','31.08.26','C:/invoice.pdf')"
        };
        for (const auto& sql : setup) QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));

        QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN status TEXT DEFAULT 'open'"));
        const auto snapshot = ReportsDataLoader::buildOrdersSnapshot(path, "2026-09-01", "2026-09-30");
        QCOMPARE(snapshot.rows.size(), 3);
        QCOMPARE(snapshot.rows[0].internalOrderNumber, 103);
        QCOMPARE(snapshot.rows[0].master, QStringLiteral("Archived Master"));
        QCOMPARE(snapshot.rows[0].purchaseTotal, FormatUtils::money(20, 2));
        QCOMPARE(snapshot.rows[1].purchaseTotal, FormatUtils::money(0, 2));
        QCOMPARE(snapshot.rows[2].orderNumber, QStringLiteral("9"));
        QCOMPARE(snapshot.rows[2].purchaseTotal, FormatUtils::money(290, 2));
        QCOMPARE(snapshot.rows[2].saleTotal, FormatUtils::money(420, 2));
        QCOMPARE(snapshot.clientPaymentError, QStringLiteral("Журнал клієнтських оплат відсутній. Історичні надходження невідомі."));
        for(const auto& row:snapshot.rows)QCOMPARE(row.clientReceivedCents,qint64(0));
        QVERIFY(q.exec("CREATE TABLE client_payments(payment_uid TEXT PRIMARY KEY,order_uid TEXT,amount_cents INTEGER,paid_on TEXT)"));
        QVERIFY(q.exec("INSERT INTO client_payments VALUES('a','uid-a',10000,'2026-08-01'),('b','uid-a',25050,'2026-09-02'),('c','uid-c',50000,'2026-09-03'),('d','uid-d',99999,'2026-09-01')"));
        const auto receipts=ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30");
        QVERIFY(receipts.clientPaymentError.isEmpty());
        QCOMPARE(receipts.rows[0].clientReceivedCents,qint64(50000));
        QCOMPARE(receipts.rows[1].clientReceivedCents,qint64(0));
        QCOMPARE(receipts.rows[2].clientReceivedCents,qint64(35050));
        QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN order_year INTEGER DEFAULT 2026"));
        QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN vin TEXT DEFAULT 'VINZZZZZ'"));
        QVERIFY(q.exec("CREATE VIEW router_order_locator AS SELECT * FROM router_work_orders"));
        QVERIFY(q.exec("CREATE TABLE work_order_services(order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)"));
        QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN works_sum REAL DEFAULT 0"));
        QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN parts_sum REAL DEFAULT 0"));
        for (const auto& filter : {QStringLiteral("AA 1"), QString::fromUtf8("АА 1"), QStringLiteral("1234"), QStringLiteral("Audi")}) {
            const auto found = ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30",filter);
            QVERIFY2(found.rows.size() == 1, qPrintable(filter + ": " + found.stats.join(" | ")));
            QCOMPARE(found.rows.first().internalOrderNumber,101);
            QCOMPARE(found.rows.first().clientReceivedCents,qint64(35050));
        }
        QCOMPARE(ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30","9").rows.size(),3);
        QCOMPARE(ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30","not-found").rows.size(),0);
        QVERIFY(q.exec("UPDATE router_work_orders SET status='completed' WHERE number=101"));
        QVERIFY(q.exec(HistoryQueryService::buildHistorySql({QDate(2026,9,1),QDate(2026,9,30),QString()})));
        int closedCount=0;
        while(q.next()) { ++closedCount; QCOMPARE(q.value(10).toInt(),101); }
        QCOMPARE(closedCount,1);

        QTableView table;
        ReportsTableSupport::ReportsSnapshotTableModel model;
        table.setModel(&model);
        ReportsTableSupport::applyOrdersSnapshot(&table, &model, snapshot);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.columnCount(), 7);
        QCOMPARE(model.cellUserValue(2, 0).toInt(), 101);
        QVERIFY(!model.rowSpansAllColumns(2));

        const auto parts = OrderDialogPrintQueryService::loadPartRows("uid-a");
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts[0].partNo, QStringLiteral("OEM-001"));
        QCOMPARE(parts[0].store, QStringLiteral("Store"));
        QCOMPARE(parts[0].purchasePrice, 100.0);
        QCOMPARE(parts[0].invoiceNumber, QStringLiteral("INV-2026-01"));
        QCOMPARE(parts[0].purchaseDate, QStringLiteral("31.08.26"));
        QCOMPARE(parts[1].purchaseDate, QStringLiteral("01.09.26"));
        QVERIFY(parts[1].invoiceNumber.isEmpty());
        QCOMPARE(parts[1].purchasePrice, 60.0);
        QCOMPARE(parts[1].amount, 120.0);
        QVERIFY(OrderDialogPrintQueryService::loadPartRows(QString()).isEmpty());
        const QStringList reportSetup = {
            "ALTER TABLE work_order_parts ADD COLUMN pricing_calc_version INTEGER DEFAULT 1",
            "ALTER TABLE work_order_parts ADD COLUMN markup_percent REAL DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN manual_markup_override INTEGER DEFAULT 0",
            "CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT)"
        };
        for (const auto& sql : reportSetup) QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
        QString partsSchemaError;QVERIFY2(WorkshopSpareStockService::ensureSchema(db,&partsSchemaError),qPrintable(partsSchemaError));
        for(const auto& ddl:QStringList{"ALTER TABLE work_order_parts ADD COLUMN installed_at TEXT DEFAULT ''","ALTER TABLE work_order_parts ADD COLUMN delivery_date TEXT DEFAULT ''","ALTER TABLE work_order_parts ADD COLUMN paid INTEGER DEFAULT 0"})QVERIFY2(q.exec(ddl),qPrintable(q.lastError().text()));
        const auto unresolved = ReportsDataLoader::buildPartsSnapshot(path, "2026-09-01", "2026-09-30", 0, 0, {}, {});
        QVERIFY(unresolved.error.contains("unique order for report part 3"));
        QVERIFY(unresolved.rows.isEmpty());
        QVERIFY(q.exec("INSERT INTO router_work_orders(number,order_number,order_uid,opened_at,car,status,order_year) "
                       "VALUES(105,14,'wrong-uid','2026-09-01','Separate owner','open',2026)"));
        const auto purchases = ReportsDataLoader::buildPartsSnapshot(path, "2026-09-01", "2026-09-30", 0, 0, {}, {});
        QVERIFY2(purchases.error.isEmpty(),qPrintable(purchases.error));
        int linkedRows = 0;
        int relocatedRows = 0;
        for (const auto& purchase : purchases.rows) {
            if (purchase.partName == "Wrong part") {
                ++relocatedRows;
                QCOMPARE(purchase.internalOrderNumber,105);
                QCOMPARE(purchase.orderUid,QString("wrong-uid"));
                QCOMPARE(purchase.purchase,FormatUtils::formatNumberSpaces(9999,0));
            }
            if (purchase.invoiceNumber.isEmpty()) continue;
            ++linkedRows;
            QCOMPARE(purchase.invoiceNumber, QStringLiteral("INV-2026-01"));
            QCOMPARE(purchase.invoicePath, QStringLiteral("C:/invoice.pdf"));
        }
        QCOMPARE(linkedRows, 1);
        QCOMPARE(relocatedRows, 1);
        QVERIFY(ReportsDataLoader::buildOrdersSnapshot(path, "2027-01-01", "2027-01-31").rows.isEmpty());
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void ReportsOrdersTests::invoiceLinkOpensAttachedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("invoice.pdf"));
    {
        QPdfWriter writer(path);
        QPainter painter(&writer);
        painter.drawText(100, 100, QStringLiteral("Synthetic invoice"));
        QVERIFY(painter.end());
    }
    PartsSnapshot snapshot;
    PartsSnapshotRow row;
    row.invoiceNumber = QStringLiteral("INV-26-001");
    row.invoicePath = path;
    snapshot.rows.append(row);
    QTableView table;
    ReportsTableSupport::ReportsSnapshotTableModel model;
    table.setModel(&model);
    ReportsTableSupport::applyPartsSnapshot(&table, &model, snapshot);
    QCOMPARE(model.columnCount(), 8);
    QCOMPARE(model.cellText(0, 3), row.invoiceNumber);
    QVERIFY(qvariant_cast<QFont>(model.index(0, 3).data(Qt::FontRole)).underline());
    QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "captureUrl");
    const auto urlHandlerGuard = qScopeGuard([] {
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    });
    openedUrl = {};
    ReportsInteractionSupport::openInvoiceFromReport(&table, model.index(0, 3));
    QVERIFY(openedUrl.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(openedUrl.toLocalFile(), path, 30000);
    QCOMPARE(ReportsDateUtils::effectiveDate("bad-date", "15.09.26"), QDate(2026, 9, 15));
}

void ReportsOrdersTests::annualOrderUiPdfVisibleIdentity()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto oldRoot=qEnvironmentVariable("CS_TEST_DATA_ROOT"), oldName=QCoreApplication::applicationName();
    const bool oldTest=QStandardPaths::isTestModeEnabled();
    const auto oldActor=ActivityAuditService::currentActor();
    const auto restore=qScopeGuard([&]{Database::close();setIsolatedTestRoot(oldRoot);
        QCoreApplication::setApplicationName(oldName);QStandardPaths::setTestModeEnabled(oldTest);ActivityAuditService::setCurrentActor(oldActor);});
    QVERIFY(setIsolatedTestRoot(root.path()));QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(isolatedSafetyName("Safety-Year-Pdf-",root.path()));
    QString error;QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    const QString artifactRoot=qEnvironmentVariable("CS_ORDER_YEAR_PDF_OUTPUT").trimmed();
    const QString output=artifactRoot.isEmpty()?QDir(QCoreApplication::applicationDirPath()).filePath("annual-order-pdf-evidence"):QFileInfo(artifactRoot).absoluteFilePath();
    QVERIFY(QDir().mkpath(output));
    {
        auto db=Database::db();QVERIFY(Database::ensureSchema());QVERIFY(rebuildRouterViews(db));
        Database::setSetting("settings.booking.sync.enabled",false);Database::setSetting("settings.cloudsync.enabled",false);
        Database::setSetting("settings.int.directories.pdf_dir",output);
        QVERIFY2(StaffAccountService::ensureSchema(db,&error),qPrintable(error));QVERIFY2(AccessControlService::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        const QStringList sql={
            "INSERT INTO staff_accounts(id,display_name,role,active) VALUES('year-owner','Synthetic Owner','owner',1)",
            "INSERT INTO masters(id,name,role) VALUES('year-master','Synthetic Master','master')",
            "INSERT INTO work_orders(number,order_number,order_uid,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id) VALUES(900101,7,'year-2025',2025,'2025-02-03T12:00:00','2025-02-04T12:00:00','closed','AA1111AA','AA1111AA','Synthetic A','Synthetic','year-master'),(900202,7,'year-2026',2026,'2026-02-03T12:00:00','2026-02-04T12:00:00','closed','AA2222AA','AA2222AA','Synthetic B','Synthetic','year-master')",
            "INSERT INTO work_order_parts(order_number,order_uid,ordered_at,name,qty,price,amount,purchase_price,purchase_amount) VALUES(900101,'year-2025','2025-02-03','PART-ONLY-2025',1,123.45,123.45,100,100),(900202,'year-2026','2026-02-03','PART-ONLY-2026',1,234.56,234.56,200,200)"};
        for(const auto& statement:sql)QVERIFY2(q.exec(statement),qPrintable(q.lastError().text()));
    }
    ActivityAuditService::setCurrentActor({"year-owner","Synthetic Owner","owner","test"});
    for(const int year:{2025,2026}) {
        const QString uid=QString("year-%1").arg(year);
        QVERIFY2(Database::ensureOrderAvailableInActiveByUid(uid,&error),qPrintable(error));
        QVERIFY2(OrderFinancialService::refreshOrderFinancialSumsByUid(Database::db(),uid,&error),qPrintable(error));
        QSqlQuery header(Database::db());
        header.prepare("SELECT parts_sum FROM work_orders WHERE order_uid=?");
        header.addBindValue(uid);
        QVERIFY(header.exec() && header.next());
        QCOMPARE(qRound64(header.value(0).toDouble()*100),year==2025?12345:23456);
    }
    QString extractor=qEnvironmentVariable("CS_TEST_PDFTOTEXT");
    if(extractor.isEmpty())extractor=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../resources/windows/ocr-layer/poppler/bin/pdftotext.exe");
    QVERIFY2(QFileInfo::exists(extractor),"Set CS_TEST_PDFTOTEXT to a real pdftotext executable");
    QStringList failures;
    for(int year:{2025,2026}) {
        OrderDialog dialog(nullptr,year==2025?900101:900202);dialog.show();QCoreApplication::processEvents();
        QCOMPARE(dialog.recoveryOrderUid(),QString("year-%1").arg(year));
        QVERIFY(dialog.grab().save(QDir(output).filePath(QString("form-%1.png").arg(year))));
        for(bool parts:{false,true}) {
            // Shared with the preview button, including its filename and actual renderer;
            // suppress only the external PDF viewer, never the rendering or file checks.
            const auto preview=dialog.renderPdfPreview(parts);
            QVERIFY2(preview.ok,qPrintable(preview.error));QVERIFY(!preview.opened);
            const QString expected=QString("7_03.02.%1%2.pdf").arg(year).arg(parts?"_parts_report":"");
            if(QFileInfo(preview.outFile).fileName()!=expected)failures<<"filename: "+preview.outFile;
            QProcess text;text.start(extractor,{"-layout","-enc","UTF-8",preview.outFile,"-"});
            QVERIFY(text.waitForFinished());QCOMPARE(text.exitCode(),0);
            const auto content=QString::fromUtf8(text.readAllStandardOutput());
            QFile evidence(QDir(output).filePath(QString("text-%1-%2.txt").arg(year).arg(parts)));
            QVERIFY(evidence.open(QIODevice::WriteOnly));evidence.write(content.toUtf8());evidence.close();
            if(!QRegularExpression(QString::fromUtf8("№\\s*наряду\\s+(?:Дата\\s+)?7\\b")).match(content).hasMatch())failures<<QString("PDF number %1/%2").arg(year).arg(parts);
            QVERIFY(content.contains(QString("PART-ONLY-%1").arg(year)));
            QVERIFY(!content.contains(QString("PART-ONLY-%1").arg(year==2025?2026:2025)));
        }
    }
    QVERIFY2(Database::archiveSingleActiveOrderToArchive(900101,&error),qPrintable(error));
    Database::close();QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    {
        ScopedSqliteConnection physical(archiveShardFilePathForOrder(900101),"annual_pdf_archive",&error);
        QVERIFY2(physical.isOpen(),qPrintable(error));
        QSqlQuery archived(physical.db());
        QVERIFY(archived.exec("SELECT parts_sum FROM work_orders WHERE order_uid='year-2025'") && archived.next());
        QCOMPARE(qRound64(archived.value(0).toDouble()*100),12345);
        QSqlQuery registry(Database::db());
        QVERIFY(registry.exec("SELECT parts_sum FROM order_registry WHERE order_uid='year-2025'") && registry.next());
        QCOMPARE(qRound64(registry.value(0).toDouble()*100),12345);
    }
    {
        HistoryPage page;page.resize(1400,900);page.show();
        auto* table=page.findChild<QTableView*>();QVERIFY(table);
        auto* from=page.findChild<QDateEdit*>("historyDateFrom");
        auto* to=page.findChild<QDateEdit*>("historyDateTo");
        auto* search=page.findChild<QLineEdit*>("historySearch");
        QVERIFY(from && to && search);
        from->setDate(QDate(2025,1,1));to->setDate(QDate(2026,12,31));
        search->setFocus();QTest::keyClicks(search,"7");QTest::keyClick(search,Qt::Key_Return);
        const auto rowFor=[&](const QString& uid){auto* m=table->model();if(!m)return -1;
            int col=-1;for(int c=0;c<m->columnCount();++c)if(m->headerData(c,Qt::Horizontal).toString()=="__order_uid")col=c;
            if(col<0)return -1;for(int r=0;r<m->rowCount();++r)if(m->index(r,col).data().toString()==uid)return r;return -1;};
        QTRY_VERIFY_WITH_TIMEOUT(rowFor("year-2025")>=0&&rowFor("year-2026")>=0,15000);
        int partsColumn=-1;
        for(int c=0;c<table->model()->columnCount();++c)
            if(table->model()->headerData(c,Qt::Horizontal).toString()==QString::fromUtf8("Запчастини"))partsColumn=c;
        QVERIFY(partsColumn>=0);
        QCOMPARE(table->model()->index(rowFor("year-2025"),partsColumn).data().toString(),QString("123.45"));
        QCOMPARE(table->model()->index(rowFor("year-2026"),partsColumn).data().toString(),QString("234.56"));
        const auto visibleYear=[&] {
            for(int c=0;c<table->model()->columnCount();++c)
                if(table->model()->headerData(c,Qt::Horizontal).toString()==QString::fromUtf8("Рік"))return c;
            return -1;
        };
        const int yearColumn=visibleYear();
        if(yearColumn<0 || table->isColumnHidden(yearColumn))failures<<"Archive search hides order year";
        else for(int year:{2025,2026})
            QCOMPARE(table->model()->index(rowFor(QString("year-%1").arg(year)),yearColumn).data().toInt(),year);
        QVERIFY(page.grab().save(QDir(output).filePath("archive-search.png")));
        for(const QString& uid:QStringList{"year-2025","year-2026"}) {
            search->clear();QTest::keyClicks(search,"7");QTest::keyClick(search,Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(rowFor(uid)>=0,15000);
            const int row=rowFor(uid);QVERIFY(row>=0);QCOMPARE(table->model()->index(row,0).data().toString(),QString("7"));
            bool opened=false,matched=false;QTimer closer;
            connect(&closer,&QTimer::timeout,&page,[&]{for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<OrderDialog*>(widget)) {
                opened=true;matched=dialog->recoveryOrderUid()==uid;dialog->done(QDialog::Rejected);}});
            closer.start(50);
            // Bound a routing/error modal as well; a failure must not hang the native gate.
            QTimer watchdog;watchdog.setSingleShot(true);connect(&watchdog,&QTimer::timeout,&page,[]{if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->done(QDialog::Rejected);});watchdog.start(10000);
            const auto index=table->model()->index(row,0);
            QVERIFY(QMetaObject::invokeMethod(table,"doubleClicked",Qt::DirectConnection,Q_ARG(QModelIndex,index)));
            closer.stop();watchdog.stop();QVERIFY(opened);QVERIFY(matched);
        }
        search->clear();QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleYear()<0,15000);
    }
    QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
}

void ReportsOrdersTests::partsPdfLayout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CarServiceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Safety-ReportsPdfLayout-") + QUuid::createUuid().toString(QUuid::Id128));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    const QString connection = QStringLiteral("carservice_main");
    {
        QString runtimeError;QVERIFY2(Database::prepareRuntime(&runtimeError),qPrintable(runtimeError));
        QVERIFY2(Database::openPrepared(&runtimeError),qPrintable(runtimeError));
        auto db = Database::db();
        Database::setSetting("settings.booking.sync.enabled",false);
        Database::setSetting("settings.cloudsync.enabled",false);
        QVERIFY(Database::ensureSchema());
        QVERIFY(rebuildRouterViews(db));
        QString accessError;
        QVERIFY2(StaffAccountService::ensureSchema(db, &accessError), qPrintable(accessError));
        QVERIFY2(AccessControlService::ensureSchema(db, &accessError), qPrintable(accessError));
        QSqlQuery q(db);
        QVERIFY2(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('pdf-owner','Test Owner','owner',1)"), qPrintable(q.lastError().text()));
        ActivityAuditService::setCurrentActor({"pdf-owner", "Test Owner", "owner", "test"});
        const auto clearActor = qScopeGuard([] { ActivityAuditService::clearCurrentActor(); });
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('pdf-master','Test Master','master')"));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_number,order_uid,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id,vin,car_year) "
                       "VALUES(7,7,'pdf-order',2026,'2026-06-16T16:00:00','2026-09-03T20:58:00','open','AA1234AA','AA1234AA','Audi Q7','Test Client','pdf-master','WAUZZZ4L000000001',2015)"), qPrintable(q.lastError().text()));
        q.prepare("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,part_number,name,store,qty,price,amount,purchase_price,purchase_amount) "
                  "VALUES(1,7,'pdf-order','2026-08-13','4L0698151H',?, ?,1,1250,1250,1000,1000)");
        q.addBindValue(QString::fromUtf8("Фільтр повітряний ДВЗ"));
        q.addBindValue(QString::fromUtf8("Автонова-Д"));
        QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO work_order_part_attachments(part_id,file_path,display_name,invoice_number,invoice_date) "
                        "VALUES(1,'invoice.pdf','Invoice','INV-2026-000123','2026-08-12')"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,title,qty,price,amount) "
                       "VALUES(101,7,'pdf-order','Brake service',2,600.25,1200.50),"
                       "(102,7,'pdf-order','Oil service',1,400,400)"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO service_masters(service_id,master_id) VALUES(101,'pdf-master')"), qPrintable(q.lastError().text()));
        QVERIFY(Database::setPayrollPercent(QStringLiteral("pdf-master"), 40.0));
        const auto serviceRows = OrderDialogPrintQueryService::loadServiceRows("pdf-order");
        QCOMPARE(serviceRows.size(), 2);
        QCOMPARE(serviceRows.first().linkedMasters, QStringLiteral("Test Master"));
        QCOMPARE(serviceRows.first().qty * serviceRows.first().price, 1200.50);
        QVERIFY(serviceRows.first().hasPayrollMaster);
        QCOMPARE(serviceRows.first().payrollAmount, 480.20);
        QVERIFY(!serviceRows.last().hasPayrollMaster);
        QCOMPARE(serviceRows.last().payrollAmount, 0.0);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role) VALUES('pdf-second','Second Master','master')"));
        QVERIFY(Database::setPayrollPercent(QStringLiteral("pdf-second"), 60.0));
        QVERIFY(q.exec("INSERT INTO service_masters(service_id,master_id) VALUES(102,'pdf-second')"));
        QCOMPARE(OrderDialogPrintQueryService::loadServiceRows("pdf-order").last().payrollAmount, 240.0);
        // Shared work must not pay every master the full amount or duplicate the work row.
        QVERIFY(q.exec("UPDATE service_masters SET share_percent=60 WHERE service_id=101"));
        QVERIFY(q.exec("INSERT INTO service_masters(service_id,master_id,share_percent) VALUES(101,'pdf-second',40)"));
        auto sharedRows = OrderDialogPrintQueryService::loadServiceRows("pdf-order");
        QCOMPARE(sharedRows.size(), 2);
        QCOMPARE(sharedRows.first().payrollAmount, 576.24);
        QVERIFY(Database::setPayrollPercent(QStringLiteral("pdf-second"), 0.0));
        QCOMPARE(OrderDialogPrintQueryService::loadServiceRows("pdf-order").last().payrollAmount, 0.0);
        QVERIFY(Database::setPayrollPercent(QStringLiteral("pdf-second"), 60.0));
        QVERIFY(q.exec("DELETE FROM service_masters WHERE service_id=101 AND master_id='pdf-second'"));
        QVERIFY(q.exec("UPDATE service_masters SET share_percent=100 WHERE service_id=101"));
        // Quantity, not a stale cached amount, determines both cost and salary.
        QVERIFY(q.exec("UPDATE work_order_services SET qty=1.5,price=100.01,amount=9999 WHERE id=102"));
        QCOMPARE(OrderDialogPrintQueryService::loadServiceRows("pdf-order").last().payrollAmount, 90.01);
        QVERIFY(q.exec("UPDATE work_order_services SET qty=1,price=400,amount=400 WHERE id=102"));
        QVERIFY(OrderDialogPrintQueryService::loadServiceRows(QString()).isEmpty());
        // Deliberately stale amounts must not replace quantity * unit price in the report.
        QVERIFY2(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,ordered_at,name,store,qty,price,amount,purchase_price,purchase_amount) VALUES "
                       "(2,7,'pdf-order','2026-09-08','Oil 5W40','Store',8,400,400,300,2400),"
                       "(3,7,'pdf-order','2026-09-08','DEX 3','Store',5,350,350,250,1250),"
                       "(4,7,'pdf-order','2026-09-08','Steering rod','Store',2,848,848,678,1356),"
                       "(5,7,'pdf-order','2026-09-08','Fractional quantity','Store',1.5,80.50,0,60,90),"
                       "(6,7,'pdf-order','2026-09-08','Rounded amount','Store',1.25,100.01,0,70,87.50)"), qPrintable(q.lastError().text()));
        {
            OrderDialog dialog(nullptr, 7);
            dialog.show();
            dialog.openWorkPartsTab();
            auto* workParts = dialog.workPartsPageForRecovery();
            QVERIFY(workParts);
            // Resizing adds decorative empty rows while the initial read is in flight.
            // These are not edits and must not invalidate the pending data snapshot.
            dialog.resize(1540, 920);
            auto containsText = [workParts](const QString& text) {
                for (auto* table : workParts->findChildren<QTableWidget*>())
                    if (!table->findItems(text, Qt::MatchExactly).isEmpty()) return true;
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(containsText(QStringLiteral("Brake service")), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(containsText(QStringLiteral("Oil 5W40")), 10000);
            QList<QSignalSpy*> resizeSpies;
            for (auto* table : workParts->findChildren<QTableWidget*>())
                resizeSpies.append(new QSignalSpy(table, &QTableWidget::itemChanged));
            dialog.resize(1600, 1000);
            workParts->resize(1600, 1400);
            QTest::qWait(100);
            int resizeChanges = 0;
            for (auto* spy : resizeSpies) { resizeChanges += spy->count(); delete spy; }
            QCOMPARE(resizeChanges, 0);
            QVERIFY(containsText(QStringLiteral("Brake service")));
            QVERIFY(containsText(QStringLiteral("Oil 5W40")));
            dialog.setReadOnly(true);
            QString output = qEnvironmentVariable("CS_REPORT_PDF_TEST_OUTPUT");
            if (output.isEmpty()) output = dir.filePath(QStringLiteral("parts-report.pdf"));
            QString error;
            QVERIFY2(dialog.exportPartsReportToFile(output, &error), qPrintable(error));
            QVERIFY(QFileInfo(output).size() > 1000);
            const QString customerOutput = QFileInfo(output).absolutePath() + "/customer-recommendations.pdf";
            QVERIFY2(dialog.exportCustomerReportToFile(customerOutput,
                QString::fromUtf8("Перевірити гальмівну систему через 1000 км.\nЗамінити фільтр повітряний ДВЗ. <Текст без HTML>"), &error), qPrintable(error));
            QVERIFY(QFileInfo(customerOutput).size() > 1000);
            QVERIFY2(dialog.exportCustomerReportToFile(QFileInfo(output).absolutePath() + "/customer-long.pdf",
                QString::fromUtf8("Перевірити гальмівну систему та рівень оливи.\n").repeated(100), &error), qPrintable(error));
            QVERIFY2(dialog.exportCustomerReportToFile(QFileInfo(output).absolutePath() + "/customer-no-recommendations.pdf",
                QString(), &error), qPrintable(error));
            const QVariant previousFooter = Database::getSetting("settings.reports.pdf.footer_text", QString());
            const auto restoreFooter = qScopeGuard([&]() {
                Database::setSetting("settings.reports.pdf.footer_text", previousFooter);
            });
            Database::setSetting("settings.reports.pdf.footer_text",
                QStringLiteral("FooterStart Contact details 0123456789\nSecond requisites line"));
            QVERIFY2(dialog.exportCustomerReportToFile(QFileInfo(output).absolutePath() + "/customer-footer.pdf",
                QString(), &error), qPrintable(error));
            // One work and eleven parts, with requisites and QR: signatures
            // must fit with the document rather than create an almost blank page.
            QVERIFY(q.exec("SAVEPOINT pdf_eleven_parts"));
            QVERIFY(q.exec("DELETE FROM work_order_services WHERE id=102"));
            for (int id = 7; id <= 11; ++id) {
                q.prepare("INSERT INTO work_order_parts(id,order_number,order_uid,name,qty,price,amount) "
                          "VALUES(?,7,'pdf-order',?,1,250,250)");
                q.addBindValue(id);
                q.addBindValue(QStringLiteral("Additional part %1").arg(id));
                QVERIFY(q.exec());
            }
            const QVariant previousWebsite = Database::getSetting("settings.reports.pdf.footer_website", QString());
            const QVariant previousQr = Database::getSetting("settings.reports.pdf.website_qr", true);
            const auto restoreWebsite = qScopeGuard([&]() {
                Database::setSetting("settings.reports.pdf.footer_website", previousWebsite);
                Database::setSetting("settings.reports.pdf.website_qr", previousQr);
            });
            Database::setSetting("settings.reports.pdf.footer_website", "https://example.test");
            Database::setSetting("settings.reports.pdf.website_qr", true);
            const QString elevenOutput = QFileInfo(output).absolutePath() + "/customer-eleven-parts.pdf";
            QVERIFY2(dialog.exportCustomerReportToFile(elevenOutput, QString(), &error), qPrintable(error));
            QFile elevenPdf(elevenOutput);
            QVERIFY(elevenPdf.open(QIODevice::ReadOnly));
            const QByteArray elevenBytes = elevenPdf.readAll();
            QCOMPARE(elevenBytes.count("/Type /Page\n"), 1);
            QVERIFY(q.exec("ROLLBACK TO pdf_eleven_parts"));
            QVERIFY(q.exec("RELEASE pdf_eleven_parts"));
            Database::setSetting("settings.reports.pdf.footer_text",
                QStringLiteral("FooterStart Long requisites Contact 0123456789\n").repeated(150));
            QVERIFY2(dialog.exportCustomerReportToFile(QFileInfo(output).absolutePath() + "/customer-long-footer.pdf",
                QString(), &error), qPrintable(error));
        }
        WorkshopInventoryPurchaseInput purchase;
        purchase.purchasedOn=QDate::currentDate(); purchase.materialName=QString::fromUtf8("Керосин");
        purchase.unit=QString::fromUtf8("л"); purchase.quantity=10; purchase.unitCost=300.01; purchase.clientPrice=450.25;
        qint64 purchaseId=0; QString error;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,purchase,&purchaseId,&error),qPrintable(error));
        PartRowPayloadInput input;
        input.name=purchase.materialName; input.store=QString::fromUtf8("База"); input.qty=2; input.purchasePrice=300.01;
        const auto part=PartRowPayloadBuilderService::build(input);
        QCOMPARE(part.clientPrice,450.25); QCOMPARE(part.clientAmount,900.50);
        int partId=0;
        QVERIFY2(PartRepository::insert(db,7,part,&partId,&error),qPrintable(error));
        const auto printRows=OrderDialogPrintQueryService::loadPartRows("pdf-order");
        QCOMPARE(printRows.last().price,450.25); QCOMPARE(printRows.last().amount,900.50);
        QCOMPARE(printRows.last().purchasePrice,300.01);
        QVERIFY(printRows.last().stockMaterial);
        QVERIFY(!printRows.first().stockMaterial);
        QVERIFY(WorkshopInventoryService::setClientPrice(db,purchaseId,500,&error));
        QCOMPARE(OrderDialogPrintQueryService::loadPartRows("pdf-order").last().price,450.25);
        q.prepare("UPDATE work_order_parts SET purchase_price=999,purchase_amount=1998 WHERE id=?");
        q.addBindValue(partId); QVERIFY(q.exec());
        const auto adminRows=OrderDialogPrintQueryService::loadPartRows("pdf-order",true);
        QCOMPARE(adminRows.last().price,500.0);
        QCOMPARE(adminRows.last().amount,1000.0);
        QCOMPARE(adminRows.last().purchasePrice,300.01);
        QCOMPARE(adminRows.last().purchaseAmount,600.02);
        QCOMPARE(adminRows.first().price,1250.0);
        QCOMPARE(adminRows.first().purchasePrice,1000.0);
        const auto storedRows=OrderDialogPrintQueryService::loadPartRows("pdf-order");
        QCOMPARE(storedRows.last().price,450.25);
        QCOMPARE(storedRows.last().purchasePrice,999.0);
        purchase.purchasedOn=QDate::currentDate().addDays(1); purchase.unitCost=900; purchase.clientPrice=999;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,purchase,nullptr,&error),qPrintable(error));
        QCOMPARE(OrderDialogPrintQueryService::loadPartRows("pdf-order",true).last().price,500.0);
        QCOMPARE(OrderDialogPrintQueryService::loadPartRows("pdf-order",true).last().purchasePrice,300.01);
        QVERIFY(q.exec("UPDATE work_orders SET status='closed' WHERE number=7"));
        input.store=QString::fromUtf8("З/Ч клієнта"); input.qty=8; input.purchasePrice=1000;
        const auto customerPart=PartRowPayloadBuilderService::build(input);
        int customerPartId=0;
        QVERIFY2(PartRepository::insert(db,7,customerPart,&customerPartId,&error),qPrintable(error));
        const auto customerPrint=OrderDialogPrintQueryService::loadPartRows("pdf-order",true).last();
        QVERIFY(customerPrint.customerSupplied); QVERIFY(!customerPrint.stockMaterial);
        QCOMPARE(customerPrint.qty,8.0); QCOMPARE(customerPrint.price,0.0);
        QCOMPARE(customerPrint.purchaseAmount,0.0);
        {
            OrderDialog customerDialog(nullptr,7);
            const QString outputRoot=qEnvironmentVariable("CS_REPORT_PDF_TEST_OUTPUT").isEmpty()
                ? dir.path() : QFileInfo(qEnvironmentVariable("CS_REPORT_PDF_TEST_OUTPUT")).absolutePath();
            const QString customerFile=outputRoot+"/customer-parts-order-test.pdf";
            QVERIFY2(customerDialog.exportCustomerReportToFile(customerFile,QString(),&error),qPrintable(error));
            QVERIFY2(customerDialog.exportPartsReportToFile(outputRoot+"/customer-parts-admin-test.pdf",&error),qPrintable(error));
        }
        {
            QWidget host;
            host.resize(1500, 920);
            OrderDialog dialog(&host, 7);
            dialog.openWorkPartsTab();
            dialog.resize(1500, 920);
            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.workPartsPageForRecovery(), 10000);
            auto* workParts = dialog.workPartsPageForRecovery();
            QVERIFY(workParts);
            auto* table = workParts->findChild<QTableWidget*>(QStringLiteral("orderPartsTable"));
            QVERIFY(table);
            QCOMPARE(table->columnCount(), 12);
            QTRY_VERIFY_WITH_TIMEOUT(RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(
                RuntimeJob::Family::WorkPartsDialog).status == RuntimeJob::Status::Succeeded, 10000);
            QTest::qWait(200);
            auto* mode=workParts->findChild<QPushButton*>("customerPartsMode"); QVERIFY(mode);
            QVERIFY(!mode->isChecked());
            mode->click(); QVERIFY(mode->isChecked()); QVERIFY(table->property("csCustomerPartsMode").toBool());
            const QString modeCapture=qEnvironmentVariable("CS_CUSTOMER_PARTS_SCREENSHOT");
            if (!modeCapture.isEmpty()) QVERIFY(dialog.grab().save(modeCapture));
            mode->click(); QVERIFY(!table->property("csCustomerPartsMode").toBool());
            bool foundCustomer=false;
            for(int row=0;row<table->rowCount();++row) {
                if(table->item(row,8) && table->item(row,8)->text()==QString::fromUtf8("З/Ч клієнта")) {
                    foundCustomer=true; QVERIFY(table->item(row,6)->text().isEmpty()); QVERIFY(table->item(row,7)->text().isEmpty());
                }
            }
            QVERIFY(foundCustomer);
            QVERIFY(!workParts->findChild<QPushButton*>(QStringLiteral("payOrderParts")));
            const QString capture = qEnvironmentVariable("CS_REPORT_UI_TEST_OUTPUT");
            if (!capture.isEmpty()) QVERIFY(dialog.grab().save(capture));
            const QString reportPdf = qEnvironmentVariable("CS_REPORT_PDF_TEST_OUTPUT");
            if (!reportPdf.isEmpty()) QVERIFY2(dialog.exportPartsReportToFile(reportPdf, &error), qPrintable(error));
            QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=7"));
            QVERIFY(q.next());
            const QString statusBeforePayment = q.value(0).toString();
            QCOMPARE(statusBeforePayment, QStringLiteral("closed"));
            QTRY_VERIFY_WITH_TIMEOUT(RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(
                RuntimeJob::Family::WorkPartsDialog).status == RuntimeJob::Status::Succeeded, 10000);
            QTest::qWait(200);
        }
        {
            // Scrolling requires a report taller than the viewport at every supported DPI.
            for (int i=0; i<60; ++i) {
                q.prepare("INSERT INTO work_order_parts(order_number,order_uid,name,qty,price,amount,purchase_price,purchase_amount) "
                          "VALUES(7,'pdf-order',?,1,1,1,0,0)");
                q.addBindValue(QStringLiteral("Preview scroll fixture %1").arg(i));
                QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
            }
            ReportsPage reports; reports.resize(1300, 850); reports.show();
            auto* tabs = reports.findChild<QTabWidget*>(); QVERIFY(tabs);
            QCOMPARE(tabs->tabText(1), QString::fromUtf8("Наряди"));
            QCOMPARE(tabs->tabText(2), QString::fromUtf8("Накладні"));
            tabs->setCurrentIndex(1); QTest::qWait(500);
            auto* table = tabs->widget(1)->findChild<QTableView*>(); QVERIFY(table);
            auto* model = dynamic_cast<ReportsTableSupport::ReportsSnapshotTableModel*>(table->model()); QVERIFY(model);
            ReportsTableSupport::ReportsSnapshotTableModel::Row row;
            row.cells.resize(7); row.cells[0].userValue=7; row.cells[1].text="7";
            model->setSnapshot(model->headers(), {row}); table->setCurrentIndex(model->index(0,1));
            openedUrl.clear(); QDesktopServices::setUrlHandler("file", this, "captureUrl");
            const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
            bool checked=false, timedOut=false;
            QTimer watchdog; watchdog.setInterval(100);
            int ticks=0;
            connect(&watchdog, &QTimer::timeout, &reports, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (++ticks > 200) { timedOut=true; if(dialog) dialog->reject(); return; }
                if (!dialog || dialog->objectName()!="orderReportPreviewDialog") return;
                auto* preview = dialog->findChild<QScrollArea*>("orderReportPdfPreview");
                const auto pages = dialog->findChildren<QLabel*>("orderReportPdfPage");
                if (!preview || pages.isEmpty() || pages.first()->pixmap().isNull()) return;
                checked = preview->verticalScrollBar()->maximum()>0
                    && dialog->findChild<QPushButton*>("reportPayOrderParts")
                    && dialog->findChild<QPushButton*>("reportPayPart");
                checked = checked && dialog->screen()->availableGeometry().contains(dialog->frameGeometry())
                    && pages.first()->width() == qRound(qMax(120, qBound(240, preview->viewport()->width() - 30, 1100) / 2) * 1.30);
                qInfo() << "Preview geometry" << dialog->frameGeometry() << dialog->screen()->availableGeometry()
                        << "page/viewport" << pages.first()->width() << preview->viewport()->width()
                        << "pages/body/viewport" << pages.size() << pages.first()->size() << preview->widget()->size() << preview->viewport()->size() << preview->widget()->minimumSize() << "scroll maximum" << preview->verticalScrollBar()->maximum() << "checked before payment" << checked;
                const QString capture=qEnvironmentVariable("CS_REPORT_PREVIEW_TEST_OUTPUT");
                if (!capture.isEmpty()) checked = checked && dialog->grab().save(capture);
                watchdog.stop();
                QTimer paymentDriver;
                int paymentTicks=0;
                connect(&paymentDriver,&QTimer::timeout,&reports,[&] {
                    auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    if (++paymentTicks>200) {timedOut=true;if(modal)modal->reject();return;}
                    if (!modal) return;
                    if (modal->objectName()=="purchasePaymentDialog") {
                        if(auto* pay=modal->findChild<QPushButton*>("payInFull"))pay->click();
                    } else if(auto* message=qobject_cast<QMessageBox*>(modal)) {
                        if(auto* yes=message->button(QMessageBox::Yes))yes->click();
                        else if(auto* ok=message->button(QMessageBox::Ok))ok->click();
                    }
                });
                paymentDriver.start(50);
                dialog->findChild<QPushButton*>("reportPayOrderParts")->click();
                paymentDriver.stop();
                qInfo() << "Payment button enabled after payment" << dialog->findChild<QPushButton*>("reportPayOrderParts")->isEnabled();
                checked = checked && !dialog->findChild<QPushButton*>("reportPayOrderParts")->isEnabled();
                dialog->reject();
            });
            watchdog.start(); table->doubleClicked(model->index(0,1)); watchdog.stop();
            QVERIFY(!timedOut); QVERIFY(checked); QVERIFY(openedUrl.isEmpty());
            QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=7")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("closed"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void ReportsOrdersTests::canonicalRewritePreservesOutsourceIdentity_data()
{
    QTest::addColumn<QString>("orderColumn");
    for (const auto& column : {"order_number", "work_order", "work_order_id", "work_order_number"})
        QTest::newRow(column) << QString::fromLatin1(column);
}

void ReportsOrdersTests::canonicalRewritePreservesOutsourceIdentity()
{
    QFETCH(QString, orderColumn);
    const QString connection = QStringLiteral("canonical-outsource-identity");
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    const auto cleanup = qScopeGuard([&] { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); });
    db.setDatabaseName(":memory:");
    QVERIFY(db.open());
    QSqlQuery q(db);
    const QStringList setup{
        "ATTACH DATABASE ':memory:' AS legacy_rewrite",
        "CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT, order_number INTEGER, order_year INTEGER)",
        "INSERT INTO work_orders VALUES(1,'uid-2025',1,2025),(2,'uid-2026',1,2026)",
        "CREATE TABLE outsource_repairs(id INTEGER PRIMARY KEY, order_number INTEGER, order_uid TEXT, repair_cost REAL, client_price REAL, service_id INTEGER)",
        QStringLiteral("CREATE TABLE legacy_rewrite.outsource_repairs(id INTEGER, %1 INTEGER, repair_cost REAL, client_price REAL, service_id INTEGER)").arg(orderColumn),
        "INSERT INTO legacy_rewrite.outsource_repairs VALUES(11,1,100.25,150.75,31),(22,2,200.50,300.25,32)"
    };
    for (const auto& sql : setup) QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
    CanonicalRewriteSourceSchema schema;
    schema.outsource = {"id", orderColumn, "repair_cost", "client_price", "service_id"};
    QString error;
    QVERIFY2(importCanonicalRewriteAncillaryTables(db, schema, &error), qPrintable(error));
    QVERIFY(q.exec("SELECT id,order_number,order_uid,repair_cost,client_price,service_id FROM outsource_repairs ORDER BY id"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 11);
    QCOMPARE(q.value(1).toInt(), 1);
    QCOMPARE(q.value(2).toString(), QString("uid-2025"));
    QCOMPARE(q.value(3).toDouble(), 100.25);
    QCOMPARE(q.value(4).toDouble(), 150.75);
    QCOMPARE(q.value(5).toInt(), 31);
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 22);
    QCOMPARE(q.value(1).toInt(), 2);
    QCOMPARE(q.value(2).toString(), QString("uid-2026"));
    QCOMPARE(q.value(3).toDouble(), 200.50);
    QCOMPARE(q.value(4).toDouble(), 300.25);
    QCOMPARE(q.value(5).toInt(), 32);
    QVERIFY(!q.next());
}

void ReportsOrdersTests::canonicalRewritePreservesChildIdentity_data()
{
    QTest::addColumn<QString>("partOrderColumn");
    QTest::addColumn<QString>("serviceOrderColumn");
    const QStringList columns{QStringLiteral("order_number"), QStringLiteral("work_order"),
                              QStringLiteral("work_order_id"), QStringLiteral("work_order_number")};
    for (const QString& partColumn : columns)
        for (const QString& serviceColumn : columns)
            QTest::newRow(qPrintable(partColumn + ":" + serviceColumn)) << partColumn << serviceColumn;
}

void ReportsOrdersTests::canonicalRewritePreservesChildIdentity()
{
    QFETCH(QString, partOrderColumn);
    QFETCH(QString, serviceOrderColumn);
    const QString connection = QStringLiteral("canonical-child-identity");
    struct Cleanup {
        QString connection;
        ~Cleanup() { QSqlDatabase::removeDatabase(connection); }
    } cleanup{connection};
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());
    QSqlQuery q(db);
    const QStringList setup{
        QStringLiteral("ATTACH DATABASE ':memory:' AS legacy_rewrite"),
        QStringLiteral("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT UNIQUE, order_number INTEGER, order_year INTEGER, opened_at TEXT)"),
        QStringLiteral("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY, order_number INTEGER, order_uid TEXT, title TEXT)"),
        QStringLiteral("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY, order_number INTEGER, order_uid TEXT, name TEXT)"),
        QStringLiteral("CREATE TABLE legacy_rewrite.work_orders(number INTEGER PRIMARY KEY, order_uid TEXT, order_number INTEGER, order_year INTEGER, opened_at TEXT)"),
        QStringLiteral("INSERT INTO legacy_rewrite.work_orders VALUES(1,'uid-2025',1,2025,'2025-12-31'),(2,'uid-2026',1,2026,'2026-01-01')"),
        QStringLiteral("CREATE TABLE legacy_rewrite.work_order_services(id INTEGER, %1 INTEGER, title TEXT)").arg(serviceOrderColumn),
        QStringLiteral("INSERT INTO legacy_rewrite.work_order_services VALUES(11,1,'first'),(22,2,'second')"),
        QStringLiteral("CREATE TABLE legacy_rewrite.work_order_parts(id INTEGER, %1 INTEGER, name TEXT)").arg(partOrderColumn),
        QStringLiteral("INSERT INTO legacy_rewrite.work_order_parts VALUES(33,1,'first part'),(44,2,'second part')")
    };
    for (const auto& sql : setup) QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
    CanonicalRewriteSourceSchema schema;
    schema.orders = {"number", "order_uid", "order_number", "order_year", "opened_at"};
    schema.services = {"id", serviceOrderColumn, "title"};
    schema.parts = {"id", partOrderColumn, "name"};
    QString error;
    QVERIFY2(importCanonicalRewritePrimaryTables(db, schema, &error), qPrintable(error));
    // Independent expected mapping: internal key 1 -> 2025 UID, key 2 -> 2026 UID.
    // Both visible numbers are 1; child ownership must never use that number.
    for (const QString& table : {QStringLiteral("work_order_services"), QStringLiteral("work_order_parts")}) {
        QVERIFY(q.exec(QStringLiteral("SELECT order_number,order_uid FROM %1 ORDER BY id").arg(table)));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        QCOMPARE(q.value(1).toString(), QStringLiteral("uid-2025"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QCOMPARE(q.value(1).toString(), QStringLiteral("uid-2026"));
        QVERIFY(!q.next());
    }
}


void ReportsOrdersTests::canonicalRewritePreservesOrderYear_data()
{
    QTest::addColumn<QString>("openedAt");
    QTest::addColumn<int>("sourceYear");
    QTest::addColumn<int>("expectedYear");
    for (int sourceYear : {-1, 0, 2031}) {
        for (const auto& date : {QStringLiteral("31.12.2025"), QStringLiteral("01.01.2026 08:30"),
                                 QStringLiteral("31.12.25/23.59"), QStringLiteral("2026-01-01T00:01:00")}) {
            const int expected = sourceYear > 0 ? sourceYear : (date.startsWith("31") ? 2025 : 2026);
            QTest::newRow(qPrintable(QString::number(sourceYear) + ":" + date)) << date << sourceYear << expected;
        }
    }
}

void ReportsOrdersTests::canonicalRewritePreservesOrderYear()
{
    QFETCH(QString, openedAt);
    QFETCH(int, sourceYear);
    QFETCH(int, expectedYear);
    const QString connection = QStringLiteral("canonical-order-year");
    struct Cleanup {
        QString connection;
        ~Cleanup() { QSqlDatabase::removeDatabase(connection); }
    } cleanup{connection};
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("ATTACH DATABASE ':memory:' AS legacy_rewrite")));
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT, order_number INTEGER, order_year INTEGER, opened_at TEXT)")));
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE legacy_rewrite.work_orders(number INTEGER PRIMARY KEY, opened_at TEXT%1)")
        .arg(sourceYear >= 0 ? QStringLiteral(", order_year INTEGER") : QString())));
    q.prepare(QStringLiteral("INSERT INTO legacy_rewrite.work_orders VALUES(73,?%1)")
        .arg(sourceYear >= 0 ? QStringLiteral(",?") : QString()));
    q.addBindValue(openedAt);
    if (sourceYear >= 0) q.addBindValue(sourceYear);
    QVERIFY(q.exec());
    CanonicalRewriteSourceSchema schema;
    schema.orders = {"number", "opened_at"};
    if (sourceYear >= 0) schema.orders.insert(QStringLiteral("order_year"));
    QString error;
    QVERIFY2(importCanonicalRewritePrimaryTables(db, schema, &error), qPrintable(error));
    QVERIFY(q.exec(QStringLiteral("SELECT number,order_number,order_year,opened_at FROM work_orders")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 73);
    QCOMPARE(q.value(1).toInt(), 73);
    QCOMPARE(q.value(2).toInt(), expectedYear);
    QCOMPARE(q.value(3).toString(), openedAt);
    QVERIFY(!q.next());
    QVERIFY(q.exec(QStringLiteral("SELECT opened_at FROM legacy_rewrite.work_orders")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), openedAt);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() >= 2 && (args[1] == "--wal-crash-fixture" || args[1] == "--snapshot-restore-crash-fixture" || args[1] == "--snapshot-stage-crash-fixture" || args[1] == "--annual-allocation-crash-fixture" || args[1] == "--registry-intent-crash-fixture" || args[1] == "--startup-bulk-crash-fixture" || args[1] == "--registry-rebuild-crash-fixture")) {
        const auto name = qEnvironmentVariable("CS_SAFETY_FIXTURE_APP_NAME");
        const auto root = qEnvironmentVariable("CS_TEST_DATA_ROOT");
        if (!name.startsWith("Safety-") || root.isEmpty()) return 81;
        QCoreApplication::setApplicationName(name);
        QCoreApplication::setOrganizationName(qEnvironmentVariable("CS_SAFETY_FIXTURE_ORGANIZATION"));
        QCoreApplication::setOrganizationDomain(qEnvironmentVariable("CS_SAFETY_FIXTURE_DOMAIN"));
        QStandardPaths::setTestModeEnabled(true);
        if (!AppRuntime::dataDir().startsWith(root)) return 82;
        if (args[1] == "--registry-rebuild-crash-fixture") {
            if(args.size()!=3 || (args[2]!="after-registry" && args[2]!="after-source"))return 120;
            qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& message) {
                const auto text=(QString(type==QtCriticalMsg?"QCRITICAL|":type==QtFatalMsg?"QFATAL|":"QT_MESSAGE|")+qFormatLogMessage(type,context,message)).toUtf8();
                std::fwrite(text.constData(),1,size_t(text.size()),stderr);std::fputc('\n',stderr);std::fflush(stderr);
            });
            QString error;if(!Database::prepareRuntime(&error) || !Database::openPrepared(&error) || !ensurePartRegistrySchemaFile())return 121;
            auto db=Database::db();QSqlQuery q(db),r(QSqlDatabase::database(dbRegistryConnectionName(),false));
            if(args[2]=="after-registry") {
                if(!q.exec("CREATE TABLE rebuild_ack_parent(id INTEGER PRIMARY KEY)")
                   || !q.exec("CREATE TABLE rebuild_ack_failure(id INTEGER REFERENCES rebuild_ack_parent(id) DEFERRABLE INITIALLY DEFERRED)")
                   || !q.exec("CREATE TRIGGER fail_rebuild_ack AFTER UPDATE OF published_registry_revision ON workspace_order_state BEGIN INSERT INTO rebuild_ack_failure VALUES(1); END"))return 122;
            } else if(!r.exec("CREATE TRIGGER fail_rebuild_completion BEFORE UPDATE ON registry_meta WHEN NEW.key='registry_rebuild_pending' AND NEW.value='0' BEGIN SELECT RAISE(IGNORE); END"))return 123;
            if(rebuildRegistryDerivedState(db,&error) || error.isEmpty())return 124;
            if(args[2]=="after-registry" && !error.contains("FOREIGN KEY constraint failed"))return 125;
            if(args[2]=="after-source" && !error.contains("marker was not cleared"))return 126;
            if(!r.exec("SELECT value FROM registry_meta WHERE key='registry_rebuild_pending'") || !r.next() || r.value(0).toString()!="1")return 127;
            std::_Exit(0); // Own fixture process only: retain committed recovery marker and WAL.
        }
        if (args[1] == "--startup-bulk-crash-fixture") {
            qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& message) {
                const auto text=(QString(type==QtCriticalMsg?"QCRITICAL|":type==QtFatalMsg?"QFATAL|":"QT_MESSAGE|")+qFormatLogMessage(type,context,message)).toUtf8();
                std::fwrite(text.constData(),1,size_t(text.size()),stderr);std::fputc('\n',stderr);std::fflush(stderr);
            });
            QString error;if(!Database::prepareRuntime(&error) || !Database::openPrepared(&error))return 110;
            if(Database::runStartupRegistryRefresh(&error) || error.isEmpty())return 111;
            QSqlQuery marker(QSqlDatabase::database(dbRegistryConnectionName(),false));
            if(!marker.exec("SELECT value FROM registry_meta WHERE key='runtime_bulk_pending'") || !marker.next() || marker.value(0).toString()!="1")return 112;
            std::_Exit(0); // Own process only: preserve real committed pending marker/WAL without close.
        }
        if (args[1] == "--registry-intent-crash-fixture") {
            if (args.size()!=3 || (args[2]!="before" && args[2]!="after" && args[2]!="after-external")) return 96;
            // GUI-subsystem Qt normally sends messages to the debugger on Windows.
            // Preserve every child diagnostic in its pipe for the parent's exact checks.
            qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& message) {
                const auto text=(QString(type==QtCriticalMsg?"QCRITICAL|":type==QtFatalMsg?"QFATAL|":"QT_MESSAGE|")+qFormatLogMessage(type,context,message)).toUtf8();
                std::fwrite(text.constData(),1,size_t(text.size()),stderr);std::fputc('\n',stderr);std::fflush(stderr);
            });
            QString error;if (!Database::prepareRuntime(&error) || !Database::openPrepared(&error)) return 97;
            auto db=Database::db();TransactionContext tx(db,"RegistryProcessCrash");if(!tx.begin("test",&error)) return 98;
            QSqlQuery q(db);if(!q.exec("UPDATE work_orders SET car='Process committed car' WHERE number=100")) return 99;
            if(!q.exec("UPDATE work_order_parts SET name='Process committed part' WHERE order_uid='durable-owner' AND name='First captured part'")) return 102;
            if(!Database::finalizeOrderWorkspaceInTransaction(db,100,&error)) return 100;
            if(args[2]!="before" && !tx.commit("test",&error)) return 101;
            if(args[2]=="after-external") {
                if(!q.exec("CREATE TABLE intent_ack_parent(id INTEGER PRIMARY KEY)")
                    || !q.exec("CREATE TABLE intent_ack_failure(id INTEGER REFERENCES intent_ack_parent(id) DEFERRABLE INITIALLY DEFERRED)")
                    || !q.exec("CREATE TRIGGER fail_local_registry_ack AFTER UPDATE OF published_registry_revision ON workspace_order_state BEGIN INSERT INTO intent_ack_failure VALUES(1); END")) return 103;
                error.clear();
                if(Database::refreshOrderRegistry(100,&error) || !error.contains("FOREIGN KEY constraint failed")) return 104;
            }
            std::_Exit(0); // Own process only; exact local commit/publication/WAL boundary.
        }
        if (args[1] == "--annual-allocation-crash-fixture") {
            if (args.size()!=3 || (args[2]!="before" && args[2]!="after")) return 90;
            auto db=QSqlDatabase::addDatabase("QSQLITE","annual-process-helper");
            db.setDatabaseName(QDir(AppRuntime::dataDir()).filePath("annual_process.db"));
            if(!db.open() || !db.transaction()) return 91;
            QString error; OrderIdentityService::Allocation allocation;
            if(!OrderIdentityService::allocateAnnualIdentity(db,"process-request",QDateTime::fromString("2026-12-31T22:30:00Z",Qt::ISODate),0,&allocation,&error)) return 92;
            QSqlQuery q(db); if(!q.prepare("INSERT INTO work_orders VALUES(?,?,?,?)")) return 93;
            q.addBindValue(allocation.internalNumber); q.addBindValue(allocation.orderUid); q.addBindValue(allocation.visibleNumber); q.addBindValue(allocation.year);
            if(!q.exec()) return 94;
            if(args[2]=="after" && !db.commit()) return 95;
            std::_Exit(0); // Own process only, before or after the outer order/allocation COMMIT.
        }
        if (args[1] == "--wal-crash-fixture") {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "crash-helper");
            db.setDatabaseName(QDir(AppRuntime::dataDir()).filePath("crash_wal_source.db"));
            if (!db.open()) return 83;
            QSqlQuery q(db);
            if (!q.exec("PRAGMA journal_mode=WAL") || !q.next() || q.value(0).toString() != "wal"
                || !q.exec("PRAGMA wal_autocheckpoint=0") || !q.exec("CREATE TABLE proof(value INTEGER)")
                || !q.exec("INSERT INTO proof VALUES(83)")) return 84;
            std::_Exit(0); // Own test process only: leave committed WAL without SQLite close/checkpoint.
        }
        if(args[1]=="--snapshot-stage-crash-fixture") {
            if(args.size()!=4 || (args[3]!="stage-write-crash" && args[3]!="stage-promoted-crash"))return 130;
            QFile manifest(QDir(args[2]).filePath("manifest.json"));if(!manifest.open(QIODevice::ReadOnly))return 131;
            const auto bytes=manifest.readAll();manifest.close();const auto document=QJsonDocument::fromJson(bytes).object();
            const auto entry=document.value("files").toObject().value("CarService_workspace.db.enc").toObject();
            QString error;const auto hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
            if(!MigrationRestoreJournal::begin(args[2],hash,&error))return 132;
            auto copier=[&](const QString& from,const QString& to,const QString& expected,QString*){
                QFile source(from);QSaveFile target(to);if(!source.open(QIODevice::ReadOnly)||!target.open(QIODevice::WriteOnly))return false;
                const auto contents=source.readAll();
                if(args[3]=="stage-write-crash") {
                    if(target.write(contents.left(4))!=4 || !target.flush())return false;
                    std::_Exit(0); // Own child: leave real uncommitted protected QSaveFile and journal.
                }
                if(target.write(contents)!=contents.size() || !target.commit())return false;
                return QString::fromLatin1(QCryptographicHash::hash(contents,QCryptographicHash::Sha256).toHex())==expected;
            };
            if(!SecureSnapshotRestore::restoreFile(QDir(args[2]).filePath("CarService_workspace.db.enc"),AppRuntime::sealedActiveDbFilePath(),
                entry.value("sha256").toString(),entry.value("metadata").toObject(),copier,&error))return 133;
            std::_Exit(0); // Own child: actual per-file promotion complete, other generation files still changed.
        }
        if (args.size() != 3) return 85;
        QFile manifest(QDir(args[2]).filePath("manifest.json"));
        if (!manifest.open(QIODevice::ReadOnly)) return 86;
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(manifest.readAll(), QCryptographicHash::Sha256).toHex());
        QString error;
        if (!MigrationRestoreJournal::begin(args[2], hash, &error)) return 87;
        QFile original(QDir(args[2]).filePath("CarService_workspace.db.enc"));
        QFile first(AppRuntime::sealedActiveDbFilePath());
        if (!original.open(QIODevice::ReadOnly) || !first.open(QIODevice::WriteOnly)) return 88;
        const auto bytes = original.readAll();
        if (first.write(bytes) != bytes.size() || !first.flush()) return 89;
        std::_Exit(0); // Persist a real mixed generation and intent; startup replay must finish it.
    }
    ReportsOrdersTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ReportsOrdersTests.moc"
