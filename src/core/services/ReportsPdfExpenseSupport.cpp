#include "core/services/ReportsPdfExpenseSupport.h"

#include "core/services/ReportsPdfSupport.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include <QSqlError>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace ReportsPdfExpenseSupport {

namespace {

QString normalizeAdminSalarySourceKey(QString source)
{
    source = source.trimmed();
    if (source == QStringLiteral("client_total")
        || source == QStringLiteral("purchase_total")
        || source == QStringLiteral("margin")) {
        return source;
    }
    if (source == QStringLiteral("all") || source == QStringLiteral("parts")) {
        return QStringLiteral("client_total");
    }
    if (source == QStringLiteral("services")) {
        return QStringLiteral("margin");
    }
    return QStringLiteral("margin");
}

} // namespace

QMap<QString, QString> loadAdminSalarySources(QString* error)
{
    if(error)error->clear();
    auto db=QSqlDatabase::database(currentConnectionName(),false);
    return ReportsDataLoader::LocalSupport::loadAdminSalarySourcesForReports(db,error);
}

AdminSalaryTotals collectAdminSalaryTotals(const QString& df, const QString& dt, QString* error)
{
    QString failure;if(!error)error=&failure;error->clear();
    if(!ReportsPdfSupport::validateFinancialPdfSource(QDate::fromString(df,Qt::ISODate),QDate::fromString(dt,Qt::ISODate),error))return {};
    AdminSalaryTotals t;

    const ReportsPdfSupport::PartPeriodTotals partTotals = ReportsPdfSupport::collectClosedPartFinancialTotalsForReports(df, dt,error);
    if(!error->isEmpty())return {};
    t.purchaseTotal += partTotals.purchaseTotal;
    t.clientTotal += partTotals.clientTotal;
    t.margin += partTotals.marginTotal;
    t.partsQty = partTotals.qtyTotal;

    QSqlQuery ordersQ(QSqlDatabase::database(currentConnectionName(),false));
    ordersQ.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT wo.number) "
        "FROM router_work_orders wo "
        "WHERE date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''), wo.opened_at) END) >= date(?) "
        "AND date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''), wo.opened_at) END) <= date(?) "
        "AND wo.status='closed'"));
    ordersQ.addBindValue(df);
    ordersQ.addBindValue(dt);
    if (ordersQ.exec() && ordersQ.next()) {
        t.ordersCount = ordersQ.value(0).toInt();
    }

    QSqlQuery outs(QSqlDatabase::database(currentConnectionName(),false));
    outs.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(company_price),0), COALESCE(SUM(client_price),0), COALESCE(SUM(client_price - company_price),0) "
        "FROM outsource_jobs "
        "WHERE date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) >= date(?) "
        "AND date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) <= date(?)"));
    outs.addBindValue(df);
    outs.addBindValue(dt);
    if (outs.exec() && outs.next()) {
        t.purchaseTotal += outs.value(0).toDouble();
        t.clientTotal += outs.value(1).toDouble();
        t.margin += outs.value(2).toDouble();
    }

    ordersQ.next();outs.next();
    for(const auto* query:{&ordersQ,&outs})if(query->lastError().isValid()){*error=query->lastError().text();return {};}
    return t;
}

double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source)
{
    const QString normalizedSource = normalizeAdminSalarySourceKey(source);
    if (normalizedSource == QStringLiteral("client_total")) return totals.clientTotal;
    if (normalizedSource == QStringLiteral("purchase_total")) return totals.purchaseTotal;
    return totals.margin;
}

QString expenseNoteJoined(const QString& base, const QString& note)
{
    if (note.trimmed().isEmpty()) return base;
    return QString::fromUtf8("%1 (%2)").arg(base, note.trimmed());
}

QString mergeExpenseNotes(const QStringList& notes)
{
    QStringList cleaned;
    for (const QString& note : notes) {
        const QString trimmed = note.trimmed();
        if (trimmed.isEmpty()) continue;
        if (!cleaned.contains(trimmed, Qt::CaseInsensitive)) cleaned.push_back(trimmed);
    }
    return cleaned.join(QString::fromUtf8("; "));
}

