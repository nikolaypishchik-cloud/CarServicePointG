#include "core/services/StatisticsSnapshotService.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

#include "core/services/ReportsDateUtils.h"
#include "core/util/FormatUtils.h"

#include <QDateTime>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <algorithm>

namespace {


QString money0(double v) { return FormatUtils::money(v, 2) + QString::fromUtf8(" грн"); }
QString num0(double v) { return FormatUtils::money(v, 0); }

QString normalizedSqlDateExpr(const QString& expr)
{
    return QStringLiteral(
        "CASE "
        "WHEN TRIM(COALESCE(%1,''))='' THEN NULL "
        "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN "
        "     CASE "
        "         WHEN CAST(substr(TRIM(%1),1,4) AS INTEGER) BETWEEN 1900 AND 1999 THEN date(printf('%04d', CAST(substr(TRIM(%1),1,4) AS INTEGER) + 100) || substr(TRIM(%1),5,6)) "
        "         ELSE date(substr(TRIM(%1),1,10)) "
        "     END "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND length(TRIM(%1))>=8 THEN "
        "     date('20' || substr(TRIM(%1),7,2) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2)) "
        "ELSE NULL END").arg(expr);
}

void appendProblemLimited(QStringList& out, const QString& problem, int maxItems = 24)
{
    if (problem.trimmed().isEmpty()) return;
    if (out.contains(problem)) return;
    if (out.size() >= maxItems) return;
    out << problem;
}

QList<StatisticsSeriesPoint> topSeries(const QMap<QString,double>& map, int limit = 8)
{
    QList<StatisticsSeriesPoint> out;
    for (auto it = map.begin(); it != map.end(); ++it) out.append({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [](const StatisticsSeriesPoint& l, const StatisticsSeriesPoint& r){ return l.value > r.value; });
    if (out.size() > limit) out = out.mid(0, limit);
    return out;
}

QList<StatisticsSeriesPoint> topSeriesInt(const QMap<QString,int>& map, int limit = 8)
{
    QList<StatisticsSeriesPoint> out;
    for (auto it = map.begin(); it != map.end(); ++it) out.append({it.key(), double(it.value())});
    std::sort(out.begin(), out.end(), [](const StatisticsSeriesPoint& l, const StatisticsSeriesPoint& r){ return l.value > r.value; });
    if (out.size() > limit) out = out.mid(0, limit);
    return out;
}

double periodExpenses(QSqlDatabase db, const QDate& from, const QDate& to, QList<StatisticsSeriesPoint>* structure, QString* error)
{
    double elec = 0.0, clean = 0.0, heat = 0.0, rent = 0.0, amort = 0.0, sw = 0.0, other = 0.0;
    QSqlQuery q(db);
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(COALESCE(elec_cost,0)),0), "
        "       COALESCE(SUM(COALESCE(cleaning,0)),0), "
        "       COALESCE(SUM(COALESCE(heating,0)),0), "
        "       COALESCE(SUM(COALESCE(rent,0)),0), "
        "       COALESCE(SUM(COALESCE(amortization,0)),0), "
        "       COALESCE(SUM(COALESCE(sw_opendatabot,0)+COALESCE(sw_tecalliance,0)),0), "
        "       COALESCE(SUM(COALESCE(other_amount,0)),0) "
        "FROM expenses "
        "WHERE date(COALESCE(NULLIF(period_to,''), period_from)) >= date(?) "
        "  AND date(COALESCE(NULLIF(period_from,''), period_to)) <= date(?)"));
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));
    if (q.exec() && q.next()) {
        elec = q.value(0).toDouble();
        clean = q.value(1).toDouble();
        heat = q.value(2).toDouble();
        rent = q.value(3).toDouble();
        amort = q.value(4).toDouble();
        sw = q.value(5).toDouble();
        other = q.value(6).toDouble();
    }
    if (db.tables().contains(QStringLiteral("additional_expenses"))) {
        QSqlQuery extra(db);
        extra.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_cents),0) FROM additional_expenses WHERE date(spent_on)>=date(?) AND date(spent_on)<=date(?)"));
        extra.addBindValue(from.toString(Qt::ISODate));
        extra.addBindValue(to.toString(Qt::ISODate));
        if (extra.exec() && extra.next()) other += extra.value(0).toLongLong() / 100.0;
        extra.next();if(extra.lastError().isValid()) *error=extra.lastError().text();
    }
    if(q.lastError().isValid()) *error=q.lastError().text();
    if (structure) *structure = {{QString::fromUtf8("Електрика"), elec},{QString::fromUtf8("Миючі"), clean},{QString::fromUtf8("Опалення"), heat},{QString::fromUtf8("Оренда"), rent},{QString::fromUtf8("Амортизація"), amort},{QString::fromUtf8("ПЗ"), sw},{QString::fromUtf8("Інше"), other}};
    return elec + clean + heat + rent + amort + sw + other;
}

