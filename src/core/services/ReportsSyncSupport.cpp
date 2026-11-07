#include "core/services/ReportsSyncSupport.h"
#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "core/services/ReportsDateUtils.h"

#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDate>
#include <QSqlQuery>
#include <QSqlError>
#include <QtMath>

namespace {

struct PayrollPercentResolver {
    QMap<QString, double> byId;
    QMap<QString, double> activeByName;
};

QString payrollMasterNameKey(QString value)
{
    return value.simplified().toCaseFolded();
}

QString formatPlateForUi(const QString& value)
{
    const QString out = PlateUtils::formatPlateDisplay(value);
    return out.trimmed().isEmpty() ? value.trimmed() : out.trimmed();
}

QString composePlateCarForUi(const QString& plate, const QString& car)
{
    const QString plateText = formatPlateForUi(plate);
    const QString carText = car.trimmed();
    if (plateText.isEmpty()) return carText;
    if (carText.isEmpty()) return plateText;
    return plateText + QString::fromUtf8(" | ") + carText;
}

PayrollPercentResolver makePayrollPercentResolver()
{
    PayrollPercentResolver out;
    out.byId = Database::payrollPercents();

    const auto allMasters = Database::masters(false);
    for (const auto& master : allMasters) {
        if (master.role != QStringLiteral("master") || !master.active) continue;
        if (!out.byId.contains(master.id)) continue;

        const QString key = payrollMasterNameKey(master.name);
        if (key.isEmpty()) continue;

        const double pct = out.byId.value(master.id, 0.0);
        if (!out.activeByName.contains(key) || (qFuzzyIsNull(out.activeByName.value(key)) && !qFuzzyIsNull(pct))) {
            out.activeByName.insert(key, pct);
        }
    }
    return out;
}

double resolvePayrollPercent(const MasterRow& master, const PayrollPercentResolver& resolver)
{
    if (master.role == QStringLiteral("admin")) return master.adminPercent;
    if (resolver.byId.contains(master.id)) return resolver.byId.value(master.id, 0.0);

    const QString key = payrollMasterNameKey(master.name);
    if (!key.isEmpty() && resolver.activeByName.contains(key)) {
        return resolver.activeByName.value(key, 0.0);
    }
    return 0.0;
}

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

QString payrollWorksTextForMasterImpl(const QString& orderNumber, const QString& masterId, double payrollPercent)
{
    QSqlQuery dq(Database::db());
    dq.prepare(QStringLiteral("SELECT COALESCE(discount,0), COALESCE(works_sum,0), COALESCE(discount_mode,'legacy') FROM router_work_orders WHERE number=?"));
    dq.addBindValue(orderNumber);
    double discountPct = 0.0;
    if (dq.exec() && dq.next()) {
        discountPct = FormatUtils::discountPercent(dq.value(0).toDouble(), dq.value(2).toString(), dq.value(1).toDouble());
    }

    QStringList works;
    QSqlQuery svcQ(Database::db());
    svcQ.prepare(
        QStringLiteral("SELECT COALESCE(s.title,''), COALESCE(s.amount,0), COALESCE(sm.share_percent,0) "
                       "FROM router_work_order_services s "
                       "JOIN router_service_masters sm ON sm.service_id = s.id "
                       "WHERE s.order_number=? AND sm.master_id=? "
                       "AND NOT EXISTS (SELECT 1 FROM outsource_jobs oj WHERE oj.service_id = s.id) "
                       "ORDER BY s.id ASC"));
    svcQ.addBindValue(orderNumber);
    svcQ.addBindValue(masterId);
    if (svcQ.exec()) {
        while (svcQ.next()) {
            const QString title = svcQ.value(0).toString();
            const double sharedAmount = svcQ.value(1).toDouble() * svcQ.value(2).toDouble() / 100.0;
            const double discountedAmount = qMax(0.0, FormatUtils::roundMoneyValue(sharedAmount * (1.0 - discountPct / 100.0)));
            const double payrollAmount = FormatUtils::roundMoneyValue(discountedAmount * payrollPercent / 100.0);
            works << QString::fromUtf8("%1 — %2 грн").arg(title, money0(payrollAmount));
        }
    }
    return works.join(QStringLiteral("\n"));
}

void ensurePayrollOrderAdjustmentsTable()
{
    QSqlQuery q(Database::db());
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS payroll_order_adjustments ("
        "master_id TEXT NOT NULL,"
        "order_number INTEGER NOT NULL,"
        "absent INTEGER NOT NULL DEFAULT 0,"
        "deleted INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT '',"
        "PRIMARY KEY(master_id, order_number))"));
}

