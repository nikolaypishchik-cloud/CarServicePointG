#include "infrastructure/db/sqlite/DbCanonicalRewriteSupport.h"

#include "infrastructure/db/sqlite/DbFacadeSupport.h"

#include <QDataStream>
#include <QIODevice>
#include <QSqlRecord>
#include <QSqlError>
#include <QSqlQuery>

bool loadCanonicalRewriteSourceSchema(QSqlDatabase database, const QString& schemaName,
                                      CanonicalRewriteSourceSchema* schema, QString* errorText)
{
    if (!database.isValid() || !database.isOpen() || !schema || schemaName.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Canonical source schema: invalid connection or output");
        return false;
    }
    CanonicalRewriteSourceSchema staged;
    const auto cols = [&](const QString& tableName, QSet<QString>* columns) {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("PRAGMA %1.table_info(%2)")
                            .arg(sqlQuoteIdent(schemaName), sqlQuoteIdent(tableName)))) {
            if (errorText) *errorText = QStringLiteral("Canonical source schema %1.%2: %3")
                .arg(schemaName, tableName, query.lastError().text());
            return false;
        }
        while (query.next()) columns->insert(query.value(1).toString());
        if (query.lastError().isValid()) {
            if (errorText) *errorText = QStringLiteral("Canonical source schema %1.%2: %3")
                .arg(schemaName, tableName, query.lastError().text());
            return false;
        }
        return true;
    };
    if (!cols(QStringLiteral("masters"), &staged.masters)
        || !cols(QStringLiteral("work_orders"), &staged.orders)
        || !cols(QStringLiteral("work_order_services"), &staged.services)
        || !cols(QStringLiteral("service_masters"), &staged.serviceMasters)
        || !cols(QStringLiteral("work_order_parts"), &staged.parts)
        || !cols(QStringLiteral("settings"), &staged.settings)
        || !cols(QStringLiteral("expenses"), &staged.expenses)
        || !cols(QStringLiteral("planning_calendar"), &staged.planning)
        || !cols(QStringLiteral("parts_catalog"), &staged.catalog)
        || !cols(QStringLiteral("outsource_repairs"), &staged.outsource)) return false;
    *schema = staged;
    return true;
}

bool snapshotWorkspaceSettingsForCanonicalRewrite(QSqlDatabase database,
                                                 QMap<QString, QVariant>* snapshot,
                                                 QString* errorText)
{
    if (!database.isValid() || !database.isOpen() || !snapshot) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite settings snapshot: invalid target or connection");
        return false;
    }
    QMap<QString, QVariant> staged;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT \"key\", value FROM settings WHERE \"key\" NOT LIKE 'db_canonical_rewrite_%'"))) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite settings snapshot: %1").arg(query.lastError().text());
        return false;
    }
    while (query.next()) staged.insert(query.value(0).toString(), query.value(1));
    if (query.lastError().isValid()) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite settings snapshot: %1").arg(query.lastError().text());
        return false;
    }
    *snapshot = staged;
    return true;
}

bool clearCanonicalRewriteWorkspaceTargets(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite target cleanup failed: invalid database");
        return false;
    }

    QSqlQuery q(database);
    const auto fail = [&](const QString& msg) {
        if (errorText) *errorText = msg;
        return false;
    };
    const auto cleanupDelete = [&](const QString& tableName, const char* label, bool required = true) {
        if (!required) {
            QSqlQuery exists(database);
            if (!exists.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?")))
                return fail(QStringLiteral("%1: %2").arg(QString::fromUtf8(label), exists.lastError().text()));
            exists.addBindValue(tableName);
            if (!exists.exec())
                return fail(QStringLiteral("%1: %2").arg(QString::fromUtf8(label), exists.lastError().text()));
            if (!exists.next()) {
                if (exists.lastError().isValid())
                    return fail(QStringLiteral("%1: %2").arg(QString::fromUtf8(label), exists.lastError().text()));
                return true;
            }
        }
        const QString sql = QStringLiteral("DELETE FROM %1").arg(sqlQuoteIdent(tableName));
        if (q.exec(sql)) return true;
        const QString sqlError = q.lastError().text();
        const bool missingTable = sqlError.contains(QStringLiteral("no such table"), Qt::CaseInsensitive);
        if (required && missingTable) {
            return fail(QStringLiteral("%1: missing target table %2").arg(QString::fromUtf8(label), tableName));
        }
        return fail(QStringLiteral("%1: %2").arg(QString::fromUtf8(label), sqlError));
    };

    if (!cleanupDelete(QStringLiteral("service_masters"), "clear service_masters")) return false;
    if (!cleanupDelete(QStringLiteral("work_order_parts"), "clear work_order_parts")) return false;
    if (!cleanupDelete(QStringLiteral("outsource_repairs"), "clear outsource_repairs", false)) return false;
    if (!cleanupDelete(QStringLiteral("work_order_services"), "clear work_order_services")) return false;
    if (!cleanupDelete(QStringLiteral("work_orders"), "clear work_orders")) return false;
    if (!cleanupDelete(QStringLiteral("planning_calendar"), "clear planning_calendar", false)) return false;
    if (!cleanupDelete(QStringLiteral("expenses"), "clear expenses", false)) return false;
    if (!cleanupDelete(QStringLiteral("parts_catalog"), "clear parts_catalog", false)) return false;
    if (!cleanupDelete(QStringLiteral("deleted_imports"), "clear deleted_imports", false)) return false;
    if (!cleanupDelete(QStringLiteral("masters"), "clear masters")) return false;
    if (!cleanupDelete(QStringLiteral("settings"), "clear settings")) return false;
    if (!cleanupDelete(QStringLiteral("order_registry"), "clear order_registry", false)) return false;
    if (!cleanupDelete(QStringLiteral("workspace_order_state"), "clear workspace_order_state", false)) return false;
    if (!cleanupDelete(QStringLiteral("router_cache_service_masters"), "clear router_cache_service_masters", false)) return false;
    if (!cleanupDelete(QStringLiteral("router_cache_work_order_parts"), "clear router_cache_work_order_parts", false)) return false;
    if (!cleanupDelete(QStringLiteral("router_cache_work_order_services"), "clear router_cache_work_order_services", false)) return false;
    if (!cleanupDelete(QStringLiteral("router_cache_work_orders"), "clear router_cache_work_orders", false)) return false;
    return true;
}