double periodExpensesTotal(const QString& df, const QString& dt)
{
    QSqlQuery ex(QSqlDatabase::database(currentConnectionName(),false));
    ex.prepare(
        "SELECT COALESCE(SUM(COALESCE(elec_cost, ((COALESCE(elec_curr,0)-COALESCE(elec_prev,0))*COALESCE(elec_tariff,0))) + "
        "COALESCE(cleaning,0) + COALESCE(heating,0) + COALESCE(rent,0) + COALESCE(amortization,0) + "
        "COALESCE(sw_opendatabot,0) + COALESCE(sw_tecalliance,0) + COALESCE(other_amount,0)),0) "
        "FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) >= date(?) AND date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) <= date(?)");
    ex.addBindValue(df);
    ex.addBindValue(dt);
    if (ex.exec() && ex.next()) return ex.value(0).toDouble();
    return 0.0;
}

ExpenseBreakdown periodExpenseBreakdown(const QString& df, const QString& dt, QString* error)
{
    QString failure;if(!error)error=&failure;error->clear();
    if(!ReportsPdfSupport::validateFinancialPdfSource(QDate::fromString(df,Qt::ISODate),QDate::fromString(dt,Qt::ISODate),error))return {};
    ExpenseBreakdown out;
    QSqlQuery ex(QSqlDatabase::database(currentConnectionName(),false));
    ex.prepare(
        "SELECT "
        "COALESCE(SUM(COALESCE(elec_cost, ((COALESCE(elec_curr,0)-COALESCE(elec_prev,0))*COALESCE(elec_tariff,0)))),0), "
        "COALESCE(SUM(COALESCE(cleaning,0)),0), "
        "COALESCE(SUM(COALESCE(sw_opendatabot,0) + COALESCE(sw_tecalliance,0)),0), "
        "COALESCE(SUM(COALESCE(heating,0)),0), "
        "COALESCE(SUM(COALESCE(amortization,0)),0), "
        "COALESCE(SUM(COALESCE(rent,0)),0), "
        "COALESCE(SUM(COALESCE(other_amount,0)),0) "
        "FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) >= date(?) AND date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) <= date(?)");
    ex.addBindValue(df);
    ex.addBindValue(dt);
    if(!ex.exec()){*error=ex.lastError().text();return {};}
    if(ex.next()) {
        out.electricity = ex.value(0).toDouble();
        out.cleaning = ex.value(1).toDouble();
        out.software = ex.value(2).toDouble();
        out.heating = ex.value(3).toDouble();
        out.amortization = ex.value(4).toDouble();
        out.rent = ex.value(5).toDouble();
        out.other = ex.value(6).toDouble();
    }
    ex.next();
    if(ex.lastError().isValid()){*error=ex.lastError().text();return {};}
    return out;
}

ExpenseNotes periodExpenseNotes(const QString& df, const QString& dt, QString* error)
{
    QString failure;if(!error)error=&failure;error->clear();
    if(!ReportsPdfSupport::validateFinancialPdfSource(QDate::fromString(df,Qt::ISODate),QDate::fromString(dt,Qt::ISODate),error))return {};
    ExpenseNotes out;
    QSqlQuery ex(QSqlDatabase::database(currentConnectionName(),false));
    ex.prepare(
        "SELECT cleaning_note, software_note, heating_note, amortization_note, rent_note, other_note "
        "FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) >= date(?) AND date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) <= date(?) ORDER BY id DESC");
    ex.addBindValue(df);
    ex.addBindValue(dt);
    if (ex.exec()) {
        QStringList cleaningNotes;
        QStringList softwareNotes;
        QStringList heatingNotes;
        QStringList amortizationNotes;
        QStringList rentNotes;
        QStringList otherNotes;
        while (ex.next()) {
            cleaningNotes.push_back(ex.value(0).toString());
            softwareNotes.push_back(ex.value(1).toString());
            heatingNotes.push_back(ex.value(2).toString());
            amortizationNotes.push_back(ex.value(3).toString());
            rentNotes.push_back(ex.value(4).toString());
            otherNotes.push_back(ex.value(5).toString());
        }
        out.cleaning = mergeExpenseNotes(cleaningNotes);
        out.software = mergeExpenseNotes(softwareNotes);
        out.heating = mergeExpenseNotes(heatingNotes);
        out.amortization = mergeExpenseNotes(amortizationNotes);
        out.rent = mergeExpenseNotes(rentNotes);
        out.other = mergeExpenseNotes(otherNotes);
    }
    if(ex.lastError().isValid()){*error=ex.lastError().text();return {};}
    return out;
}

} // namespace ReportsPdfExpenseSupport
