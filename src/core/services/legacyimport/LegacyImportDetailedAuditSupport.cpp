#include "core/services/legacyimport/LegacyImportDetailedAuditSupport.h"
#include "core/services/legacyimport/LegacyImportEntityAuditSupport.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>

namespace {
int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

bool tableExists(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& tableName)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return cols;
    }
    while (q.next()) cols.insert(q.value(1).toString().trimmed());
    return cols;
}

void addFinding(LegacyImportAuditReport* report, const QString& severity, const QString& code,
                const QString& entityName, const QString& message, int affectedCount,
                bool blocksExecute)
{
    if (!report) return;
    LegacyImportFinding finding;
    finding.severity = severity;
    finding.code = code;
    finding.entityName = entityName;
    finding.message = message;
    finding.affectedCount = affectedCount;
    finding.blocksExecute = blocksExecute;
    report->findings.push_back(finding);

    const QString decorated = affectedCount >= 0
        ? QStringLiteral("[%1] %2 (affected=%3)").arg(code, message).arg(affectedCount)
        : QStringLiteral("[%1] %2").arg(code, message);
    if (severity == QStringLiteral("critical")) {
        report->errorMessages << decorated;
    } else if (severity == QStringLiteral("warning")) {
        report->warningMessages << decorated;
    } else {
        report->infoMessages << decorated;
    }
}
}