bool upsertPayrollOrderAdjustment(const QString& masterId,
                                  int orderNumber,
                                  const QString& columnName,
                                  bool value,
                                  QString* errorMessage)
{
    const QString cleanMasterId = masterId.trimmed();
    if (cleanMasterId.isEmpty() || orderNumber <= 0) {
        if (errorMessage) *errorMessage = QString::fromUtf8("Некоректний майстер або наряд.");
        return false;
    }
    if (columnName != QStringLiteral("absent") && columnName != QStringLiteral("deleted")) {
        if (errorMessage) *errorMessage = QString::fromUtf8("Некоректне поле деталізації зарплати.");
        return false;
    }

    ensurePayrollOrderAdjustmentsTable();
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "INSERT INTO payroll_order_adjustments(master_id, order_number, %1, updated_at) "
        "VALUES(?, ?, ?, datetime('now')) "
        "ON CONFLICT(master_id, order_number) DO UPDATE SET %1=excluded.%1, updated_at=datetime('now')")
        .arg(columnName));
    q.addBindValue(cleanMasterId);
    q.addBindValue(orderNumber);
    q.addBindValue(value ? 1 : 0);
    if (!q.exec()) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

namespace ReportsSyncSupport {

QString payrollWorksTextForMaster(const QString& orderNumber, const QString& masterId, double payrollPercent)
{
    return payrollWorksTextForMasterImpl(orderNumber, masterId, payrollPercent);
}

QList<PayrollOrderRow> collectPayrollOrdersForMaster(const QString& masterId, const QString& df, const QString& dt)
{
    QList<PayrollOrderRow> out;
    if (masterId.trimmed().isEmpty()) return out;
    ensurePayrollOrderAdjustmentsTable();

    const auto masters = Database::mastersForPeriod(df, dt);
    const PayrollPercentResolver payrollResolver = makePayrollPercentResolver();
    QString role;
    double percent = 0.0;
    for (const auto& m : masters) {
        if (m.id == masterId) {
            role = m.role;
            percent = resolvePayrollPercent(m, payrollResolver);
            break;
        }
    }

    if (role == QStringLiteral("admin")) {
        QSqlQuery q(Database::db());
        q.prepare(
            QStringLiteral("SELECT wo.number, COALESCE(wo.order_number,wo.number), substr(COALESCE(NULLIF(wo.opened_at,''),''),1,10), substr(NULLIF(TRIM(wo.closed_at),''),1,10), "
                           "COALESCE(wo.plate,''), COALESCE(wo.car,''), "
                           "COALESCE((SELECT GROUP_CONCAT(s.title, ', ') FROM router_work_order_services s WHERE s.order_number = wo.number), ''), "
                           "COALESCE(wo.works_sum,0) "
                           "FROM router_work_orders wo "
                           "WHERE wo.status='closed' "
                           "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                           "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                           "ORDER BY date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) DESC, wo.number DESC"));
        q.addBindValue(df);
        q.addBindValue(dt);
        if (q.exec()) {
            while (q.next()) {
                PayrollOrderRow row;
                row.internalOrderNumber = q.value(0).toInt();
                row.orderNumber = q.value(1).toString();
                row.dateStart = ReportsDateUtils::displayDate(q.value(2).toString());
                row.dateEnd = ReportsDateUtils::displayDate(q.value(3).toString(), q.value(2).toString());
                row.car = composePlateCarForUi(q.value(4).toString(), q.value(5).toString());
                row.works = q.value(6).toString();
                row.amount = q.value(7).toDouble();
                row.absent = false;
                row.masterAmount = FormatUtils::roundMoneyValue(row.amount * percent / 100.0);
                out.append(row);
            }
        }
        return out;
    }

    QSqlQuery q(Database::db());
    q.prepare(
        QStringLiteral("SELECT wo.number, COALESCE(wo.order_number,wo.number), substr(COALESCE(NULLIF(wo.opened_at,''),''),1,10), substr(NULLIF(TRIM(wo.closed_at),''),1,10), "
                       "COALESCE(wo.plate,''), COALESCE(wo.car,''), "
                       "COALESCE(SUM(COALESCE(s.amount,0) * COALESCE(sm.share_percent,0) / 100.0), 0) AS share_raw, "
                       "COALESCE(wo.discount,0), COALESCE(wo.works_sum,0), COALESCE(adj.absent,0), COALESCE(wo.discount_mode,'legacy') "
                       "FROM router_work_orders wo "
                       "JOIN router_work_order_services s ON s.order_number = wo.number "
                       "JOIN router_service_masters sm ON sm.service_id = s.id "
                       "LEFT JOIN payroll_order_adjustments adj ON adj.master_id = sm.master_id AND adj.order_number = wo.number "
                       "WHERE wo.status='closed' AND sm.master_id=? "
                       "AND COALESCE(adj.deleted,0)=0 "
                       "AND NOT EXISTS (SELECT 1 FROM outsource_jobs oj WHERE oj.service_id = s.id) "
                       "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                       "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                       "GROUP BY wo.number, wo.order_number, wo.opened_at, wo.closed_at, wo.plate, wo.car, wo.discount, wo.works_sum, adj.absent "
                       "ORDER BY date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) ASC, wo.number ASC"));
    q.addBindValue(masterId);
    q.addBindValue(df);
    q.addBindValue(dt);

    if (q.exec()) {
        while (q.next()) {
            PayrollOrderRow row;
            row.internalOrderNumber = q.value(0).toInt();
            row.orderNumber = q.value(1).toString();
            row.dateStart = ReportsDateUtils::displayDate(q.value(2).toString());
            row.dateEnd = ReportsDateUtils::displayDate(q.value(3).toString(), q.value(2).toString());
            row.car = composePlateCarForUi(q.value(4).toString(), q.value(5).toString());
            const double shareRaw = q.value(6).toDouble();
            const double discountRaw = q.value(7).toDouble();
            const double worksTotal = q.value(8).toDouble();
            const double discountPct = FormatUtils::discountPercent(discountRaw, q.value(10).toString(), worksTotal);
            row.absent = q.value(9).toInt() != 0;
            row.amount = qMax(0.0, FormatUtils::roundMoneyValue(shareRaw * (1.0 - discountPct / 100.0)));
            row.masterAmount = row.absent ? 0.0 : FormatUtils::roundMoneyValue(row.amount * percent / 100.0);
            row.works = payrollWorksTextForMaster(QString::number(row.internalOrderNumber), masterId, percent);
            out.append(row);
        }
    }
    return out;
}

bool setPayrollOrderAbsent(const QString& masterId, int orderNumber, bool absent, QString* errorMessage)
{
    return upsertPayrollOrderAdjustment(masterId, orderNumber, QStringLiteral("absent"), absent, errorMessage);
}

bool setPayrollOrderDeleted(const QString& masterId, int orderNumber, bool deleted, QString* errorMessage)
{
    return upsertPayrollOrderAdjustment(masterId, orderNumber, QStringLiteral("deleted"), deleted, errorMessage);
}

bool saveExpensesRecord(const QString& df, const QString& dt, const ExpensesSavePayload& payload, QString* errorMessage)
{
    const QString beforeJson = LegacyAncillaryMutationLogSupport::snapshotExpensePeriod(Database::db(), df, dt);
    QSqlQuery del(Database::db());
    del.prepare(QStringLiteral("DELETE FROM expenses WHERE (CASE WHEN substr(period_from,3,1)='.' THEN '20'||substr(period_from,7,2)||'-'||substr(period_from,4,2)||'-'||substr(period_from,1,2) ELSE period_from END)=? AND (CASE WHEN substr(period_to,3,1)='.' THEN '20'||substr(period_to,7,2)||'-'||substr(period_to,4,2)||'-'||substr(period_to,1,2) ELSE period_to END)=?"));
    del.addBindValue(df);
    del.addBindValue(dt);
    if (!del.exec() && errorMessage) *errorMessage = del.lastError().text();

    QSqlQuery ins(Database::db());
    ins.prepare(QStringLiteral("INSERT INTO expenses(period_from, period_to, elec_prev, elec_curr, elec_tariff, cleaning, heating, rent, amortization, sw_opendatabot, sw_tecalliance, other_amount, cleaning_note, software_note, heating_note, amortization_note, rent_note, other_note, elec_kwh, elec_cost) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (auto v : {QVariant(df), QVariant(dt), QVariant(payload.elecPrev), QVariant(payload.elecCurr), QVariant(payload.elecTariff), QVariant(payload.cleaning), QVariant(payload.heating), QVariant(payload.rent), QVariant(payload.amortization), QVariant(payload.software), QVariant(0.0), QVariant(payload.other), QVariant(payload.cleaningNote.trimmed()), QVariant(payload.softwareNote.trimmed()), QVariant(payload.heatingNote.trimmed()), QVariant(payload.amortizationNote.trimmed()), QVariant(payload.rentNote.trimmed()), QVariant(payload.otherNote.trimmed()), QVariant(payload.elecKwh), QVariant(payload.elecCost)}) ins.addBindValue(v);
    if (!ins.exec()) {
        if (errorMessage) *errorMessage = ins.lastError().text();
        return false;
    }

    QSqlQuery e(Database::db());
    e.prepare(QStringLiteral("DELETE FROM electricity_periods WHERE (CASE WHEN substr(period_from,3,1)='.' THEN '20'||substr(period_from,7,2)||'-'||substr(period_from,4,2)||'-'||substr(period_from,1,2) ELSE period_from END)=? AND (CASE WHEN substr(period_to,3,1)='.' THEN '20'||substr(period_to,7,2)||'-'||substr(period_to,4,2)||'-'||substr(period_to,1,2) ELSE period_to END)=?"));
    e.addBindValue(df);
    e.addBindValue(dt);
    e.exec();

    QSqlQuery ein(Database::db());
    ein.prepare(QStringLiteral("INSERT INTO electricity_periods(period_from, period_to, meter_before, meter_after, consumed_kwh, tariff, amount) VALUES(?,?,?,?,?,?,?)"));
    for (auto v : {QVariant(df), QVariant(dt), QVariant(payload.elecPrev), QVariant(payload.elecCurr), QVariant(payload.elecKwh), QVariant(payload.elecTariff), QVariant(payload.elecCost)}) ein.addBindValue(v);
    if (!ein.exec() && errorMessage) *errorMessage = ein.lastError().text();
    QString logError;
    LegacyAncillaryMutationLogSupport::logExpenseMutation(Database::db(), beforeJson == QStringLiteral("{}") ? QStringLiteral("insert") : QStringLiteral("update"), df, dt, beforeJson, LegacyAncillaryMutationLogSupport::snapshotExpensePeriod(Database::db(), df, dt), &logError);
    return true;
}

QList<ElectricityPeriodRow> loadElectricityPeriods()
{
    QList<ElectricityPeriodRow> out;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT period_from, period_to, meter_before, meter_after, consumed_kwh, tariff, amount FROM electricity_periods ORDER BY date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) ASC"));
    if (!q.exec()) return out;
    while (q.next()) {
        ElectricityPeriodRow row;
        row.periodFrom = q.value(0).toString();
        row.periodTo = q.value(1).toString();
        row.meterBefore = q.value(2).toDouble();
        row.meterAfter = q.value(3).toDouble();
        row.consumedKwh = q.value(4).toDouble();
        row.tariff = q.value(5).toDouble();
        row.amount = q.value(6).toDouble();
        out.append(row);
    }
    return out;
}

} // namespace ReportsSyncSupport
