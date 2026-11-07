#include "infrastructure/audit/BackupRecoveryAuditTrail.h"

#include "AppVersion.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include <QSqlDatabase>
#include <QSqlQuery>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>

namespace {
QString currentSchemaVersion()
{
    // Audit must not materialize/reopen a database while backup or restore owns it.
    const QString name = currentConnectionName();
    if (!QSqlDatabase::contains(name) || !DbThreadConnectionSupport::isConnectionOwnedByCurrentThread(name))
        return QStringLiteral("runtime");
    const auto db = QSqlDatabase::database(name, false);
    if (!db.isOpen()) return QStringLiteral("runtime");
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT value FROM settings WHERE key='app.schema.version'")) || !query.next())
        return QStringLiteral("runtime");
    return query.value(0).toString();
}

QString auditDirPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.trimmed().isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/CarServicePointG");
    }
    QDir dir(base);
    dir.mkpath(QStringLiteral("logs"));
    return dir.filePath(QStringLiteral("logs"));
}

QString auditFilePath()
{
    return QDir(auditDirPath()).filePath(QStringLiteral("backup_recovery_audit.jsonl"));
}

void appendEvent(const QString& category,
                 const QString& status,
                 const QString& operation,
                 const QString& operationId,
                 const QString& sourceModule,
                 const QString& entityType,
                 const QString& entityId,
                 const QString& message,
                 const QString& errorText,
                 const QStringList& changedTables)
{
    QFile file(auditFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    obj.insert(QStringLiteral("category"), category);
    obj.insert(QStringLiteral("status"), status);
    obj.insert(QStringLiteral("operation"), operation);
    obj.insert(QStringLiteral("operation_id"), operationId);
    obj.insert(QStringLiteral("source_module"), sourceModule);
    obj.insert(QStringLiteral("entity_type"), entityType);
    obj.insert(QStringLiteral("entity_id"), entityId);
    obj.insert(QStringLiteral("message"), message);
    obj.insert(QStringLiteral("error_text"), errorText);
    obj.insert(QStringLiteral("app_version"), CarService::AppVersion::versionString());
    obj.insert(QStringLiteral("schema_version"), currentSchemaVersion());
    obj.insert(QStringLiteral("db_path"), AppRuntime::activeDbFilePath());
    obj.insert(QStringLiteral("backup_path"), message.contains(QStringLiteral("snapshot"), Qt::CaseInsensitive) ? entityId : QString());
    obj.insert(QStringLiteral("thread"), QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    obj.insert(QStringLiteral("duration_ms"), QString());
    obj.insert(QStringLiteral("retry_count"), QString());

    QJsonArray tables;
    for (const QString& table : changedTables) {
        tables.append(table);
    }
    obj.insert(QStringLiteral("changed_tables"), tables);

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");
}
} // namespace

namespace BackupRecoveryAuditTrail {

void appendBackupEvent(const QString& status,
                       const QString& operation,
                       const QString& operationId,
                       const QString& sourceModule,
                       const QString& entityType,
                       const QString& entityId,
                       const QString& message,
                       const QString& errorText,
                       const QStringList& changedTables)
{
    appendEvent(QStringLiteral("backup"), status, operation, operationId, sourceModule, entityType, entityId, message, errorText, changedTables);
}

void appendRecoveryEvent(const QString& status,
                         const QString& operation,
                         const QString& operationId,
                         const QString& sourceModule,
                         const QString& entityType,
                         const QString& entityId,
                         const QString& message,
                         const QString& errorText)
{
    appendEvent(QStringLiteral("recovery"), status, operation, operationId, sourceModule, entityType, entityId, message, errorText, {});
}

} // namespace BackupRecoveryAuditTrail
