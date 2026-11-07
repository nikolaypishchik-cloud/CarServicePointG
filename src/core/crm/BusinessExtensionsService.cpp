#include "core/crm/BusinessExtensionsService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/services/MutationExecutor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace {

bool execPrepared(QSqlQuery& q, QString* errorText)
{
    if (q.exec()) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

QString text(const QVariantMap& payload, const QString& key)
{
    return payload.value(key).toString().trimmed();
}

double number(const QVariantMap& payload, const QString& key)
{
    return payload.value(key).toDouble();
}

int integer(const QVariantMap& payload, const QString& key)
{
    return payload.value(key).toInt();
}

QString compactJson(const QVariantMap& payload, const QString& key)
{
    const QVariant value = payload.value(key);
    if (value.toString().trimmed().startsWith(QLatin1Char('{'))) return value.toString().trimmed();
    return QString::fromUtf8(QJsonDocument::fromVariant(value.isValid() ? value : QVariantMap{}).toJson(QJsonDocument::Compact));
}

bool safeIdentifier(const QString& identifier)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(identifier).hasMatch();
}

bool tableExists(QSqlDatabase db, const QString& table)
{
    if (!safeIdentifier(table)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    if (!safeIdentifier(table) || !safeIdentifier(column)) return false;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool upsertEstimateApprovalDirect(QSqlDatabase db, const QString& uid, const QVariantMap& p, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO estimate_approvals(approval_uid,order_uid,client_uid,vehicle_uid,estimate_json,status,decision_comment,approved_at,declined_at,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,datetime('now')) "
        "ON CONFLICT(approval_uid) DO UPDATE SET order_uid=excluded.order_uid, client_uid=excluded.client_uid, vehicle_uid=excluded.vehicle_uid, estimate_json=excluded.estimate_json, status=excluded.status, decision_comment=excluded.decision_comment, approved_at=excluded.approved_at, declined_at=excluded.declined_at, updated_at=datetime('now')"));
    q.addBindValue(uid);
    q.addBindValue(text(p, QStringLiteral("order_uid")));
    q.addBindValue(text(p, QStringLiteral("client_uid")));
    q.addBindValue(text(p, QStringLiteral("vehicle_uid")));
    q.addBindValue(compactJson(p, QStringLiteral("estimate_json")));
    q.addBindValue(text(p, QStringLiteral("status")).isEmpty() ? QStringLiteral("pending") : text(p, QStringLiteral("status")));
    q.addBindValue(text(p, QStringLiteral("decision_comment")));
    q.addBindValue(text(p, QStringLiteral("approved_at")));
    q.addBindValue(text(p, QStringLiteral("declined_at")));
    return execPrepared(q, errorText);
}

bool upsertSupplierQuoteDirect(QSqlDatabase db, const QString& uid, const QVariantMap& p, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO supplier_price_quotes(quote_uid,supplier_code,part_number,brand,name,price,currency,available_qty,status,raw_payload_json,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,datetime('now')) "
        "ON CONFLICT(quote_uid) DO UPDATE SET supplier_code=excluded.supplier_code, part_number=excluded.part_number, brand=excluded.brand, name=excluded.name, price=excluded.price, currency=excluded.currency, available_qty=excluded.available_qty, status=excluded.status, raw_payload_json=excluded.raw_payload_json, updated_at=datetime('now')"));
    q.addBindValue(uid);
    q.addBindValue(text(p, QStringLiteral("supplier_code")));
    q.addBindValue(text(p, QStringLiteral("part_number")));
    q.addBindValue(text(p, QStringLiteral("brand")));
    q.addBindValue(text(p, QStringLiteral("name")));
    q.addBindValue(number(p, QStringLiteral("price")));
    q.addBindValue(text(p, QStringLiteral("currency")));
    q.addBindValue(integer(p, QStringLiteral("available_qty")));
    q.addBindValue(text(p, QStringLiteral("status")).isEmpty() ? QStringLiteral("cached") : text(p, QStringLiteral("status")));
    q.addBindValue(compactJson(p, QStringLiteral("raw_payload_json")));
    return execPrepared(q, errorText);
}

bool queueCommunicationDirect(QSqlDatabase db, const QString& uid, const QVariantMap& p, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO communication_messages(message_uid,client_uid,order_uid,channel,recipient,subject,body,status,provider,last_error) "
        "VALUES(?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(message_uid) DO UPDATE SET status=excluded.status, last_error=excluded.last_error, provider=excluded.provider"));
    q.addBindValue(uid);
    q.addBindValue(text(p, QStringLiteral("client_uid")));
    q.addBindValue(text(p, QStringLiteral("order_uid")));
    q.addBindValue(text(p, QStringLiteral("channel")));
    q.addBindValue(text(p, QStringLiteral("recipient")));
    q.addBindValue(text(p, QStringLiteral("subject")));
    q.addBindValue(text(p, QStringLiteral("body")));
    q.addBindValue(text(p, QStringLiteral("status")).isEmpty() ? QStringLiteral("queued") : text(p, QStringLiteral("status")));
    q.addBindValue(text(p, QStringLiteral("provider")));
    q.addBindValue(text(p, QStringLiteral("last_error")));
    return execPrepared(q, errorText);
}

} // namespace

