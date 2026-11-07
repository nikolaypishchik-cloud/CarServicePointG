#include "core/services/orders/OrderDialogReadQueryService.h"
#include "core/services/AccessControlService.h"

#include "core/services/OrderIdentityResolver.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/util/PlateUtils.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

CrmFinancialService::Snapshot OrderDialogReadQueryService::settlement(int number, const QString& uid)
{
    const auto db = Database::db();
    if (number > 0 && !uid.isEmpty() && AccessControlService::canAccessOrder(db, number)) {
        const auto result = CrmFinancialService::readOrder(db, uid);
        if (result.ok() && result.orders.size() == 1 && result.orders.first().number == number) return result;
    }
    CrmFinancialService::Snapshot missing;
    missing.error = QStringLiteral("Немає даних для розрахунку");
    return missing;
}

bool OrderDialogReadQueryService::loadVehicleHistory(
    const QString& plate, const QString& vin,
    QList<OrderSnapshotService::OrderSnapshot>* result, QString* errorText)
{
    if (errorText) errorText->clear();
    if (!result) return false;
    result->clear();
    const auto normalizedPlate = PlateUtils::normalizePlate(plate);
    QString matchedVin = vin.trimmed().toUpper();
    if (normalizedPlate.isEmpty() && matchedVin.isEmpty()) return true;
    const auto db = Database::db();
    QSqlQuery history(db);
    if (!history.exec(QStringLiteral("SELECT number,plate,vin FROM router_work_orders ORDER BY opened_at DESC,number DESC"))) {
        if (errorText) *errorText = history.lastError().text();
        return false;
    }
    QList<OrderSnapshotService::OrderSnapshot> snapshots;
    while (history.next()) {
        const auto candidateVin = history.value(2).toString().trimmed().toUpper();
        const bool matches = !matchedVin.isEmpty() && !candidateVin.isEmpty()
            ? matchedVin == candidateVin
            : !normalizedPlate.isEmpty() && PlateUtils::normalizePlate(history.value(1).toString()) == normalizedPlate;
        const int number = history.value(0).toInt();
        if (!matches || !AccessControlService::canAccessOrder(db, number)) continue;
        OrderSnapshotService::OrderSnapshot snapshot;
        if (!OrderSnapshotService::loadByNumber(db, number, &snapshot, errorText)) return false;
        if (matchedVin.isEmpty() && !candidateVin.isEmpty()) matchedVin = candidateVin;
        snapshots.append(snapshot);
    }
    if (history.lastError().isValid()) {
        if (errorText) *errorText = history.lastError().text();
        return false;
    }
    *result = snapshots;
    return true;
}

bool OrderDialogReadQueryService::loadSnapshotByNumber(int orderNumber,
                                                       OrderSnapshotService::OrderSnapshot* outSnapshot,
                                                       QString* errorText)
{
    if (!AccessControlService::canAccessOrder(Database::db(), orderNumber)) {
        if (errorText) *errorText = QString::fromUtf8("Немає доступу до наряду іншого майстра.");
        return false;
    }
    return OrderSnapshotService::loadByNumber(Database::db(), orderNumber, outSnapshot, errorText);
}

