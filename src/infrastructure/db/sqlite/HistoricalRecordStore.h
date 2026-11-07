#pragma once
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>
#include <QUuid>

// Immutable source-scoped records. No view or query used for billing joins these
// tables. They preserve evidence, not additional payable work or stock entries.
namespace HistoricalRecordStore {
struct Record {
    QString sourceId, table, rowKey, ownerUid, recordUid;
    QStringList columns, storageTypes;
    QList<QVariant> values;
};
struct Source { QString id, origin, digest, recordedAt; qint64 rows=0; };
inline QString ident(QString value){return '"'+value.replace('"',"\"\"")+'"';}
inline bool fail(QString* error,const QString& text){if(error)*error="Historical records: "+text;return false;}
inline QByteArray digestFile(const QString& path)
{
    QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file))return {};
    return hash.result().toHex();
}
inline bool ensure(QSqlDatabase db,QString* error)
{
    QSqlQuery q(db);
    for(const auto& sql:QStringList{
        "CREATE TABLE IF NOT EXISTS archive_rollover.historical_sources(source_id TEXT PRIMARY KEY,origin TEXT NOT NULL,digest TEXT NOT NULL,schema_payload BLOB NOT NULL,recorded_at TEXT NOT NULL,rows_count INTEGER NOT NULL CHECK(rows_count>=0))",
        "CREATE TABLE IF NOT EXISTS archive_rollover.historical_records(source_id TEXT NOT NULL,table_name TEXT NOT NULL,row_key TEXT NOT NULL,owner_uid TEXT NOT NULL,record_uid TEXT NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(source_id,table_name,row_key))",
        "CREATE INDEX IF NOT EXISTS archive_rollover.historical_records_owner ON historical_records(owner_uid,source_id,table_name)",
        "CREATE INDEX IF NOT EXISTS archive_rollover.historical_records_uid ON historical_records(record_uid,source_id,table_name)"})
        if(!q.exec(sql))return fail(error,q.lastError().text());
    if(!q.exec("SELECT 1 FROM archive_rollover.sqlite_master WHERE type='trigger' AND tbl_name IN ('historical_sources','historical_records') LIMIT 1"))return fail(error,q.lastError().text());
    if(q.next())return fail(error,"History store has unexpected triggers");
    return !q.lastError().isValid() || fail(error,q.lastError().text());
}
inline QByteArray encode(const Record& row)
{
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
    stream<<quint32(1)<<row.columns<<row.storageTypes;
    for(int i=0;i<row.values.size();++i) {
        const auto& type=row.storageTypes[i];const auto& value=row.values[i];
        if(type=="integer")stream<<value.toLongLong();else if(type=="real")stream<<value.toDouble();
        else if(type=="text")stream<<value.toString();else if(type=="blob")stream<<value.toByteArray();
        else if(type!="null")return {};
    }
    return stream.status()==QDataStream::Ok ? bytes : QByteArray();
}
inline bool decode(const QByteArray& bytes,Record* row,QString* error)
{
    if(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=row->rowKey.section(':',0,0))return fail(error,"Historical payload fingerprint differs");
    QDataStream stream(bytes);stream.setVersion(QDataStream::Qt_6_0);quint32 version=0;
    stream>>version>>row->columns>>row->storageTypes;row->values.clear();
    if(version!=1 || row->columns.size()!=row->storageTypes.size())return fail(error,"Unsupported historical field format");
    for(const auto& type:row->storageTypes) {
        if(type=="integer"){qint64 value=0;stream>>value;row->values<<QVariant::fromValue(value);}
        else if(type=="real"){double value=0;stream>>value;row->values<<value;}
        else if(type=="text"){QString value;stream>>value;row->values<<value;}
        else if(type=="blob"){QByteArray value;stream>>value;row->values<<value;}
        else if(type=="null")row->values<<QVariant();else return fail(error,"Unsupported historical storage class");
    }
    return (stream.status()==QDataStream::Ok && stream.atEnd()) || fail(error,"Damaged historical record");
}
inline bool inherit(QSqlDatabase db,QString* error)
{
    QSqlQuery q(db);if(!q.exec("SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='historical_sources'"))return fail(error,q.lastError().text());
    const bool present=q.next();q.finish();if(!present)return true;
    if(!q.exec("SELECT source_id FROM main.historical_sources ORDER BY source_id"))return fail(error,q.lastError().text());
    QStringList ids;while(q.next())ids<<q.value(0).toString();if(q.lastError().isValid())return fail(error,q.lastError().text());q.finish();
    if(!q.exec("SELECT 1 FROM main.historical_records r LEFT JOIN main.historical_sources s ON s.source_id=r.source_id WHERE s.source_id IS NULL LIMIT 1"))return fail(error,q.lastError().text());
    if(q.next())return fail(error,"Inherited record has no source provenance");q.finish();
    for(const auto& id:ids) {
        q.prepare("SELECT origin,digest,rows_count,(SELECT COUNT(*) FROM main.historical_records r WHERE r.source_id=s.source_id) FROM main.historical_sources s WHERE source_id=?");q.addBindValue(id);
        if(!q.exec() || !q.next())return fail(error,"Inherited source is unavailable");
        const auto expected=QString::fromLatin1(QCryptographicHash::hash(q.value(0).toString().toUtf8()+'\0'+q.value(1).toByteArray(),QCryptographicHash::Sha256).toHex());
        if(expected!=id || q.value(2).toLongLong()!=q.value(3).toLongLong())return fail(error,"Inherited source provenance/count differs");q.finish();
        q.prepare("SELECT row_key,payload FROM main.historical_records WHERE source_id=?");q.addBindValue(id);
        if(!q.exec())return fail(error,q.lastError().text());
        while(q.next()){Record row;row.rowKey=q.value(0).toString();if(!decode(q.value(1).toByteArray(),&row,error))return false;}
        if(q.lastError().isValid())return fail(error,q.lastError().text());q.finish();
        q.prepare("SELECT 1 FROM archive_rollover.historical_sources WHERE source_id=?");q.addBindValue(id);
        if(!q.exec())return fail(error,q.lastError().text());const bool exists=q.next();q.finish();
        for(const auto& table:QStringList{"historical_sources","historical_records"}) {
            if(!exists) {
                q.prepare("INSERT INTO archive_rollover."+table+" SELECT * FROM main."+table+" WHERE source_id=?");q.addBindValue(id);
                if(!q.exec())return fail(error,q.lastError().text());q.finish();
            }
            for(const bool reverse:{false,true}) {
                const QString from=reverse ? "archive_rollover" : "main",to=reverse ? "main" : "archive_rollover";
                q.prepare("SELECT * FROM "+from+"."+table+" WHERE source_id=? EXCEPT SELECT * FROM "+to+"."+table+" WHERE source_id=?");q.addBindValue(id);q.addBindValue(id);
                if(!q.exec())return fail(error,q.lastError().text());
                if(q.next())return fail(error,"Inherited historical version differs: "+id);
                if(q.lastError().isValid())return fail(error,q.lastError().text());q.finish();
            }
        }
    }
    return true;
}
// Caller holds the encompassing archive/workspace transaction. The source is
// an immutable staging snapshot; source_id includes origin, never just values.
inline bool capture(QSqlDatabase db,const QString& origin,const QByteArray& digest,QString* sourceId,QString* error)
{
    if(origin.isEmpty() || digest.size()!=64)return fail(error,"Missing source provenance");
    QSqlQuery encoding(db);
    if(!encoding.exec("PRAGMA main.encoding") || !encoding.next() || encoding.value(0).toString()!="UTF-8")return fail(error,"Source text encoding requires an explicit lossless adapter");encoding.finish();
    if(!ensure(db,error) || !inherit(db,error))return false;
    const QString id=QString::fromLatin1(QCryptographicHash::hash(origin.toUtf8()+'\0'+digest,QCryptographicHash::Sha256).toHex());
    QSqlQuery q(db);QJsonArray schema;QStringList tables;
    if(!q.exec("SELECT type,name,tbl_name,sql FROM main.sqlite_master WHERE name NOT GLOB 'sqlite_*' AND tbl_name NOT IN ('historical_sources','historical_records') ORDER BY type,name"))return fail(error,q.lastError().text());
    while(q.next()) {
        schema.append(QJsonObject{{"type",q.value(0).toString()},{"name",q.value(1).toString()},{"table",q.value(2).toString()},{"sql",q.value(3).toString()}});
        if(q.value(0).toString()=="table")tables<<q.value(1).toString();
    }
    if(q.lastError().isValid())return fail(error,q.lastError().text());q.finish();
    const auto schemaBytes=QJsonDocument(schema).toJson(QJsonDocument::Compact);
    q.prepare("SELECT origin,digest,schema_payload FROM archive_rollover.historical_sources WHERE source_id=?");q.addBindValue(id);
    if(!q.exec())return fail(error,q.lastError().text());const bool exists=q.next();
    if(exists && (q.value(0).toString()!=origin || q.value(1).toByteArray()!=digest || q.value(2).toByteArray()!=schemaBytes))return fail(error,"Source identity record differs");q.finish();
    if(!exists) {
        q.prepare("INSERT INTO archive_rollover.historical_sources VALUES(?,?,?,?,?,0)");q.addBindValue(id);q.addBindValue(origin);q.addBindValue(QString::fromLatin1(digest));q.addBindValue(schemaBytes);q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        if(!q.exec())return fail(error,q.lastError().text());q.finish();
    }
    qint64 total=0;
    for(const auto& table:tables) {
        Record row;row.sourceId=id;row.table=table;QStringList fields,types,raw;
        if(!q.exec("SELECT * FROM main."+ident(table)+" LIMIT 0"))return fail(error,q.lastError().text());
        const auto info=q.record();for(int i=0;i<info.count();++i){const auto name=info.fieldName(i);row.columns<<name;fields<<ident(name);types<<"typeof("+ident(name)+")";raw<<"CAST("+ident(name)+" AS BLOB)";}q.finish();
        if(fields.isEmpty())return fail(error,"No fields in source table: "+table);
        QSqlQuery read(db);if(!read.exec("SELECT "+fields.join(',')+","+types.join(',')+","+raw.join(',')+" FROM main."+ident(table)))return fail(error,read.lastError().text());
        QMap<QString,qint64> occurrences;
        while(read.next()) {
            row.values.clear();row.storageTypes.clear();
            for(int i=0;i<row.columns.size();++i){row.values<<read.value(i);row.storageTypes<<read.value(i+row.columns.size()).toString();}
            for(int i=0;i<row.columns.size();++i)if(row.storageTypes[i]=="text" && row.values[i].toString().toUtf8()!=read.value(i+2*row.columns.size()).toByteArray())return fail(error,"Source text cannot be decoded losslessly: "+table+"."+row.columns[i]);
            auto value=[&](const QString& name){const int index=row.columns.indexOf(name);return index<0 ? QVariant() : row.values.at(index);};
            row.ownerUid=value("order_uid").toString();row.recordUid.clear();
            if(table=="work_orders")row.recordUid=row.ownerUid;
            else if(table=="work_order_parts")row.recordUid=value("part_uid").toString();
            else if(table=="work_order_services")row.recordUid=value("service_uid").toString();
            if(row.ownerUid.isEmpty()) {
                QString parent,field;
                if(table=="service_masters" || table=="work_order_service_attachments" || table=="outsource_jobs") {parent="work_order_services";field="service_id";}
                if(table=="work_order_part_attachments") {parent="work_order_parts";field="part_id";}
                if(!parent.isEmpty() && row.columns.contains(field)) {
                    QSqlQuery owner(db);owner.prepare("SELECT order_uid FROM main."+parent+" WHERE id=?");owner.addBindValue(value(field));
                    if(!owner.exec())return fail(error,owner.lastError().text());
                    if(owner.next())row.ownerUid=owner.value(0).toString();
                    if(owner.next() || owner.lastError().isValid())return fail(error,"Ambiguous historical child owner");
                }
            }
            // Catalogue keys are source-scoped metadata, never child UID aliases.
            if(table.startsWith("parts_catalog"))row.ownerUid.clear();
            const auto payload=encode(row);if(payload.isEmpty())return fail(error,"Cannot encode historical scalar values");const auto hash=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
            row.rowKey=hash+":"+QString::number(++occurrences[hash]);
            if(exists) {
                q.prepare("SELECT owner_uid,record_uid,payload FROM archive_rollover.historical_records WHERE source_id=? AND table_name=? AND row_key=?");q.addBindValue(id);q.addBindValue(table);q.addBindValue(row.rowKey);
                if(!q.exec() || !q.next() || q.value(0).toString()!=row.ownerUid || q.value(1).toString()!=row.recordUid || q.value(2).toByteArray()!=payload)return fail(error,"Historical record is missing or differs");q.finish();
            } else {
                q.prepare("INSERT INTO archive_rollover.historical_records VALUES(?,?,?,?,?,?)");q.addBindValue(id);q.addBindValue(table);q.addBindValue(row.rowKey);q.addBindValue(row.ownerUid.isNull()?QString(""):row.ownerUid);q.addBindValue(row.recordUid.isNull()?QString(""):row.recordUid);q.addBindValue(payload);
                if(!q.exec())return fail(error,q.lastError().text());q.finish();
            }
            ++total;
        }
        if(read.lastError().isValid())return fail(error,read.lastError().text());
    }
    q.prepare("SELECT COUNT(*) FROM archive_rollover.historical_records WHERE source_id=?");q.addBindValue(id);
    if(!q.exec() || !q.next() || q.value(0).toLongLong()!=total)return fail(error,"Historical row count differs");q.finish();
    if(!exists) {
        q.prepare("UPDATE archive_rollover.historical_sources SET rows_count=? WHERE source_id=?");q.addBindValue(total);q.addBindValue(id);if(!q.exec() || q.numRowsAffected()!=1)return fail(error,q.lastError().text());q.finish();
    } else {
        q.prepare("SELECT rows_count FROM archive_rollover.historical_sources WHERE source_id=?");q.addBindValue(id);if(!q.exec() || !q.next() || q.value(0).toLongLong()!=total)return fail(error,"Historical inventory count differs");q.finish();
    }
    if(sourceId)*sourceId=id;return true;
}
inline bool captureFile(const QString& input,const QString& workspace,const QString& origin,QString* error)
{
    if(!QFile::exists(input) || !QFile::exists(workspace))return fail(error,"Snapshot or workspace is missing");
    const auto digest=digestFile(input);const auto name=QUuid::createUuid().toString();
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(input);if(!db.open())return fail(error,db.lastError().text());QSqlQuery q(db);
    q.prepare("ATTACH DATABASE ? AS archive_rollover");q.addBindValue(workspace);if(!q.exec())return fail(error,q.lastError().text());q.finish();
    if(!db.transaction())return fail(error,db.lastError().text());auto rollback=qScopeGuard([&]{db.rollback();});
    if(!capture(db,origin,digest,nullptr,error))return false;
    if(!db.commit())return fail(error,db.lastError().text());rollback.dismiss();return true;
}
inline bool sources(QSqlDatabase db,const QString& owner,QList<Source>* output,QString* error)
{
    output->clear();QSqlQuery q(db);
    if(!q.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='historical_sources'"))return fail(error,q.lastError().text());const bool present=q.next();q.finish();if(!present)return true;
    q.prepare("SELECT source_id,origin,digest,recorded_at,rows_count,(SELECT COUNT(*) FROM historical_records r WHERE r.source_id=s.source_id) FROM historical_sources s WHERE ?='' OR EXISTS(SELECT 1 FROM historical_records r WHERE r.source_id=s.source_id AND r.owner_uid=?) ORDER BY recorded_at,source_id");q.addBindValue(owner.isNull()?QString(""):owner);q.addBindValue(owner.isNull()?QString(""):owner);
    if(!q.exec())return fail(error,q.lastError().text());QList<Source> result;
    while(q.next()) {
        const auto expected=QString::fromLatin1(QCryptographicHash::hash(q.value(1).toString().toUtf8()+'\0'+q.value(2).toByteArray(),QCryptographicHash::Sha256).toHex());
        if(expected!=q.value(0).toString() || q.value(4).toLongLong()!=q.value(5).toLongLong())return fail(error,"Historical source provenance/count differs");
        result.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toLongLong()});
    }
    if(q.lastError().isValid())return fail(error,q.lastError().text());*output=result;return true;
}
inline bool read(QSqlDatabase db,const QString& sourceId,const QString& table,const QString& owner,const QString& uid,int offset,int limit,QList<Record>* output,QString* error)
{
    output->clear();if(sourceId.isEmpty() || table.isEmpty() || offset<0 || limit<1 || limit>500)return fail(error,"Invalid history read scope");
    QSqlQuery q(db);
    q.prepare("SELECT origin,digest,rows_count,(SELECT COUNT(*) FROM historical_records r WHERE r.source_id=s.source_id) FROM historical_sources s WHERE source_id=?");q.addBindValue(sourceId);
    if(!q.exec() || !q.next())return fail(error,"Historical source is unavailable");
    const auto expected=QString::fromLatin1(QCryptographicHash::hash(q.value(0).toString().toUtf8()+'\0'+q.value(1).toByteArray(),QCryptographicHash::Sha256).toHex());
    if(expected!=sourceId || q.value(2).toLongLong()!=q.value(3).toLongLong())return fail(error,"Historical source provenance/count differs");q.finish();
    q.prepare("SELECT row_key,owner_uid,record_uid,payload FROM historical_records WHERE source_id=? AND table_name=? AND (?='' OR owner_uid=?) AND (?='' OR record_uid=?) ORDER BY row_key LIMIT ? OFFSET ?");
    q.addBindValue(sourceId);q.addBindValue(table);q.addBindValue(owner.isNull()?QString(""):owner);q.addBindValue(owner.isNull()?QString(""):owner);q.addBindValue(uid.isNull()?QString(""):uid);q.addBindValue(uid.isNull()?QString(""):uid);q.addBindValue(limit);q.addBindValue(offset);
    if(!q.exec())return fail(error,q.lastError().text());QList<Record> rows;
    while(q.next()){Record row;row.sourceId=sourceId;row.table=table;row.rowKey=q.value(0).toString();row.ownerUid=q.value(1).toString();row.recordUid=q.value(2).toString();if(!decode(q.value(3).toByteArray(),&row,error))return false;rows.append(row);}
    if(q.lastError().isValid())return fail(error,q.lastError().text());*output=rows;return true;
}
}
