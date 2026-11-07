#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include "core/util/DateFormats.h"
#include "core/services/legacyimport/LegacyImportPostCheckSupport.h"

#include "core/services/HistoryQueryService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/planning/PlanningRepository.h"

#include <QDate>
#include <QSqlError>
#include <QSqlQuery>

namespace {

int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

LegacyImportVerifyEntityResult verifyCounts(QSqlDatabase db,
                                            const QString& entityName,
                                            const QString& sourceTable,
                                            const QString& targetTable,
                                            const QString& missingSql,
                                            const QString& invalidSql = QString())
{
    LegacyImportVerifyEntityResult result;
    result.entityName = entityName;
    const bool optionalAbsent = (sourceTable == "parts_catalog" || sourceTable == "outsource_repairs" || sourceTable == "planning_calendar" || sourceTable == "expenses")
        && SqliteSchemaInspection::tableAbsent(db, "verify_src", sourceTable);
    result.sourceCount = optionalAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.%1").arg(sourceTable));
    result.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM %1").arg(targetTable));
    result.missingCount = (optionalAbsent || missingSql.trimmed().isEmpty()) ? 0 : scalarCount(db, missingSql);
    result.invalidLinkCount = invalidSql.trimmed().isEmpty() ? 0 : scalarCount(db, invalidSql);
    result.ok = result.sourceCount >= 0 && result.targetCount >= 0 && result.missingCount == 0 && result.invalidLinkCount == 0;
    if (result.sourceCount != result.targetCount) result.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(result.sourceCount).arg(result.targetCount);
    if (result.missingCount > 0) result.messages << QStringLiteral("missing=%1").arg(result.missingCount);
    if (result.invalidLinkCount > 0) result.messages << QStringLiteral("invalid=%1").arg(result.invalidLinkCount);
    return result;
}

void addWarning(LegacyImportVerifyReport* report, const QString& message)
{
    if (report) report->warningMessages << message;
}

void addError(LegacyImportVerifyReport* report, const QString& message)
{
    if (report) report->errorMessages << message;
}

void addInfo(LegacyImportVerifyReport* report, const QString& message)
{
    if (report) report->infoMessages << message;
}

}

