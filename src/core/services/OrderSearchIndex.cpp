#include "core/services/OrderSearchIndex.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStringList>

namespace {
QString compact(QString expression)
{
    QString result = "lower(coalesce(" + expression + ",''))";
    const QString upper = QString::fromUtf8("АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯЁ");
    const QString lower = upper.toLower();
    for (int i=0; i<upper.size(); ++i)
        result = QString("replace(%1,'%2','%3')").arg(result, upper.mid(i,1), lower.mid(i,1));
    const QString lookalikes = QString::fromUtf8("авсеніїкморт ух").remove(' ');
    const QString latin = "abcehiikmoptyx";
    for (int i=0; i<lookalikes.size(); ++i)
        result = QString("replace(%1,'%2','%3')").arg(result,lookalikes.mid(i,1),latin.mid(i,1));
    for (const QString& ch : {QString(" "),QString("-"),QString("_"),QString("."),QString("/"),QString(":"),QString("'"),QString("(") ,QString(")")}) {
        QString escaped=ch; escaped.replace("'","''");
        result=QString("replace(%1,'%2','')").arg(result,escaped);
    }
    for (const QChar ch : QString::fromUtf8(",;!?\"+\\[]{}@#$%^&*=|<>~`’ʼ–—\t\n\r\u00a0"))
        result=QString("replace(%1,'%2','')").arg(result,QString(ch));
    return result;
}
QString dateDisplay(const QString& col)
{
    return QString("CASE WHEN substr(%1,5,1)='-' THEN substr(%1,9,2)||'.'||substr(%1,6,2)||'.'||substr(%1,3,2) "
                   "WHEN substr(%1,3,1)='.' AND substr(%1,6,1)='.' AND substr(%1,7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(%1,1,6)||substr(%1,9,2) "
                   "ELSE substr(coalesce(%1,''),1,8) END").arg(col);
}
QString dateTime(const QString& col)
{
    return QString("CASE WHEN substr(%1,5,1)='-' THEN datetime(replace(substr(%1,1,19),'T',' ')) "
        "WHEN substr(%1,3,1)='.' AND substr(%1,6,1)='.' THEN datetime("
        "CASE WHEN substr(%1,7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(%1,7,4) ELSE '20'||substr(%1,7,2) END"
        "||'-'||substr(%1,4,2)||'-'||substr(%1,1,2)||' '||"
        "CASE WHEN length(%1)>=14 THEN replace(substr(%1,CASE WHEN substr(%1,7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN 12 ELSE 10 END,5),'.',':') ELSE '00:00' END) ELSE NULL END").arg(col);
}
}
namespace OrderSearchIndex {
bool available(QSqlDatabase db)
{
    QSqlQuery q(db);
    return q.exec("SELECT 1 FROM sqlite_master WHERE name='order_search_v1' AND type='table'") && q.next();
}
bool ensure(QSqlDatabase db, QString* error)
{
    const bool exists=available(db);
    QSqlQuery q(db);
    auto exec=[&](const QString& sql) {
        if(q.exec(sql)) return true;
        if(error) *error=q.lastError().text();
        return false;
    };
    if(!exec("SAVEPOINT order_search_setup")) return false;
    auto fail=[&]() { q.exec("ROLLBACK TO order_search_setup"); q.exec("RELEASE order_search_setup"); return false; };
    if(!exec("CREATE TABLE IF NOT EXISTS order_search_v1(number INTEGER PRIMARY KEY,plate_search TEXT,vin_search TEXT,car_search TEXT,client_search TEXT,opened_display TEXT,closed_display TEXT,opened_search TEXT,closed_search TEXT,visible_number INTEGER,opened_date TEXT,closed_date TEXT,opened_datetime TEXT,closed_datetime TEXT)")) return fail();
    const QString select=QString("SELECT number,%1,%2,%3,%4,%5,%6,replace(%5,'.',''),replace(%6,'.',''),COALESCE(order_number,number),date(%7),date(%8),%7,%8 FROM router_order_locator")
        .arg(compact("plate"),compact("vin"),compact("car"),compact("client"),dateDisplay("opened_at"),dateDisplay("closed_at"),dateTime("opened_at"),dateTime("closed_at"));
    // Source triggers update the projection in the same transaction as the order.
    for(const QString& table : {QString("work_orders"),QString("order_registry")}) {
        QSqlQuery probe(db); probe.prepare("SELECT 1 FROM sqlite_master WHERE name=? AND type='table'"); probe.addBindValue(table);
        if(!probe.exec()) { if(error) *error=probe.lastError().text(); return fail(); }
        if(!probe.next()) continue;
        const QString key=table=="order_registry" ? "order_number" : "number";
        for(const QString& action : {QString("INSERT"),QString("UPDATE"),QString("DELETE")}) {
            const QString keys=action=="INSERT" ? "NEW."+key : action=="DELETE" ? "OLD."+key : "OLD."+key+",NEW."+key;
            const QString trigger=QString("CREATE TRIGGER IF NOT EXISTS search_v1_%1_%2 AFTER %2 ON %1 BEGIN DELETE FROM order_search_v1 WHERE number IN (%3); INSERT OR REPLACE INTO order_search_v1 %4 WHERE number IN (%3); END").arg(table,action,keys,select);
            if(!exec(trigger)) return fail();
        }
    }
    if(!exists && !exec("INSERT INTO order_search_v1 "+select)) return fail();
    for(const QString& column : {QString("plate_search"),QString("vin_search")})
        if(!exec(QString("CREATE INDEX IF NOT EXISTS idx_search_v1_%1 ON order_search_v1(%1 COLLATE NOCASE)").arg(column))) return fail();
    if(!exec("CREATE INDEX IF NOT EXISTS idx_search_v1_visible ON order_search_v1(visible_number)")) return fail();
    for(const QString& table : {QString("work_order_services"),QString("work_order_parts"),QString("router_cache_work_order_parts")}) {
        QSqlQuery probe(db); probe.prepare("SELECT 1 FROM sqlite_master WHERE name=? AND type='table'"); probe.addBindValue(table);
        if(!probe.exec()) { if(error) *error=probe.lastError().text(); return fail(); }
        if(!probe.next()) continue;
        if(!exec(QString("CREATE INDEX IF NOT EXISTS idx_%1_search_uid ON %1(TRIM(COALESCE(order_uid,'')),COALESCE(order_number,0))").arg(table))) return fail();
        if(!exec(QString("CREATE INDEX IF NOT EXISTS idx_%1_search_number ON %1(COALESCE(order_number,0))").arg(table))) return fail();
    }
    return exec("RELEASE order_search_setup");
}
}
