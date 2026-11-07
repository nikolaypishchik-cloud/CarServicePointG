#include "core/services/SparePaymentAllocation.h"
#include "core/services/PurchasePaymentService.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <cmath>

namespace SparePaymentAllocation {
namespace {
bool failed(const QSqlQuery& q, QString* error) {
    if (error) *error = q.lastError().text();
    return false;
}
QString allocationKey(qint64 issue, qint64 purchase) {
    return QString::number(issue) + ':' + QString::number(purchase);
}
}
bool distribution(QSqlDatabase db,QVector<Distribution>* result,QString* error) {
    result->clear();if(error)error->clear();
    if(!db.tables().contains("workshop_spare_movements")) return true;
    struct Receipt {qint64 purchase,spare,total;double quantity,used=0;};
    QVector<Receipt> receipts;QVector<Distribution> output;QSet<qint64> purchases;
    auto invalid=[&]{if(error)*error=QString::fromUtf8("Невідповідність надходжень і видач складу. Розподіл накладних не виконано.");return false;};
    QSqlQuery q(db);
    if(!q.exec("SELECT m.operation,m.spare_id,m.quantity,m.purchase_id,m.part_id,m.order_number,p.quantity,p.unit_cost FROM workshop_spare_movements m LEFT JOIN workshop_inventory_purchases p ON p.id=m.purchase_id ORDER BY m.id")) return failed(q,error);
    while(q.next()) {
        const auto spare=q.value(1).toLongLong();const double quantity=q.value(2).toDouble();
        if(!std::isfinite(quantity) || quantity<=0) return invalid();
        if(q.value(0).toString()=="receipt") {
            const auto purchase=q.value(3).toLongLong();const double cost=q.value(7).toDouble();
            if(purchase<=0 || purchases.contains(purchase) || q.value(6).isNull() || qAbs(q.value(6).toDouble()-quantity)>0.000001 || !std::isfinite(cost) || cost<0) return invalid();
            purchases.insert(purchase);receipts.append({purchase,spare,PurchasePaymentService::cents(quantity*cost),quantity});
        } else if(q.value(0).toString()=="issue") {
            double remaining=quantity;
            const int part=q.value(4).toInt(),order=q.value(5).toInt();
            if(part<=0 || order<=0) return invalid();
            for(auto& receipt:receipts) {
                if(receipt.spare!=spare || remaining<=0.000001) continue;
                const double amount=qMin(remaining,receipt.quantity-receipt.used);if(amount<=0)continue;
                const auto cents=qRound64(receipt.total*(receipt.used+amount)/receipt.quantity)-qRound64(receipt.total*receipt.used/receipt.quantity);
                output.append({receipt.purchase,cents,part,order,amount});receipt.used+=amount;remaining-=amount;
            }
            if(remaining>0.000001) return invalid();
        } else return invalid();
    }
    for(const auto& receipt:receipts) if(receipt.quantity-receipt.used>0.000001)
        output.append({receipt.purchase,receipt.total-qRound64(receipt.total*receipt.used/receipt.quantity),0,0,receipt.quantity-receipt.used});
    *result=output;return true;
}
bool load(QSqlDatabase db, const QHash<QString, qint64>& balances, QVector<Slice>* slices, QString* error) {
    slices->clear();
    if (!db.tables().contains("workshop_spare_movements")) return true;
    struct Receipt { qint64 id, spare, total, paid; double quantity, used = 0; };
    QVector<Receipt> receipts;
    QSqlQuery q(db);
    if (!q.exec("SELECT m.purchase_id,m.spare_id,m.quantity,p.unit_cost FROM workshop_spare_movements m "
                "JOIN workshop_inventory_purchases p ON p.id=m.purchase_id WHERE m.operation='receipt' ORDER BY m.id")) return failed(q,error);
    while(q.next()) {
        const auto id=q.value(0).toLongLong();
        const auto total=PurchasePaymentService::cents(q.value(2).toDouble()*q.value(3).toDouble());
        receipts.append({id,q.value(1).toLongLong(),total,
            qBound<qint64>(0,balances.value(PurchasePaymentService::key("inventory",id),0),total),q.value(2).toDouble()});
    }
    QHash<QString,qint64> allocated;
    if (db.tables().contains("workshop_spare_payment_allocations")) {
        if (!q.exec("SELECT issue_id,purchase_id,paid_cents FROM workshop_spare_payment_allocations")) return failed(q,error);
        while(q.next()) allocated.insert(allocationKey(q.value(0).toLongLong(),q.value(1).toLongLong()),q.value(2).toLongLong());
    }
    if (!q.exec("SELECT m.id,m.spare_id,m.quantity,m.part_id,p.order_uid FROM workshop_spare_movements m "
                "JOIN work_order_parts p ON p.id=m.part_id AND p.order_number=m.order_number "
                "WHERE m.operation='issue' ORDER BY m.id")) return failed(q,error);
    while(q.next()) {
        double remaining=q.value(2).toDouble();
        for(auto& receipt:receipts) {
            if(receipt.spare!=q.value(1).toLongLong() || remaining<=0) continue;
            const double quantity=qMin(remaining,receipt.quantity-receipt.used);
            if(quantity<=0) continue;
            // Cumulative rounding keeps the sum of split quantities equal to the receipt total.
            const qint64 total=qRound64(receipt.total*(receipt.used+quantity)/receipt.quantity)
                -qRound64(receipt.total*receipt.used/receipt.quantity);
            Slice slice{q.value(0).toLongLong(),receipt.id,total,0,q.value(3).toInt(),q.value(4).toString()};
            slice.paid=qMin(receipt.paid,qBound<qint64>(0,allocated.value(allocationKey(slice.issueId,receipt.id)),total));
            receipt.paid-=slice.paid;
            slices->append(slice);receipt.used+=quantity;remaining-=quantity;
        }
        if(remaining>0.000001) {
            if(error) *error=QString::fromUtf8("Видача зі складу не має відповідного надходження. Потрібна звірка руху запчастини.");
            return false;
        }
    }
    // Historical receipts and payments made before issuing stock are assigned FIFO.
    for(auto& receipt:receipts) for(auto& slice:*slices) if(slice.purchaseId==receipt.id
        && !allocated.contains(allocationKey(slice.issueId,slice.purchaseId))) {
        const auto extra=qMin(receipt.paid,slice.total-slice.paid);
        slice.paid+=extra;receipt.paid-=extra;
    }
    return true;
}
void project(const QVector<Slice>& slices, QHash<QString,qint64>* balances) {
    QHash<QString,qint64> paid;
    for(const auto& slice:slices) paid[PurchasePaymentService::key("part",slice.partId,slice.orderUid)]+=slice.paid;
    for(auto it=paid.cbegin();it!=paid.cend();++it) balances->insert(it.key(),it.value());
}
bool save(QSqlDatabase db, const QVector<Slice>& slices, QString* error) {
    QSqlQuery q(db);
    QHash<QString,qint64> paid,totals;
    for(const auto& slice:slices) {
        q.prepare("INSERT INTO workshop_spare_payment_allocations(issue_id,purchase_id,paid_cents) VALUES(?,?,?) "
                  "ON CONFLICT(issue_id,purchase_id) DO UPDATE SET paid_cents=excluded.paid_cents");
        q.addBindValue(slice.issueId);q.addBindValue(slice.purchaseId);q.addBindValue(slice.paid);
        if(!q.exec()) return failed(q,error);
        const auto key=PurchasePaymentService::key("part",slice.partId,slice.orderUid);
        paid[key]+=slice.paid;totals[key]+=slice.total;
    }
    for(const auto& slice:slices) {
        const auto key=PurchasePaymentService::key("part",slice.partId,slice.orderUid);
        q.prepare("INSERT INTO purchase_payment_balances(payment_key,paid_cents) VALUES(?,?) "
                  "ON CONFLICT(payment_key) DO UPDATE SET paid_cents=excluded.paid_cents");
        q.addBindValue(key);q.addBindValue(paid.value(key));if(!q.exec()) return failed(q,error);
        q.prepare("UPDATE work_order_parts SET paid=? WHERE id=? AND COALESCE(order_uid,'')=?");
        q.addBindValue(paid.value(key)>=totals.value(key));q.addBindValue(slice.partId);
        q.addBindValue(slice.orderUid.isNull()?QStringLiteral(""):slice.orderUid);
        if(!q.exec()) return failed(q,error);
    }
    return true;
}
}
