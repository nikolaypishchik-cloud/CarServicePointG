#include "core/services/legacyimport/LegacyImportAuditSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

#include "core/services/legacyimport/LegacyImportDetailedAuditSupport.h"
#include "core/services/legacyimport/LegacyImportAncillaryAuditSupport.h"
#include "core/services/legacyimport/LegacyImportSchemaMapSupport.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace {
struct EntitySpec {
    QString entityName;
    QString tableName;
    bool required = false;
    QStringList requiredColumns;
};

QString buildFingerprint(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("open-error|%1|%2")
            .arg(QFileInfo(path).absoluteFilePath())
            .arg(QFileInfo(path).size());
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk;
    chunk.resize(1024 * 1024);
    while (true) {
        const qint64 read = file.read(chunk.data(), chunk.size());
        if (read < 0) {
            return QStringLiteral("read-error|%1|%2")
                .arg(QFileInfo(path).absoluteFilePath())
                .arg(QFileInfo(path).size());
        }
        if (read == 0) break;
        hash.addData(chunk.constData(), read);
    }
    const QFileInfo fi(path);
    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(QString::fromLatin1(hash.result().toHex()));
}

bool tableExists(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& tableName)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return cols;
    }
    while (q.next()) {
        cols.insert(q.value(1).toString().trimmed());
    }
    return cols;
}

int rowCount(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

bool parseJsonFile(const QString& path, QString* errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || doc.isNull()) {
        if (errorText) *errorText = parseError.errorString();
        return false;
    }
    return true;
}
}

