#include "core/services/orders/ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
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