namespace {
QByteArray migrationRecordBytes(const QMap<QString, QVariant>& values)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << values;
    return bytes;
}
}

bool recordCanonicalRewriteCorrespondence(QSqlDatabase database, const QString& sourceIdentity,
    const QString& sourceSchema, const CanonicalRewriteSourceSchema& schema, QString* errorText, const QMap<qint64,qint64>& orderKeys)
{
    const auto fail = [&](const QString& message) {
        if (errorText) *errorText = QStringLiteral("Canonical correspondence: ") + message;
        return false;
    };
    if (!database.isOpen() || sourceIdentity.isEmpty() || sourceSchema.isEmpty())
        return fail(QStringLiteral("invalid database or source identity"));
    QSqlQuery ddl(database);
    if (!ddl.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS migration_record_map ("
        "source_identity TEXT NOT NULL, source_table TEXT NOT NULL, source_key BLOB NOT NULL, "
        "target_table TEXT NOT NULL, target_key BLOB NOT NULL, target_order_uid TEXT NOT NULL, "
        "source_payload BLOB NOT NULL, payload_format TEXT NOT NULL, "
        "PRIMARY KEY(source_identity,source_table,source_key))"))) return fail(ddl.lastError().text());
    struct Table { QString name; QSet<QString> columns; QStringList keys; };
    const QList<Table> tables = {
        {"masters", schema.masters, {"id"}}, {"work_orders", schema.orders, {"number"}},
        {"work_order_services", schema.services, {"id"}}, {"work_order_parts", schema.parts, {"id"}},
        {"service_masters", schema.serviceMasters, {"service_id", "master_id", "role"}},
        {"outsource_repairs", schema.outsource, {"id"}}, {"parts_catalog", schema.catalog, {"id"}},
        {"expenses", schema.expenses, {"id"}}, {"planning_calendar", schema.planning, {"day"}},
        {"settings", schema.settings, {"key"}}
    };
    for (const auto& table : tables) {
        if (table.columns.isEmpty()) continue;
        const bool rowIdKey = table.keys == QStringList{"id"} && !table.columns.contains("id")
            && table.name != "masters";
        QSqlQuery source(database);
        const QString extra = rowIdKey ? QStringLiteral(", rowid AS __migration_source_rowid") : QString();
        if (!source.exec(QStringLiteral("SELECT *%1 FROM %2.%3").arg(extra,
            sqlQuoteIdent(sourceSchema), sqlQuoteIdent(table.name)))) return fail(source.lastError().text());
        QSet<QByteArray> seenKeys;
        while (source.next()) {
            QMap<QString, QVariant> payload, key, targetKey;
            const auto record = source.record();
            for (int i = 0; i < record.count(); ++i) payload.insert(record.fieldName(i), source.value(i));
            if (table.name == "settings" && payload.value("key").toString().startsWith("db_canonical_rewrite_"))
                continue; // Control metadata belongs to the current run, not imported settings.
            QStringList predicates;
            for (const auto& name : table.keys) {
                QVariant value = payload.value(name);
                if (name == "id" && rowIdKey) value = payload.value("__migration_source_rowid");
                if (name == "role" && !table.columns.contains(name)) value = QStringLiteral("master");
                if (name != "role" && (!value.isValid() || value.isNull()))
                    return fail(table.name + ": missing key " + name);
                key.insert(name, value);
                if (name == "role") {
                    const QString role = value.toString().trimmed();
                    value = role.isEmpty() ? QStringLiteral("master") : role;
                }
                if (table.name=="work_orders" && name=="number" && orderKeys.contains(value.toLongLong()))
                    value=orderKeys.value(value.toLongLong());
                targetKey.insert(name, value);
                predicates << sqlQuoteIdent(name) + " = ?";
            }
            QSqlQuery target(database);
            if (!target.prepare(QStringLiteral("SELECT * FROM main.%1 WHERE %2")
                .arg(sqlQuoteIdent(table.name), predicates.join(" AND ")))) return fail(target.lastError().text());
            for (const auto& name : table.keys) target.addBindValue(targetKey.value(name));
            if (!target.exec()) return fail(target.lastError().text());
            if (!target.next()) return fail(table.name + ": source record has no target: " + target.lastError().text());
            const auto targetRecord = target.record();
            const QString targetUid = targetRecord.contains("order_uid") ? target.value("order_uid").toString() : QString();
            if (table.name == "work_orders") {
                if (targetUid.isEmpty()) return fail("work_orders: empty target UID");
                const QString sourceUid = payload.value("order_uid").toString().trimmed();
                if (!sourceUid.isEmpty() && sourceUid != targetUid)
                    return fail("work_orders: source identity changed without correspondence resolution");
            }
            if (table.name == "work_order_services" || table.name == "work_order_parts" || table.name == "outsource_repairs") {
                const auto sourceOwner=payload.value("order_uid").toString();
                if (!sourceOwner.isEmpty() && sourceOwner!=targetUid) return fail(table.name+": source child owner differs");
                const QString childId = table.name=="work_order_parts" ? QStringLiteral("part_uid") :
                    (table.name=="work_order_services" ? QStringLiteral("service_uid") : QStringLiteral("outsource_uid"));
                const auto uid=payload.value(childId).toString();
                if (!uid.isEmpty() && target.value(childId).toString()!=uid) return fail(table.name+": child UID changed");
                QVariant parent;
                for (const auto& alias : QStringList{"order_number", "work_order", "work_order_id", "work_order_number"}) {
                    if (table.columns.contains(alias)) { parent = payload.value(alias); break; }
                }
                QSqlQuery owner(database);
                if (!owner.prepare("SELECT order_uid FROM main.work_orders WHERE number=?")) return fail(owner.lastError().text());
                if (orderKeys.contains(parent.toLongLong())) parent=orderKeys.value(parent.toLongLong());
                owner.addBindValue(parent);
                if (!owner.exec() || !owner.next()) return fail(table.name + ": missing parent");
                if (target.value("order_number").toLongLong() != parent.toLongLong()
                    || targetUid.isEmpty() || targetUid != owner.value(0).toString())
                    return fail(table.name + ": target ownership mismatch");
            }
            if (target.next()) return fail(table.name + ": ambiguous target key");
            if (target.lastError().isValid()) return fail(target.lastError().text());
            const QByteArray keyBytes = migrationRecordBytes(key);
            if (seenKeys.contains(keyBytes)) return fail(table.name + ": duplicate source key requires resolution");
            seenKeys.insert(keyBytes);
            const QByteArray targetBytes = migrationRecordBytes(targetKey);
            const QByteArray payloadBytes = migrationRecordBytes(payload);
            QSqlQuery existing(database);
            if (!existing.prepare("SELECT target_table,target_key,target_order_uid,source_payload,payload_format "
                "FROM migration_record_map WHERE source_identity=? AND source_table=? AND source_key=?"))
                return fail(existing.lastError().text());
            existing.addBindValue(sourceIdentity); existing.addBindValue(table.name); existing.addBindValue(keyBytes);
            if (!existing.exec()) return fail(existing.lastError().text());
            if (existing.next()) {
                if (existing.value(0).toString() != table.name || existing.value(1).toByteArray() != targetBytes
                    || existing.value(2).toString() != targetUid || existing.value(3).toByteArray() != payloadBytes
                    || existing.value(4).toString() != "QDataStream.Qt_6_0.QMapQStringQVariant")
                    return fail(table.name + ": existing correspondence differs; source identity cannot be reused");
                continue;
            }
            if (existing.lastError().isValid()) return fail(existing.lastError().text());
            QSqlQuery mapping(database);
            if (!mapping.prepare("INSERT INTO migration_record_map "
                "(source_identity,source_table,source_key,target_table,target_key,target_order_uid,source_payload,payload_format) "
                "VALUES (?,?,?,?,?,?,?,'QDataStream.Qt_6_0.QMapQStringQVariant')")) return fail(mapping.lastError().text());
            mapping.addBindValue(sourceIdentity); mapping.addBindValue(table.name); mapping.addBindValue(keyBytes);
            mapping.addBindValue(table.name); mapping.addBindValue(targetBytes); mapping.addBindValue(targetUid.isNull() ? QStringLiteral("") : targetUid);
            mapping.addBindValue(payloadBytes);
            if (!mapping.exec()) return fail(mapping.lastError().text());
        }
        if (source.lastError().isValid()) return fail(source.lastError().text());
    }
    return true;
}