double periodPayroll(QSqlDatabase db, const QDate& from, const QDate& to, QList<StatisticsSeriesPoint>* byMaster, QList<StatisticsRowItem>* rows, QString* error)
{
    QMap<QString, double> revenueByMaster;
    QMap<QString, int> jobsByMaster;
    const QString closedExpr = normalizedSqlDateExpr(QStringLiteral("wo.closed_at"));

    QSqlQuery q(db);
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(NULLIF(TRIM(m.name),''), '%1'), "
        "       COALESCE(wo.discount,0), "
        "       COALESCE(wo.works_sum,0), "
        "       COALESCE(s.amount,0), "
        "       COALESCE(sm.share_percent,100), "
        "       COALESCE(m.admin_percent,0), COALESCE(wo.discount_mode,'legacy') "
        "FROM router_work_orders wo "
        "JOIN router_work_order_services s ON s.order_number=wo.number "
        "LEFT JOIN router_service_masters sm ON sm.service_id=s.id "
        "LEFT JOIN masters m ON m.id=sm.master_id "
        "WHERE LOWER(COALESCE(wo.status,''))='closed' "
        "  AND (%2) >= date(?) "
        "  AND (%2) <= date(?)")
        .arg(QString::fromUtf8("Без майстра"), closedExpr));
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));

    double total = 0.0;
    if (q.exec()) {
        while (q.next()) {
            const QString name = q.value(0).toString();
            const double discountPct = FormatUtils::discountPercent(q.value(1).toDouble(), q.value(6).toString(), q.value(2).toDouble());
            const double baseShare = q.value(3).toDouble() * q.value(4).toDouble() / 100.0;
            const double discounted = qMax(0.0, FormatUtils::roundMoneyValue(baseShare * (1.0 - discountPct / 100.0)));
            const double payrollAmount = FormatUtils::roundMoneyValue(discounted * q.value(5).toDouble() / 100.0);
            total += payrollAmount;
            revenueByMaster[name] += payrollAmount;
            jobsByMaster[name] += 1;
        }
    }

    if(q.lastError().isValid()) *error=q.lastError().text();
    QList<StatisticsSeriesPoint> masterPoints;
    QList<StatisticsRowItem> masterRows;
    for (auto it = revenueByMaster.begin(); it != revenueByMaster.end(); ++it) {
        masterPoints.append({it.key(), it.value()});
        masterRows.append({it.key(), num0(jobsByMaster.value(it.key())), money0(it.value()), QString(), QString()});
    }
    std::sort(masterPoints.begin(), masterPoints.end(), [](const StatisticsSeriesPoint& l, const StatisticsSeriesPoint& r){ return l.value > r.value; });
    std::sort(masterRows.begin(), masterRows.end(), [](const StatisticsRowItem& l, const StatisticsRowItem& r){ return l.c > r.c; });
    if (byMaster) *byMaster = masterPoints;
    if (rows) *rows = masterRows;
    return total;
}

