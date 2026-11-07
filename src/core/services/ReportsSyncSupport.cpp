#include "core/services/PayrollIdentityRead.h"
#include "core/services/ReportsSyncSupport.h"
#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "core/services/ReportsDateUtils.h"

#include "core/util/FormatUtils.h"
#include "core/util/DateFormats.h"
#include "core/util/PlateUtils.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"

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

QString payrollWorksTextForMasterImpl(QSqlDatabase db,const QString& orderNumber, const QString& masterId, double payrollPercent, QString* error)
{
    const auto rows=PayrollIdentityRead::read(db,masterId,{}, {},payrollPercent,error,orderNumber);
    return rows.isEmpty()?QString():rows.first().works;
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

    QSqlQuery owner(Database::db());
    owner.prepare(QStringLiteral("SELECT COUNT(*) FROM router_work_orders WHERE number=?"));
    owner.addBindValue(orderNumber);
    if(!owner.exec()||!owner.next()||owner.value(0).toInt()!=1) {
        if(errorMessage)*errorMessage=owner.lastError().isValid()?owner.lastError().text():QStringLiteral("Payroll adjustment order identity is ambiguous or unavailable");
        return false;
    }
    owner.finish();
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

QString payrollWorksTextForMaster(const QString& orderNumber, const QString& masterId, double payrollPercent, QString* error)
{
    auto db=QSqlDatabase::database(currentConnectionName(),false);
    if(!db.isOpen()){if(error)*error=QStringLiteral("Payroll database is not open");return {};}
    return payrollWorksTextForMasterImpl(db,orderNumber, masterId, payrollPercent,error);
}

QList<PayrollOrderRow> collectPayrollOrdersForMaster(const QString& masterId,const QString& df,const QString& dt,QString* error)
{
    return collectPayrollOrdersForMaster(QSqlDatabase::database(currentConnectionName(),false),masterId,df,dt,error);
}

QList<PayrollOrderRow> collectPayrollOrdersForMaster(QSqlDatabase db,const QString& masterId, const QString& df, const QString& dt, QString* error)
{
    QList<PayrollOrderRow> out;QString failure;
    if(error)error->clear();
    auto fail=[&](){if(error)*error=failure;return QList<PayrollOrderRow>{};};
    if(!db.isOpen()){failure=QStringLiteral("Payroll database is not open");return fail();}
    if(masterId.trimmed().isEmpty()){failure=QStringLiteral("Payroll employee is required");return fail();}
    const auto masters=ReportsDataLoader::LocalSupport::loadMastersForPeriodForReports(db,df,dt,&failure);if(!failure.isEmpty())return fail();
    const auto checked=ReportsDataLoader::LocalSupport::makePayrollPercentResolverForReports(db,&failure);if(!failure.isEmpty())return fail();
    const PayrollPercentResolver payrollResolver{checked.byId,checked.activeByName};
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
        PayrollIdentityRead::AdminTitles titles;
        if(!titles.loadFrom(db,&failure))return fail();
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("SELECT wo.number, COALESCE(wo.order_number,wo.number), substr(COALESCE(NULLIF(wo.opened_at,''),''),1,10), substr(NULLIF(TRIM(wo.closed_at),''),1,10), "
                           "COALESCE(wo.plate,''), COALESCE(wo.car,''), "
                           "%1, "
                           "COALESCE(wo.works_sum,0) "
                           "FROM router_work_orders wo "
                           "WHERE wo.status='closed' "
                           "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                           "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                           "ORDER BY date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) DESC, wo.number DESC").arg(titles.hasUid?QStringLiteral("wo.order_uid"):QStringLiteral("NULL")));
        q.addBindValue(df);
        q.addBindValue(dt);
        if(!q.exec()){failure=q.lastError().text();return fail();}
        {
            while (q.next()) {
                PayrollOrderRow row;
                row.internalOrderNumber = q.value(0).toInt();
                row.orderNumber = q.value(1).toString();
                row.dateStart = ReportsDateUtils::displayDate(q.value(2).toString());
                row.dateEnd = ReportsDateUtils::displayDate(q.value(3).toString(), q.value(2).toString());
                row.car = composePlateCarForUi(q.value(4).toString(), q.value(5).toString());
                if(!titles.text(q.value(0),q.value(6),&row.works,&failure))return fail();
                row.amount = q.value(7).toDouble();
                row.absent = false;
                row.masterAmount = FormatUtils::roundMoneyValue(row.amount * percent / 100.0);
                out.append(row);
            }
        }
        if(q.lastError().isValid()){failure=q.lastError().text();return fail();}
        return out;
    }

    return PayrollIdentityRead::read(db,masterId,df,dt,percent,error);
}

bool setPayrollOrderAbsent(const QString& masterId, int orderNumber, bool absent, QString* errorMessage)
{
    return upsertPayrollOrderAdjustment(masterId, orderNumber, QStringLiteral("absent"), absent, errorMessage);
}

bool setPayrollOrderDeleted(const QString& masterId, int orderNumber, bool deleted, QString* errorMessage)
{
    return upsertPayrollOrderAdjustment(masterId, orderNumber, QStringLiteral("deleted"), deleted, errorMessage);
}