namespace BusinessExtensionsService {

bool upsertEstimateApproval(QSqlDatabase db, const QVariantMap& payload, QString* approvalUid, QString* errorText)
{
    const QString uid = text(payload, QStringLiteral("approval_uid")).isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("approval"))
        : text(payload, QStringLiteral("approval_uid"));
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("BusinessExtensionsService");
    request.entityType = QStringLiteral("estimate_approval");
    request.entityId = uid;
    request.operation = QStringLiteral("upsert");
    request.description = QString::fromUtf8("Оновлення погодження кошторису");
    request.changedTables = {QStringLiteral("estimate_approvals")};
    request.systemMaintenance = true;
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return upsertEstimateApprovalDirect(txDb, uid, payload, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    if (result.ok && approvalUid) *approvalUid = uid;
    return result.ok;
}

bool decideEstimateApproval(QSqlDatabase db, const QString& approvalUid, const QString& status, const QString& comment, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("BusinessExtensionsService");
    request.entityType = QStringLiteral("estimate_approval");
    request.entityId = approvalUid;
    request.operation = QStringLiteral("decision");
    request.description = QString::fromUtf8("Рішення клієнта щодо кошторису");
    request.changedTables = {QStringLiteral("estimate_approvals")};
    request.systemMaintenance = true;
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        if (status == QStringLiteral("approved")) {
            q.prepare(QStringLiteral("UPDATE estimate_approvals SET status=?, decision_comment=?, approved_at=datetime('now'), updated_at=datetime('now') WHERE approval_uid=?"));
        } else if (status == QStringLiteral("declined")) {
            q.prepare(QStringLiteral("UPDATE estimate_approvals SET status=?, decision_comment=?, declined_at=datetime('now'), updated_at=datetime('now') WHERE approval_uid=?"));
        } else {
            if (txError) *txError = QStringLiteral("Unsupported approval status");
            return false;
        }
        q.addBindValue(status);
        q.addBindValue(comment);
        q.addBindValue(approvalUid);
        return execPrepared(q, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool upsertSupplierQuote(QSqlDatabase db, const QVariantMap& payload, QString* quoteUid, QString* errorText)
{
    const QString uid = text(payload, QStringLiteral("quote_uid")).isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("quote"))
        : text(payload, QStringLiteral("quote_uid"));
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("BusinessExtensionsService");
    request.entityType = QStringLiteral("supplier_quote");
    request.entityId = uid;
    request.operation = QStringLiteral("upsert");
    request.description = QString::fromUtf8("Кешування пропозиції постачальника");
    request.changedTables = {QStringLiteral("supplier_price_quotes")};
    request.systemMaintenance = true;
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return upsertSupplierQuoteDirect(txDb, uid, payload, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    if (result.ok && quoteUid) *quoteUid = uid;
    return result.ok;
}

bool queueCommunication(QSqlDatabase db, const QVariantMap& payload, QString* messageUid, QString* errorText)
{
    const QString uid = text(payload, QStringLiteral("message_uid")).isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("msg"))
        : text(payload, QStringLiteral("message_uid"));
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("BusinessExtensionsService");
    request.entityType = QStringLiteral("communication_message");
    request.entityId = uid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Постановка комунікації в чергу");
    request.changedTables = {QStringLiteral("communication_messages")};
    request.systemMaintenance = true;
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return queueCommunicationDirect(txDb, uid, payload, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    if (result.ok && messageUid) *messageUid = uid;
    return result.ok;
}

bool recordAuditEvent(QSqlDatabase db, const QString& action, const QString& entityType, const QString& entityUid, const QVariantMap& details, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO crm_audit_events(audit_uid,actor,actor_user_uid,actor_login,actor_role,action,entity_type,entity_uid,before_json,after_json,details_json,source) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(EntityUidService::generateUid(QStringLiteral("audit")));
    q.addBindValue(QStringLiteral("desktop"));
    q.addBindValue(text(details, QStringLiteral("actor_user_uid")));
    q.addBindValue(text(details, QStringLiteral("actor_login")));
    q.addBindValue(text(details, QStringLiteral("actor_role")));
    q.addBindValue(action.trimmed());
    q.addBindValue(entityType.trimmed());
    q.addBindValue(entityUid.trimmed());
    q.addBindValue(compactJson(details, QStringLiteral("before")));
    q.addBindValue(compactJson(details, QStringLiteral("after")));
    q.addBindValue(QString::fromUtf8(QJsonDocument::fromVariant(details).toJson(QJsonDocument::Compact)));
    if (q.exec()) return true;
    if (errorText) *errorText = q.lastError().text();
    return true; // Audit is best-effort and must not block local saves.
}

bool upsertMappedEntity(QSqlDatabase db, const QString& table, const QString& uidColumn, const QString& uid, const QStringList& writableFields, const QVariantMap& payload, QString* errorText)
{
    if (!safeIdentifier(table) || !safeIdentifier(uidColumn) || !tableExists(db, table) || !columnExists(db, table, uidColumn)) {
        if (errorText) *errorText = QStringLiteral("Unsupported mapped entity target");
        return false;
    }
    QStringList columns{uidColumn};
    QList<QVariant> values{uid};
    for (const QString& field : writableFields) {
        if (!safeIdentifier(field) || !columnExists(db, table, field) || !payload.contains(field)) continue;
        columns.append(field);
        values.append(payload.value(field));
    }
    QStringList assignments;
    for (const QString& column : columns) {
        if (column != uidColumn) assignments.append(QStringLiteral("%1=excluded.%1").arg(column));
    }
    QStringList placeholders;
    for (int i = 0; i < columns.size(); ++i) placeholders.append(QStringLiteral("?"));
    QSqlQuery q(db);
    if (assignments.isEmpty()) {
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO %1(%2) VALUES(%3)")
            .arg(table, columns.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))));
    } else {
        q.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3) ON CONFLICT(%4) DO UPDATE SET %5")
            .arg(table, columns.join(QLatin1Char(',')), placeholders.join(QLatin1Char(',')), uidColumn, assignments.join(QLatin1Char(','))));
    }
    for (const QVariant& value : values) q.addBindValue(value);
    return execPrepared(q, errorText);
}

} // namespace BusinessExtensionsService
