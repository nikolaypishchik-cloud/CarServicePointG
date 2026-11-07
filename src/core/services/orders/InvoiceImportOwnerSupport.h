#pragma once

#include "core/services/OrderSearchRepository.h"
#include "infrastructure/db/sqlite/OrderDeletionPublication.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace InvoiceImportOwnerSupport {
inline bool fail(QString* error, const QString& text)
{
    if (error) *error = text;
    return false;
}

// Read authoritative owners, not the optional search index or a physical-number join.
inline QList<OrderSearchHit> candidates(QSqlDatabase db, int visible, QString* error)
{
    if (error) error->clear();
    QList<OrderSearchHit> result;
    if (visible <= 0 || !db.isOpen()) {
        fail(error, QString::fromUtf8("База або номер наряду недоступні."));
        return result;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT order_uid,number,COALESCE(order_number,number),order_year,opened_at,plate,car,'active','' "
        "FROM work_orders WHERE COALESCE(order_number,number)=? "
        "UNION ALL SELECT r.order_uid,r.order_number,r.visible_order_number,r.order_year,r.opened_at,r.plate,r.car,r.storage_kind,r.storage_path "
        "FROM order_registry r WHERE r.visible_order_number=? "
        "AND NOT EXISTS(SELECT 1 FROM work_orders w WHERE w.order_uid=r.order_uid) "
        "ORDER BY 4 DESC,5 DESC,1"));
    q.addBindValue(visible); q.addBindValue(visible);
    if (!q.exec()) { fail(error, q.lastError().text()); return {}; }
    QSet<QString> seen;
    while (q.next()) {
        OrderSearchHit h;
        h.orderUid=q.value(0).toString().trimmed(); h.internalNumber=q.value(1).toInt();
        h.visibleNumber=q.value(2).toInt(); h.orderYear=q.value(3).toInt();
        h.openedAt=q.value(4).toString(); h.plate=q.value(5).toString(); h.car=q.value(6).toString();
        h.storageKind=q.value(7).toString(); h.storagePath=q.value(8).toString();
        if (h.orderUid.isEmpty() || h.internalNumber<=0 || seen.contains(h.orderUid)) {
            fail(error, QString::fromUtf8("Ідентичність наряду пошкоджена. Імпорт заблоковано.")); return {};
        }
        seen.insert(h.orderUid); result.append(h);
    }
    if (q.lastError().isValid()) { fail(error,q.lastError().text()); return {}; }
    return result;
}

inline QString select(const QList<OrderSearchHit>& hits, const QString& selected, QString* error)
{
    if (!selected.trimmed().isEmpty()) {
        for (const auto& hit:hits) if (hit.orderUid==selected.trimmed()) return hit.orderUid;
        fail(error,QString::fromUtf8("Вибраний власник наряду змінився або зник. Виберіть наряд повторно."));
    } else if (hits.size()==1) return hits.first().orderUid;
    else fail(error,hits.isEmpty()?QString::fromUtf8("Наряд не знайдено. Імпорт не створює нових нарядів.")
        :QString::fromUtf8("Номер належить кільком нарядам. Виберіть автомобіль, дату та UID у формі імпорту."));
    return {};
}

inline bool validateActive(QSqlDatabase db, int internal, int visible, const QString& uid, QString* error)
{
    if (!OrderDeletionPublication::checkOpenAllowed(db,internal,uid,error)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT order_uid,COALESCE(order_number,number) FROM work_orders WHERE number=? AND (SELECT COUNT(*) FROM work_orders WHERE order_uid=?)=1"));
    q.addBindValue(internal); q.addBindValue(uid);
    if (!q.exec()) return fail(error,q.lastError().text());
    if (!q.next() || uid.isEmpty() || q.value(0).toString()!=uid || q.value(1).toInt()!=visible)
        return fail(error,QString::fromUtf8("Власник наряду змінився. Імпорт скасовано до запису."));
    if (q.next() || q.lastError().isValid()) return fail(error,QString::fromUtf8("Не вдалося перевірити власника наряду."));
    return true;
}

inline bool validateStoredOwner(QSqlDatabase db, const OrderSearchHit& hit, QString* error)
{
    if (!OrderDeletionPublication::checkOpenAllowed(db,hit.internalNumber,hit.orderUid,error)) return false;
    QSqlQuery active(db);
    active.prepare(QStringLiteral("SELECT order_uid FROM work_orders WHERE number=?"));
    active.addBindValue(hit.internalNumber);
    if (!active.exec()) return fail(error,active.lastError().text());
    if (active.next()) return validateActive(db,hit.internalNumber,hit.visibleNumber,hit.orderUid,error);
    if (active.lastError().isValid()) return fail(error,active.lastError().text());
    if (!QFileInfo(hit.storagePath).isFile())
        return fail(error,QString::fromUtf8("Архів вибраного наряду недоступний. Імпорт не розпочато."));
    const QString connection=QStringLiteral("invoice-owner-")+QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool valid=false;
    {
        auto archive=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
        archive.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        archive.setDatabaseName(hit.storagePath);
        if (!archive.open()) fail(error,archive.lastError().text());
        else valid=validateActive(archive,hit.internalNumber,hit.visibleNumber,hit.orderUid,error);
        archive.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return valid;
}

inline QByteArray fileDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error()!=QFileDevice::NoError) return {};
    return hash.result();
}

// v1 keys and ID arrays remain readable. Verify each row's owner and actual bytes,
// never repair a receipt by copying a document over its existing attachment.
inline bool verifyReceipt(QSqlDatabase db, const QByteArray& json, const QList<QString>& owners,
                          const QByteArray& digest, QString* error)
{
    const auto saved=QJsonDocument::fromJson(json);
    if (!saved.isArray() || saved.array().size()!=owners.size() || owners.isEmpty() || digest.isEmpty())
        return fail(error,QString::fromUtf8("Не вдалося перевірити попередній імпорт. Повторне додавання заблоковано."));
    QSet<int> ids;
    const auto entries=saved.array();
    for (int i=0;i<entries.size();++i) {
        const int id=entries.at(i).toInt();
        if (id<=0 || ids.contains(id)) return fail(error,QString::fromUtf8("Пошкоджена квитанція імпорту."));
        ids.insert(id);
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT p.order_uid,w.order_uid,a.file_path,p.part_uid "
            "FROM work_order_parts p JOIN work_orders w ON w.number=p.order_number "
            "JOIN work_order_part_attachments a ON a.part_id=p.id WHERE p.id=?"));
        q.addBindValue(id);
        if (!q.exec()) return fail(error,q.lastError().text());
        if (!q.next() || q.value(0).toString()!=owners.at(i) || q.value(1).toString()!=owners.at(i)
            || q.value(3).toString().isEmpty()
            || fileDigest(q.value(2).toString())!=digest)
            return fail(error,QString::fromUtf8("Власник або вкладення попереднього імпорту змінилися. Повтор заблоковано."));
        if (q.next() || q.lastError().isValid()) return fail(error,QString::fromUtf8("Неоднозначне вкладення попереднього імпорту."));
    }
    return true;
}
} // namespace InvoiceImportOwnerSupport