// Both tables describe the same period. Updating an existing row keeps its ID,
// creation time and fields added by historical installations.
static bool writeExpenseRow(QSqlDatabase db, const QString& table, const QString& df, const QString& dt,
                            const QStringList& fields, const QVariantList& values, QString& error)
{
    QSqlQuery lookup(db);
    lookup.prepare(QStringLiteral("SELECT id FROM %1 WHERE %2=? AND %3=? LIMIT 2")
                       .arg(table, DateFormats::isoSql("period_from"), DateFormats::isoSql("period_to")));
    lookup.addBindValue(df);
    lookup.addBindValue(dt);
    if (!lookup.exec()) { error = lookup.lastError().text(); return false; }
    const bool existing = lookup.next();
    const auto id = existing ? lookup.value(0) : QVariant();
    if (existing && lookup.next()) {
        error = QStringLiteral("Кілька записів за один період у таблиці %1").arg(table);
        return false;
    }
    if (lookup.lastError().isValid()) { error = lookup.lastError().text(); return false; }
    lookup.finish();

    QSqlQuery write(db);
    if (existing) {
        QStringList assignments;
        for (const auto& field : fields) assignments.append(field + "=?");
        write.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE id=?").arg(table, assignments.join(',')));
        for (const auto& value : values) write.addBindValue(value);
        write.addBindValue(id);
    } else {
        QStringList placeholders;
        for (qsizetype i = 0; i < fields.size(); ++i) placeholders.append("?");
        write.prepare(QStringLiteral("INSERT INTO %1(period_from,period_to,%2) VALUES(?,?,%3)")
                          .arg(table, fields.join(','), placeholders.join(',')));
        write.addBindValue(df);
        write.addBindValue(dt);
        for (const auto& value : values) write.addBindValue(value);
    }
    if (!write.exec()) { error = write.lastError().text(); return false; }
    if (write.numRowsAffected() == 1) return true;
    error = QStringLiteral("Запис за період у %1 не збережено").arg(table);
    return false;
}

bool saveExpensesRecord(const QString& df, const QString& dt, const ExpensesSavePayload& payload, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    auto db = Database::db();
    QSqlQuery tx(db);
    QString error;
    const auto fail = [&](const QString& detail) {
        if (errorMessage) *errorMessage = detail;
        return false;
    };
    const auto from = QDate::fromString(df, Qt::ISODate);
    const auto to = QDate::fromString(dt, Qt::ISODate);
    if (!from.isValid() || !to.isValid() || from > to)
        return fail(QStringLiteral("Некоректний період витрат"));
    if (!tx.exec("SAVEPOINT report_expense_period")) return fail(tx.lastError().text());
    const auto rollback = [&]() {
        if (!tx.exec("ROLLBACK TO report_expense_period") || !tx.exec("RELEASE report_expense_period"))
            error += " | " + tx.lastError().text();
        return fail(error);
    };

    const auto before = LegacyAncillaryMutationLogSupport::snapshotExpensePeriod(db, df, dt, &error);
    if (before.isEmpty()) return rollback();
    const auto note = [](const QString& text) { return text.isNull() ? QStringLiteral("") : text.trimmed(); };
    if (!writeExpenseRow(db, "expenses", df, dt,
            {"elec_prev","elec_curr","elec_tariff","cleaning","heating","rent","amortization","sw_opendatabot",
             "sw_tecalliance","other_amount","cleaning_note","software_note","heating_note","amortization_note",
             "rent_note","other_note","elec_kwh","elec_cost"},
            {payload.elecPrev,payload.elecCurr,payload.elecTariff,payload.cleaning,payload.heating,payload.rent,
             payload.amortization,payload.software,0.0,payload.other,note(payload.cleaningNote),note(payload.softwareNote),
             note(payload.heatingNote),note(payload.amortizationNote),note(payload.rentNote),note(payload.otherNote),
             payload.elecKwh,payload.elecCost}, error)) return rollback();
    if (!writeExpenseRow(db, "electricity_periods", df, dt,
            {"meter_before","meter_after","consumed_kwh","tariff","amount"},
            {payload.elecPrev,payload.elecCurr,payload.elecKwh,payload.elecTariff,payload.elecCost}, error)) return rollback();
    const auto after = LegacyAncillaryMutationLogSupport::snapshotExpensePeriod(db, df, dt, &error);
    if (after.isEmpty() || after == "{}") {
        if (error.isEmpty()) error = QStringLiteral("Не вдалося звірити запис витрат після збереження");
        return rollback();
    }
    if (!LegacyAncillaryMutationLogSupport::logExpenseMutation(db, before == "{}" ? QStringLiteral("insert") : QStringLiteral("update"),
                                                               df, dt, before, after, &error)) return rollback();
    if (!tx.exec("RELEASE report_expense_period")) {
        error = tx.lastError().text();
        return rollback();
    }
    return true;
}

QList<ElectricityPeriodRow> loadElectricityPeriods(QString* error) {
    return loadElectricityPeriods(QSqlDatabase::database(currentConnectionName(),false),error);
}
QList<ElectricityPeriodRow> loadElectricityPeriods(QSqlDatabase db,QString* error)
{
    QList<ElectricityPeriodRow> out;
    if(error)error->clear();
    if(!db.isOpen()){if(error)*error=QStringLiteral("Electricity database is not open");return {};}
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT period_from, period_to, meter_before, meter_after, consumed_kwh, tariff, amount FROM electricity_periods ORDER BY date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) ASC"));
    if (!q.exec()){if(error)*error=q.lastError().text();return {};}
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
    if(q.lastError().isValid()){if(error)*error=q.lastError().text();return {};}
    return out;
}

} // namespace ReportsSyncSupport
