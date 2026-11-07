#include "StatisticsReadService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/util/DateFormats.h"
#include <QSqlQuery>
#include <QSqlError>
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

QVector<PartTraceRow> loadParts(QSqlDatabase database, const QDate& from, const QDate& to, const QString& search, const std::shared_ptr<std::atomic_bool>& cancel)
{
    QVector<PartTraceRow> rows;
    const QString partsSource = databaseObjectExists(database, QStringLiteral("router_work_order_parts"))
        ? QStringLiteral("router_work_order_parts") : QStringLiteral("work_order_parts");
    const QString ordersSource = databaseObjectExists(database, QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders");
    if (!databaseObjectExists(database, partsSource) || !databaseObjectExists(database, ordersSource)) return rows;

    QSqlQuery query(database);
    const QString sql = QStringLiteral(
        "SELECT p.id, COALESCE(p.order_number,0), COALESCE(p.order_uid,''), COALESCE(p.name,''), COALESCE(p.part_number,''), "
        "COALESCE(p.ordered_at,''), COALESCE(p.delivery_date,''), COALESCE(p.installed_at,''), COALESCE(p.store,''), "
        "COALESCE(o.car,''), COALESCE(o.client,''), "
        "COALESCE(NULLIF(TRIM(o.master_name_closed),''), NULLIF(TRIM(m.name),''), '') "
        "FROM %1 p LEFT JOIN %2 o ON o.number=p.order_number "
        "LEFT JOIN masters m ON m.id=o.master_id "
        "WHERE TRIM(COALESCE(p.name,''))<>'' "
        "ORDER BY date(CASE WHEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),3,1)='.' AND substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),7,2) END||'-'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),4,2)||'-'||substr(trim(COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,''))),1,2) ELSE COALESCE(NULLIF(p.installed_at,''), NULLIF(p.delivery_date,''), NULLIF(p.ordered_at,'')) END) DESC, p.id DESC")
        .arg(partsSource, ordersSource);
    if (!query.exec(sql)) return rows;

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
        const QString probe = QString::number(row.orderNumber) + QLatin1Char(' ') + row.name + QLatin1Char(' ')
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
        rows.append(row);
    }
    // Resolve invoice links once, after date/search filtering, not once per part.
    QList<int> partIds;
    partIds.reserve(rows.size());
    for(const auto& row:rows) partIds.append(int(row.id));
    const auto attachments=OrderAttachmentService::partAttachmentMap(database,partIds);
    for(auto& row:rows) {
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
    if(cancel->load() || !r.allowed)return result;
    QString error;
    ScopedSqliteConnection connection(r.path,QStringLiteral("statistics_snapshot"),&error);
    const auto db=connection.db();
    if(!db.isOpen() || db.databaseName()!=r.path) {
        result.error=error.isEmpty()?QString::fromUtf8("Профіль статистики змінився або недоступний."):error;return result;
    }
    return load(db,r.from,r.to,r.performance,r.scope,r.ownMaster,cancel);
}
Snapshot load(QSqlDatabase db, const QDate& fromDate, const QDate& toDate,
              bool performance, const AccessControlService::OrderScope& scope,
              const QString& ownMaster, const std::shared_ptr<std::atomic_bool>& cancel)
{
    Snapshot result;
    if(cancel->load()) return result;
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

        result.staff=staff;result.orders=orders;result.stock=stock;result.error=errors.join(' ');
    } else {
        ActivityAuditFilter filter;
        filter.from=fromDate;filter.to=toDate;filter.limit=5000;
        filter.includeSystem=false;filter.includeLegacy=false;
        const auto events=ActivityAuditService::load(db,filter,&result.error);
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
        result.parts=loadParts(db,fromDate,toDate,{},cancel);
    return result;
}
}
