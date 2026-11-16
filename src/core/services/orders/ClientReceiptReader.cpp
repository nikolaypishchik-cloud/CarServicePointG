#include "core/services/orders/ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
#include "core/services/AccessControlService.h"
#include <QSqlQuery>
#include <QSqlError>

namespace ClientReceiptReader {
bool beginTrackingNewOrder(QSqlDatabase db, const QString& uid, QString* error) {
    QSqlQuery q(db);
    if (q.exec("CREATE TABLE IF NOT EXISTS main.client_receipt_tracking(order_uid TEXT PRIMARY KEY)")
        && q.prepare("INSERT INTO main.client_receipt_tracking(order_uid) VALUES(?)")) {
        q.addBindValue(uid);
        if (q.exec()) return true;
    }
    if (error) *error = q.lastError().text();
    return false;
}
Result read(QSqlDatabase db, const QSet<QString>& orderUids) {
    auto fail=[](const QString& error) { Result result; result.error=error; return result; };
    if (!db.isValid() || !db.isOpen()) return fail(QStringLiteral("З'єднання з БД недоступне."));
    for (const auto& uid:orderUids)
        if (uid.trimmed().isEmpty()) return fail(QStringLiteral("Відсутній UID наряду для клієнтських оплат."));
    QSqlQuery query(db);
    if (!query.exec("SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='client_payments'"))
        return fail(query.lastError().text());
    Result result;
    if (!query.next()) {
        if (query.lastError().isValid()) return fail(query.lastError().text());
        return result;
    }
    result.available=true;
    if (orderUids.isEmpty()) return result;
    // One statement provides a consistent receipt snapshot; never SQL SUM on unchecked values.
    QString sql = "SELECT order_uid,amount_cents,typeof(amount_cents) FROM main.client_payments";
    if (orderUids.size() == 1) sql += " WHERE order_uid=?";
    if (!query.prepare(sql)) return fail(query.lastError().text());
    if (orderUids.size() == 1) query.addBindValue(*orderUids.cbegin());
    if (!query.exec())
        return fail(query.lastError().text());
    while (query.next()) {
        const auto uid=query.value(0).toString();
        if (!orderUids.contains(uid)) continue;
        bool valid=false;
        const qint64 cents=query.value(1).toLongLong(&valid);
        if (!valid || query.value(2).toString()!="integer" || cents<=0)
            return fail(QStringLiteral("Некоректна сума клієнтської оплати наряду %1.").arg(uid));
        const auto sum=ExactMoney::sum({result.centsByOrder.value(uid),cents});
        if (const auto* value=std::get_if<ExactMoney::Cents>(&sum)) result.centsByOrder.insert(uid,*value);
        else return fail(QStringLiteral("Переповнення клієнтських оплат наряду %1.").arg(uid));
    }
    if (query.lastError().isValid()) return fail(query.lastError().text());
    return result;
}
Journal readJournal(QSqlDatabase db, QDate firstDay, QDate lastDay)
{
    auto fail = [](QString error) { Journal result; result.error = error; return result; };
    if (!AccessControlService::currentUserHasPermission(db, QStringLiteral("cash.view")))
        return fail(QStringLiteral("Немає доступу до журналу каси."));
    if (!db.isValid() || !db.isOpen() || !firstDay.isValid() || !lastDay.isValid() || firstDay > lastDay)
        return fail(QStringLiteral("Некоректні параметри журналу надходжень."));
    QSqlQuery query(db);
    if (!query.exec("SELECT EXISTS(SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='client_payments')")
        || !query.next())
        return fail(query.lastError().text());
    Journal result;
    result.available = query.value(0).toBool();
    if (!result.available)
        return result;
    if (!query.prepare("SELECT payment_uid,order_uid,paid_on,amount_cents,typeof(amount_cents),method,actor "
                       "FROM main.client_payments WHERE paid_on BETWEEN ? AND ? "
                       "ORDER BY paid_on DESC,recorded_ms DESC,payment_uid"))
        return fail(query.lastError().text());
    query.addBindValue(firstDay.toString(Qt::ISODate));
    query.addBindValue(lastDay.toString(Qt::ISODate));
    if (!query.exec())
        return fail(query.lastError().text());
    while (query.next()) {
        const QString uid = query.value(0).toString();
        const QString order = query.value(1).toString();
        const QDate day = QDate::fromString(query.value(2).toString(), Qt::ISODate);
        const qint64 cents = query.value(3).toLongLong();
        if (uid.trimmed().isEmpty() || order.trimmed().isEmpty() || !day.isValid()
            || query.value(4).toString() != "integer" || cents <= 0)
            return fail(QStringLiteral("Некоректний запис у журналі клієнтських оплат."));
        const auto sum = ExactMoney::sum({result.directPaymentsCents, cents});
        const auto* total = std::get_if<ExactMoney::Cents>(&sum);
        if (!total)
            return fail(QStringLiteral("Переповнення суми надходжень."));
        result.directPaymentsCents = *total;
        result.entries.append(JournalEntry{{uid, day, cents, query.value(5).toString()}, order, query.value(6).toString()});
    }
    if (query.lastError().isValid())
        return fail(query.lastError().text());
    return result;
}

PaymentHistory readPeriod(QSqlDatabase db, const QString& orderUid, QDate firstDay, QDate lastDay) {
    const auto fail = [](QString error) { PaymentHistory failed; failed.error = error; return failed; };
    if (!db.isValid() || !db.isOpen() || orderUid.trimmed().isEmpty() ||
        !firstDay.isValid() || !lastDay.isValid() || firstDay > lastDay)
        return fail(QStringLiteral("Некоректні параметри історії оплат."));
    QSqlQuery query(db);
    if (!query.exec("SELECT EXISTS(SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='client_payments')")
        || !query.next()) return fail(query.lastError().text());
    PaymentHistory history;
    history.available = query.value(0).toBool();
    if (!history.available) return history;
    if (!query.prepare("SELECT payment_uid,paid_on,amount_cents,typeof(amount_cents),method FROM main.client_payments "
                       "WHERE order_uid=? ORDER BY paid_on,recorded_ms,payment_uid")) return fail(query.lastError().text());
    query.addBindValue(orderUid);
    if (!query.exec()) return fail(query.lastError().text());
    while (query.next()) {
        const auto day = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        if (!day.isValid()) return fail(QStringLiteral("В оплаті відсутня коректна дата."));
        if (day > lastDay) continue;
        const auto cents = query.value(2).toLongLong();
        const auto uid = query.value(0).toString();
        if (uid.trimmed().isEmpty() || query.value(3).toString() != "integer" || cents <= 0)
            return fail(QStringLiteral("Некоректна ідентичність або сума оплати."));
        if (day >= firstDay) {
            history.payments.append({uid, day, cents, query.value(4).toString()});
        } else {
            const auto sum = ExactMoney::sum({history.receivedBefore, cents});
            const auto* value = std::get_if<ExactMoney::Cents>(&sum);
            if (!value) return fail(QStringLiteral("Переповнення підсумку попередніх оплат."));
            history.receivedBefore = *value;
        }
    }
    if (query.lastError().isValid()) return fail(query.lastError().text());
    return history;
}
Total total(const QList<qint64>& values) {
    Total result;
    for (const auto value:values) {
        if (value<0) return {0,QStringLiteral("Від'ємна сума клієнтських надходжень.")};
        const auto sum=ExactMoney::sum({result.cents,value});
        if (const auto* cents=std::get_if<ExactMoney::Cents>(&sum)) result.cents=*cents;
        else return {0,QStringLiteral("Переповнення підсумку клієнтських надходжень.")};
    }
    return result;
}
QString money(qint64 cents) {
    const QString digits=QString::number(cents);
    QString absolute=cents<0?digits.mid(1):digits;
    absolute=absolute.rightJustified(3,'0');
    absolute.insert(absolute.size()-2,',');
    return (cents<0?QStringLiteral("-"):QString())+absolute+QStringLiteral(" грн");
}
}
