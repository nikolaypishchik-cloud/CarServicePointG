#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include <QRegularExpression>
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbSettingsSqlSupport.h"
#include "infrastructure/db/sqlite/DbSourceFingerprintSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteExecutionSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteFinalizeSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.h"
#include "core/services/MutationExecutor.h"
#include "runtime/AppRuntime.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool rewriteLegacyToCanonicalWorkspaceIfNeeded(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Main DB connection is invalid");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    const bool prepared = !SqliteSchemaInspection::tableAbsent(database,"main","migration_prepared_source");
    QString originalIdentity, projectedSignature;
    MigrationOrderKeys::Map keys;
    if (prepared) {
        QSqlQuery state(database);
        if (!state.exec("SELECT source_identity,finalized FROM migration_prepared_source WHERE singleton=1") || !state.next()) {
            if (errorText) *errorText = QStringLiteral("Prepared import identity is unavailable");
            return false;
        }
        originalIdentity = state.value(0).toString();
        if (!QRegularExpression(QStringLiteral("[|][0-9]+[|][0-9a-f]{64}$")).match(originalIdentity).hasMatch()) {
            if (errorText) *errorText = QStringLiteral("Invalid prepared import identity");
            return false;
        }
        projectedSignature=originalIdentity.section('|',-2);
        if (!SqliteSchemaInspection::tableAbsent(database,"main","migration_archive_pin")) {
            QSqlQuery pin(database);
            pin.prepare("SELECT fingerprint FROM migration_archive_pin WHERE source_identity=?");
            pin.addBindValue(originalIdentity);
            if (!pin.exec() || !pin.next() || !QRegularExpression("^[0-9]+[|][0-9a-f]{64}$").match(pin.value(0).toString()).hasMatch()) {
                if (errorText) *errorText="Invalid projected archive fingerprint";
                return false;
            }
            projectedSignature=pin.value(0).toString();
        }
        if (!MigrationOrderKeys::load(database,originalIdentity,&keys,errorText)) return false;
        bool stateOk=false;
        const auto finalized=state.value(1).toLongLong(&stateOk);
        if (!stateOk || (finalized!=0 && finalized!=1)) {
            if (errorText) *errorText = QStringLiteral("Invalid prepared import completion state");
            return false;
        }
        if (finalized==1) {
            if (dbGetSettingValue(database, QStringLiteral("db_canonical_rewrite_done"))==QStringLiteral("1")
                && dbGetSettingValue(database, QStringLiteral("db_canonical_rewrite_fingerprint")).section('|',-2)==projectedSignature) return true;
            if (errorText) *errorText = QStringLiteral("Prepared import completion evidence differs");
            return false;
        }
    }
    if (!ensureLegacyArchiveSnapshot()) {
        if (errorText) *errorText = QStringLiteral("Legacy archive snapshot is unavailable");
        return false;
    }

    const QString sourcePath = QFileInfo::exists(AppRuntime::archiveDbFilePath()) ? AppRuntime::archiveDbFilePath() : AppRuntime::legacyDbFilePath();
    if (!QFileInfo::exists(sourcePath)) {
        if (prepared && errorText) *errorText = QStringLiteral("Prepared import source unavailable");
        return !prepared;
    }

    QString supportSchemaError;
    if (!ensureCanonicalRewriteSupportSchema(database, &supportSchemaError)) {
        if (errorText) *errorText = supportSchemaError.trimmed().isEmpty() ? QStringLiteral("Canonical rewrite support schema bootstrap failed") : supportSchemaError;
        return false;
    }

    const QString fingerprint = legacySourceFingerprint(sourcePath);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Canonical rewrite source fingerprint:" << sourcePath << fingerprint.left(96);
    const QString done = dbGetSettingValue(database, QStringLiteral("db_canonical_rewrite_done"));
    const QString doneFingerprint = dbGetSettingValue(database, QStringLiteral("db_canonical_rewrite_fingerprint"));
    if (prepared && fingerprint.section('|',-2) != projectedSignature) {
        if (errorText) *errorText = QStringLiteral("Prepared import source fingerprint mismatch");
        return false;
    }
    if (!prepared && done == QStringLiteral("1") && doneFingerprint == fingerprint) return true;

    const QString correspondenceSource = prepared ? AppRuntime::legacyDbFilePath() : sourcePath;
    if (prepared && legacySourceFingerprint(correspondenceSource).section('|',-2)!=originalIdentity.section('|',-2)) {
        if (errorText) *errorText=QStringLiteral("Original import bytes do not match the audited source");
        return false;
    }
    QSqlQuery attach(database);
    attach.exec("DETACH DATABASE legacy_rewrite");
    if (!attach.exec(QStringLiteral("ATTACH DATABASE %1 AS legacy_rewrite").arg(sqlQuoteText(correspondenceSource)))) {
        if (errorText) *errorText = attach.lastError().text();
        return false;
    }
    auto detachLegacy = [&]() { QSqlQuery dq(database); dq.exec("DETACH DATABASE legacy_rewrite"); };

    CanonicalRewriteSourceSchema legacySchema;

    QMap<QString, QVariant> workspaceSettingsSnapshot;
    if (!loadCanonicalRewriteSourceSchema(database, QStringLiteral("legacy_rewrite"), &legacySchema, errorText)
        || !snapshotWorkspaceSettingsForCanonicalRewrite(database, &workspaceSettingsSnapshot, errorText)) {
        detachLegacy();
        return false;
    }
    if (workspaceSettingsSnapshot.isEmpty() && tableExistsInDb(database, QStringLiteral("settings"))) {
        qWarning() << "Settings snapshot before canonical rewrite is empty";
    }

    if (legacySchema.orders.isEmpty()) {
        detachLegacy();
        if (errorText) *errorText = QStringLiteral("Legacy DB does not contain work_orders");
        return false;
    }

    QSqlQuery q(database);
    q.exec("PRAGMA foreign_keys = OFF");

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbImportSupport::rewriteLegacyDatabaseToCanonicalLayout");
    request.entityType = QStringLiteral("workspace");
    request.entityId = fingerprint.left(32);
    request.operation = QStringLiteral("rewrite_legacy_database_to_canonical_layout");
    request.changedTables = {QStringLiteral("masters"), QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_order_parts"), QStringLiteral("outsource_repairs"), QStringLiteral("parts_catalog"), QStringLiteral("expenses"), QStringLiteral("planning_calendar"), QStringLiteral("settings"), QStringLiteral("migration_record_map")};
    request.beginErrorFallback = QStringLiteral("Canonical rewrite transaction begin failed");
    request.commitErrorFallback = QStringLiteral("Canonical rewrite transaction commit failed");

    const QString attachedSource = QStringLiteral("legacy_rewrite");
    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase database = txDb;
        QSqlQuery q(database);
        auto fail = [&](const QString& msg) {
            if (txError) *txError = msg;
            return false;
        };
    if (prepared) {
        if (!recordCanonicalRewriteCorrespondence(database,originalIdentity,attachedSource,legacySchema,txError,keys)
            || !finalizeCanonicalRewriteImportedWorkspace(database,fingerprint,sourcePath,txError)) return false;
        QSqlQuery finish(database);
        if (!finish.exec("UPDATE migration_prepared_source SET finalized=1 WHERE singleton=1 AND finalized=0") || finish.numRowsAffected()!=1)
            return fail(QStringLiteral("Cannot finalize prepared migration: %1").arg(finish.lastError().text()));
        return true;
    }
    QString cleanupError;
    if (!clearCanonicalRewriteWorkspaceTargets(database, &cleanupError)) {
        return fail(cleanupError);
    }

    QString primaryImportError;
    if (!importCanonicalRewritePrimaryTables(database, legacySchema, &primaryImportError)) {
        return fail(primaryImportError);
    }

    QString ancillaryImportError;
    if (!importCanonicalRewriteAncillaryTables(database, legacySchema, &ancillaryImportError)) {
        return fail(ancillaryImportError);
    }

    if (!recordCanonicalRewriteCorrespondence(database, fingerprint, attachedSource, legacySchema, txError))
        return false;

    if (!workspaceSettingsSnapshot.isEmpty()) {
        QString restoreError;
        if (!restoreCanonicalRewriteWorkspaceSettings(database, workspaceSettingsSnapshot, &restoreError)) {
            return fail(restoreError);
        }
    }

    QString finalizationError;
    if (!finalizeCanonicalRewriteImportedWorkspace(database, fingerprint, sourcePath, &finalizationError)) {
        return fail(finalizationError);
    }

        return true;
    });

    // Connection pragmas and attached databases must be restored after the
    // transaction has committed or rolled back, never inside its callback.
    QSqlQuery restoreConnection(database);
    const bool foreignKeysRestored = restoreConnection.exec("PRAGMA foreign_keys = ON");
    const QString restoreError = restoreConnection.lastError().text();
    detachLegacy();
    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    if (!foreignKeysRestored) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite connection restore failed: %1").arg(restoreError);
        return false;
    }
    return true;
}


