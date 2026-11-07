#include "core/services/catalog/CatalogMutationService.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace CatalogMutationService {

SaveResult save(QSqlDatabase db, const SaveParams& params)
{
    SaveResult result;
    QSqlQuery q(db);
    const QString nowText = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (params.id > 0) {
        q.prepare(QStringLiteral("UPDATE parts_catalog SET vehicle_brand=?, vehicle_model=?, year_from=?, year_to=?, fuel_type=?, group_name=?, part_number=?, oem_number=?, name=?, brand=?, supplier=?, price=?, note=?, manually_edited=1, last_imported_at=CASE WHEN COALESCE(last_imported_at,'')='' THEN ? ELSE last_imported_at END WHERE id=?"));
        q.addBindValue(params.brand);
        q.addBindValue(params.model);
        q.addBindValue(params.yearFrom);
        q.addBindValue(params.yearTo);
        q.addBindValue(params.fuel);
        q.addBindValue(params.group);
        q.addBindValue(params.oem);
        q.addBindValue(params.oem);
        q.addBindValue(params.name);
        q.addBindValue(params.brandName);
        q.addBindValue(params.supplier);
        q.addBindValue(params.price);
        q.addBindValue(params.note);
        q.addBindValue(nowText);
        q.addBindValue(params.id);
        result.operation = QStringLiteral("update");
    } else {
        q.prepare(QStringLiteral("INSERT INTO parts_catalog(vehicle_brand, vehicle_model, year_from, year_to, fuel_type, group_name, part_number, oem_number, name, brand, supplier, price, note, currency, unit, source_type, first_imported_at, last_imported_at, manually_edited, is_active) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(params.brand);
        q.addBindValue(params.model);
        q.addBindValue(params.yearFrom);
        q.addBindValue(params.yearTo);
        q.addBindValue(params.fuel);
        q.addBindValue(params.group);
        q.addBindValue(params.oem);
        q.addBindValue(params.oem);
        q.addBindValue(params.name);
        q.addBindValue(params.brandName);
        q.addBindValue(params.supplier);
        q.addBindValue(params.price);
        q.addBindValue(params.note);
        q.addBindValue(QStringLiteral("UAH"));
        q.addBindValue(QString::fromUtf8("шт"));
        q.addBindValue(QStringLiteral("manual"));
        q.addBindValue(nowText);
        q.addBindValue(nowText);
        q.addBindValue(1);
        q.addBindValue(1);
        result.operation = QStringLiteral("insert");
    }

    if (!q.exec()) {
        result.errorText = q.lastError().text();
        return result;
    }

    result.ok = true;
    result.recordId = params.id > 0 ? params.id : q.lastInsertId().toInt();
    return result;
}

DeleteResult remove(QSqlDatabase db, int id)
{
    DeleteResult result;
    if (id <= 0) {
        result.errorText = QStringLiteral("Некоректний id");
        return result;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM parts_catalog WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        result.errorText = q.lastError().text();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace CatalogMutationService
