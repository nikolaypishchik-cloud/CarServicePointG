#include "core/services/orders/OrderSnapshotService.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {

QString trimmedValue(const QVariant& value)
{
    return value.toString().trimmed();
}

bool fail(QString* errorText, const QString& text)
{
    if (errorText) *errorText = text;
    return false;
}

} // namespace

bool OrderSnapshotService::loadByNumber(QSqlDatabase database,
                                        int orderNumber,
                                        OrderSnapshot* outSnapshot,
                                        QString* errorText)
{
    if (orderNumber <= 0) {
        return fail(errorText, QStringLiteral("Invalid order number"));
    }
    if (!database.isValid()) {
        return fail(errorText, QStringLiteral("Invalid database connection"));
    }
    if (!database.isOpen() && !database.open()) {
        return fail(errorText, database.lastError().text());
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT number, COALESCE(NULLIF(order_number,0), number), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(status,''), "
        "COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(car_brand,''), COALESCE(car_model,''), "
        "COALESCE(color, COALESCE(car_color,'')), COALESCE(car_year,0), COALESCE(odometer,0), COALESCE(engine_type,''), COALESCE(engine_volume,''), "
        "COALESCE(fuel_type,''), COALESCE(client,''), COALESCE(owner_name,''), COALESCE(client_phone,''), COALESCE(master_id,''), "
        "COALESCE(master_name_closed,''), COALESCE(discount,0), COALESCE(discount_mode,'legacy') "
        "FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec()) {
        return fail(errorText, q.lastError().text());
    }
    if (!q.next()) {
        return fail(errorText, QStringLiteral("Order not found: %1").arg(orderNumber));
    }

    if (outSnapshot) {
        outSnapshot->number = q.value(0).toInt();
        outSnapshot->visibleNumber = q.value(1).toInt();
        outSnapshot->openedAt = trimmedValue(q.value(2));
        outSnapshot->closedAt = trimmedValue(q.value(3));
        outSnapshot->status = trimmedValue(q.value(4));
        outSnapshot->plate = trimmedValue(q.value(5));
        outSnapshot->plateNorm = trimmedValue(q.value(6));
        outSnapshot->vin = trimmedValue(q.value(7));
        outSnapshot->car = trimmedValue(q.value(8));
        outSnapshot->carBrand = trimmedValue(q.value(9));
        outSnapshot->carModel = trimmedValue(q.value(10));
        outSnapshot->color = trimmedValue(q.value(11));
        outSnapshot->carYear = q.value(12).toInt();
        outSnapshot->odometer = q.value(13).toInt();
        outSnapshot->engineType = trimmedValue(q.value(14));
        outSnapshot->engineVolume = trimmedValue(q.value(15));
        outSnapshot->fuelType = trimmedValue(q.value(16));
        outSnapshot->client = trimmedValue(q.value(17));
        outSnapshot->ownerName = trimmedValue(q.value(18));
        outSnapshot->clientPhone = trimmedValue(q.value(19));
        outSnapshot->masterId = trimmedValue(q.value(20));
        outSnapshot->masterNameClosed = trimmedValue(q.value(21));
        outSnapshot->discount = q.value(22).toDouble();
    outSnapshot->discountMode = q.value(23).toString();
    }
    return true;
}