StatisticsSnapshot collectSnapshot(QSqlDatabase db, const QDate& from, const QDate& to)
{
    StatisticsSnapshot s;
    s.from = from;
    s.to = to;
    double totalJobs = 0.0;

    const QString openedExpr = normalizedSqlDateExpr(QStringLiteral("opened_at"));
    const QString closedExpr = normalizedSqlDateExpr(QStringLiteral("closed_at"));

    QMap<QDate,double> revByDay;
    QMap<QString,double> supplierSpend;
    QMap<QString,double> partUsage;
    QMap<QString,double> clientRevenue;
    QMap<QString,int> clientVisits;
    QMap<QString,int> stageCount;
    QMap<QString,int> masterJobsTmp;
    QMap<int,int> serviceCountByOrder;

    QSqlQuery svcCounts(db);
    svcCounts.setForwardOnly(true);
    svcCounts.prepare(QStringLiteral(
        "SELECT s.order_number, COUNT(*) "
        "FROM router_work_order_services s "
        "JOIN router_work_orders wo ON wo.number=s.order_number "
        "WHERE LOWER(COALESCE(wo.status,''))='closed' "
        "  AND (%1) >= date(?) AND (%1) <= date(?) "
        "GROUP BY s.order_number").arg(closedExpr));
    svcCounts.addBindValue(from.toString(Qt::ISODate));
    svcCounts.addBindValue(to.toString(Qt::ISODate));
    if (svcCounts.exec()) {
        while (svcCounts.next()) {
            serviceCountByOrder.insert(svcCounts.value(0).toInt(), svcCounts.value(1).toInt());
        }
    }

    QSqlQuery closedQ(db);
    closedQ.setForwardOnly(true);
    closedQ.prepare(QStringLiteral(
        "SELECT number, COALESCE(NULLIF(TRIM(client),''), '%1'), COALESCE(works_sum,0), COALESCE(parts_sum,0), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(discount,0), COALESCE(discount_mode,'legacy') "
        "FROM router_work_orders "
        "WHERE LOWER(COALESCE(status,''))='closed' "
        "  AND (%2) >= date(?) AND (%2) <= date(?)")
        .arg(QString::fromUtf8("Без клієнта"), closedExpr));
    closedQ.addBindValue(from.toString(Qt::ISODate));
    closedQ.addBindValue(to.toString(Qt::ISODate));
    if (closedQ.exec()) {
        while (closedQ.next()) {
            const int number = closedQ.value(0).toInt();
            const QString client = closedQ.value(1).toString();
            const double grossWorks = closedQ.value(2).toDouble();
            const double works = FormatUtils::roundMoneyValue(grossWorks * (1.0 - FormatUtils::discountPercent(closedQ.value(6).toDouble(), closedQ.value(7).toString(), grossWorks) / 100.0));
            const double parts = closedQ.value(3).toDouble();
            const QDate opened = ReportsDateUtils::parseFlexibleDate(closedQ.value(4).toString());
            const QDate closed = ReportsDateUtils::parseFlexibleDate(closedQ.value(5).toString());
            s.closedOrders += 1;
            s.revenueLabor += works;
            s.revenueParts += parts;
            if (closed.isValid()) revByDay[closed] += works + parts;
            clientRevenue[client] += works + parts;
            clientVisits[client] += 1;
            if (opened.isValid() && closed.isValid()) s.avgRepairDays += qMax(0LL, opened.daysTo(closed));
            totalJobs += serviceCountByOrder.value(number);
        }
    }

    QSqlQuery openQ(db);
    openQ.setForwardOnly(true);
    openQ.prepare(QStringLiteral(
        "SELECT number, COALESCE(opened_at,''), COALESCE(NULLIF(TRIM(status),''), '%1') "
        "FROM router_work_orders "
        "WHERE LOWER(COALESCE(status,''))!='closed'")
        .arg(QString::fromUtf8("в роботі")));
    if (openQ.exec()) {
        while (openQ.next()) {
            const int number = openQ.value(0).toInt();
            const QDate opened = ReportsDateUtils::parseFlexibleDate(openQ.value(1).toString());
            const QString stage = openQ.value(2).toString();
            s.openOrders += 1;
            s.carsInWork += 1;
            stageCount[stage] += 1;
            if (opened.isValid() && opened.daysTo(QDate::currentDate()) >= 5) {
                appendProblemLimited(s.problems, QString::fromUtf8("Наряд #%1 відкритий %2 днів").arg(number).arg(opened.daysTo(QDate::currentDate())));
            }
        }
    }

    s.expenses = periodExpenses(db, from, to, &s.expenseStructure, &s.error);
    s.payroll = periodPayroll(db, from, to, &s.masterRevenue, &s.masterRows, &s.error);

    QSqlQuery partsQ(db);
    partsQ.setForwardOnly(true);
    partsQ.prepare(QStringLiteral(
        "SELECT COALESCE(NULLIF(TRIM(p.store),''), '%1'), COALESCE(NULLIF(TRIM(p.name),''), '%2'), "
        "       COALESCE(p.qty,0), COALESCE(p.purchase_amount,0), COALESCE(p.amount,0), COALESCE(p.paid,0) "
        "FROM router_work_order_parts p "
        "JOIN router_work_orders wo ON wo.number=p.order_number "
        "WHERE LOWER(COALESCE(wo.status,''))='closed' "
        "  AND (%3) >= date(?) AND (%3) <= date(?)")
        .arg(QString::fromUtf8("Без магазину"), QString::fromUtf8("Без назви"), closedExpr));
    partsQ.addBindValue(from.toString(Qt::ISODate));
    partsQ.addBindValue(to.toString(Qt::ISODate));
    if (partsQ.exec()) {
        while (partsQ.next()) {
            const QString store = partsQ.value(0).toString();
            const QString partName = partsQ.value(1).toString();
            const double qty = partsQ.value(2).toDouble();
            const double purch = partsQ.value(3).toDouble();
            const double sale = partsQ.value(4).toDouble();
            const bool paid = partsQ.value(5).toInt() != 0;
            s.purchaseParts += purch;
            supplierSpend[store] += purch;
            partUsage[partName] += qty;
            if (!paid) s.unpaidOrders += 1;
            if (sale > 0 && purch > sale) {
                appendProblemLimited(s.problems, QString::fromUtf8("Негативна маржа по запчастині: %1").arg(partName));
            }
        }
    }

    s.markupParts = s.revenueParts - s.purchaseParts;

    const QString sentExpr = normalizedSqlDateExpr(QStringLiteral("sent_at"));
    QSqlQuery outsourceQ(db);
    outsourceQ.setForwardOnly(true);
    outsourceQ.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(COALESCE(client_price,0)),0), COALESCE(SUM(COALESCE(repair_cost,0)),0) "
        "FROM router_outsource_repairs WHERE (%1) >= date(?) AND (%1) <= date(?)").arg(sentExpr));
    outsourceQ.addBindValue(from.toString(Qt::ISODate));
    outsourceQ.addBindValue(to.toString(Qt::ISODate));
    if (outsourceQ.exec() && outsourceQ.next()) {
        s.outsourceRevenue = outsourceQ.value(0).toDouble();
        s.outsourceCost = outsourceQ.value(1).toDouble();
    }

    QSqlQuery masterJobsQ(db);
    masterJobsQ.setForwardOnly(true);
    masterJobsQ.prepare(QStringLiteral(
        "SELECT COALESCE(NULLIF(TRIM(m.name),''), '%1'), COUNT(*) "
        "FROM router_service_masters sm "
        "JOIN router_work_order_services s ON s.id=sm.service_id "
        "JOIN router_work_orders wo ON wo.number=s.order_number "
        "LEFT JOIN masters m ON m.id=sm.master_id "
        "WHERE LOWER(COALESCE(wo.status,''))='closed' "
        "  AND (%2) >= date(?) AND (%2) <= date(?) "
        "GROUP BY COALESCE(NULLIF(TRIM(m.name),''), '%1')")
        .arg(QString::fromUtf8("Без майстра"), closedExpr));
    masterJobsQ.addBindValue(from.toString(Qt::ISODate));
    masterJobsQ.addBindValue(to.toString(Qt::ISODate));
    if (masterJobsQ.exec()) {
        while (masterJobsQ.next()) {
            masterJobsTmp[masterJobsQ.value(0).toString()] = masterJobsQ.value(1).toInt();
        }
    }

    outsourceQ.next();
    for(const auto* query : {&svcCounts,&closedQ,&openQ,&partsQ,&outsourceQ,&masterJobsQ})
        if(query->lastError().isValid()) s.error=query->lastError().text();
    s.outsourceMargin = s.outsourceRevenue - s.outsourceCost;
    s.grossProfit = s.revenueLabor + s.revenueParts - s.purchaseParts - s.payroll + s.outsourceMargin;
    s.netProfit = s.grossProfit - s.expenses;
    s.avgCheck = s.closedOrders > 0 ? (s.revenueLabor + s.revenueParts) / s.closedOrders : 0.0;
    s.avgRepairDays = s.closedOrders > 0 ? s.avgRepairDays / s.closedOrders : 0.0;
    s.avgJobsPerOrder = s.closedOrders > 0 ? totalJobs / s.closedOrders : 0.0;
    s.uniqueClients = clientRevenue.size();
    for (auto it = clientVisits.begin(); it != clientVisits.end(); ++it) if (it.value() > 1) s.repeatedClients += 1;

    for (QDate d = from; d <= to; d = d.addDays(1)) s.dailyRevenue.append({d.toString("dd.MM"), revByDay.value(d)});
    s.incomeStructure = {{QString::fromUtf8("Дохід"), s.revenueLabor},{QString::fromUtf8("Запчастини"), s.revenueParts},{QString::fromUtf8("Чистий прибуток"), s.netProfit}};
    s.supplierSpend = topSeries(supplierSpend);
    s.partUsage = topSeries(partUsage);
    s.clientRevenue = topSeries(clientRevenue);
    s.repairStages = topSeriesInt(stageCount);
    s.masterJobs = topSeriesInt(masterJobsTmp);

    for (const auto& sp : s.clientRevenue) s.clientRows.append({sp.label, money0(sp.value), QString(), QString(), QString()});
    if (s.unpaidOrders > 0) appendProblemLimited(s.problems, QString::fromUtf8("Є неоплачені / не підтверджені позиції запчастин: %1").arg(s.unpaidOrders));
    if (s.netProfit < 0) appendProblemLimited(s.problems, QString::fromUtf8("Чистий прибуток від’ємний за вибраний період"));
    if (s.carsInWork > 5) appendProblemLimited(s.problems, QString::fromUtf8("Багато авто в роботі зараз: %1").arg(s.carsInWork));
    if (s.problems.isEmpty()) s.problems << QString::fromUtf8("Критичних попереджень не виявлено");
    return s;
}

} // namespace

