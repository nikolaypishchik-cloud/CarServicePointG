#include "StatisticsReadService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/util/DateFormats.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QScopeGuard>
#include <algorithm>
namespace StatisticsReadService {
QDate parseBusinessDate(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return {};
    QDate date = DateFormats::parseDate(value.left(10));
    if (date.isValid()) return date;
    date = QDate::fromString(value, QStringLiteral("dd.MM.yyyy"));
    if (date.isValid()) return date;
    return QDate::fromString(value, QStringLiteral("dd.MM.yy"));
}

namespace {
bool databaseObjectExists(QSqlDatabase database, const QString& name)
{
    const QStringList objects = database.tables(static_cast<QSql::TableType>(QSql::Tables | QSql::Views));
    return std::any_of(objects.cbegin(), objects.cend(), [&name](const QString& item) {
        return item.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QHash<int,int> auditVisibleNumbers(QSqlDatabase db, const QList<ActivityAuditEvent>& events, QString* error)
{
    QSet<int> referenced;
    for (const auto& event : events)
        if (event.orderNumber > 0) referenced.insert(event.orderNumber);
    if (referenced.isEmpty()) return {};

    QString headers = QStringLiteral("work_orders");
    if (databaseObjectExists(db, "router_order_locator")) headers = "router_order_locator";
    else if (databaseObjectExists(db, "router_work_orders")) headers = "router_work_orders";
    const QString visible = db.record(headers).contains("order_number") ? "COALESCE(NULLIF(order_number,0),number)" : "number";
    QString source = QStringLiteral("SELECT number,order_uid,%1 AS visible FROM %2 WHERE number IN (SELECT id FROM wanted)").arg(visible,headers);
    if (databaseObjectExists(db, "order_creation_reservations"))
        source += " UNION SELECT internal_number,order_uid,order_number FROM order_creation_reservations WHERE internal_number IN (SELECT id FROM wanted)";

    const auto numbers = referenced.values();
    QHash<int,int> result;
    for (qsizetype offset = 0; offset < numbers.size(); offset += 400) {
        const auto batch = numbers.mid(offset,400);
        const QString values = QStringList(batch.size(), QStringLiteral("(?)")).join(',');
        QSqlQuery q(db);
        if (!q.prepare(QStringLiteral("WITH wanted(id) AS (VALUES %1), owners AS (%2) "
                                      "SELECT number,MIN(visible) FROM owners GROUP BY number HAVING COUNT(*)=1").arg(values, source))) {
            *error = q.lastError().text();
            return {};
        }
        for (int number : batch) q.addBindValue(number);
        if (!q.exec()) { *error = q.lastError().text(); return {}; }
        while (q.next()) result.insert(q.value(0).toInt(), q.value(1).toInt());
        if (q.lastError().isValid()) { *error = q.lastError().text(); return {}; }
    }
    return result;
}

QVector<PartTraceRow> loadParts(QSqlDatabase database, const QDate& from, const QDate& to, const QString& search, const std::shared_ptr<std::atomic_bool>& cancel, QString* error)
{
    QVector<PartTraceRow> rows;
    const QString partsSource = databaseObjectExists(database, QStringLiteral("router_work_order_parts"))
        ? QStringLiteral("router_work_order_parts") : QStringLiteral("work_order_parts");
    const QString ordersSource = databaseObjectExists(database, QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders");
    if (!databaseObjectExists(database, partsSource) || !databaseObjectExists(database, ordersSource)) {*error=QStringLiteral("Statistics order/part source is missing");return {};}

    QSqlQuery query(database);
    QVector<int> localRows;
    const QString sql = QStringLiteral(
        "WITH headers AS MATERIALIZED (SELECT number,order_uid,%5 AS visible_number,car,client,master_name_closed,master_id FROM %2), "
        "owner_keys AS (SELECT 'u:'||order_uid AS lookup,number,order_uid FROM headers WHERE COALESCE(order_uid,'')<>'' "
        "UNION ALL SELECT 'n:'||number,number,order_uid FROM headers), "
        "owners AS (SELECT lookup,MIN(number) AS number,MIN(order_uid) AS order_uid FROM owner_keys GROUP BY lookup HAVING COUNT(*)=1) "
        "SELECT p.id, COALESCE(p.order_number,0), COALESCE(p.order_uid,''), COALESCE(p.name,''), COALESCE(p.part_number,''), "
        "COALESCE(p.ordered_at,''), COALESCE(p.delivery_date,''), COALESCE(p.installed_at,''), COALESCE(p.store,''), "
        "COALESCE(o.car,''), COALESCE(o.client,''), "
        "COALESCE(NULLIF(TRIM(o.master_name_closed),''), NULLIF(TRIM(m.name),''), ''), o.number, o.order_uid, local.id, o.visible_number "
        "FROM %1 p LEFT JOIN owners owner ON owner.lookup=CASE WHEN COALESCE(p.order_uid,'')<>'' THEN 'u:'||p.order_uid ELSE 'n:'||p.order_number END "
        "LEFT JOIN headers o ON o.number=owner.number AND o.order_uid IS owner.order_uid "
        "LEFT JOIN work_order_parts local ON local.id=p.id AND local.order_number=p.order_number "
        "AND COALESCE(local.order_uid,'')=COALESCE(p.order_uid,'') AND %3=%4 "
        "LEFT JOIN masters m ON m.id=o.master_id "
        "WHERE TRIM(COALESCE(p.name,''))<>'' "
        "ORDER BY date(CASE WHEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),3,1)='.' AND substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,2) END||'-'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),4,2)||'-'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),1,2) ELSE COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,'')) END) DESC, p.id DESC")
        .arg(partsSource, ordersSource,
             database.record("work_order_parts").contains("part_uid") ? "COALESCE(local.part_uid,'')" : "''",
             database.record(partsSource).contains("part_uid") ? "COALESCE(p.part_uid,'')" : "''",
             database.record(ordersSource).contains("order_number") ? "COALESCE(NULLIF(order_number,0),number)" : "number");
    if (!query.exec(sql)) {*error=query.lastError().text();return {};}

    const QDate today = QDate::currentDate();
    while (!cancel->load() && query.next()) {
        PartTraceRow row;
        row.id = query.value(0).toLongLong();
        row.orderNumber = query.value(1).toInt();
        row.orderUid = query.value(2).toString().trimmed();
        row.name = query.value(3).toString().trimmed();
        row.partNumber = query.value(4).toString().trimmed();
        row.orderedAt = query.value(5).toString().trimmed();
        row.orderedOn = parseBusinessDate(row.orderedAt);
        row.deliveredOn = parseBusinessDate(query.value(6).toString());
        row.installedOn = parseBusinessDate(query.value(7).toString());
        row.store = query.value(8).toString().trimmed();
        row.car = query.value(9).toString().trimmed();
        row.client = query.value(10).toString().trimmed();
        row.responsible = query.value(11).toString().trimmed();

        const QDate latestDate = row.installedOn.isValid() ? row.installedOn
            : (row.deliveredOn.isValid() ? row.deliveredOn : row.orderedOn);
        if (latestDate.isValid() && (latestDate < from || latestDate > to)) continue;
        if (query.value(12).isNull()) {
            *error = QStringLiteral("Не вдалося однозначно визначити наряд для запчастини %1 (№%2).")
                         .arg(row.id).arg(row.orderNumber);
            return {};
        }
        row.orderNumber = query.value(12).toInt();
        row.orderUid = query.value(13).toString();
        row.visibleOrderNumber = query.value(15).toInt();
        const QString probe = QString::number(row.visibleOrderNumber) + QLatin1Char(' ') + row.name + QLatin1Char(' ')
            + row.partNumber + QLatin1Char(' ') + row.car + QLatin1Char(' ') + row.client;
        if (!search.trimmed().isEmpty() && !probe.contains(search, Qt::CaseInsensitive)) continue;

        if (row.installedOn.isValid()) {
            row.status = QString::fromUtf8("Встановлено");
        } else if (row.deliveredOn.isValid()) {
            row.status = QString::fromUtf8("На складі");
            row.storageDays = qMax(0, row.deliveredOn.daysTo(today));
        } else {
            row.status = QString::fromUtf8("Очікується");
        }
        if (!query.value(14).isNull()) localRows.append(rows.size());
        rows.append(row);
    }
    if(query.lastError().isValid()){*error=query.lastError().text();return {};}
    // Resolve invoice links once, after date/search filtering, not once per part.
    QList<int> partIds;
    partIds.reserve(localRows.size());
    for (int index : localRows) partIds.append(int(rows.at(index).id));
    const auto attachments=OrderAttachmentService::partAttachmentMap(database,partIds,error);
    if (!error->isEmpty()) return {};
    for (int index : localRows) {
        auto& row = rows[index];
        const auto attachment=attachments.value(int(row.id));
        row.invoiceFileName=attachment.displayName;
        row.invoiceNumber=attachment.invoiceNumber;
        row.invoiceDate=attachment.invoiceDate;
    }
    return rows;
}

}
Request capture(const QDate& from,const QDate& to,bool performance)
{
    const auto db=Database::db();
    Request r;r.from=from;r.to=to;r.performance=performance;
    r.allowed=performance || AccessControlService::currentUserHasPermission(db,QStringLiteral("nav.statistics"));
    r.scope=AccessControlService::currentOrderScope(db);r.ownMaster=AccessControlService::currentMasterId(db);
    r.scopeKey=AccessControlService::currentScopeKey();r.path=db.databaseName();return r;
}
Snapshot run(const Request& r,const std::shared_ptr<std::atomic_bool>& cancel)
{
    Snapshot result;
    if(cancel->load())return result;
    if(!r.allowed){result.error=QString::fromUtf8("Немає дозволу на перегляд статистики.");return result;}
    QString error;
    ScopedSqliteConnection connection(r.path,QStringLiteral("statistics_snapshot"),&error,true);
    auto db=connection.db();
    if(!db.isOpen() || db.databaseName()!=r.path) {
        result.error=error.isEmpty()?QString::fromUtf8("Профіль статистики змінився або недоступний."):error;return result;
    }
    if (!db.transaction()) { result.error=db.lastError().text(); return result; }
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(!r.performance && !AccessControlService::currentUserHasPermission(db,QStringLiteral("nav.statistics"))) {result.error=QString::fromUtf8("Немає дозволу на перегляд статистики.");return result;}
    result=load(db,r.from,r.to,r.performance,AccessControlService::currentOrderScope(db),AccessControlService::currentMasterId(db),cancel);
    if (!result.error.isEmpty()) return result;
    if (!db.commit()) { result={}; result.error=db.lastError().text(); return result; }
    rollback.dismiss();
    return result;
}
Snapshot load(QSqlDatabase db, const QDate& fromDate, const QDate& toDate,
              bool performance, const AccessControlService::OrderScope& scope,
              const QString& ownMaster, const std::shared_ptr<std::atomic_bool>& cancel)
{
    Snapshot result;
    if(cancel->load()) return result;
    if(!db.isOpen()){result.error=QStringLiteral("Statistics database is not open");return result;}
    if(performance) {
    QVariantList staff, orders, stock;
    QStringList errors;
    QSqlQuery q(db);
    if (q.exec("SELECT id,name,role,active,active_from,active_to FROM masters ORDER BY name")) {
        while (!cancel->load() && q.next()) {
            if (scope.restricted && q.value(0).toString() != ownMaster) continue;
            const auto from = parseBusinessDate(q.value(4).toString());
            const auto to = parseBusinessDate(q.value(5).toString());
            const auto today=QDate::currentDate();
            const bool current=q.value(3).toBool() && (!from.isValid() || from<=today)
                && (!to.isValid() || to>=today);
            if (q.value(2).toString() == "master" && current)
                staff.append(QVariantMap{{"id",q.value(0)},{"name",q.value(1)}});
        }
    } else errors << QString::fromUtf8("Не вдалося завантажити персонал.");
    if(q.lastError().isValid()) errors<<q.lastError().text();
    const QString source = databaseObjectExists(db,"router_work_orders") ? "router_work_orders" : "work_orders";
    if (q.exec("SELECT number,order_uid,order_number,master_id,master_name_closed,car,plate,opened_at,closed_at,status FROM "+source)) {
        while (!cancel->load() && q.next()) {
            if (!scope.contains(q.value(1).toString(),q.value(0).toInt())) continue;
            const auto opened = parseBusinessDate(q.value(7).toString());
            const auto closed = parseBusinessDate(q.value(8).toString());
            const bool completed = closed.isValid() && closed >= fromDate && closed <= toDate;
            // Closed dates, not creation dates, define completed orders in the selected period.
            const bool inWork = !closed.isValid() && opened.isValid() && opened <= toDate
                && q.value(9).toString().compare("closed",Qt::CaseInsensitive)!=0;
            if (!completed && !inWork) continue;
            const auto masterId = q.value(3).toString();
            orders.append(QVariantMap{{"number",q.value(0)},{"uid",q.value(1)},
                {"display",q.value(2).toInt()>0?q.value(2):q.value(0)},{"master",masterId},
                {"car",q.value(5).toString()+" | "+q.value(6).toString()},
                {"closed",closed},{"completed",completed},{"opened",opened}});
        }
    } else errors << QString::fromUtf8("Не вдалося завантажити наряди.");
    if(q.lastError().isValid()) errors<<q.lastError().text();
    if(databaseObjectExists(db,"workshop_spare_orders") != databaseObjectExists(db,"workshop_spare_movements"))
        errors<<QStringLiteral("Incomplete statistics stock schema");
    // Read only: do not call the stock service's schema/backfill path while rendering statistics.
    if (databaseObjectExists(db,"workshop_spare_orders") && databaseObjectExists(db,"workshop_spare_movements")) {
        if (q.exec("SELECT s.id,s.master_id,s.name,s.article,s.brand,s.model,s.plate,s.quantity,s.ordered_on,"
                   "COALESCE(SUM(CASE WHEN m.operation='receipt' THEN m.quantity ELSE 0 END),0),"
                   "COALESCE(SUM(CASE WHEN m.operation='issue' THEN m.quantity ELSE 0 END),0),"
                   "MAX(CASE WHEN m.operation='receipt' THEN m.moved_on END) "
                   "FROM workshop_spare_orders s LEFT JOIN workshop_spare_movements m ON m.spare_id=s.id GROUP BY s.id")) {
            while(!cancel->load() && q.next()) {
                if(scope.restricted && q.value(1).toString()!=ownMaster) continue;
                const double waiting=qMax(0.0,q.value(7).toDouble()-q.value(9).toDouble());
                const double balance=qMax(0.0,q.value(9).toDouble()-q.value(10).toDouble());
                if(waiting<=0 && balance<=0) continue;
                const auto received=parseBusinessDate(q.value(11).toString());
                stock.append(QVariantMap{{"master",q.value(1)},{"name",q.value(2).toString()+" | "+q.value(3).toString()},
                    {"car",q.value(4).toString()+" "+q.value(5).toString()+" | "+q.value(6).toString()},
                    {"waiting",waiting},{"balance",balance},{"received",received},
                    {"days",received.isValid() && received<=QDate::currentDate()?received.daysTo(QDate::currentDate()):-1}});
            }
        } else errors << QString::fromUtf8("Не вдалося завантажити склад.");
    }

        if(q.lastError().isValid()) errors<<q.lastError().text();
        result.staff=staff;result.orders=orders;result.stock=stock;result.error=errors.join(' ');
    } else {
        ActivityAuditFilter filter;
        filter.from=fromDate;filter.to=toDate;filter.limit=5000;
        filter.includeSystem=false;filter.includeLegacy=false;
        const auto events=ActivityAuditService::readEvents(db,filter,&result.error);
        const auto visibleNumbers = auditVisibleNumbers(db, events, &result.error);
    QVariantList serialised;
    serialised.reserve(events.size());
    for (const ActivityAuditEvent& event : events) {
        if(cancel->load()) break;
        QVariantMap map;
        map.insert(QStringLiteral("id"), event.id);
        map.insert(QStringLiteral("at"), event.occurredAt);
        map.insert(QStringLiteral("actor_id"), event.actor.accountId);
        map.insert(QStringLiteral("actor"), event.actor.displayName);
        map.insert(QStringLiteral("role"), event.actor.role);
        map.insert(QStringLiteral("category"), event.category);
        map.insert(QStringLiteral("action"), event.action);
        map.insert(QStringLiteral("title"), event.title);
        map.insert(QStringLiteral("order"), event.orderNumber);
        const int visibleOrder = visibleNumbers.value(event.orderNumber);
        if (visibleOrder > 0) map.insert(QStringLiteral("visibleOrder"), visibleOrder);
        map.insert(QStringLiteral("entity_type"), event.entityType);
        map.insert(QStringLiteral("entity_id"), event.entityId);
        map.insert(QStringLiteral("subject"), event.subject);
        map.insert(QStringLiteral("payload"), event.payloadJson);
        map.insert(QStringLiteral("legacy"), event.legacy);
        serialised.append(map);
    }

        result.events=serialised;
    }
    if(!cancel->load() && result.error.isEmpty() && !performance)
        result.parts=loadParts(db,fromDate,toDate,{},cancel,&result.error);
    if(!result.error.isEmpty()){const auto error=result.error;result={};result.error=error;}
    return result;
}
}
