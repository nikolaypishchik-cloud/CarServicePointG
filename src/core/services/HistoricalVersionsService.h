#pragma once
#include "infrastructure/db/sqlite/HistoricalRecordStore.h"
#include "infrastructure/db/sqlite/MutationJournalSchema.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/services/AccessControlService.h"

namespace HistoricalVersionsService {
struct Version {QString id,origin,digest,recordedAt;};
struct Page {QStringList columns;QList<QList<QVariant>> rows;QList<QStringList> storageTypes;};
inline bool fail(QString* error,const QString& text){if(error)*error=text;return false;}
inline bool authorize(const QString& owner,QString* error)
{
    auto db=Database::db();if(!db.isOpen())return fail(error,QString::fromUtf8("Робочу базу не відкрито"));
    if(AccessControlService::currentActorIsMaster())return fail(error,QString::fromUtf8("Недостатньо прав для перегляду історичних версій"));
    if(owner.isEmpty())return AccessControlService::currentUserHasPermission(db,"nav.catalog") || fail(error,QString::fromUtf8("Немає доступу до каталогу"));
    const int number=Database::resolveOrderInternalNumberByUid(owner);
    return (number>0 && AccessControlService::currentUserHasPermission(db,"nav.history") && AccessControlService::canAccessOrder(db,number)) || fail(error,QString::fromUtf8("Немає доступу до історії цього наряду"));
}
inline bool versions(const QString& owner,QList<Version>* output,QString* error)
{
    output->clear();if(!authorize(owner,error))return false;
    QList<HistoricalRecordStore::Source> sources;if(!HistoricalRecordStore::sources(Database::db(),owner,&sources,error))return false;
    QList<Version> rows{{"current",QString::fromUtf8("Поточна версія"),{}, {}}};
    for(const auto& source:sources)rows.append({source.id,source.origin,source.digest,source.recordedAt});
    *output=rows;return true;
}
inline bool allowedVersion(const QString& owner,const QString& id,QString* error)
{
    QList<Version> rows;if(!versions(owner,&rows,error))return false;
    for(const auto& row:rows)if(row.id==id)return true;
    return fail(error,QString::fromUtf8("Версія не належить вибраному джерелу або наряду"));
}
inline bool catalogue(const QString& table){return table=="parts_catalog" || table=="parts_catalog_sources" || table=="parts_catalog_vehicle_map";}
inline QString uidColumn(const QString& table,const QString& owner)
{
    if(table=="order_registry" && owner.isEmpty())return "order_uid";
    if(table=="work_orders")return "order_uid";
    if(table=="work_order_services")return "service_uid";
    if(table=="work_order_parts")return "part_uid";
    return {};
}
inline bool tables(const QString& owner,const QString& id,QStringList* output,QString* error)
{
    output->clear();if(!allowedVersion(owner,id,error))return false;auto db=Database::db();QSqlQuery q(db);QStringList names;
    if(id=="current") {
        if(!q.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"))return fail(error,q.lastError().text());
        while(q.next()) {
            const auto name=q.value(0).toString();
            if((owner.isEmpty() && catalogue(name)) || (!owner.isEmpty() && QStringList{"work_orders","work_order_services","work_order_parts","outsource_repairs"}.contains(name)))names<<name;
        }
    } else {
        q.prepare("SELECT DISTINCT table_name FROM historical_records WHERE source_id=? AND (?='' OR owner_uid=?) ORDER BY table_name");q.addBindValue(id);q.addBindValue(owner.isNull()?QString(""):owner);q.addBindValue(owner.isNull()?QString(""):owner);
        if(!q.exec())return fail(error,q.lastError().text());
        const bool inspectAdditional=AccessControlService::currentUserHasPermission(db,"security.manage");
        const QStringList orderTables={"work_orders","work_order_services","work_order_parts","service_masters","work_order_part_attachments","work_order_service_attachments","outsource_jobs","outsource_repairs"};
        const bool sourceAdmin = inspectAdditional && !AccessControlService::currentOrderScope(db).restricted;
        while(q.next()) {
            const auto name=q.value(0).toString();
            const bool visible=owner.isEmpty()
                ? catalogue(name) || (sourceAdmin && (name==mutationJournalTableName() || name=="order_registry"))
                : orderTables.contains(name) || inspectAdditional;
            if(visible)names<<name;
        }
    }
    if(q.lastError().isValid())return fail(error,q.lastError().text());*output=names;return true;
}
inline bool read(const QString& owner,const QString& id,const QString& table,int offset,int limit,Page* output,QString* error,const QString& recordUid={})
{
    *output={};if(offset<0 || limit<1 || limit>100)return fail(error,QString::fromUtf8("Некоректна сторінка історії"));
    QStringList names;if(!tables(owner,id,&names,error))return false;if(!names.contains(table))return fail(error,QString::fromUtf8("Таблиця недоступна у вибраній версії"));
    const QString filterColumn=uidColumn(table,owner);
    if(!recordUid.isEmpty() && filterColumn.isEmpty())return fail(error,QString::fromUtf8("У цій таблиці немає стабільного UID запису"));
    Page page;
    if(id!="current") {
        QList<HistoricalRecordStore::Record> rows;
        // Registry projections carry the recorded order UID in the owner field.
        const bool byRegistryOwner=table=="order_registry" && owner.isEmpty() && !recordUid.isEmpty();
        if(!HistoricalRecordStore::read(Database::db(),id,table,byRegistryOwner?recordUid:owner,
                byRegistryOwner?QString():recordUid,offset,limit,&rows,error))return false;
        for(const auto& row:rows) {if(page.columns.isEmpty())page.columns=row.columns;if(page.columns!=row.columns)return fail(error,QString::fromUtf8("Схема історичних записів не узгоджена"));page.rows<<row.values;page.storageTypes<<row.storageTypes;}
    } else {
        QSqlQuery q(Database::db());const auto quoted=HistoricalRecordStore::ident(table);
        if(!q.exec("SELECT * FROM "+quoted+" LIMIT 0"))return fail(error,q.lastError().text());
        const auto info=q.record();QStringList fields,types;
        for(int i=0;i<info.count();++i){const auto name=info.fieldName(i);page.columns<<name;fields<<HistoricalRecordStore::ident(name);types<<"typeof("+HistoricalRecordStore::ident(name)+")";}q.finish();
        if(fields.isEmpty())return fail(error,QString::fromUtf8("Поточна схема недоступна"));
        QStringList predicates;if(!owner.isEmpty())predicates<<"order_uid=?";if(!recordUid.isEmpty())predicates<<filterColumn+"=?";
        q.prepare("SELECT "+fields.join(',')+","+types.join(',')+" FROM "+quoted+(predicates.isEmpty()?QString():QString(" WHERE ")+predicates.join(" AND "))+" ORDER BY "+fields.join(',')+" LIMIT ? OFFSET ?");
        if(!owner.isEmpty())q.addBindValue(owner);if(!recordUid.isEmpty())q.addBindValue(recordUid);q.addBindValue(limit);q.addBindValue(offset);
        if(!q.exec())return fail(error,q.lastError().text());
        while(q.next()){QList<QVariant> row;QStringList kinds;for(int i=0;i<fields.size();++i){row<<q.value(i);kinds<<q.value(i+fields.size()).toString();}page.rows<<row;page.storageTypes<<kinds;}
        if(q.lastError().isValid())return fail(error,q.lastError().text());
    }
    *output=page;return true;
}
}
