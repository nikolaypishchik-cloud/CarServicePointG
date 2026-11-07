#include "core/util/DateFormats.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"

#include <QDate>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
QString nonNullText(QString value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizeIsoDate(QString value)
{
    const QDate parsed = DateFormats::parseDate(value);
    if (parsed.isValid()) return parsed.toString(Qt::ISODate);
    value = nonNullText(value).trimmed();
    if (value.isEmpty()) return QStringLiteral("");
    const QString raw = value.left(10);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("dd/MM/yyyy")
    };
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(raw, fmt);
        if (d.isValid()) return d.toString(Qt::ISODate);
    }
    const QDate iso = DateFormats::parseDate(raw);
    return iso.isValid() ? iso.toString(Qt::ISODate) : nonNullText(raw);
}

bool execSql(QSqlDatabase db, const QString& sql, const QString& label, QString* errorText)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (errorText) *errorText = QStringLiteral("%1: %2").arg(label, q.lastError().text());
        return false;
    }
    return true;
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
    while (q.next()) {
        cols.insert(q.value(1).toString().trimmed());
    }
    return cols;
}

bool ensureColumn(QSqlDatabase db, const QString& tableName, const QString& columnName, const QString& ddl, QString* errorText)
{
    const QSet<QString> cols = tableColumns(db, tableName);
    if (cols.contains(columnName)) return true;
    return execSql(db,
                   QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, ddl),
                   QStringLiteral("ensure %1.%2").arg(tableName, columnName),
                   errorText);
}

