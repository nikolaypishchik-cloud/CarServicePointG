#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QUuid>
#include <QStringList>

#ifdef ORDER_ARCHIVE_TEST
#include <cstdlib>
#endif
namespace ArchiveTransferJournal {
inline void crashPoint(const char* phase) {
#ifdef ORDER_ARCHIVE_TEST
    if(qEnvironmentVariable("CS_ARCHIVE_CRASH")==QString::fromLatin1(phase)) std::_Exit(87);
#else
    Q_UNUSED(phase);
#endif
}
inline bool ensure(QSqlDatabase db, QString* error)
{
    QSqlQuery q(db);
    if (q.exec("CREATE TABLE IF NOT EXISTS archive_transfer_journal_v1(order_uid TEXT PRIMARY KEY NOT NULL, operation_id TEXT NOT NULL UNIQUE, order_number INTEGER NOT NULL, target_path TEXT NOT NULL, phase TEXT NOT NULL CHECK(phase IN ('prepared','copied','verified','routed','complete')), updated_at TEXT NOT NULL)")) return true;
    if (error) *error=q.lastError().text();
    return false;
}
inline bool begin(QSqlDatabase db, const QString& uid, int number, const QString& target, QString* error)
{
    if (uid.trimmed().isEmpty() || number<=0 || target.isEmpty()) {
        if (error) *error=QStringLiteral("Archive transfer identity is missing");
        return false;
    }
    if (!ensure(db,error)) return false;
    QSqlQuery q(db);
    if (!q.prepare("SELECT order_number,target_path,phase FROM archive_transfer_journal_v1 WHERE order_uid=?")) {if(error)*error=q.lastError().text();return false;}
    q.addBindValue(uid);
    if (!q.exec()) {if(error)*error=q.lastError().text();return false;}
    if (q.next() && q.value(2).toString()!="complete") {
        if (q.value(0).toInt()==number && q.value(1).toString()==target) return true;
        if(error)*error=QStringLiteral("Pending archive transfer has different identity/path");
        return false;
    }
    q.finish();
    if (!q.prepare("INSERT INTO archive_transfer_journal_v1 VALUES(?,?,?,?,'prepared',datetime('now')) ON CONFLICT(order_uid) DO UPDATE SET operation_id=excluded.operation_id,order_number=excluded.order_number,target_path=excluded.target_path,phase='prepared',updated_at=excluded.updated_at WHERE archive_transfer_journal_v1.phase='complete'")) {if(error)*error=q.lastError().text();return false;}
    q.addBindValue(uid);q.addBindValue(QUuid::createUuid().toString(QUuid::Id128));q.addBindValue(number);q.addBindValue(target);
    if (!q.exec() || q.numRowsAffected()!=1) {if(error)*error=q.lastError().text();return false;}
    return true;
}
inline bool advance(QSqlDatabase db, const QString& uid, const QString& next, QString* error)
{
    const QStringList phases{"prepared","copied","verified","routed","complete"};
    const int to=phases.indexOf(next);
    QSqlQuery q(db);
    if(to<1 || !q.prepare("SELECT phase FROM archive_transfer_journal_v1 WHERE order_uid=?")) {if(error)*error="Invalid archive phase";return false;}
    q.addBindValue(uid);
    if(!q.exec() || !q.next()) {if(error)*error="Archive transfer journal row missing";return false;}
    const auto previous=q.value(0).toString();const int from=phases.indexOf(previous);q.finish();
    if(from>=to)return true; // Restart never moves the journal backwards.
    if(from!=to-1) {if(error)*error="Archive phase cannot be skipped";return false;}
    if(!q.prepare("UPDATE archive_transfer_journal_v1 SET phase=?,updated_at=datetime('now') WHERE order_uid=? AND phase=?")) {if(error)*error=q.lastError().text();return false;}
    q.addBindValue(next);q.addBindValue(uid);q.addBindValue(previous);
    if(!q.exec() || q.numRowsAffected()!=1) {if(error)*error="Archive phase changed concurrently";return false;}
    return true;
}
}
