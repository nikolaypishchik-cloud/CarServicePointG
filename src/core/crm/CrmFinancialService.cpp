#include "core/services/orders/ClientReceiptReader.h"
#include "core/crm/CrmFinancialService.h"
#include "core/finance/ExactMoney.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QScopeGuard>
#include <QUuid>

namespace CrmFinancialService {
QString money(qint64 cents) {
    return ClientReceiptReader::money(cents);
}
static Snapshot readFiltered(QSqlDatabase db, const QString& identity, bool singleOrder) {
    Snapshot result;
    auto fail = [&](const QString& error) { Snapshot failed; failed.error=error; return failed; };
    if (!db.isOpen() || identity.trimmed().isEmpty())
        return fail(QStringLiteral("Немає з'єднання з БД або клієнта."));
    const QString savepoint="crm_financial_"+QUuid::createUuid().toString(QUuid::Id128);
    QSqlQuery boundary(db);
    if (!boundary.exec("SAVEPOINT "+savepoint)) return fail(boundary.lastError().text());
    auto cleanup=qScopeGuard([&]{ QSqlQuery q(db); q.exec("ROLLBACK TO "+savepoint); q.exec("RELEASE "+savepoint); });
    const auto tables=db.tables(QSql::AllTables);
    const bool routed=tables.contains("router_work_orders");
    const QString orders=routed?"router_work_orders":"work_orders";
    const QString services=routed?"router_work_order_services":"work_order_services";
    const QString parts=routed?"router_work_order_parts":"work_order_parts";
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT number,order_uid,car,COALESCE(discount,0),COALESCE(discount_mode,'') FROM %1 WHERE %2=? ORDER BY number DESC").arg(orders, singleOrder ? QStringLiteral("order_uid") : QStringLiteral("client_uid")));
    query.addBindValue(identity);
    if (!query.exec()) return fail(query.lastError().text());
    QSet<QString> identities;
    auto exact = [](const ExactMoney::Result<ExactMoney::Cents>& value, qint64& out) {
        if (const auto* cents=std::get_if<ExactMoney::Cents>(&value)) { out=*cents; return true; }
        return false;
    };
    while (query.next()) {
        Order order; order.number=query.value(0).toInt(); order.uid=query.value(1).toString(); order.car=query.value(2).toString();
        if (order.uid.trimmed().isEmpty() || identities.contains(order.uid))
            return fail(QStringLiteral("Відсутній або неоднозначний UID наряду %1.").arg(order.number));
        identities.insert(order.uid);
        qint64 works=0, materials=0;
        QSqlQuery lines(db);
        lines.prepare(QStringLiteral("SELECT kind,qty,price,amount FROM (SELECT 0 AS kind,order_uid,order_number,qty,price,amount FROM %1 UNION ALL SELECT 1,order_uid,order_number,qty,price,amount FROM %2) WHERE (TRIM(COALESCE(order_uid,''))<>'' AND order_uid=?) OR (TRIM(COALESCE(order_uid,''))='' AND order_number=?)").arg(services,parts));
        lines.addBindValue(order.uid); lines.addBindValue(order.number);
        if (!lines.exec()) return fail(lines.lastError().text());
        while (lines.next()) {
            const auto amount=(singleOrder || lines.value(3).isNull())
                ? ExactMoney::storedLineAmount(lines.value(1).toString().toStdString(),lines.value(2).toString().toStdString())
                : ExactMoney::payment(lines.value(3).toString().toStdString());
            qint64 cents=0; auto& sum=lines.value(0).toInt()==0?works:materials;
            if (!exact(amount,cents) || cents<0 || !exact(ExactMoney::sum({sum,cents}),sum))
                return fail(QStringLiteral("Некоректні суми рядків наряду %1.").arg(order.number));
        }
        if (lines.lastError().isValid()) return fail(lines.lastError().text());
        const auto mode=query.value(4).toString();
        const auto discount=query.value(3).toString().toStdString();
        qint64 deduction=0;
        if (mode=="percent") {
            qint64 net=0;
            if (!exact(ExactMoney::discounted(works,ExactMoney::DiscountMode::Percent,discount),net))
                return fail(QStringLiteral("Некоректна відсоткова знижка."));
            deduction=works-net;
        } else {
            // Existing legacy totals used a UAH amount, not inferred percent.
            if ((mode!="" && mode!="uah" && mode!="legacy") || !exact(ExactMoney::payment(discount),deduction) || deduction<0)
                return fail(QStringLiteral("Некоректна знижка наряду."));
        }
        if (!exact(ExactMoney::sum({works,materials}),order.totalCents)) return fail(QStringLiteral("Переповнення суми."));
        order.totalCents=qMax(qint64(0),order.totalCents-deduction);
        result.orders.append(order);
    }
    if (query.lastError().isValid()) return fail(query.lastError().text());
    query.finish();
    const auto receipts=ClientReceiptReader::read(db,identities);
    if (!receipts.ok()) return fail(receipts.error);
    result.ledgerAvailable=receipts.available;
    for (auto& order:result.orders) {
        order.receivedCents=receipts.centsByOrder.value(order.uid);
        const auto balance=order.balanceCents();
        if (!exact(ExactMoney::sum({result.totalCents,order.totalCents}),result.totalCents)
            || !exact(ExactMoney::sum({result.receivedCents,order.receivedCents}),result.receivedCents)
            || !exact(ExactMoney::sum({result.outstandingCents,qMax(qint64(0),balance)}),result.outstandingCents)
            || !exact(ExactMoney::sum({result.overpaidCents,qMax(qint64(0),-balance)}),result.overpaidCents))
            return fail(QStringLiteral("Переповнення підсумків клієнта."));
    }
    if (singleOrder) {
        result.documentedOrderReceipts = result.receivedCents > 0;
        const auto checkEvidence = [&](const QString& table, const QString& sql) {
            if (!tables.contains(table) || result.documentedOrderReceipts) return true;
            QSqlQuery evidence(db);
            evidence.prepare(sql);
            evidence.addBindValue(identity);
            if (!evidence.exec()) return false;
            result.documentedOrderReceipts = evidence.next();
            return !evidence.lastError().isValid();
        };
        if (!checkEvidence("client_receipt_tracking","SELECT 1 FROM main.client_receipt_tracking WHERE order_uid=?")
            || !checkEvidence("order_closure_commands","SELECT 1 FROM main.order_closure_commands WHERE order_uid=? AND state='applied' LIMIT 1"))
            return fail(QStringLiteral("Не вдалося перевірити історію оплат."));
    }
    if (!boundary.exec("RELEASE "+savepoint)) return fail(boundary.lastError().text());
    cleanup.dismiss();
    return result;
}
Snapshot read(QSqlDatabase db, const QString& clientUid) {
    return readFiltered(db,clientUid,false);
}
Snapshot readOrder(QSqlDatabase db, const QString& uid) {
    return readFiltered(db,uid,true);
}
}
