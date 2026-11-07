#include "infrastructure/db/sqlite/Db.h"
#include "HistoryPartsAutofillService.h"

#include "core/repository/PartRepository.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/util/DateFormats.h"

#include <QHash>
#include <QStringList>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

namespace {
QString key(QString text)
{
    text = text.normalized(QString::NormalizationForm_C).toCaseFolded();
    static const QRegularExpression nonAlphanumeric(QStringLiteral("[^\\p{L}\\p{N}]+"));
    text.remove(nonAlphanumeric);
    return text;
}

QString titleKey(const QString& text)
{
    return text.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

int volume(QString text)
{
    text = text.toCaseFolded().simplified();
    text.replace(',', '.');
    static const QRegularExpression pattern(QStringLiteral("^([0-9]+(?:\\.[0-9]+)?)\\s*(л|l|л\\.|літр|літра|літри|cm3|см3|см³|cc)?$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return 0;
    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    const bool cubic = unit == "cm3" || unit == QString::fromUtf8("см3")
        || unit == QString::fromUtf8("см³") || unit == "cc";
    const double cc = cubic ? value : value * 1000;
    return cc >= 100 && cc <= 20000 ? qRound(cc) : 0;
}

template<class Key, class Value, class Compute>
Value normalizedOnce(QHash<Key, Value>& cache, const Key& raw, Compute compute)
{
    const auto found=cache.constFind(raw);
    if(found!=cache.cend())return found.value();
    const auto value=compute();
    // Bound memory for heterogeneous histories; lifetime is only this suggestion call.
    if(cache.size()<1024)cache.insert(raw,value);
    return value;
}

QString vehicleKey(const QString& brand, const QString& model, const QString& car)
{
    if (!brand.trimmed().isEmpty() && !model.trimmed().isEmpty()) return key(brand + model);
    const QString combined = key(car);
    // A bare brand does not identify a model.
    return combined.isEmpty() || combined == key(brand) ? QString() : combined;
}

bool exec(QSqlQuery& query, QString* error)
{
    if (query.exec()) return true;
    if (error) *error = query.lastError().text();
    return false;
}

}

bool HistoryPartsAutofillService::samePart(const WorkOrderPartRow& a, const WorkOrderPartRow& b)
{
    const QString aOem = key(a.partNumber), bOem = key(b.partNumber);
    if (!aOem.isEmpty() && !bOem.isEmpty()) return aOem == bOem;
    return titleKey(a.name) == titleKey(b.name);
}
bool HistoryPartsAutofillService::suggest(QSqlDatabase db, int orderNumber, const QString& serviceTitle,
                                         QList<WorkOrderPartRow>* output, QString* errorText)
{
    output->clear();
    if (errorText) errorText->clear();
    QSqlQuery target(db);
    target.prepare(QStringLiteral(
        "SELECT o.order_uid,o.car_brand,o.car_model,o.car,o.car_year,o.engine_volume,o.fuel_type "
        "FROM work_orders o WHERE o.number=? AND NOT %1").arg(OrderStoragePolicyService::closedOrderPredicate("o")));
    target.addBindValue(orderNumber);
    if (!exec(target, errorText)) return false;
    if (!target.next()) return true;
    const QString uid = target.value(0).toString();
    const QString car = vehicleKey(target.value(1).toString(), target.value(2).toString(), target.value(3).toString());
    const int year = target.value(4).toInt();
    const int cc = volume(target.value(5).toString());
    const QString fuel = key(target.value(6).toString());
    const QString title = titleKey(serviceTitle);
    if (uid.isEmpty() || car.isEmpty() || year <= 0 || cc == 0 || fuel.isEmpty() || title.isEmpty()) return true;
    target.finish();

    // Router views include both active and archived completed orders, keyed by UID.
    const auto views = db.tables(QSql::Views);
    const QString orders = views.contains("router_work_orders") ? "router_work_orders" : "work_orders";
    const QString services = views.contains("router_work_order_services") ? "router_work_order_services" : "work_order_services";
    const QString parts = views.contains("router_work_order_parts") ? "router_work_order_parts" : "work_order_parts";
    // Most histories reuse a small set of raw vehicle descriptions. Normalize those
    // once before the expensive service/part join; retain the general path when diverse.
    QSqlQuery descriptions(db);
    descriptions.setForwardOnly(true);
    descriptions.prepare(QStringLiteral(
        "SELECT DISTINCT o.car_brand,o.car_model,o.car,o.engine_volume,o.fuel_type "
        "FROM %1 o WHERE o.car_year=? AND o.order_uid<>? AND %2 LIMIT 257")
        .arg(orders,OrderStoragePolicyService::closedOrderPredicate("o")));
    descriptions.addBindValue(year);descriptions.addBindValue(uid);
    if(!exec(descriptions,errorText))return false;
    QStringList vehiclePredicates;
    QVariantList vehicleBindings;
    int descriptionsSeen=0;
    bool bounded=true;
    while(descriptions.next()) {
        if(++descriptionsSeen>256){bounded=false;break;}
        if(vehicleKey(descriptions.value(0).toString(),descriptions.value(1).toString(),descriptions.value(2).toString())!=car
            || volume(descriptions.value(3).toString())!=cc || key(descriptions.value(4).toString())!=fuel)continue;
        if(vehiclePredicates.size()>=64){bounded=false;break;}
        vehiclePredicates<<QStringLiteral("(o.car_brand IS ? AND o.car_model IS ? AND o.car IS ? AND o.engine_volume IS ? AND o.fuel_type IS ?)");
        for(int i=0;i<5;++i)vehicleBindings<<descriptions.value(i);
    }
    if(descriptions.lastError().isValid()){if(errorText)*errorText=descriptions.lastError().text();return false;}
    descriptions.finish();
    if(bounded && vehiclePredicates.isEmpty())return true;
    const QString vehicleFilter=bounded?QStringLiteral(" AND (")+vehiclePredicates.join(QStringLiteral(" OR "))+QStringLiteral(") "):QString();
    QSqlQuery history(db);
    history.setForwardOnly(true);
    history.prepare(QStringLiteral(
        "SELECT o.order_uid,o.number,o.car_brand,o.car_model,o.car,o.engine_volume,o.fuel_type,s.id,s.title "
        "FROM %1 o JOIN %2 s ON s.order_uid=o.order_uid "
        "WHERE o.car_year=? AND o.order_uid<>? AND %3 %6 "
        "AND EXISTS (SELECT 1 FROM %5 p WHERE p.order_uid=o.order_uid AND p.qty>0 "
        "AND TRIM(COALESCE(p.name,''))<>'' AND (p.service_id=s.id OR "
        "(COALESCE(p.service_id,0)=0 AND (SELECT COUNT(*) FROM %2 only_service WHERE only_service.order_uid=o.order_uid)=1))) "
        "ORDER BY %4 DESC,o.number DESC,s.id DESC")
        .arg(orders, services, OrderStoragePolicyService::closedOrderPredicate("o"),
             DateFormats::isoSql("COALESCE(NULLIF(o.closed_at,''),o.opened_at)"), parts, vehicleFilter));
    history.addBindValue(year);
    history.addBindValue(uid);
    if(bounded)for(const auto& value:vehicleBindings)history.addBindValue(value);
    if (!exec(history, errorText)) return false;

    QList<WorkOrderPartRow> templateParts;
    QString sourceUid;
    int sourceNumber = 0;
    QHash<QString,QString> titleKeys,fuelKeys;
    QHash<QStringList,QString> vehicleKeys;
    QHash<QString,int> volumes;
    while (history.next()) {
        const QString rawTitle=history.value(8).toString();
        if(normalizedOnce(titleKeys,rawTitle,[&]{return titleKey(rawTitle);})!=title)continue;
        const QStringList rawVehicle{history.value(2).toString(),history.value(3).toString(),history.value(4).toString()};
        if(normalizedOnce(vehicleKeys,rawVehicle,[&]{return vehicleKey(rawVehicle[0],rawVehicle[1],rawVehicle[2]);})!=car)continue;
        const QString rawVolume=history.value(5).toString(),rawFuel=history.value(6).toString();
        if(normalizedOnce(volumes,rawVolume,[&]{return volume(rawVolume);})!=cc
            || normalizedOnce(fuelKeys,rawFuel,[&]{return key(rawFuel);})!=fuel)continue;
        sourceUid = history.value(0).toString();
        sourceNumber = history.value(1).toInt();
        const int sourceService = history.value(7).toInt();
        QSqlQuery selection(db);
        selection.prepare(QStringLiteral(
            "SELECT p.part_number,p.name,p.qty FROM %1 p WHERE p.order_uid=? AND "
            "(p.service_id=? OR (COALESCE(p.service_id,0)=0 AND "
            "(SELECT COUNT(*) FROM %2 s WHERE s.order_uid=p.order_uid)=1)) ORDER BY p.id").arg(parts, services));
        selection.addBindValue(sourceUid);
        selection.addBindValue(sourceService);
        if (!exec(selection, errorText)) return false;
        QHash<QString, int> positions;
        while (selection.next()) {
            WorkOrderPartRow row;
            row.partNumber = selection.value(0).toString().trimmed();
            row.name = selection.value(1).toString().trimmed();
            row.qty = selection.value(2).toDouble();
            if (row.name.isEmpty() || !std::isfinite(row.qty) || row.qty <= 0) continue;
            const QString identity = key(row.partNumber).isEmpty() ? "name:" + titleKey(row.name) : "oem:" + key(row.partNumber);
            if (positions.contains(identity)) templateParts[positions.value(identity)].qty += row.qty;
            else {
                positions.insert(identity, templateParts.size());
                templateParts.append(row);
            }
        }
        if (!templateParts.isEmpty()) break;
    }
    history.finish();
    for (auto& row : templateParts)
        row.note = QString::fromUtf8("Автопідбір із завершеного наряду №%1 (%2)").arg(sourceNumber).arg(sourceUid);
    *output = templateParts;
    return true;
}

bool HistoryPartsAutofillService::apply(QSqlDatabase db, int orderNumber, int serviceId, QString* errorText)
{
    if (errorText) errorText->clear();
    QSqlQuery target(db);
    target.prepare("SELECT o.order_uid,s.title FROM work_orders o JOIN work_order_services s ON s.order_uid=o.order_uid WHERE o.number=? AND s.id=?");
    target.addBindValue(orderNumber);
    target.addBindValue(serviceId);
    if (!exec(target, errorText)) return false;
    if (!target.next()) return true;
    const QString uid = target.value(0).toString();
    const QString title = target.value(1).toString();
    target.finish();
    QList<WorkOrderPartRow> templateParts;
    if (!suggest(db, orderNumber, title, &templateParts, errorText)) return false;
    if (templateParts.isEmpty()) return true;

    QSqlQuery existingQuery(db);
    existingQuery.prepare("SELECT part_number,name FROM work_order_parts WHERE order_uid=?");
    existingQuery.addBindValue(uid);
    if (!exec(existingQuery, errorText)) return false;
    QList<WorkOrderPartRow> existing;
    while (existingQuery.next()) {
        WorkOrderPartRow row;
        row.partNumber = existingQuery.value(0).toString();
        row.name = existingQuery.value(1).toString();
        existing.append(row);
    }
    existingQuery.finish();
    for (auto row : templateParts) {
        bool found = false;
        for (const auto& current : existing) {
            if (samePart(row, current)) { found = true; break; }
        }
        if (found) continue;
        row.serviceId = serviceId;
        if (!PartRepository::insert(db, orderNumber, row, nullptr, errorText)) return false;
        existing.append(row);
    }
    return true;
}

namespace HistoryPartsAutofillService {
bool suggest(int orderNumber, const QString& title, QList<WorkOrderPartRow>* parts, QString* errorText) { return suggest(Database::db(), orderNumber, title, parts, errorText); }
}