namespace StatisticsSnapshotService {

QString currentDatabasePath()
{
    return QSqlDatabase::database(currentConnectionName(),false).databaseName();
}

StatisticsSnapshot collectCurrent(const QDate& from, const QDate& to)
{
    return collectFromPath(currentDatabasePath(), from, to);
}


StatisticsSnapshot collectFromPath(const QString& dbPath, const QDate& from, const QDate& to)
{
    if (dbPath.trimmed().isEmpty()) {
        StatisticsSnapshot s;
        s.from = from;
        s.to = to;
        s.error=QString::fromUtf8("Не вдалося визначити шлях до робочої бази даних");
        s.problems << s.error;
        return s;
    }

    const QString connectionName = QStringLiteral("stats_bg_%1_%2")
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16)
        .arg(QDateTime::currentMSecsSinceEpoch());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dbPath);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    if (!db.open()) {
        StatisticsSnapshot s;
        s.from = from;
        s.to = to;
        s.error=QString::fromUtf8("Не вдалося відкрити базу для статистики: %1").arg(db.lastError().text());
        s.problems << s.error;
        db = QSqlDatabase();
        SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
        return s;
    }

    StatisticsSnapshot out;
    if(!from.isValid() || !to.isValid() || from>to) out.error=QStringLiteral("Invalid statistics period");
    else if(!db.transaction()) out.error=db.lastError().text();
    else {out=collectSnapshot(db,from,to);if(out.error.isEmpty() && !db.commit())out.error=db.lastError().text();}
    if(!out.error.isEmpty()){const auto error=out.error;db.rollback();out={};out.from=from;out.to=to;out.error=error;out.problems<<error;}
    db.close();
    db = QSqlDatabase();
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return out;
}

} // namespace StatisticsSnapshotService