bool LegacyImportAuditSupport::runAudit(const LegacyImportBundle& bundle, LegacyImportAuditReport* report, QString* errorText)
{
    LegacyImportAuditReport localReport;
    localReport.sourceKind = bundle.sourceKind;
    localReport.sourcePath = bundle.sourcePath;
    localReport.sourceDbPath = bundle.primaryDbPath;
    localReport.sourceResolved = !bundle.primaryDbPath.trimmed().isEmpty();
    localReport.schemaMap = LegacyImportSchemaMapSupport::buildSchemaMap();

    if (!localReport.sourceResolved) {
        localReport.errorMessages << QStringLiteral("Не знайдено primary legacy DB у bundle");
        if (report) *report = localReport;
        if (errorText) *errorText = localReport.errorMessages.constFirst();
        return false;
    }

    const QFileInfo dbFi(bundle.primaryDbPath);
    if (!dbFi.exists()) {
        localReport.errorMessages << QStringLiteral("Primary legacy DB не існує");
        if (report) *report = localReport;
        if (errorText) *errorText = localReport.errorMessages.constFirst();
        return false;
    }

    localReport.sourceFingerprint = buildFingerprint(bundle.primaryDbPath);
    if (!bundle.settingsJsonPath.trimmed().isEmpty()) localReport.detectedSidecars << QStringLiteral("settings.json");
    if (!bundle.uiStateJsonPath.trimmed().isEmpty()) localReport.detectedSidecars << QStringLiteral("ui_state.json");
    if (!bundle.appLogPath.trimmed().isEmpty()) localReport.detectedSidecars << QStringLiteral("app.log");
    if (!bundle.snapshotDbPath.trimmed().isEmpty()) localReport.detectedSidecars << QStringLiteral("_db_snapshot_tmp.db");

    const QString connName = QStringLiteral("legacy_import_audit_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(bundle.primaryDbPath);
        if (!db.open()) {
            localReport.errorMessages << db.lastError().text();
        } else {
            QSqlQuery integrity(db);
            if (!integrity.exec(QStringLiteral("PRAGMA integrity_check")) || !integrity.next()) {
                localReport.errorMessages << QStringLiteral("PRAGMA integrity_check не виконано: %1").arg(integrity.lastError().text());
            } else {
                const QString integrityText = integrity.value(0).toString().trimmed();
                localReport.sqliteIntegrityOk = (integrityText.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0);
                if (!localReport.sqliteIntegrityOk) {
                    localReport.errorMessages << QStringLiteral("Legacy DB integrity_check повернув: %1").arg(integrityText);
                }
            }

            const QVector<EntitySpec> specs = {
                {QStringLiteral("orders"), QStringLiteral("work_orders"), true, {QStringLiteral("number")}},
                {QStringLiteral("services"), QStringLiteral("work_order_services"), true, {QStringLiteral("id"), QStringLiteral("order_number")}},
                {QStringLiteral("parts"), QStringLiteral("work_order_parts"), true, {QStringLiteral("id")}},
                {QStringLiteral("service_masters"), QStringLiteral("service_masters"), false, {QStringLiteral("service_id"), QStringLiteral("master_id")}},
                {QStringLiteral("personnel"), QStringLiteral("masters"), true, {QStringLiteral("id"), QStringLiteral("name")}},
                {QStringLiteral("catalog"), QStringLiteral("parts_catalog"), false, {}},
                {QStringLiteral("planning"), QStringLiteral("planning_calendar"), false, {QStringLiteral("day"), QStringLiteral("note")}},
                {QStringLiteral("outsource"), QStringLiteral("outsource_repairs"), false, {}},
                {QStringLiteral("expenses"), QStringLiteral("expenses"), false, {}}
            };

            for (const EntitySpec& spec : specs) {
                LegacyImportEntityAudit entity;
                entity.entityName = spec.entityName;
                entity.sourceTable = spec.tableName;
                entity.required = spec.required;
                entity.present = tableExists(db, spec.tableName);
                if (entity.present) {
                    const QSet<QString> cols = tableColumns(db, spec.tableName);
                    entity.rowCount = rowCount(db, spec.tableName);
                    for (const QString& column : spec.requiredColumns) {
                        if (!cols.contains(column)) {
                            entity.missingColumns << column;
                        }
                    }
                }
                if (!entity.present && entity.required) {
                    localReport.errorMessages << QStringLiteral("У legacy DB відсутня обов’язкова таблиця %1").arg(spec.tableName);
                }
                if (entity.present && !entity.missingColumns.isEmpty() && entity.required) {
                    localReport.errorMessages << QStringLiteral("Обов’язкова таблиця %1 не містить потрібних полів: %2")
                        .arg(spec.tableName, entity.missingColumns.join(QStringLiteral(", ")));
                }
                localReport.entities.push_back(entity);
            }

            for (const LegacyImportEntityAudit& entity : localReport.entities) {
                if (!entity.present) continue;
                if (entity.sourceTable == QStringLiteral("work_orders")) localReport.orderCount = qMax(0, entity.rowCount);
                if (entity.sourceTable == QStringLiteral("work_order_services")) localReport.serviceCount = qMax(0, entity.rowCount);
                if (entity.sourceTable == QStringLiteral("work_order_parts")) localReport.partCount = qMax(0, entity.rowCount);
            }
            LegacyImportDetailedAuditSupport::appendDetailedFindings(db, &localReport);
            LegacyImportAncillaryAuditSupport::appendTableFindings(db, &localReport);

            for (const LegacyImportEntitySchemaMap& map : localReport.schemaMap) {
                const bool present = tableExists(db, map.sourceTable);
                if (!present) continue;
                const QSet<QString> cols = tableColumns(db, map.sourceTable);
                QStringList missingMappedRequired;
                for (const LegacyImportFieldMap& field : map.fieldMaps) {
                    if (!field.required) continue;
                    bool matched = field.sourceCandidates.isEmpty();
                    for (const QString& candidate : field.sourceCandidates) {
                        if (cols.contains(candidate)) { matched = true; break; }
                    }
                    if (!matched) missingMappedRequired << field.targetField;
                }
                if (!missingMappedRequired.isEmpty()) {
                    localReport.errorMessages << QStringLiteral("Таблиця %1 не покриває обов’язкові mapped поля для %2: %3")
                        .arg(map.sourceTable, map.entityName, missingMappedRequired.join(QStringLiteral(", ")));
                }
            }

            if (localReport.orderCount <= 0) {
                localReport.errorMessages << QStringLiteral("У legacy DB немає жодного наряду для імпорту");
            }
        }
        db.close();
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);

    if (bundle.settingsJsonPath.trimmed().isEmpty()) {
        localReport.warningMessages << QStringLiteral("У bundle не знайдено settings.json");
    } else {
        QString jsonError;
        if (!parseJsonFile(bundle.settingsJsonPath, &jsonError)) {
            localReport.warningMessages << QStringLiteral("settings.json знайдено, але JSON не читається: %1").arg(jsonError);
        }
    }

    if (bundle.uiStateJsonPath.trimmed().isEmpty()) {
        localReport.warningMessages << QStringLiteral("У bundle не знайдено ui_state.json");
    } else {
        QString jsonError;
        if (!parseJsonFile(bundle.uiStateJsonPath, &jsonError)) {
            localReport.warningMessages << QStringLiteral("ui_state.json знайдено, але JSON не читається: %1").arg(jsonError);
        }
    }

    LegacyImportAncillaryAuditSupport::appendBundleFindings(bundle, &localReport);

    if (bundle.appLogPath.trimmed().isEmpty()) {
        localReport.infoMessages << QStringLiteral("app.log у bundle відсутній; це не блокує імпорт");
    }
    if (bundle.snapshotDbPath.trimmed().isEmpty()) {
        localReport.infoMessages << QStringLiteral("_db_snapshot_tmp.db у bundle відсутній; використовується primary legacy DB");
    } else if (QFileInfo(bundle.snapshotDbPath).absoluteFilePath() != QFileInfo(bundle.primaryDbPath).absoluteFilePath()) {
        const QString snapshotFingerprint = buildFingerprint(bundle.snapshotDbPath);
        if (snapshotFingerprint == localReport.sourceFingerprint) {
            localReport.infoMessages << QStringLiteral("_db_snapshot_tmp.db збігається з primary legacy DB");
        } else {
            localReport.warningMessages << QStringLiteral("_db_snapshot_tmp.db відрізняється від primary legacy DB; для execute mode буде використано primary legacy DB");
        }
    }

    bool hasBlockingFinding = false;
    for (const LegacyImportFinding& finding : localReport.findings) {
        if (finding.blocksExecute) {
            hasBlockingFinding = true;
            break;
        }
    }

    localReport.canExecute = localReport.sqliteIntegrityOk && localReport.errorMessages.isEmpty() && !hasBlockingFinding;
    if (!localReport.canExecute && localReport.errorMessages.isEmpty()) {
        localReport.errorMessages << QStringLiteral("Legacy bundle не пройшов pre-import audit");
    }

    if (report) *report = localReport;
    if (!localReport.canExecute && errorText) *errorText = localReport.errorMessages.constFirst();
    return localReport.canExecute;
}
