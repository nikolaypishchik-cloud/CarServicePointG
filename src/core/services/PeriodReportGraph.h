#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QSet>
#include <QMap>
#include <QDate>
namespace PeriodReportGraph {
using Row=QList<QVariant>;
struct Graph {QList<Row> orders;QMap<QString,QList<Row>> services,parts;};
inline bool read(QSqlDatabase db,const QString& from,const QString& to,Graph* output,QString* error) {
    *output={};if(error)error->clear();
    auto fail=[&](const QString& text){if(error)*error=text;return false;};
    if(!db.isOpen())return fail("Period report database is not open");
    const auto first=QDate::fromString(from,Qt::ISODate),last=QDate::fromString(to,Qt::ISODate);
    if(!first.isValid() || !last.isValid() || first>last)return fail("Invalid period report dates");
    const QString selection=QStringLiteral("SELECT number,COALESCE(order_number,number) AS visible_number,car,plate,substr(NULLIF(TRIM(closed_at),''),1,10) AS report_date,COALESCE(works_sum,0) AS works_sum,order_uid,COALESCE(NULLIF(closed_at,''), opened_at) AS effective_date FROM router_work_orders WHERE date(CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),1,2) ELSE COALESCE(NULLIF(closed_at,''), opened_at) END) >= date(?) AND date(CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),1,2) ELSE COALESCE(NULLIF(closed_at,''), opened_at) END) <= date(?) ORDER BY date(CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(closed_at,''), opened_at)),1,2) ELSE COALESCE(NULLIF(closed_at,''), opened_at) END) ASC, number ASC");
    Graph graph;QSqlQuery q(db);QSet<QString> owners;
    auto execute=[&](const QString& sql){q=QSqlQuery(db);if(!q.prepare(sql))return fail(q.lastError().text());q.addBindValue(from);q.addBindValue(to);if(!q.exec())return fail(q.lastError().text());return true;};
    if(!execute(selection))return false;
    while(q.next()){Row row;for(int i=0;i<7;++i)row<<q.value(i);const auto uid=row[6].toString();if(uid.trimmed().isEmpty() || owners.contains(uid))return fail("Missing or ambiguous selected order UID");owners.insert(uid);graph.orders<<row;}
    if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    if(graph.orders.isEmpty()){*output=graph;return true;}
    const QString selected="WITH selected AS MATERIALIZED ("+selection+") ";
    if(!execute(selected+"SELECT s.id,COALESCE(s.title,''),COALESCE(s.amount,0),s.order_uid,s.service_uid FROM router_work_order_services s WHERE s.order_uid IN (SELECT order_uid FROM selected) OR (NULLIF(TRIM(s.order_uid),'') IS NULL AND s.order_number IN (SELECT number FROM selected)) ORDER BY s.id ASC"))return false;
    QSet<QString> serviceUids;QMap<QString,QSet<int>> serviceIds;
    while(q.next()) {const auto owner=q.value(3).toString(),uid=q.value(4).toString();const auto id=q.value(0).toInt();if(!owners.contains(owner) || uid.trimmed().isEmpty() || serviceUids.contains(uid) || serviceIds[owner].contains(id))return fail("Ambiguous service identity in selected period");serviceUids.insert(uid);serviceIds[owner].insert(id);graph.services[owner]<<Row{q.value(0),q.value(1),q.value(2)};}
    if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    if(!execute(selected+"SELECT p.id,COALESCE(p.name,''),COALESCE(p.store,''),COALESCE(p.qty,0),COALESCE(p.amount,0),COALESCE(p.purchase_amount,CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END),COALESCE(p.purchase_price,0),COALESCE(p.markup_percent,0),COALESCE(p.manual_markup_override,0),COALESCE(p.service_id,0),COALESCE(p.pricing_calc_version,1),p.order_uid,p.part_uid FROM router_work_order_parts p WHERE p.order_uid IN (SELECT order_uid FROM selected) OR (NULLIF(TRIM(p.order_uid),'') IS NULL AND p.order_number IN (SELECT number FROM selected)) ORDER BY p.id ASC"))return false;
    QSet<QString> partUids;QMap<QString,QSet<int>> partIds;
    while(q.next()){const auto owner=q.value(11).toString(),uid=q.value(12).toString();const int id=q.value(0).toInt(),service=q.value(9).toInt();if(!owners.contains(owner) || uid.trimmed().isEmpty() || partUids.contains(uid) || partIds[owner].contains(id) || (service>0 && !serviceIds[owner].contains(service)))return fail("Ambiguous part identity or service owner in selected period");partUids.insert(uid);partIds[owner].insert(id);Row row;for(int i=0;i<11;++i)row<<q.value(i);graph.parts[owner]<<row;}
    if(q.lastError().isValid())return fail(q.lastError().text());*output=graph;return true;
}
}
