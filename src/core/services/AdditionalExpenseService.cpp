#include "infrastructure/db/sqlite/Db.h"
#include "core/services/AdditionalExpenseService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/backup/BackupCommitPublisher.h"
#include "runtime/RuntimeServices.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool finish(QSqlDatabase db, qint64 id, const QString& operation,
            const QJsonObject& data, QString* error)
{
    const QString description = data.value("description").toString();
    const QJsonObject payload{{operation == "add" ? "after" : "before", data}};
    if (!ActivityAuditService::record(db, "expenses", "expense_" + operation,
            QString::fromUtf8("Додаткові витрати: %1").arg(description), 0,
            "additional_expense", QString::number(id), description,
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), error)) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        const QString message = db.lastError().text();
        db.rollback();
        return fail(error, message);
    }
    SnapshotCacheInvalidator::invalidateReportsAndStats("additional_expense");
    if (RuntimeServices::backupManager()) {
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            "reports", "total_expenses", "additional_expense", QString::number(id), {}, 0,
            operation, description, {"additional_expenses", "activity_audit_events"}));
    }
    return true;
}
}

namespace AdditionalExpenseService {
bool ensureSchema(QSqlDatabase db, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS additional_expenses ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, spent_on TEXT NOT NULL,"
                "category TEXT NOT NULL, description TEXT NOT NULL,"
                "amount_cents INTEGER NOT NULL CHECK(amount_cents > 0),"
                "created_at TEXT NOT NULL DEFAULT (datetime('now')))"))
        return fail(error, q.lastError().text());
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_additional_expenses_date ON additional_expenses(spent_on)"))
        return fail(error, q.lastError().text());
    return true;
}

bool canWrite(QSqlDatabase db, bool deleting)
{
    if (!ActivityAuditService::currentActor().isValid()) return true;
    return AccessControlService::currentUserHasPermission(db, "nav.reports")
        && AccessControlService::currentUserHasPermission(db, deleting ? "inventory.delete" : "inventory.purchase");
}

bool add(QSqlDatabase db, const QDate& date, const QString& category,
         const QString& description, double amount, qint64* id, QString* error)
{
    if (!canWrite(db)) return fail(error, QString::fromUtf8("Недостатньо прав для зміни витрат."));
    if (!date.isValid() || category.trimmed().isEmpty() || description.trimmed().isEmpty()
        || category.size() > 120 || description.size() > 1000
        || !std::isfinite(amount) || amount <= 0 || amount > 999999999.99 || qRound64(amount * 100) <= 0)
        return fail(error, QString::fromUtf8("Вкажіть дату, категорію, опис і суму більше нуля."));
    if (!ensureSchema(db, error) || !ActivityAuditService::ensureSchema(db, error)) return false;
    if (!db.transaction()) return fail(error, db.lastError().text());
    QSqlQuery q(db);
    const qint64 cents = qRound64(amount * 100);
    q.prepare("INSERT INTO additional_expenses(spent_on,category,description,amount_cents) VALUES(?,?,?,?)");
    q.addBindValue(date.toString(Qt::ISODate));
    q.addBindValue(category.trimmed());
    q.addBindValue(description.trimmed());
    q.addBindValue(cents);
    if (!q.exec()) { db.rollback(); return fail(error, q.lastError().text()); }
    const qint64 inserted = q.lastInsertId().toLongLong();
    if (!finish(db, inserted, "add", {{"date", date.toString(Qt::ISODate)},
            {"category", category.trimmed()}, {"description", description.trimmed()},
            {"amount_cents", cents}}, error)) return false;
    if (id) *id = inserted;
    return true;
}

bool remove(QSqlDatabase db, qint64 id, QString* error)
{
    if (!canWrite(db, true)) return fail(error, QString::fromUtf8("Недостатньо прав для зміни витрат."));
    if (!ensureSchema(db, error) || !ActivityAuditService::ensureSchema(db, error)) return false;
    if (!db.transaction()) return fail(error, db.lastError().text());
    QSqlQuery q(db);
    q.prepare("SELECT spent_on,category,description,amount_cents FROM additional_expenses WHERE id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next()) {
        db.rollback(); return fail(error, QString::fromUtf8("Запис витрати не знайдено. Оновіть перелік."));
    }
    const QJsonObject data{{"date", q.value(0).toString()}, {"category", q.value(1).toString()},
                          {"description", q.value(2).toString()}, {"amount_cents", q.value(3).toLongLong()}};
    q.finish();
    q.prepare("DELETE FROM additional_expenses WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) { db.rollback(); return fail(error, q.lastError().text()); }
    return finish(db, id, "delete", data, error);
}

QStringList categories(QSqlDatabase db)
{
    QStringList result;
    QSqlQuery q(db);
    if (q.exec("SELECT DISTINCT category FROM additional_expenses ORDER BY category COLLATE NOCASE"))
        while (q.next()) result.append(q.value(0).toString());
    return result;
}
}

namespace AdditionalExpenseService {
bool canWrite(bool deleting) { return canWrite(Database::db(),deleting); }
QStringList categories() { return categories(Database::db()); }
bool add(const QDate& date, const QString& category, const QString& description, double amount, qint64* id, QString* error) { return add(Database::db(),date,category,description,amount,id,error); }
bool remove(qint64 id, QString* error) { return remove(Database::db(),id,error); }
}