namespace {

QString closedVehicleSnapshotSql(const QString& sourceName)
{
    return QStringLiteral(
        "SELECT number, COALESCE(NULLIF(order_number,0), number), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(status,''), "
        "COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(car_brand,''), COALESCE(car_model,''), "
        "COALESCE(color, COALESCE(car_color,'')), COALESCE(car_year,0), COALESCE(odometer,0), COALESCE(engine_type,''), COALESCE(engine_volume,''), "
        "COALESCE(fuel_type,''), COALESCE(client,''), COALESCE(owner_name,''), COALESCE(client_phone,''), COALESCE(master_id,''), "
        "COALESCE(master_name_closed,''), COALESCE(discount,0), COALESCE(discount_mode,'legacy') "
        "FROM %1 "
        "WHERE (%2) AND (COALESCE(NULLIF(TRIM(closed_at),''), '')<>'' "
        "       OR LOWER(TRIM(COALESCE(status,''))) IN ('closed','закритий','закрито','виконаний','виконано','done','completed')) "
        "  AND (UPPER(TRIM(COALESCE(plate_norm,''))) = ? "
        "       OR UPPER(REPLACE(REPLACE(REPLACE(COALESCE(plate_norm,''),' ',''),'-',''),'_','')) = ? "
        "       OR UPPER(REPLACE(REPLACE(REPLACE(COALESCE(plate,''),' ',''),'-',''),'_','')) = ?) "
        "ORDER BY COALESCE(NULLIF(order_year,0),0) DESC, "
        "         COALESCE(NULLIF(order_number,0), number) DESC, "
        "         number DESC "
        "LIMIT 1").arg(sourceName, AccessControlService::currentOrderPredicate(Database::db(), sourceName));
}

void fillSnapshotFromQuery(QSqlQuery& q, OrderSnapshotService::OrderSnapshot* outSnapshot)
{
    if (!outSnapshot) return;
    outSnapshot->number = q.value(0).toInt();
    outSnapshot->visibleNumber = q.value(1).toInt();
    outSnapshot->openedAt = q.value(2).toString().trimmed();
    outSnapshot->closedAt = q.value(3).toString().trimmed();
    outSnapshot->status = q.value(4).toString().trimmed();
    outSnapshot->plate = q.value(5).toString().trimmed();
    outSnapshot->plateNorm = q.value(6).toString().trimmed();
    outSnapshot->vin = q.value(7).toString().trimmed();
    outSnapshot->car = q.value(8).toString().trimmed();
    outSnapshot->carBrand = q.value(9).toString().trimmed();
    outSnapshot->carModel = q.value(10).toString().trimmed();
    outSnapshot->color = q.value(11).toString().trimmed();
    outSnapshot->carYear = q.value(12).toInt();
    outSnapshot->odometer = q.value(13).toInt();
    outSnapshot->engineType = q.value(14).toString().trimmed();
    outSnapshot->engineVolume = q.value(15).toString().trimmed();
    outSnapshot->fuelType = q.value(16).toString().trimmed();
    outSnapshot->client = q.value(17).toString().trimmed();
    outSnapshot->ownerName = q.value(18).toString().trimmed();
    outSnapshot->clientPhone = q.value(19).toString().trimmed();
    outSnapshot->masterId = q.value(20).toString().trimmed();
    outSnapshot->masterNameClosed = q.value(21).toString().trimmed();
    outSnapshot->discount = q.value(22).toDouble();
    outSnapshot->discountMode = q.value(23).toString();
}

} // namespace

bool OrderDialogReadQueryService::loadLatestClosedVehicleSnapshotByPlate(const QString& plateText,
                                                                         OrderSnapshotService::OrderSnapshot* outSnapshot,
                                                                         QString* errorText)
{
    const QString plateNorm = PlateUtils::normalizePlate(plateText).trimmed().toUpper();
    if (plateNorm.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Plate is empty");
        return false;
    }

    QSqlDatabase database = Database::db();
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database connection");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    QString lastSqlError;
    const QStringList sources = {QStringLiteral("router_work_orders"), QStringLiteral("work_orders")};
    for (const QString& source : sources) {
        QSqlQuery q(database);
        q.prepare(closedVehicleSnapshotSql(source));
        q.addBindValue(plateNorm);
        q.addBindValue(plateNorm);
        q.addBindValue(plateNorm);
        if (!q.exec()) {
            lastSqlError = q.lastError().text();
            continue;
        }
        if (!q.next()) {
            continue;
        }
        fillSnapshotFromQuery(q, outSnapshot);
        return true;
    }

    if (errorText) {
        *errorText = lastSqlError.trimmed().isEmpty()
            ? QStringLiteral("Completed vehicle order not found for plate: %1").arg(plateNorm)
            : lastSqlError;
    }
    return false;
}