void LegacyImportDetailedAuditSupport::appendDetailedFindings(QSqlDatabase db, LegacyImportAuditReport* report)
{
    if (!report) return;

    const bool hasOrders = tableExists(db, QStringLiteral("work_orders"));
    const bool hasServices = tableExists(db, QStringLiteral("work_order_services"));
    const bool hasParts = tableExists(db, QStringLiteral("work_order_parts"));
    const bool hasServiceMasters = tableExists(db, QStringLiteral("service_masters"));
    const bool hasMasters = tableExists(db, QStringLiteral("masters"));
    const bool hasOutsource = tableExists(db, QStringLiteral("outsource_repairs"));

    if (hasOrders) {
        const int invalidOrderNumbers = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_orders WHERE COALESCE(number,0)<=0"));
        if (invalidOrderNumbers > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("invalid_order_number"), QStringLiteral("orders"),
                       QStringLiteral("У legacy БД є наряди з number<=0"), invalidOrderNumbers, true);
        }

        const int duplicateOrderNumbers = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT number FROM work_orders GROUP BY number HAVING COUNT(*)>1)"));
        if (duplicateOrderNumbers > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("duplicate_order_number"), QStringLiteral("orders"),
                       QStringLiteral("У legacy БД є дублікати number у work_orders; execute mode повинен бути заблокований"), duplicateOrderNumbers, true);
        }
    }

    if (hasMasters) {
        const int duplicateMasterIds = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT id FROM masters GROUP BY id HAVING COUNT(*)>1)"));
        if (duplicateMasterIds > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("duplicate_master_id"), QStringLiteral("personnel"),
                       QStringLiteral("У legacy БД є дублікати id у masters"), duplicateMasterIds, true);
        }
        const int blankMasterNames = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM masters WHERE TRIM(COALESCE(name,''))=''"));
        if (blankMasterNames > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("blank_master_name"), QStringLiteral("personnel"),
                       QStringLiteral("У legacy БД є записи персоналу без імені"), blankMasterNames, false);
        }
    }

    if (hasOrders && hasServices) {
        const int orphanServices = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM work_order_services s LEFT JOIN work_orders o ON o.number=s.order_number WHERE o.number IS NULL"));
        if (orphanServices > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("orphan_service_order"), QStringLiteral("services"),
                       QStringLiteral("Є роботи без існуючого наряду"), orphanServices, true);
        }

        const int worksSumMismatches = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM ("
            "SELECT o.number, ABS(COALESCE(o.works_sum,0) - COALESCE(SUM(COALESCE(s.amount, COALESCE(s.qty,1)*COALESCE(s.price,0))),0)) AS delta "
            "FROM work_orders o LEFT JOIN work_order_services s ON s.order_number=o.number "
            "GROUP BY o.number HAVING delta > 0.009)"));
        if (worksSumMismatches > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("works_sum_mismatch"), QStringLiteral("orders"),
                       QStringLiteral("Є наряди, де works_sum не збігається з сумою робіт"), worksSumMismatches, false);
        }

        const QSet<QString> serviceCols = tableColumns(db, QStringLiteral("work_order_services"));
        if (serviceCols.contains(QStringLiteral("parent_id"))) {
            const int zeroParents = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services WHERE COALESCE(parent_id,0)=0"));
            if (zeroParents > 0) {
                addFinding(report, QStringLiteral("info"), QStringLiteral("service_parent_zero"), QStringLiteral("services"),
                           QStringLiteral("parent_id=0 буде нормалізовано до NULL під час імпорту"), zeroParents, false);
            }
            const int invalidParents = scalarCount(db, QStringLiteral(
                "SELECT COUNT(*) FROM work_order_services s "
                "LEFT JOIN work_order_services p ON p.id=s.parent_id "
                "WHERE COALESCE(s.parent_id,0)<>0 AND p.id IS NULL"));
            if (invalidParents > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("invalid_service_parent"), QStringLiteral("services"),
                           QStringLiteral("Є роботи з parent_id, що не посилається на існуючу роботу"), invalidParents, false);
            }
        }
    }

    if (hasOrders && hasParts) {
        const int orphanParts = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM work_order_parts p LEFT JOIN work_orders o ON o.number=p.order_number WHERE o.number IS NULL"));
        if (orphanParts > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("orphan_part_order"), QStringLiteral("parts"),
                       QStringLiteral("Є запчастини без існуючого наряду"), orphanParts, true);
        }
    }

    if (hasServiceMasters && hasServices) {
        const int orphanServiceMastersByService = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM service_masters sm LEFT JOIN work_order_services s ON s.id=sm.service_id WHERE s.id IS NULL"));
        if (orphanServiceMastersByService > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("orphan_service_master_service"), QStringLiteral("service_masters"),
                       QStringLiteral("Є прив’язки майстрів до неіснуючих робіт"), orphanServiceMastersByService, true);
        }
    }

    if (hasServiceMasters && hasMasters) {
        const int orphanServiceMastersByMaster = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM service_masters sm LEFT JOIN masters m ON m.id=sm.master_id WHERE m.id IS NULL"));
        if (orphanServiceMastersByMaster > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("orphan_service_master_master"), QStringLiteral("service_masters"),
                       QStringLiteral("Є прив’язки робіт до неіснуючих майстрів"), orphanServiceMastersByMaster, true);
        }
    }

    if (hasOutsource && hasOrders) {
        const int orphanOutsourceOrders = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM outsource_repairs o LEFT JOIN work_orders w ON w.number=o.order_number WHERE w.number IS NULL"));
        if (orphanOutsourceOrders > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("orphan_outsource_order"), QStringLiteral("outsource"),
                       QStringLiteral("Є outsource-записи без існуючого наряду"), orphanOutsourceOrders, true);
        }
        const QSet<QString> outCols = tableColumns(db, QStringLiteral("outsource_repairs"));
        if (hasServices && outCols.contains(QStringLiteral("service_id"))) {
            const int orphanOutsourceServices = scalarCount(db, QStringLiteral(
                "SELECT COUNT(*) FROM outsource_repairs o "
                "LEFT JOIN work_order_services s ON s.id=o.service_id "
                "WHERE COALESCE(o.service_id,0)<>0 AND s.id IS NULL"));
            if (orphanOutsourceServices > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("orphan_outsource_service"), QStringLiteral("outsource"),
                           QStringLiteral("Є outsource-записи з service_id, що не посилається на існуючу роботу"), orphanOutsourceServices, false);
            }
        }
    }
    LegacyImportEntityAuditSupport::appendEntityFindings(db, report);

}