bool ensureAncillarySchema(QSqlDatabase db, QString* errorText)
{
    if (!execSql(db,
                 QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS parts_catalog ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  part_number TEXT NOT NULL DEFAULT '',"
                     "  name TEXT NOT NULL DEFAULT '',"
                     "  brand TEXT NOT NULL DEFAULT '',"
                     "  price REAL NOT NULL DEFAULT 0"
                     ");"),
                 QStringLiteral("ensure table parts_catalog"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("parts_catalog"), QStringLiteral("group_name"), QStringLiteral("group_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("parts_catalog"), QStringLiteral("supplier"), QStringLiteral("supplier TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("parts_catalog"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS planning_calendar ("
                     "  day TEXT PRIMARY KEY,"
                     "  note TEXT NOT NULL DEFAULT ''"
                     ");"),
                 QStringLiteral("ensure table planning_calendar"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("planning_calendar"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS outsource_repairs ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  order_uid TEXT NOT NULL DEFAULT '',"
                     "  order_number INTEGER NOT NULL DEFAULT 0,"
                     "  sent_at TEXT NOT NULL DEFAULT '',"
                     "  company TEXT NOT NULL DEFAULT '',"
                     "  part_name TEXT NOT NULL DEFAULT '',"
                     "  repair_cost REAL NOT NULL DEFAULT 0,"
                     "  client_price REAL NOT NULL DEFAULT 0,"
                     "  note TEXT NOT NULL DEFAULT '',"
                     "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                     "  service_id INTEGER"
                     ");"),
                 QStringLiteral("ensure table outsource_repairs"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("outsource_repairs"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("outsource_repairs"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("outsource_repairs"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("outsource_repairs"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS outsource_jobs ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  order_uid TEXT NOT NULL DEFAULT '',"
                     "  order_number INTEGER NOT NULL DEFAULT 0,"
                     "  service_id INTEGER NOT NULL DEFAULT 0,"
                     "  company_id INTEGER NOT NULL DEFAULT 0,"
                     "  company_name TEXT NOT NULL DEFAULT '',"
                     "  sent_date TEXT NOT NULL DEFAULT '',"
                     "  return_date TEXT NOT NULL DEFAULT '',"
                     "  part_name TEXT NOT NULL DEFAULT '',"
                     "  company_price REAL NOT NULL DEFAULT 0,"
                     "  markup_percent REAL NOT NULL DEFAULT 0,"
                     "  client_price REAL NOT NULL DEFAULT 0,"
                     "  note TEXT NOT NULL DEFAULT ''"
                     ");"),
                 QStringLiteral("ensure table outsource_jobs"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("outsource_jobs"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS expenses ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  period_from TEXT NOT NULL DEFAULT '',"
                     "  period_to TEXT NOT NULL DEFAULT '',"
                     "  elec_prev REAL NOT NULL DEFAULT 0,"
                     "  elec_curr REAL NOT NULL DEFAULT 0,"
                     "  elec_tariff REAL NOT NULL DEFAULT 0,"
                     "  cleaning REAL NOT NULL DEFAULT 0,"
                     "  heating REAL NOT NULL DEFAULT 0,"
                     "  rent REAL NOT NULL DEFAULT 0,"
                     "  amortization REAL NOT NULL DEFAULT 0,"
                     "  sw_opendatabot REAL NOT NULL DEFAULT 0,"
                     "  sw_tecalliance REAL NOT NULL DEFAULT 0,"
                     "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                     ");"),
                 QStringLiteral("ensure table expenses"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("expenses"), QStringLiteral("other_note"), QStringLiteral("other_note TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("expenses"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    return true;
}
}

bool LegacyCanonicalAncillaryImportSupport::finalizeImportedAncillaryData(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Ancillary normalization DB is invalid");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    if (!ensureAncillarySchema(database, errorText)) return false;

    const bool hasWorkOrders = tableExists(database, QStringLiteral("work_orders"));
    const bool hasServices = tableExists(database, QStringLiteral("work_order_services"));
    const bool hasPartsCatalog = tableExists(database, QStringLiteral("parts_catalog"));
    const bool hasPlanning = tableExists(database, QStringLiteral("planning_calendar"));
    const bool hasOutsourceRepairs = tableExists(database, QStringLiteral("outsource_repairs"));
    const bool hasOutsourceJobs = tableExists(database, QStringLiteral("outsource_jobs"));
    const bool hasExpenses = tableExists(database, QStringLiteral("expenses"));

    const QSet<QString> workOrderCols = hasWorkOrders ? tableColumns(database, QStringLiteral("work_orders")) : QSet<QString>();
    const QSet<QString> serviceCols = hasServices ? tableColumns(database, QStringLiteral("work_order_services")) : QSet<QString>();
    const QSet<QString> partCols = hasPartsCatalog ? tableColumns(database, QStringLiteral("parts_catalog")) : QSet<QString>();
    const QSet<QString> planningCols = hasPlanning ? tableColumns(database, QStringLiteral("planning_calendar")) : QSet<QString>();
    const QSet<QString> outsourceCols = hasOutsourceRepairs ? tableColumns(database, QStringLiteral("outsource_repairs")) : QSet<QString>();
    const QSet<QString> outsourceJobCols = hasOutsourceJobs ? tableColumns(database, QStringLiteral("outsource_jobs")) : QSet<QString>();
    const QSet<QString> expenseCols = hasExpenses ? tableColumns(database, QStringLiteral("expenses")) : QSet<QString>();

    if (hasWorkOrders && workOrderCols.contains(QStringLiteral("number")) && workOrderCols.contains(QStringLiteral("order_number"))) {
        if (!execSql(database,
                     QStringLiteral("UPDATE work_orders SET order_number=number WHERE COALESCE(order_number,0)=0"),
                     QStringLiteral("normalize work_orders.order_number"), errorText)) return false;
    }
    if (hasWorkOrders && workOrderCols.contains(QStringLiteral("order_uid")) && workOrderCols.contains(QStringLiteral("number"))) {
        const QString fallbackOrderNumberExpr = workOrderCols.contains(QStringLiteral("order_number"))
                ? QStringLiteral("COALESCE(NULLIF(order_number,0), number)")
                : QStringLiteral("number");
        if (!execSql(database,
                     QStringLiteral(
                         "UPDATE work_orders SET order_uid=CASE "
                         "WHEN TRIM(COALESCE(order_uid,''))<>'' THEN order_uid "
                         "WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 "
                         "ELSE '' END")
                         .arg(fallbackOrderNumberExpr),
                     QStringLiteral("normalize work_orders.order_uid"), errorText)) return false;
    }
    if (hasServices && serviceCols.contains(QStringLiteral("order_uid")) && serviceCols.contains(QStringLiteral("order_number"))) {
        const QString linkedOrderUidExpr = (hasWorkOrders && workOrderCols.contains(QStringLiteral("number")) && workOrderCols.contains(QStringLiteral("order_uid")))
                ? QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number=work_order_services.order_number)")
                : QStringLiteral("NULL");
        if (!execSql(database,
                     QStringLiteral(
                         "UPDATE work_order_services SET order_uid=COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), %1, "
                         "CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END)")
                         .arg(linkedOrderUidExpr),
                     QStringLiteral("normalize work_order_services.order_uid"), errorText)) return false;
    }

    if (hasPartsCatalog) {
        if (partCols.contains(QStringLiteral("name")) || partCols.contains(QStringLiteral("brand")) || partCols.contains(QStringLiteral("note")) || partCols.contains(QStringLiteral("supplier"))) {
            QStringList assignments;
            if (partCols.contains(QStringLiteral("name"))) assignments << QStringLiteral("name=TRIM(COALESCE(name,''))");
            if (partCols.contains(QStringLiteral("brand"))) assignments << QStringLiteral("brand=TRIM(COALESCE(brand,''))");
            if (partCols.contains(QStringLiteral("note"))) assignments << QStringLiteral("note=TRIM(COALESCE(note,''))");
            if (partCols.contains(QStringLiteral("supplier"))) assignments << QStringLiteral("supplier=TRIM(COALESCE(supplier,''))");
            if (!assignments.isEmpty() && !execSql(database, QStringLiteral("UPDATE parts_catalog SET %1").arg(assignments.join(QStringLiteral(", "))), QStringLiteral("normalize parts_catalog.trim"), errorText)) return false;
        }
        if (partCols.contains(QStringLiteral("group_name")) && !execSql(database, QStringLiteral("UPDATE parts_catalog SET group_name=TRIM(COALESCE(group_name,''))"), QStringLiteral("normalize parts_catalog.group_name"), errorText)) return false;
        if (partCols.contains(QStringLiteral("part_number")) && !execSql(database, QStringLiteral("UPDATE parts_catalog SET part_number=COALESCE(part_number,'')"), QStringLiteral("normalize parts_catalog.part_number"), errorText)) return false;
    }

    if (hasPlanning && planningCols.contains(QStringLiteral("day")) && planningCols.contains(QStringLiteral("note"))) {
        QSqlQuery q(database);
        if (!q.exec(QStringLiteral("SELECT day, note FROM planning_calendar"))) {
            if (errorText) *errorText = QStringLiteral("normalize planning_calendar.read: %1").arg(q.lastError().text());
            return false;
        }
        struct Row { QString oldDay; QString newDay; QString note; };
        QList<Row> rows;
        while (q.next()) {
            const QString oldDay = q.value(0).toString();
            rows.push_back(Row{nonNullText(oldDay), normalizeIsoDate(oldDay), nonNullText(q.value(1).toString())});
        }
        for (const Row& row : rows) {
            if (row.newDay.isEmpty() || row.newDay == row.oldDay) continue;
            QSqlQuery del(database);
            del.prepare(QStringLiteral("DELETE FROM planning_calendar WHERE day=?"));
            del.addBindValue(row.oldDay);
            if (!del.exec()) {
                if (errorText) *errorText = QStringLiteral("normalize planning_calendar.delete: %1").arg(del.lastError().text());
                return false;
            }
            QSqlQuery ins(database);
            ins.prepare(QStringLiteral("INSERT OR REPLACE INTO planning_calendar(day, note) VALUES(?,?)"));
            ins.addBindValue(nonNullText(row.newDay));
            ins.addBindValue(nonNullText(row.note));
            if (!ins.exec()) {
                if (errorText) *errorText = QStringLiteral("normalize planning_calendar.insert: %1").arg(ins.lastError().text());
                return false;
            }
        }
    }

    if (hasOutsourceRepairs) {
        if (outsourceCols.contains(QStringLiteral("order_uid")) && outsourceCols.contains(QStringLiteral("order_number"))) {
            const QString linkedOrderUidExpr = (hasWorkOrders && workOrderCols.contains(QStringLiteral("number")) && workOrderCols.contains(QStringLiteral("order_uid")))
                    ? QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number=outsource_repairs.order_number)")
                    : QStringLiteral("NULL");
            if (!execSql(database,
                         QStringLiteral(
                             "UPDATE outsource_repairs SET order_uid=COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), %1, "
                             "CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END)")
                             .arg(linkedOrderUidExpr),
                         QStringLiteral("normalize outsource_repairs.order_uid"), errorText)) return false;
        }
        if (outsourceCols.contains(QStringLiteral("service_id")) && hasServices && serviceCols.contains(QStringLiteral("id"))) {
            if (!execSql(database,
                         QStringLiteral("UPDATE outsource_repairs SET service_id=NULL WHERE COALESCE(service_id,0)>0 AND service_id NOT IN (SELECT id FROM work_order_services)"),
                         QStringLiteral("normalize outsource_repairs.service_id_missing"), errorText)) return false;
            if (outsourceCols.contains(QStringLiteral("order_number")) && serviceCols.contains(QStringLiteral("order_number"))
                    && !execSql(database,
                                QStringLiteral("UPDATE outsource_repairs SET service_id=NULL WHERE COALESCE(service_id,0)>0 AND EXISTS (SELECT 1 FROM work_order_services s WHERE s.id=outsource_repairs.service_id AND s.order_number<>outsource_repairs.order_number)"),
                                QStringLiteral("normalize outsource_repairs.service_id_cross_order"), errorText)) return false;
        }
        if (hasOutsourceJobs && outsourceCols.contains(QStringLiteral("service_id")) && outsourceCols.contains(QStringLiteral("order_number")) && outsourceCols.contains(QStringLiteral("order_uid"))
                && outsourceJobCols.contains(QStringLiteral("service_id")) && hasServices && serviceCols.contains(QStringLiteral("id")) && serviceCols.contains(QStringLiteral("order_number"))) {
            const QString noteExpr = outsourceCols.contains(QStringLiteral("note")) ? QStringLiteral("COALESCE(r.note,'')") : QStringLiteral("''");
            const QString companyExpr = outsourceCols.contains(QStringLiteral("company")) ? QStringLiteral("COALESCE(r.company,'')") : QStringLiteral("''");
            const QString sentExpr = outsourceCols.contains(QStringLiteral("sent_at")) ? QStringLiteral("substr(COALESCE(r.sent_at,''),1,10)") : QStringLiteral("''");
            const QString partNameExpr = outsourceCols.contains(QStringLiteral("part_name")) ? QStringLiteral("COALESCE(r.part_name,'')") : QStringLiteral("''");
            const QString repairCostExpr = outsourceCols.contains(QStringLiteral("repair_cost")) ? QStringLiteral("COALESCE(r.repair_cost,0)") : QStringLiteral("0");
            const QString clientPriceExpr = outsourceCols.contains(QStringLiteral("client_price")) ? QStringLiteral("COALESCE(r.client_price,0)") : QStringLiteral("0");
            if (!execSql(database, QStringLiteral(
                "INSERT INTO outsource_jobs(order_uid, order_number, service_id, company_id, company_name, sent_date, return_date, part_name, company_price, markup_percent, client_price, note) "
                "SELECT COALESCE(r.order_uid,''), r.order_number, r.service_id, 0, %1, %2, '', %3, %4, 0, %5, %6 "
                "FROM outsource_repairs r "
                "WHERE COALESCE(r.service_id,0)>0 "
                "AND EXISTS (SELECT 1 FROM work_order_services s WHERE s.id=r.service_id AND s.order_number=r.order_number) "
                "AND NOT EXISTS (SELECT 1 FROM outsource_jobs oj WHERE oj.service_id=r.service_id)")
                .arg(companyExpr, sentExpr, partNameExpr, repairCostExpr, clientPriceExpr, noteExpr),
                QStringLiteral("normalize outsource_jobs.import"), errorText)) return false;
        }
    }

    if (hasExpenses && expenseCols.contains(QStringLiteral("id")) && expenseCols.contains(QStringLiteral("period_from")) && expenseCols.contains(QStringLiteral("period_to"))) {
        QSqlQuery q(database);
        if (!q.exec(QStringLiteral("SELECT id, period_from, period_to FROM expenses"))) {
            if (errorText) *errorText = QStringLiteral("normalize expenses.read: %1").arg(q.lastError().text());
            return false;
        }
        struct ExpRow { int id; QString from; QString to; };
        QList<ExpRow> rows;
        while (q.next()) rows.push_back(ExpRow{q.value(0).toInt(), nonNullText(normalizeIsoDate(q.value(1).toString())), nonNullText(normalizeIsoDate(q.value(2).toString()))});
        QSqlQuery upd(database);
        upd.prepare(QStringLiteral("UPDATE expenses SET period_from=?, period_to=? WHERE id=?"));
        for (const ExpRow& row : rows) {
            upd.addBindValue(nonNullText(row.from));
            upd.addBindValue(nonNullText(row.to));
            upd.addBindValue(row.id);
            if (!upd.exec()) {
                if (errorText) *errorText = QStringLiteral("normalize expenses.update_dates: %1").arg(upd.lastError().text());
                return false;
            }
            upd.clear();
            upd.prepare(QStringLiteral("UPDATE expenses SET period_from=?, period_to=? WHERE id=?"));
        }
        if (expenseCols.contains(QStringLiteral("other_note")) && !execSql(database, QStringLiteral("UPDATE expenses SET other_note=COALESCE(other_note,'')"), QStringLiteral("normalize expenses.other_note"), errorText)) return false;
        if (expenseCols.contains(QStringLiteral("created_at")) && !execSql(database, QStringLiteral("UPDATE expenses SET created_at=COALESCE(NULLIF(created_at,''), datetime('now'))"), QStringLiteral("normalize expenses.created_at"), errorText)) return false;
    }

    return true;
}
