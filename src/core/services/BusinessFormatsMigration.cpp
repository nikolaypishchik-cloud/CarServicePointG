#include "core/services/BusinessFormatsMigration.h"
#include "core/util/DateFormats.h"
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
QString quoted(QString name) { name.replace('"',"\"\""); return '"'+name+'"'; }
QString dateStorageSql(const QString& value)
{
    return QString("CASE WHEN substr(%1,5,1)='-' AND substr(%1,8,1)='-' "
        "AND substr(%1,1,4) BETWEEN '2000' AND '2099' THEN "
        "substr(%1,9,2)||'.'||substr(%1,6,2)||'.'||substr(%1,3,2)||replace(substr(%1,11),'T',' ') "
        "WHEN substr(%1,3,1)='.' AND substr(%1,6,1)='.' AND substr(%1,7,4) BETWEEN '2000' AND '2099' "
        "THEN substr(%1,1,6)||substr(%1,9) ELSE %1 END").arg(value);
}
QString plateStorageSql(const QString& value)
{
    QString compact="upper(trim("+value+"))";
    const QString cyr=QString::fromUtf8("АВСЕНІКМОРТХ");
    const QString latin="ABCEHIKMOPTX";
    for(int i=0;i<cyr.size();++i) {
        compact=QString("replace(replace(%1,'%2','%3'),'%4','%3')")
            .arg(compact,QString(cyr[i]),QString(latin[i]),QString(cyr[i].toLower()));
    }
    for(const QString& separator : {QString(" "),QString("-"),QString("_"),QString("."),QString(",")})
        compact=QString("replace(%1,'%2','')").arg(compact,separator);
    QString spaced=QString("substr(%1,1,2)||' '||substr(%1,3,4)||' '||substr(%1,7,2)").arg(compact);
    for(int i=0;i<latin.size();++i) spaced=QString("replace(%1,'%2','%3')").arg(spaced,QString(latin[i]),QString(cyr[i]));
    return QString("CASE WHEN %1 GLOB '[A-Z][A-Z][0-9][0-9][0-9][0-9][A-Z][A-Z]' THEN %2 ELSE %3 END")
        .arg(compact,spaced,value);
}
}

bool BusinessFormatsMigration::apply(QSqlDatabase db, QString* error)
{
    const QMap<QString,QStringList> dates={
        {"work_orders",{"opened_at","closed_at"}},
        {"order_registry",{"opened_at","closed_at"}},
        {"router_cache_work_orders",{"opened_at","closed_at"}},
        {"work_order_parts",{"ordered_at","delivery_date","installed_at"}},
        {"router_cache_work_order_parts",{"ordered_at","delivery_date","installed_at"}},
        {"parts_catalog",{"ordered_at","first_seen_at","last_seen_at"}},
        {"parts_catalog_sources",{"ordered_at","delivery_date","seen_at"}},
        {"outsource_jobs",{"sent_date","return_date"}},
        {"outsource_repairs",{"sent_at"}},
        {"router_cache_outsource_repairs",{"sent_at"}},
        {"work_order_part_attachments",{"invoice_date"}},
        {"masters",{"active_from","active_to"}},
        {"planning_calendar",{"day"}},
        {"online_bookings",{"date","planning_day"}},
        {"workshop_inventory_purchases",{"purchased_on"}},
        {"additional_expenses",{"spent_on"}},
        {"expenses",{"period_from","period_to"}},
        {"electricity_periods",{"period_from","period_to"}},
        {"purchase_payment_terms",{"due_date"}},
        {"purchase_payment_events",{"due_date"}},
        {"crm_client_tasks",{"due_at"}}
    };
    QSqlQuery q(db);
    auto exec=[&](const QString& sql) {
        if(q.exec(sql)) return true;
        if(error) *error=q.lastError().text();
        return false;
    };
    if(!exec("SAVEPOINT business_formats_v1")) return false;
    auto fail=[&]() { q.exec("ROLLBACK TO business_formats_v1"); q.exec("RELEASE business_formats_v1"); return false; };
    const auto tables=db.tables(QSql::Tables);
    for(const QString& table : tables) {
        if(!dates.contains(table) && table!="crm_vehicles" && table!="part_registry") continue;
        QSet<QString> columns;
        QSqlQuery info(db);
        if(!info.exec("PRAGMA table_info("+quoted(table)+")")) { if(error)*error=info.lastError().text(); return fail(); }
        while(info.next()) columns.insert(info.value(1).toString());
        info.finish();
        QStringList assignments, changes, watched;
        auto append=[&](const QString& column, const QString& expression) {
            assignments << quoted(column)+"="+expression;
            changes << quoted(column)+" IS NOT ("+expression+")";
            watched << quoted(column);
        };
        for(const QString& col : dates.value(table)) {
            if (!columns.contains(col)) continue;
            // A two-digit year cannot retain a different century. Never guess during migration.
            QSqlQuery values(db);
            if (!values.exec("SELECT DISTINCT " + quoted(col) + " FROM " + quoted(table)
                             + " WHERE " + quoted(col) + " IS NOT NULL AND " + quoted(col) + "<>''")) {
                if (error) *error = values.lastError().text();
                return fail();
            }
            while (values.next()) {
                const QString raw = values.value(0).toString();
                const QDate day = DateFormats::parseDate(raw);
                if (day.isValid() && (day.year() < 2000 || day.year() > 2099)) {
                    if (error) *error = QString("Date century requires explicit resolution: %1.%2 = %3").arg(table, col, raw);
                    values.finish();
                    return fail();
                }
            }
            values.finish();
            append(col,dateStorageSql(quoted(col)));
        }
        if(columns.contains("plate")) append("plate",plateStorageSql("plate"));
        if(columns.contains("car_number")) append("car_number",plateStorageSql("car_number"));
        if(assignments.isEmpty()) continue;
        const QString update="UPDATE "+quoted(table)+" SET "+assignments.join(',')+" WHERE ("+changes.join(" OR ")+")";
        if(!exec(update)) return fail();
        for(const QString& action : {QString("INSERT"),QString("UPDATE OF ")+watched.join(',')}) {
            const QString suffix=action.startsWith("UPDATE") ? "update" : "insert";
            if(!exec("CREATE TRIGGER IF NOT EXISTS "+quoted("business_formats_v1_"+table+"_"+suffix)+
                " AFTER "+action+" ON "+quoted(table)+" BEGIN "+update+" AND rowid=NEW.rowid; END")) return fail();
        }
    }
    if(!exec("RELEASE business_formats_v1")) return fail();
    return true;
}