void LegacyImportPostCheckSupport::appendPostImportChecks(QSqlDatabase db, LegacyImportVerifyReport* report)
{
    if (!report) return;

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("parts"),
        QStringLiteral("work_order_parts"),
        QStringLiteral("work_order_parts"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_parts s LEFT JOIN work_order_parts t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
        QStringLiteral("SELECT COUNT(*) FROM work_order_parts p LEFT JOIN work_orders o ON o.number=p.order_number WHERE o.number IS NULL")));

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("masters"),
        QStringLiteral("masters"),
        QStringLiteral("masters"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.masters s LEFT JOIN masters t ON t.id=s.id WHERE t.id IS NULL")));

    {
        LegacyImportVerifyEntityResult result;
        result.entityName = QStringLiteral("service_masters");
        const bool absent = SqliteSchemaInspection::tableAbsent(db, "verify_src", "service_masters");
        result.sourceCount = absent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters"));
        result.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM service_masters"));
        result.missingCount = absent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters s LEFT JOIN service_masters t ON t.service_id=s.service_id AND t.master_id=s.master_id WHERE t.service_id IS NULL"));
        result.invalidLinkCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM service_masters sm LEFT JOIN work_order_services s ON s.id=sm.service_id LEFT JOIN masters m ON m.id=sm.master_id WHERE s.id IS NULL OR m.id IS NULL"));
        result.ok = result.sourceCount >= 0 && result.targetCount >= result.sourceCount && result.missingCount == 0 && result.invalidLinkCount == 0;
        if (result.targetCount < result.sourceCount) result.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(result.sourceCount).arg(result.targetCount);
        if (result.missingCount > 0) result.messages << QStringLiteral("missing=%1").arg(result.missingCount);
        if (result.invalidLinkCount > 0) result.messages << QStringLiteral("invalid=%1").arg(result.invalidLinkCount);
        report->entities.push_back(result);
    }

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("parts_catalog"),
        QStringLiteral("parts_catalog"),
        QStringLiteral("parts_catalog"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.parts_catalog s LEFT JOIN parts_catalog t ON COALESCE(t.part_number,'')=COALESCE(s.part_number,'') AND COALESCE(t.name,'')=COALESCE(s.name,'') WHERE t.id IS NULL")));

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("planning_calendar"),
        QStringLiteral("planning_calendar"),
        QStringLiteral("planning_calendar"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.planning_calendar s LEFT JOIN planning_calendar t ON t.day=s.day WHERE t.day IS NULL")));

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("outsource_repairs"),
        QStringLiteral("outsource_repairs"),
        QStringLiteral("outsource_repairs"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs s LEFT JOIN outsource_repairs t ON t.order_number=s.order_number AND COALESCE(t.part_name,'')=COALESCE(s.part_name,'') AND COALESCE(t.sent_at,'')=COALESCE(s.sent_at,'') WHERE t.id IS NULL"),
        QStringLiteral("SELECT COUNT(*) FROM outsource_repairs r LEFT JOIN work_orders o ON o.number=r.order_number WHERE o.number IS NULL")));

    report->entities.push_back(verifyCounts(
        db,
        QStringLiteral("expenses"),
        QStringLiteral("expenses"),
        QStringLiteral("expenses"),
        QStringLiteral("SELECT COUNT(*) FROM verify_src.expenses s LEFT JOIN expenses t ON COALESCE(t.period_from,'')=COALESCE(s.period_from,'') AND COALESCE(t.period_to,'')=COALESCE(s.period_to,'') WHERE t.id IS NULL")));

    const int invalidPartServiceRefs = scalarCount(db, QStringLiteral(
        "SELECT COUNT(*) FROM work_order_parts p LEFT JOIN work_order_services s ON s.id=p.service_id "
        "WHERE COALESCE(p.service_id,0)>0 AND (s.id IS NULL OR s.order_number<>p.order_number)"));
    if (invalidPartServiceRefs > 0) addError(report, QStringLiteral("Після імпорту залишилися parts з невалідним service_id: %1").arg(invalidPartServiceRefs));

    const int invalidOutsourceServiceRefs = scalarCount(db, QStringLiteral(
        "SELECT COUNT(*) FROM outsource_repairs r LEFT JOIN work_order_services s ON s.id=r.service_id "
        "WHERE COALESCE(r.service_id,0)>0 AND (s.id IS NULL OR s.order_number<>r.order_number)"));
    if (invalidOutsourceServiceRefs > 0) addError(report, QStringLiteral("Після імпорту залишилися outsource записи з невалідним service_id: %1").arg(invalidOutsourceServiceRefs));

    const int worksSumMismatch = scalarCount(db, QStringLiteral(
        "SELECT COUNT(*) FROM (SELECT o.number, ABS(COALESCE(o.works_sum,0)-COALESCE(SUM(COALESCE(s.amount,COALESCE(s.qty,1)*COALESCE(s.price,0))),0)) AS delta "
        "FROM work_orders o LEFT JOIN work_order_services s ON s.order_number=o.number GROUP BY o.number HAVING delta>0.009)"));
    if (worksSumMismatch > 0) addWarning(report, QStringLiteral("Розбіжність works_sum проти суми робіт: %1 нарядів").arg(worksSumMismatch));

    const int partsSumMismatch = scalarCount(db, QStringLiteral(
        "SELECT COUNT(*) FROM (SELECT o.number, ABS(COALESCE(o.parts_sum,0)-COALESCE(SUM(COALESCE(p.amount,COALESCE(p.qty,1)*COALESCE(p.price,0))),0)) AS delta "
        "FROM work_orders o LEFT JOIN work_order_parts p ON p.order_number=o.number GROUP BY o.number HAVING delta>0.009)"));
    if (partsSumMismatch > 0) addWarning(report, QStringLiteral("Розбіжність parts_sum проти суми запчастин: %1 нарядів").arg(partsSumMismatch));

    const int blankOpenedAt = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_orders WHERE TRIM(COALESCE(opened_at,''))=''"));
    const int blankStatus = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_orders WHERE TRIM(COALESCE(status,''))=''"));
    const int blankOrderedAt = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE TRIM(COALESCE(ordered_at,''))=''"));
    const int blankSentAt = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM outsource_repairs WHERE TRIM(COALESCE(sent_at,''))=''"));
    const int blankExpensePeriod = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM expenses WHERE TRIM(COALESCE(period_from,''))='' OR TRIM(COALESCE(period_to,''))=''"));
    if (blankOpenedAt > 0) addError(report, QStringLiteral("Є наряди без opened_at: %1").arg(blankOpenedAt));
    if (blankStatus > 0) addError(report, QStringLiteral("Є наряди без status: %1").arg(blankStatus));
    if (blankOrderedAt > 0) addWarning(report, QStringLiteral("Є запчастини без ordered_at: %1").arg(blankOrderedAt));
    if (blankSentAt > 0) addWarning(report, QStringLiteral("Є outsource записи без sent_at: %1").arg(blankSentAt));
    if (blankExpensePeriod > 0) addWarning(report, QStringLiteral("Є витрати без повного періоду: %1").arg(blankExpensePeriod));

    const int duplicateOrders = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT number FROM work_orders GROUP BY number HAVING COUNT(*)>1)"));
    if (duplicateOrders > 0) addError(report, QStringLiteral("Після імпорту виявлені дублікати номерів нарядів: %1").arg(duplicateOrders));

    QSqlQuery firstOrderQ(db);
    if (firstOrderQ.exec(QStringLiteral("SELECT number, COALESCE(opened_at,'') FROM work_orders ORDER BY number LIMIT 1")) && firstOrderQ.next()) {
        const int firstOrder = firstOrderQ.value(0).toInt();
        const QString firstOpened = firstOrderQ.value(1).toString();

        const int locatorHits = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM router_order_locator "
            "WHERE CAST(COALESCE(order_number, number) AS INTEGER)=%1").arg(firstOrder));
        if (locatorHits <= 0) {
            addWarning(report, QStringLiteral("Order locator readiness check не знайшов жодного результату для наряду %1").arg(firstOrder));
        } else {
            addInfo(report, QStringLiteral("Order locator readiness check пройдено для наряду %1").arg(firstOrder));
        }

        HistoryQueryParams params;
        const QDate openedDate = DateFormats::parseDate(firstOpened.left(10));
        params.from = openedDate.isValid() ? openedDate : QDate::currentDate().addMonths(-1);
        params.to = params.from.addDays(31);
        const QString historySql = HistoryQueryService::buildHistorySql(params);
        if (historySql.trimmed().isEmpty()) {
            addWarning(report, QStringLiteral("History readiness check повернув порожній SQL"));
        } else {
            QSqlQuery historyQ(db);
            if (!historyQ.exec(historySql)) {
                addWarning(report, QStringLiteral("History readiness check failed: %1").arg(historyQ.lastError().text()));
            } else {
                addInfo(report, QStringLiteral("History readiness check пройдено"));
            }
        }
    }

    QString partsError;
    PartsQueryService::loadRows(db, QString(), QDate(), QDate(), &partsError);
    if (!partsError.trimmed().isEmpty()) addWarning(report, QStringLiteral("Parts readiness check failed: %1").arg(partsError));
    else addInfo(report, QStringLiteral("Parts readiness check пройдено"));

    PlanningRepository repo;
    const QDate today = QDate::currentDate();
    repo.getMonthMaps(today.year(), today.month());
    addInfo(report, QStringLiteral("Planning readiness check пройдено"));

    QSqlQuery reportsQ(db);
    if (!reportsQ.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(works_sum),0), COALESCE(SUM(parts_sum),0) FROM work_orders"))) {
        addWarning(report, QStringLiteral("Reports/PDF data readiness check failed: %1").arg(reportsQ.lastError().text()));
    } else {
        addInfo(report, QStringLiteral("Reports/PDF data readiness check пройдено"));
    }

    QSqlQuery extQ(db);
    if (!extQ.exec(QStringLiteral("SELECT COUNT(*) FROM outsource_repairs"))) {
        addWarning(report, QStringLiteral("Outsource readiness check failed: %1").arg(extQ.lastError().text()));
    }
    if (!extQ.exec(QStringLiteral("SELECT COUNT(*) FROM expenses"))) {
        addWarning(report, QStringLiteral("Expenses readiness check failed: %1").arg(extQ.lastError().text()));
    }
}
