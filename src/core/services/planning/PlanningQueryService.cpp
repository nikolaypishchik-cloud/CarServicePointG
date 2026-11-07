#include "core/services/planning/PlanningQueryService.h"
#include "core/services/AccessControlService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QSqlQuery>

bool PlanningQueryService::vehicleWorkInProgress(const QString& plateNorm)
{
    const int number = findLatestVehicleOrderNumber(plateNorm);
    if (number <= 0) return false;
    QSqlQuery query(Database::db());
    query.prepare("SELECT status,closed_at FROM work_orders WHERE number=?");
    query.addBindValue(number);
    if (!query.exec() || !query.next() || !query.value(1).toString().trimmed().isEmpty()) return false;
    const auto status = query.value(0).toString().trimmed().toLower();
    return status == QStringLiteral("in_progress") || status == QString::fromUtf8("в роботі")
        || status == QString::fromUtf8("в работе");
}

int PlanningQueryService::findLatestVehicleOrderNumber(const QString& plateNorm)
{
    if (plateNorm.trimmed().isEmpty()) return -1;

    QSqlQuery q(Database::db());
    q.prepare(
        "SELECT number "
        "FROM work_orders o "
        "WHERE (" + AccessControlService::currentOrderPredicate(Database::db(), "o") + ") AND (COALESCE(plate_norm,'') = ? "
        "   OR UPPER(REPLACE(REPLACE(REPLACE(COALESCE(plate,''),' ',''),'-',''),'_','')) = ?) "
        "ORDER BY number DESC "
        "LIMIT 1");
    q.addBindValue(plateNorm.trimmed().toUpper());
    q.addBindValue(plateNorm.trimmed().toUpper());
    if (!q.exec() || !q.next()) {
        return -1;
    }

    bool ok = false;
    const int number = q.value(0).toInt(&ok);
    return ok ? number : -1;
}
