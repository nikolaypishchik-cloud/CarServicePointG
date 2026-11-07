#include "core/services/OrderSearchRepository.h"
#include "core/services/OrderSearchIndex.h"

#include <QSqlError>
#include <QSqlQuery>

namespace OrderSearchRepository {

QList<OrderSearchHit> findByVisibleNumber(QSqlDatabase database,
                                          int visibleNumber,
                                          int limit,
                                          QString* errorText)
{
    QList<OrderSearchHit> hits;
    if (visibleNumber <= 0) return hits;
    if (!database.isValid() || (!database.isOpen() && !database.open())) {
        if (errorText) *errorText = database.lastError().text();
        return hits;
    }

    QSqlQuery q(database);
    QString sql = QStringLiteral(
        "SELECT COALESCE(order_uid,''), COALESCE(number,0), COALESCE(order_number,0), COALESCE(order_year,0), "
        "COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(status,''), COALESCE(plate,''), COALESCE(car,''), "
        "COALESCE(client,''), COALESCE(storage_kind,''), COALESCE(storage_path,'') "
        "FROM router_order_locator "
        "WHERE order_number=? "
        "ORDER BY order_year DESC, COALESCE(NULLIF(closed_at,''), opened_at) DESC, number DESC "
        "LIMIT ?");
    if (OrderSearchIndex::available(database))
        sql.replace("WHERE order_number=?", "WHERE number IN (SELECT number FROM order_search_v1 WHERE visible_number=?)");
    q.prepare(sql);
    q.addBindValue(visibleNumber);
    q.addBindValue(limit > 0 ? limit : 100);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return hits;
    }

    while (q.next()) {
        OrderSearchHit hit;
        hit.orderUid = q.value(0).toString().trimmed();
        hit.internalNumber = q.value(1).toInt();
        hit.visibleNumber = q.value(2).toInt();
        hit.orderYear = q.value(3).toInt();
        hit.openedAt = q.value(4).toString();
        hit.closedAt = q.value(5).toString();
        hit.status = q.value(6).toString();
        hit.plate = q.value(7).toString();
        hit.car = q.value(8).toString();
        hit.client = q.value(9).toString();
        hit.storageKind = q.value(10).toString();
        hit.storagePath = q.value(11).toString();
        hits.push_back(hit);
    }
    return hits;
}

} // namespace OrderSearchRepository
