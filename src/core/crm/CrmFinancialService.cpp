#include "core/services/orders/ClientReceiptReader.h"
#include "core/crm/CrmFinancialService.h"
#include "core/finance/ExactMoney.h"
#include <QDateTime>
#include <QTimeZone>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include "core/services/PartOverduePolicy.h"

namespace CrmFinancialService {
ClosureCharge readClosureCharge(QSqlDatabase db, const QString& uid, QDate firstDay, QDate lastDay) {
    ClosureCharge result;
    if (!db.isOpen() || uid.trimmed().isEmpty() || !firstDay.isValid() || !lastDay.isValid() || firstDay>lastDay) {
        result.error = QStringLiteral("Некоректні параметри нарахування за нарядом.");
        return result;
    }
    QSqlQuery query(db);
    if (!query.exec("SELECT EXISTS(SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='order_closure_commands')")
        || !query.next()) { result.error=query.lastError().text(); return result; }
    if (!query.value(0).toBool()) return result;
    query.prepare("SELECT request_uid,json_extract(response_json,'$.order_uid'),json_extract(response_json,'$.order.closed_at'),"
                  "json_type(response_json,'$.total_cents'),json_extract(response_json,'$.total_cents'),"
                  "typeof(json_extract(response_json,'$.total_cents')),json_extract(response_json,'$.order.order_uid') FROM main.order_closure_commands "
                  "WHERE order_uid=? AND state='applied' LIMIT 2");
    query.addBindValue(uid);
    if (!query.exec() || !query.next()) { result.error=query.lastError().text(); return result; }
    const QString request=query.value(0).toString();
    const auto closed=QDateTime::fromString(query.value(2).toString(),Qt::ISODate);
    const auto cents=query.value(4).toLongLong();
    if (request.trimmed().isEmpty() || query.value(1).toString()!=uid || query.value(6).toString()!=uid || !closed.isValid()
        || query.value(3).toString()!="integer" || query.value(5).toString()!="integer" || cents<0) {
        result.error=QStringLiteral("Некоректний запис закриття наряду.");
        return result;
    }
    if (query.next()) { result.error=QStringLiteral("Кілька записів закриття наряду потребують звірки."); return result; }
    if (query.lastError().isValid()) { result.error=query.lastError().text(); return result; }
    result.requestUid=request;
    result.closedOn=closed.toTimeZone(QTimeZone("Europe/Kyiv")).date();
    if (result.closedOn<firstDay) result.priorCents=cents;
    else if (result.closedOn<=lastDay) result.periodCents=cents;
    return result;
}
OrderJournal readOrderJournal(QSqlDatabase db, const QString& uid, QDate firstDay, QDate lastDay) {
    const auto fail=[](const QString& error) { OrderJournal failed; failed.error=error; return failed; };
    const auto savepoint="order_journal_"+QUuid::createUuid().toString(QUuid::Id128);
    QSqlQuery boundary(db);
    if (!db.isOpen() || !boundary.exec("SAVEPOINT "+savepoint))
        return fail(db.isOpen()?boundary.lastError().text():QStringLiteral("База даних недоступна."));
    auto cleanup=qScopeGuard([&] { QSqlQuery q(db); q.exec("ROLLBACK TO "+savepoint); q.exec("RELEASE "+savepoint); });
    const auto charge=readClosureCharge(db,uid,firstDay,lastDay);
    if (!charge.error.isEmpty()) return fail(charge.error);
    const auto receipts=ClientReceiptReader::readPeriod(db,uid,firstDay,lastDay);
    if (!receipts.error.isEmpty()) return fail(receipts.error);
    OrderJournal result;
    result.closureRecorded=!charge.requestUid.isEmpty();
    result.receiptsAvailable=receipts.available;
    result.opening=charge.priorCents-receipts.receivedBefore;
    result.charged=charge.periodCents;
    for (const auto& payment:receipts.payments)
        result.entries.append({payment.paidOn,payment.uid,true,payment.cents,0});
    if (charge.closedOn>=firstDay && charge.closedOn<=lastDay) {
        auto it=result.entries.begin();
        while (it!=result.entries.end() && it->day<charge.closedOn) ++it;
        result.entries.insert(it,{charge.closedOn,charge.requestUid,false,charge.periodCents,0});
    }
    result.closing=result.opening;
    for (auto& entry:result.entries) {
        const auto balance=ExactMoney::sum({result.closing,entry.payment?-entry.cents:entry.cents});
        const auto* next=std::get_if<ExactMoney::Cents>(&balance);
        if (!next) return fail(QStringLiteral("Переповнення залишку виписки."));
        result.closing=entry.balance=*next;
        if (entry.payment) {
            const auto paid=ExactMoney::sum({result.paid,entry.cents});
            const auto* total=std::get_if<ExactMoney::Cents>(&paid);
            if (!total) return fail(QStringLiteral("Переповнення підсумку оплат."));
            result.paid=*total;
        }
    }
    if (!boundary.exec("RELEASE "+savepoint)) return fail(boundary.lastError().text());
    cleanup.dismiss();
    return result;
}
ClientJournal readClientJournal(QSqlDatabase db, const QString& clientUid, const QString& vehicleUid, QDate from, QDate through) {
    const auto fail=[](const QString& error) { ClientJournal failed; failed.error=error; return failed; };
    if (clientUid.trimmed().isEmpty() || !from.isValid() || !through.isValid() || from>through || !db.isOpen())
        return fail(QStringLiteral("Некоректні параметри клієнтської виписки."));
    const QString savepoint="client_journal_"+QUuid::createUuid().toString(QUuid::Id128);
    QSqlQuery boundary(db);
    if (!boundary.exec("SAVEPOINT "+savepoint)) return fail(boundary.lastError().text());
    auto cleanup=qScopeGuard([&] { QSqlQuery q(db); q.exec("ROLLBACK TO "+savepoint); q.exec("RELEASE "+savepoint); });
    const QString orders=db.tables(QSql::AllTables).contains("router_work_orders")?"router_work_orders":"work_orders";
    QString sql=QStringLiteral("SELECT o.order_uid,o.vehicle_uid,o.car,COALESCE(NULLIF(o.order_number,0),o.number),o.status,o.closed_at,"
                               "(SELECT COUNT(*) FROM %1 x WHERE x.order_uid=o.order_uid) FROM %1 o WHERE o.client_uid=?").arg(orders);
    if (!vehicleUid.isEmpty()) sql+=" AND o.vehicle_uid=?";
    QSqlQuery query(db);
    query.prepare(sql+" ORDER BY o.order_uid");
    query.addBindValue(clientUid);
    if (!vehicleUid.isEmpty()) query.addBindValue(vehicleUid);
    if (!query.exec()) return fail(query.lastError().text());
    ClientJournal result;
    const auto add=[](qint64& total,qint64 delta) {
        const auto sum=ExactMoney::sum({total,delta});
        if (const auto* cents=std::get_if<ExactMoney::Cents>(&sum)) { total=*cents; return true; }
        return false;
    };
    while (query.next()) {
        const auto uid=query.value(0).toString();
        if (uid.trimmed().isEmpty() || query.value(6).toInt()!=1)
            return fail(QStringLiteral("Відсутній або неоднозначний UID наряду клієнта."));
        const auto order=readOrderJournal(db,uid,from,through);
        if (!order.error.isEmpty()) return fail(order.error);
        if (!add(result.opening,order.opening) || !add(result.charged,order.charged) || !add(result.paid,order.paid))
            return fail(QStringLiteral("Переповнення підсумків клієнтської виписки."));
        const int number=query.value(3).toInt();
        if (!order.closureRecorded && (!query.value(5).toString().trimmed().isEmpty()
            || PartOverduePolicy::isClosedOrderStatus(PartOverduePolicy::normalizedOrderStatus(query.value(4).toString()))))
            result.unverifiedClosures.append(number);
        for (const auto& event:order.entries)
            result.entries.append({event,uid,query.value(1).toString(),query.value(2).toString(),number});
    }
    if (query.lastError().isValid()) return fail(query.lastError().text());
    query.finish();
    // Receipts have a business date. Keep their source order for ties within an order.
    std::stable_sort(result.entries.begin(),result.entries.end(),[](const ClientEntry& a,const ClientEntry& b) {
        if (a.event.day!=b.event.day) return a.event.day<b.event.day;
        if (a.event.payment!=b.event.payment) return !a.event.payment;
        return a.orderUid<b.orderUid;
    });
    result.closing=result.opening;
    for (auto& row:result.entries) {
        if (!add(result.closing,row.event.payment?-row.event.cents:row.event.cents))
            return fail(QStringLiteral("Переповнення поточного залишку клієнта."));
        row.event.balance=result.closing;
    }
    if (!boundary.exec("RELEASE "+savepoint)) return fail(boundary.lastError().text());
    cleanup.dismiss();
    return result;
}
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
        lines.prepare(QStringLiteral("SELECT kind,qty,price,amount, CASE WHEN TRIM(COALESCE(order_uid,''))='' THEN (SELECT COUNT(*) FROM %3 WHERE number=?) ELSE 1 END FROM (SELECT 0 AS kind,order_uid,order_number,qty,price,amount FROM %1 UNION ALL SELECT 1,order_uid,order_number,qty,price,amount FROM %2) WHERE (TRIM(COALESCE(order_uid,''))<>'' AND order_uid=?) OR (TRIM(COALESCE(order_uid,''))='' AND order_number=?)").arg(services,parts,orders));
        lines.addBindValue(order.number);
        lines.addBindValue(order.uid); lines.addBindValue(order.number);
        if (!lines.exec()) return fail(lines.lastError().text());
        while (lines.next()) {
            if (lines.value(4).toLongLong()!=1)
                return fail(QStringLiteral("Неоднозначний номер наряду %1 для історичної позиції без UID.").arg(order.number));
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
