#include "core/services/OrderDetailIntegrityValidator.h"

#include <QSqlError>
#include <QDebug>
#include <QScopeGuard>
#include <QSet>
#include <QSqlQuery>
#include <QStringList>

namespace {

bool ensureOpen(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database connection");
        return false;
    }
    if (!database.isOpen()) {
        if (errorText) *errorText = QStringLiteral("Detail validation requires an already open database");
        return false;
    }
    return true;
}

bool tableExists(QSqlDatabase database, const QString& tableName)
{
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

// Read-only preflight: existing identity statements below can only see already
// consistent recorded identities. Historical reconciliation needs an explicit map.
bool validatePreparedDetailIdentity(QSqlDatabase database,QString* errorText)
{
    auto fail=[&](const QString& text){if(errorText)*errorText=text;return false;};
    QSet<QString> names;QSqlQuery tables(database);
    if(!tables.exec("SELECT name FROM sqlite_master WHERE type='table'"))return fail(tables.lastError().text());
    while(tables.next())names.insert(tables.value(0).toString());
    if(tables.lastError().isValid())return fail(tables.lastError().text());tables.finish();
    for(const auto& required:QStringList{"work_orders","work_order_services","work_order_parts"})if(!names.contains(required))return fail("Prepared detail table is missing: "+required);
    auto clear=[&](const QString& sql,const QString& label){
        QSqlQuery q(database);if(!q.exec(sql))return fail(q.lastError().text());
        if(q.next())return fail("Detail identity requires reconciliation: "+label);
        return !q.lastError().isValid() || fail(q.lastError().text());
    };
    for(const auto& table:QStringList{"work_orders","router_cache_work_orders"}) {
        if(!names.contains(table))continue;
        if(!clear("SELECT 1 FROM "+table+" WHERE typeof(number)<>'integer' OR number<=0 OR number>2147483647 OR typeof(order_uid)<>'text' OR TRIM(COALESCE(order_uid,''))='' OR order_uid<>TRIM(order_uid) OR typeof(order_number)<>'integer' OR order_number<=0 OR order_number>2147483647 OR typeof(order_year)<>'integer' OR order_year<=0 OR order_year>9999 LIMIT 1",table+" invalid recorded identity"))return false;
        for(const auto& fields:QStringList{"number","order_uid COLLATE BINARY","order_year,order_number"})if(!clear("SELECT 1 FROM "+table+" GROUP BY "+fields+" HAVING COUNT(*)>1 LIMIT 1",table+" duplicate identity"))return false;
    }
    if(names.contains("router_cache_work_orders") && !clear("SELECT 1 FROM router_cache_work_orders c JOIN work_orders w ON w.number=c.number WHERE c.order_uid COLLATE BINARY<>w.order_uid COLLATE BINARY OR c.order_number<>w.order_number OR c.order_year<>w.order_year LIMIT 1","cached root conflicts with canonical identity"))return false;
    struct Link {const char* child;const char* parent;bool stock;};
    const Link links[]={{"work_order_services","work_orders",false},{"work_order_parts","work_orders",true},{"outsource_jobs","work_orders",false},{"outsource_repairs","work_orders",false},{"router_cache_work_order_services","router_cache_work_orders",false},{"router_cache_work_order_parts","router_cache_work_orders",true}};
    for(const auto& link:links) {
        const auto child=QString::fromLatin1(link.child),parent=QString::fromLatin1(link.parent);if(!names.contains(child))continue;
        if(!names.contains(parent))return fail("Prepared parent table is missing: "+parent);
        const auto stock=link.stock?QString("NOT (COALESCE(c.order_number,0)=0 AND TRIM(COALESCE(c.order_uid,''))='') AND "):QString();
        if(!clear("SELECT 1 FROM "+child+" c LEFT JOIN "+parent+" p ON p.number=c.order_number WHERE "+stock+"(typeof(c.order_number)<>'integer' OR c.order_number<=0 OR p.number IS NULL OR TRIM(COALESCE(c.order_uid,''))='' OR c.order_uid COLLATE BINARY<>p.order_uid COLLATE BINARY) LIMIT 1",child+" parent UID/internal number conflict"))return false;
    }
    return true;
}

int scalar(QSqlDatabase database, const QString& sql, QString* errorText)
{
    QSqlQuery q(database);
    if (!q.exec(sql) || !q.next()) {
        if(errorText)*errorText=q.lastError().isValid()?q.lastError().text():QStringLiteral("Detail count returned no result");
        return 0;
    }
    return q.value(0).toInt();
}

bool execSql(QSqlDatabase database, const QString& sql, QString* errorText)
{
    QSqlQuery q(database);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

QString orderUidFromParentExpr(const QString& parentTable, const QString& childTable)
{
    return QStringLiteral("(SELECT COALESCE(p.order_uid,'') FROM %1 p WHERE p.number=%2.order_number)")
        .arg(parentTable, childTable);
}


bool repairRootOrderIdentity(QSqlDatabase database, QString* errorText)
{
    if (!tableExists(database, QStringLiteral("work_orders"))) return true;

    auto execOrError = [&](const QString& sql) -> bool {
        QString sqlError;
        if (execSql(database, sql, &sqlError)) return true;
        if (errorText) *errorText = sqlError;
        return false;
    };

    // Startup must be able to repair databases created by earlier builds before
    // strict unique indexes are created.  Duplicate/blank order_uid values used
    // to make idx_work_orders_uid_unique fail and abort application startup at
    // schema_check.  Only corrupted identity values are regenerated; normal
    // order data and business fields are not touched.
    if (!execOrError(QStringLiteral(
            "UPDATE work_orders "
            "SET order_uid=TRIM(COALESCE(order_uid,'')) "
            "WHERE COALESCE(order_uid,'')<>TRIM(COALESCE(order_uid,''))"))) return false;

    if (!execOrError(QStringLiteral(
            "UPDATE work_orders "
            "SET order_uid='wo_' || COALESCE(NULLIF(CAST(order_year AS TEXT),'0'), strftime('%Y','now')) || '_' || CAST(number AS TEXT) || '_' || lower(hex(randomblob(8))) "
            "WHERE TRIM(COALESCE(order_uid,''))='' "
            "OR EXISTS ("
            "  SELECT 1 FROM work_orders prev "
            "  WHERE prev.rowid < work_orders.rowid "
            "    AND TRIM(COALESCE(prev.order_uid,'')) = TRIM(COALESCE(work_orders.order_uid,'')) "
            "    AND TRIM(COALESCE(work_orders.order_uid,''))<>''"
            ")"))) return false;

    if (!execOrError(QStringLiteral(
            "UPDATE work_orders "
            "SET order_year=COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER)) "
            "WHERE COALESCE(order_year,0)=0"))) return false;

    if (!execOrError(QStringLiteral(
            "UPDATE work_orders "
            "SET order_number=COALESCE(NULLIF(order_number,0), number) "
            "WHERE COALESCE(order_number,0)=0"))) return false;

    // If damaged data contains duplicate visible numbers inside the
    // same year, keep the first record as-is and make the duplicate rows unique
    // by using their internal number. This is limited to rows that
    // would otherwise block the required canonical unique index during startup.
    if (!execOrError(QStringLiteral(
            "UPDATE work_orders "
            "SET order_number=number "
            "WHERE EXISTS ("
            "  SELECT 1 FROM work_orders prev "
            "  WHERE prev.rowid < work_orders.rowid "
            "    AND COALESCE(prev.order_year,0)=COALESCE(work_orders.order_year,0) "
            "    AND COALESCE(prev.order_number,0)=COALESCE(work_orders.order_number,0) "
            "    AND COALESCE(work_orders.order_number,0)>0"
            ")"))) return false;

    if (tableExists(database, QStringLiteral("router_cache_work_orders"))) {
        if (!execOrError(QStringLiteral(
                "UPDATE router_cache_work_orders "
                "SET order_uid=(SELECT COALESCE(w.order_uid,'') FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                "WHERE EXISTS (SELECT 1 FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                "AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(w.order_uid,'') FROM work_orders w WHERE w.number=router_cache_work_orders.number),'')"))) return false;

        if (!execOrError(QStringLiteral(
                "UPDATE router_cache_work_orders "
                "SET order_uid='rcwo_' || CAST(number AS TEXT) || '_' || lower(hex(randomblob(8))) "
                "WHERE TRIM(COALESCE(order_uid,''))='' "
                "OR EXISTS ("
                "  SELECT 1 FROM router_cache_work_orders prev "
                "  WHERE prev.rowid < router_cache_work_orders.rowid "
                "    AND TRIM(COALESCE(prev.order_uid,'')) = TRIM(COALESCE(router_cache_work_orders.order_uid,'')) "
                "    AND TRIM(COALESCE(router_cache_work_orders.order_uid,''))<>''"
                ")"))) return false;
    }

    return true;
}

} // namespace

namespace OrderDetailIntegrityValidator {

bool ensureCanonicalDetailSchema(QSqlDatabase database, QString* errorText)
{
    if (!ensureOpen(database, errorText)) return false;
    QSqlQuery preparation(database);
    if(!preparation.exec("SAVEPOINT detail_schema_guard")){if(errorText)*errorText=preparation.lastError().text();return false;}
    bool active=true;
    const auto rollback=qScopeGuard([&]{if(active){QSqlQuery q(database);if(!q.exec("ROLLBACK TO detail_schema_guard"))qCritical()<<"Detail schema rollback failed"<<q.lastError().text();if(!q.exec("RELEASE detail_schema_guard"))qCritical()<<"Detail schema release failed"<<q.lastError().text();}});
    if(!validatePreparedDetailIdentity(database,errorText))return false;
    if (!repairRootOrderIdentity(database, errorText)) return false;
    if (tableExists(database, QStringLiteral("work_orders"))
        && !execSql(database, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_work_orders_uid_unique ON work_orders(order_uid)"), errorText)) return false;
    if (tableExists(database, QStringLiteral("work_order_services"))
        && !execSql(database, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid_id ON work_order_services(order_uid, id)"), errorText)) return false;
    if (tableExists(database, QStringLiteral("work_order_parts"))
        && !execSql(database, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_uid_id ON work_order_parts(order_uid, id)"), errorText)) return false;
    if (tableExists(database, QStringLiteral("router_cache_work_order_services"))
        && !execSql(database, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_router_cache_work_order_services_order_uid_id ON router_cache_work_order_services(order_uid, id)"), errorText)) return false;
    if (tableExists(database, QStringLiteral("router_cache_work_order_parts"))
        && !execSql(database, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_router_cache_work_order_parts_order_uid_id ON router_cache_work_order_parts(order_uid, id)"), errorText)) return false;
    if (tableExists(database, QStringLiteral("router_cache_work_orders"))
        && !execSql(database, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_router_cache_work_orders_uid_unique ON router_cache_work_orders(order_uid)"), errorText)) return false;
    if(!preparation.exec("RELEASE detail_schema_guard")){if(errorText)*errorText=preparation.lastError().text();return false;}
    active=false;
    return true;
}

bool repairCanonicalOrderUids(QSqlDatabase database, QString* errorText)
{
    if (!ensureOpen(database, errorText)) return false;
    if (!ensureCanonicalDetailSchema(database, errorText)) return false;

    const QString savepointName = QStringLiteral("order_detail_uid_repair");
    QSqlQuery tx(database);
    if (!tx.exec(QStringLiteral("SAVEPOINT %1").arg(savepointName))) {
        if (errorText) *errorText = tx.lastError().text();
        return false;
    }

    auto fail = [&](const QString& msg) {
        QSqlQuery q(database);
        q.exec(QStringLiteral("ROLLBACK TO SAVEPOINT %1").arg(savepointName));
        q.exec(QStringLiteral("RELEASE SAVEPOINT %1").arg(savepointName));
        q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        if (errorText) *errorText = msg;
        return false;
    };

    auto execOrFail = [&](const QString& sql, const char* label) {
        QString sqlError;
        if (execSql(database, sql, &sqlError)) return true;
        return fail(QStringLiteral("%1: %2").arg(QString::fromUtf8(label), sqlError));
    };

    QString identityError;
    if(!validatePreparedDetailIdentity(database,&identityError))return fail(identityError);

    if (tableExists(database, QStringLiteral("work_orders")) && tableExists(database, QStringLiteral("work_order_services"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE work_order_services SET order_uid=%1 "
                    "WHERE EXISTS (SELECT 1 FROM work_orders p WHERE p.number=work_order_services.order_number) "
                    "AND COALESCE(order_uid,'')<>COALESCE(%1,'')")
                    .arg(orderUidFromParentExpr(QStringLiteral("work_orders"), QStringLiteral("work_order_services"))),
                "repair work_order_services.order_uid")) return false;
    }

    if (tableExists(database, QStringLiteral("work_orders")) && tableExists(database, QStringLiteral("work_order_parts"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE work_order_parts SET order_uid=%1 "
                    "WHERE EXISTS (SELECT 1 FROM work_orders p WHERE p.number=work_order_parts.order_number) "
                    "AND COALESCE(order_uid,'')<>COALESCE(%1,'')")
                    .arg(orderUidFromParentExpr(QStringLiteral("work_orders"), QStringLiteral("work_order_parts"))),
                "repair work_order_parts.order_uid")) return false;
    }

    if (tableExists(database, QStringLiteral("outsource_jobs"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE outsource_jobs SET order_uid=%1 "
                    "WHERE EXISTS (SELECT 1 FROM work_orders p WHERE p.number=outsource_jobs.order_number) "
                    "AND COALESCE(order_uid,'')<>COALESCE(%1,'')")
                    .arg(orderUidFromParentExpr(QStringLiteral("work_orders"), QStringLiteral("outsource_jobs"))),
                "repair outsource_jobs.order_uid")) return false;
    }

    if (tableExists(database, QStringLiteral("router_cache_work_orders"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE router_cache_work_orders SET order_uid=(SELECT COALESCE(w.order_uid,'') FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                    "WHERE EXISTS (SELECT 1 FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                    "AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(w.order_uid,'') FROM work_orders w WHERE w.number=router_cache_work_orders.number),'')"),
                "align router_cache_work_orders.order_uid")) return false;
    }

    if (tableExists(database, QStringLiteral("router_cache_work_order_services")) && tableExists(database, QStringLiteral("router_cache_work_orders"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE router_cache_work_order_services SET order_uid=%1 "
                    "WHERE EXISTS (SELECT 1 FROM router_cache_work_orders p WHERE p.number=router_cache_work_order_services.order_number) "
                    "AND COALESCE(order_uid,'')<>COALESCE(%1,'')")
                    .arg(orderUidFromParentExpr(QStringLiteral("router_cache_work_orders"), QStringLiteral("router_cache_work_order_services"))),
                "repair router_cache_work_order_services.order_uid")) return false;
    }

    if (tableExists(database, QStringLiteral("router_cache_work_order_parts")) && tableExists(database, QStringLiteral("router_cache_work_orders"))) {
        if (!execOrFail(
                QStringLiteral(
                    "UPDATE router_cache_work_order_parts SET order_uid=%1 "
                    "WHERE EXISTS (SELECT 1 FROM router_cache_work_orders p WHERE p.number=router_cache_work_order_parts.order_number) "
                    "AND COALESCE(order_uid,'')<>COALESCE(%1,'')")
                    .arg(orderUidFromParentExpr(QStringLiteral("router_cache_work_orders"), QStringLiteral("router_cache_work_order_parts"))),
                "repair router_cache_work_order_parts.order_uid")) return false;
    }

    if (!tx.exec(QStringLiteral("RELEASE SAVEPOINT %1").arg(savepointName))) {
        return fail(tx.lastError().text());
    }
    return true;
}

Report analyze(QSqlDatabase database)
{
    Report report;
    if (!ensureOpen(database, &report.errorText)) return report;
    auto count=[&](const QString& sql){return scalar(database,sql,&report.errorText);};

    report.orderCount = count(QStringLiteral("SELECT COUNT(*) FROM work_orders"));
    report.serviceCount = count(QStringLiteral("SELECT COUNT(*) FROM work_order_services"));
    report.partCount = count(QStringLiteral("SELECT COUNT(*) FROM work_order_parts"));
    report.distinctServiceOrderUidCount = count(QStringLiteral("SELECT COUNT(DISTINCT NULLIF(TRIM(COALESCE(order_uid,'')),'')) FROM work_order_services"));
    report.distinctPartOrderUidCount = count(QStringLiteral("SELECT COUNT(DISTINCT NULLIF(TRIM(COALESCE(order_uid,'')),'')) FROM work_order_parts"));
    report.orphanServiceCount = count(QStringLiteral("SELECT COUNT(*) FROM work_order_services s LEFT JOIN work_orders o ON o.order_uid=s.order_uid WHERE TRIM(COALESCE(s.order_uid,''))<>'' AND o.order_uid IS NULL"));
    report.orphanPartCount = count(QStringLiteral("SELECT COUNT(*) FROM work_order_parts p LEFT JOIN work_orders o ON o.order_uid=p.order_uid WHERE TRIM(COALESCE(p.order_uid,''))<>'' AND o.order_uid IS NULL"));
    report.routerOrderUidMismatchCount = tableExists(database, QStringLiteral("router_cache_work_orders"))
        ? count(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders rc JOIN work_orders w ON w.number=rc.number WHERE COALESCE(rc.order_uid,'')<>COALESCE(w.order_uid,'')"))
        : 0;
    report.routerServiceMismatchCount = tableExists(database, QStringLiteral("router_cache_work_order_services"))
        ? count(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services s LEFT JOIN router_cache_work_orders o ON o.number=s.order_number WHERE TRIM(COALESCE(s.order_uid,''))<>'' AND COALESCE(s.order_uid,'')<>COALESCE(o.order_uid,'')"))
        : 0;
    report.routerPartMismatchCount = tableExists(database, QStringLiteral("router_cache_work_order_parts"))
        ? count(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts p LEFT JOIN router_cache_work_orders o ON o.number=p.order_number WHERE TRIM(COALESCE(p.order_uid,''))<>'' AND COALESCE(p.order_uid,'')<>COALESCE(o.order_uid,'')"))
        : 0;
    report.ok=report.errorText.isEmpty();
    return report;
}

bool validateAfterImport(QSqlDatabase database, QString* errorText)
{
    if (!ensureOpen(database, errorText)) return false;
    if(!validatePreparedDetailIdentity(database,errorText))return false;
    const Report report = analyze(database);
    if(!report.ok){if(errorText)*errorText=report.errorText;return false;}
    QStringList failures;
    // Uneven child distribution is legitimate; identity links decide validity.
    if (report.orphanServiceCount > 0) failures << QStringLiteral("orphan services=%1").arg(report.orphanServiceCount);
    if (report.orphanPartCount > 0) failures << QStringLiteral("orphan parts=%1").arg(report.orphanPartCount);
    if (report.routerOrderUidMismatchCount > 0) failures << QStringLiteral("router order uid mismatch=%1").arg(report.routerOrderUidMismatchCount);
    if (report.routerServiceMismatchCount > 0) failures << QStringLiteral("router service uid mismatch=%1").arg(report.routerServiceMismatchCount);
    if (report.routerPartMismatchCount > 0) failures << QStringLiteral("router part uid mismatch=%1").arg(report.routerPartMismatchCount);
    if (!failures.isEmpty()) {
        if (errorText) *errorText = failures.join(QStringLiteral("; "));
        return false;
    }
    return true;
}

} // namespace OrderDetailIntegrityValidator
