#include "infrastructure/audit/MutationAuditTrail.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
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
    return QDir(auditDirPath()).filePath(QStringLiteral("mutation_audit.jsonl"));
}
} // namespace

namespace MutationAuditTrail {

void appendEvent(const MutationResult& result, const QString& status)
{
    QFile file(auditFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    obj.insert(QStringLiteral("status"), status);
    obj.insert(QStringLiteral("ok"), result.ok);
    obj.insert(QStringLiteral("operation_id"), result.operationId);
    obj.insert(QStringLiteral("source_module"), result.sourceModule);
    obj.insert(QStringLiteral("entity_type"), result.entityType);
    obj.insert(QStringLiteral("entity_id"), result.entityId);
    obj.insert(QStringLiteral("operation"), result.operation);
    obj.insert(QStringLiteral("description"), result.description);
    obj.insert(QStringLiteral("error_text"), result.errorText);

    QJsonArray tables;
    for (const QString& table : result.changedTables) {
        tables.append(table);
    }
    obj.insert(QStringLiteral("changed_tables"), tables);

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.write("\n");
}

} // namespace MutationAuditTrail