QString OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(int orderNumber)
{
    return OrderIdentityResolver::canonicalOrderUidForInternalNumber(Database::db(), orderNumber);
}

double OrderDialogReadQueryService::servicesTotal(int orderNumber)
{
    return OrderFinancialService::servicesTotal(Database::db(), orderNumber);
}

double OrderDialogReadQueryService::partsTotal(int orderNumber)
{
    return OrderFinancialService::partsTotal(Database::db(), orderNumber);
}

OrderFinancialService::TotalsResult OrderDialogReadQueryService::financialTotals(int orderNumber)
{
    return OrderFinancialService::readTotals(Database::db(),orderNumber);
}

bool OrderDialogReadQueryService::loadCrmVehiclePrefill(const QString& clientUid,const QString& vehicleUid,
                                                       CrmVehiclePrefill* result,QString* errorText)
{
    if(errorText)errorText->clear();
    if(!result || clientUid.isEmpty() || vehicleUid.isEmpty())return false;
    *result={};
    const auto db=Database::db();
    QSqlQuery vehicle(db);
    vehicle.prepare(QStringLiteral(
        "SELECT v.plate,v.vin,v.brand,v.model,v.year,v.engine,v.fuel_type,v.mileage,v.color,"
        "c.full_name,c.phone,c.discount_percent FROM crm_vehicles v "
        "JOIN crm_clients c ON c.client_uid=v.client_uid WHERE v.vehicle_uid=? AND v.client_uid=?"));
    vehicle.addBindValue(vehicleUid);vehicle.addBindValue(clientUid);
    if(!vehicle.exec()){if(errorText)*errorText=vehicle.lastError().text();return false;}
    if(!vehicle.next()){if(errorText)*errorText=vehicle.lastError().text();return false;}
    CrmVehiclePrefill value;
    value.plate=PlateUtils::normalizePlate(vehicle.value(0).toString());value.vin=vehicle.value(1).toString();
    value.brand=vehicle.value(2).toString();value.model=vehicle.value(3).toString();value.year=vehicle.value(4).toInt();
    value.engine=vehicle.value(5).toString();value.fuelType=vehicle.value(6).toString();value.mileage=vehicle.value(7).toInt();
    value.color=vehicle.value(8).toString();value.clientName=vehicle.value(9).toString();value.phone=vehicle.value(10).toString();
    value.discountPercent=vehicle.value(11).toDouble();
    QSqlQuery history(db);
    history.prepare(QStringLiteral("SELECT number,vehicle_uid,plate FROM router_work_orders WHERE client_uid=? ORDER BY number DESC"));
    history.addBindValue(clientUid);
    if(!history.exec()){if(errorText)*errorText=history.lastError().text();return false;}
    while(history.next()) {
        if(history.value(1).toString()==vehicleUid ||
           (!value.plate.isEmpty() && PlateUtils::normalizePlate(history.value(2).toString())==value.plate)) {
            const int number=history.value(0).toInt();
            if(AccessControlService::canAccessOrder(db,number)){value.previousOrder=number;break;}
        }
    }
    if(history.lastError().isValid()){if(errorText)*errorText=history.lastError().text();return false;}
    *result=value;return true;
}

bool OrderDialogReadQueryService::loadCurrentStatus(int orderNumber, QString* status, QString* errorText)
{
    if (errorText) errorText->clear();
    if (!status) return false;
    status->clear();
    const auto db = Database::db();
    if (!AccessControlService::canAccessOrder(db, orderNumber)) {
        if (errorText) *errorText = QStringLiteral("Немає доступу до наряду.");
        return false;
    }
    QSqlQuery query(db);
    query.prepare("SELECT status FROM work_orders WHERE number=?");
    query.addBindValue(orderNumber);
    if (!query.exec() || !query.next()) {
        if (errorText) *errorText = query.lastError().isValid() ? query.lastError().text() : QStringLiteral("Наряд не знайдено.");
        return false;
    }
    *status = query.value(0).toString();
    return true;
}
