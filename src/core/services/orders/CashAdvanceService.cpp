#include "CashAdvanceService.h"
#include "CashBankService.h"
#include "core/finance/CustomerCash.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "core/crm/CrmFinancialService.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QUuid>

namespace CashAdvanceService {
namespace {
QString binding(QSqlDatabase db)
{
    const auto machine = QSysInfo::machineUniqueId();
    const auto path = QFileInfo(db.databaseName()).canonicalFilePath();
    if (machine.isEmpty() || path.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(machine + '\0' + QDir::cleanPath(path).toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool authorized(QSqlDatabase db)
{
    return AccessControlService::currentUserHasPermission(db,"cash.view");
}
bool exec(QSqlQuery& q, const QString& sql, const QVariantList& args)
{
    if (!q.prepare(sql)) return false;
    for (const auto& arg : args) q.addBindValue(arg);
    return q.exec();
}
QString receiptChannel(QSqlDatabase db,const Command& c,const QString& type,bool replay)
{
    QSqlQuery q(db);
    if(replay){
        QString saved;
        if(db.tables().contains("cash_receipt_channels")){
            if(!exec(q,"SELECT channel_uid FROM main.cash_receipt_channels WHERE kind=? AND receipt_uid=?",{type,c.requestUid}))return q.lastError().text();
            if(q.next())saved=q.value(0).toString();
        }
        return saved==c.channelUid?QString():QStringLiteral("Канал проведеного запиту відрізняється.");
    }
    if(c.channelUid.isEmpty())return {};
    if(c.method!="card")return QStringLiteral("Канал еквайрингу можна обрати лише для карткової оплати.");
    if(!db.tables().contains("cash_bank_events"))return QStringLiteral("Налаштуйте канал еквайрингу.");
    if(!exec(q,"SELECT account FROM main.cash_bank_events WHERE uid=? AND action='channel'",{c.channelUid}))return q.lastError().text();
    if(!q.next())return QStringLiteral("Канал еквайрингу не знайдено.");
    if(auto error=CashBankService::activeAccountError(db,q.value(0).toString());!error.isEmpty())return error;
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_receipt_channels(kind TEXT NOT NULL,receipt_uid TEXT NOT NULL,channel_uid TEXT NOT NULL REFERENCES cash_bank_events(uid),PRIMARY KEY(kind,receipt_uid))"))return q.lastError().text();
    if(!exec(q,"INSERT INTO main.cash_receipt_channels VALUES(?,?,?)",{type,c.requestUid,c.channelUid}))return q.lastError().text();
    return {};
}
QString advanceCorrections(QSqlDatabase db)
{
    if(!db.tables().contains("cash_outflows"))return {};
    return "-COALESCE((SELECT SUM(CASE WHEN kind='undo_advance' THEN -cents ELSE cents END) FROM main.cash_outflows "
           "WHERE kind IN ('advance','undo_advance','void_advance') AND source=a.uid),0)"
           "+COALESCE((SELECT SUM(v.cents) FROM main.cash_outflows v JOIN main.cash_advance_events b ON b.uid=v.source "
           "WHERE v.kind='void_allocation' AND b.source_uid=a.uid),0)";
}
Choices choices(const QString& sql, const QVariantList& args)
{
    auto db = Database::db();
    if (!authorized(db)) return {{},QStringLiteral("Немає прав на операції з авансами.")};
    QSqlQuery q(db);
    if (!exec(q,sql,args)) return {{},q.lastError().text()};
    Choices result;
    while (q.next()) result.rows.append({q.value(0).toString(),q.value(1).toString()});
    if (q.lastError().isValid()) return {{},q.lastError().text()};
    return result;
}
}
QString designateSingleWorkstation()
{
    auto db = Database::db();
    if (!authorized(db) || !AccessControlService::currentUserHasPermission(db,"cash.configure") || !CloudSyncSettings::localBackendEnabled())
        return QStringLiteral("Підтвердити єдиний ПК може працівник із правом налаштування каси в автономному режимі.");
    const auto key = binding(db);
    if (key.isEmpty()) return QStringLiteral("Не вдалося визначити локальний профіль.");
    const auto registered = Database::getSetting("cash.single_station",QString()).toString();
    if (registered == key) return {};
    if (!registered.isEmpty()) return QStringLiteral("Базу вже прив’язано до іншого профілю. Потрібна звірка.");
    return Database::setSetting("cash.single_station",key) ? QString() : QStringLiteral("Не вдалося зберегти прив’язку.");
}
Ledger ledger(QDate firstDay,QDate lastDay)
{
    auto db=Database::db();
    if (!AccessControlService::currentUserHasPermission(db,"cash.view")) return {{},QStringLiteral("Немає доступу до журналу каси.")};
    if (!firstDay.isValid() || !lastDay.isValid() || firstDay>lastDay) return {{},QStringLiteral("Перевірте межі періоду.")};
    if (!db.tables().contains("cash_advance_events")) return {};
    QSqlQuery q(db);
    if (!exec(q,"SELECT uid,order_uid,source_uid,day,cents FROM main.cash_advance_events "
                "WHERE day BETWEEN ? AND ? ORDER BY day DESC,recorded_ms DESC,uid",{firstDay.toString(Qt::ISODate),lastDay.toString(Qt::ISODate)}))
        return {{},q.lastError().text()};
    Ledger result;
    while(q.next()) result.entries.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),
        QDate::fromString(q.value(3).toString(),Qt::ISODate),q.value(4).toLongLong()});
    if (q.lastError().isValid()) return {{},q.lastError().text()};
    return result;
}
Choices clients()
{
    return choices("SELECT client_uid,full_name FROM crm_clients WHERE length(trim(client_uid))>0 ORDER BY full_name,client_uid",{});
}
Choices orders(const QString& client)
{
    return choices("SELECT order_uid,number FROM main.work_orders WHERE client_uid=? AND length(trim(order_uid))>0 ORDER BY number DESC",{client});
}
Choices advances(const QString& client)
{
    auto db = Database::db();
    if (!authorized(db)) return {{},QStringLiteral("Немає доступу.")};
    if (!db.tables().contains("cash_advance_events")) return {};
    const QString refunds=advanceCorrections(db);
    return choices("SELECT uid,day || ' · ' || printf('%lld,%02lld грн',remaining/100,remaining%100) || ' · ' || uid "
                   "FROM (SELECT a.uid,a.day,a.cents-COALESCE((SELECT SUM(b.cents) FROM main.cash_advance_events b "
                   "WHERE b.source_uid=a.uid),0)"+refunds+" AS remaining FROM main.cash_advance_events a "
                   "WHERE a.source_uid IS NULL AND a.client_uid=?) WHERE remaining>0 ORDER BY day,uid",{client});
}
QString postingAuthorityError(const QString& permission)
{
    const auto db=Database::db();
    if (!authorized(db) || !AccessControlService::currentUserHasPermission(db,permission)) return QStringLiteral("Немає прав на проведення.");
    return stationError();
}
QString stationError()
{
    const auto db=Database::db();
    const auto key=binding(db);
    if (!CloudSyncSettings::localBackendEnabled() || key.isEmpty() || Database::getSetting("cash.single_station",QString()).toString()!=key)
        return QStringLiteral("Для проведення потрібен підтверджений єдиний ПК або відповідь сервера.");
    return {};
}
Result postPayment(const Command& c)
{
    auto db=Database::db();
    const auto actor=ActivityAuditService::currentActor().accountId;
    auto fail=[](QString error){return Result{{},error,false};};
    if(QUuid(c.requestUid).isNull() || !c.advanceUid.isEmpty() || c.cents<=0 || c.cents>9000000000000000LL || c.clientUid.isEmpty() || c.orderUid.isEmpty() || c.note.size()>300 ||
       !QStringList{"cash","card","transfer"}.contains(c.method))return fail(QStringLiteral("Перевірте клієнта, наряд, спосіб і суму оплати."));
    QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return fail(q.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto error=postingAuthorityError("cash.receive");!error.isEmpty())return fail(error);
    QString error;
    if(!OrderClosureJournal::ensureSchema(db,&error))return fail(error);
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_payment_details(payment_uid TEXT PRIMARY KEY REFERENCES client_payments(payment_uid),client_uid TEXT NOT NULL,note TEXT NOT NULL)"))return fail(q.lastError().text());
    const auto method=c.method=="transfer"?QStringLiteral("bank_transfer"):c.method;
    const auto day=c.day.toString(Qt::ISODate);
    if(!exec(q,"SELECT p.order_uid,p.amount_cents,p.method,p.paid_on,p.actor,d.client_uid,d.note FROM main.client_payments p LEFT JOIN main.cash_payment_details d ON p.payment_uid=d.payment_uid WHERE p.request_uid=?",{c.requestUid}))return fail(q.lastError().text());
    if(q.next()){
        if(q.value(0)!=c.orderUid || q.value(1).toLongLong()!=c.cents || q.value(2)!=method || q.value(3)!=day || q.value(4)!=actor || q.value(5)!=c.clientUid || q.value(6).toString()!=c.note)
            return fail(QStringLiteral("Запит має інші реквізити."));
        if(auto err=receiptChannel(db,c,"payment",true);!err.isEmpty())return fail(err);
        return {c.requestUid,{},true};
    }
    if(auto err=receiptChannel(db,c,"payment",false);!err.isEmpty())return fail(err);
    if(c.day!=QDate::currentDate())return fail(QStringLiteral("Для нової оплати потрібна поточна дата."));
    if(!exec(q,"SELECT client_uid FROM main.work_orders WHERE order_uid=?",{c.orderUid}) || !q.next() || q.value(0)!=c.clientUid || q.next())return fail(QStringLiteral("Перевірте клієнта наряду."));
    const auto snapshot=CrmFinancialService::readOrder(db,c.orderUid);
    if(!snapshot.error.isEmpty())return fail(snapshot.error);
    if(!snapshot.documentedOrderReceipts || snapshot.orders.size()!=1 || c.cents>snapshot.orders.front().balanceCents())return fail(QStringLiteral("Потрібна підтверджена заборгованість на суму оплати."));
    if(!exec(q,"INSERT INTO main.client_payments(payment_uid,request_uid,order_uid,amount_cents,method,paid_on,actor,recorded_ms) VALUES(?,?,?,?,?,?,?,?)",
        {c.requestUid,c.requestUid,c.orderUid,c.cents,method,day,actor,QDateTime::currentMSecsSinceEpoch()}))return fail(q.lastError().text());
    if(!exec(q,"INSERT INTO main.cash_payment_details VALUES(?,?,?)",{c.requestUid,c.clientUid,c.note.isNull()?QStringLiteral(""):c.note}))return fail(q.lastError().text());
    if(c.method=="cash"){
        if(!db.tables().contains("cash_shifts") || !exec(q,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor}) || !q.next())return fail(QStringLiteral("Потрібна відкрита зміна оператора."));
        const auto shift=q.value(0).toString();
        if(q.next())return fail(QStringLiteral("Потрібна звірка касових змін."));
        if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_shift_receipts(payment_uid TEXT PRIMARY KEY NOT NULL,shift_uid TEXT NOT NULL REFERENCES cash_shifts(shift_uid))") ||
           !exec(q,"INSERT INTO main.cash_shift_receipts VALUES(?,?)",{c.requestUid,shift}))return fail(q.lastError().text());
    }
    if(!ActivityAuditService::record(db,"cash","payment",QStringLiteral("Оплата наряду"),0,"client_payment",c.requestUid,c.orderUid,"{}",&error))return fail(error);
    if(!db.commit())return fail(db.lastError().text());
    rollback.dismiss();return {c.requestUid,{},false};
}
Result post(const Command& c)
{
    auto fail=[](const QString& error){return Result{{},error,false};};
    auto db=Database::db();
    const auto actor=ActivityAuditService::currentActor().accountId;
    if (QUuid(c.requestUid).isNull() || c.cents<=0 || c.cents>9000000000000000LL || c.clientUid.trimmed().isEmpty() || !c.day.isValid())
        return fail(QStringLiteral("Некоректні реквізити авансу."));
    const bool allocation=!c.advanceUid.isEmpty();
    if ((allocation && (c.orderUid.isEmpty() || c.method!="allocation")) ||
        (!allocation && c.method!="cash" && c.method!="card" && c.method!="transfer"))
        return fail(QStringLiteral("Некоректний спосіб операції."));
    QSqlQuery q(db);
    if (!q.exec("BEGIN IMMEDIATE")) return fail(q.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(const auto error=postingAuthorityError(allocation?"cash.allocate":"cash.receive"); !error.isEmpty())return fail(error);
    if (!q.exec("CREATE TABLE IF NOT EXISTS main.cash_advance_events(uid TEXT PRIMARY KEY NOT NULL,client_uid TEXT NOT NULL,"
                "order_uid TEXT,source_uid TEXT REFERENCES cash_advance_events(uid),method TEXT NOT NULL,cents INTEGER NOT NULL CHECK(cents>0),"
                "day TEXT NOT NULL,note TEXT NOT NULL,actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL,shift_uid TEXT)"))
        return fail(q.lastError().text());
    if (!q.exec("CREATE INDEX IF NOT EXISTS main.cash_advance_source ON cash_advance_events(source_uid)"))
        return fail(q.lastError().text());
    const QString day=c.day.toString(Qt::ISODate);
    const QVariantList payload{c.clientUid,c.orderUid,c.advanceUid,c.method,c.cents,day,c.note,actor};
    if (!exec(q,"SELECT client_uid,order_uid,source_uid,method,cents,day,note,actor FROM main.cash_advance_events WHERE uid=?",{c.requestUid}))
        return fail(q.lastError().text());
    if (q.next()) {
        for (int i=0;i<payload.size();++i)
            if (q.value(i).toString()!=payload[i].toString()) return fail(QStringLiteral("Запит уже проведено з іншими реквізитами."));
        if(auto err=receiptChannel(db,c,"advance",true);!err.isEmpty())return fail(err);
        return {c.requestUid,{},true};
    }
    if(auto err=receiptChannel(db,c,"advance",false);!err.isEmpty())return fail(err);
    if (c.day!=QDate::currentDate()) return fail(QStringLiteral("Для нової операції потрібна поточна дата."));
    if (!exec(q,"SELECT COUNT(*) FROM crm_clients WHERE client_uid=?",{c.clientUid}) || !q.next() || q.value(0).toInt()!=1)
        return fail(QStringLiteral("Не підтверджено клієнта."));
    if (!c.orderUid.isEmpty()) {
        if (!exec(q,"SELECT client_uid FROM main.work_orders WHERE order_uid=?",{c.orderUid}) || !q.next()
            || q.value(0).toString()!=c.clientUid || q.next()) return fail(QStringLiteral("Наряд не належить обраному клієнту."));
    }
    QString shift;
    if (c.method=="cash") {
        // A shift is authoritative only once its opening count has been confirmed.
        if (!db.tables().contains("cash_shifts") || !exec(q,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor})
            || !q.next() || (shift=q.value(0).toString()).isEmpty() || q.next()) return fail(QStringLiteral("Потрібна підтверджена відкрита касова зміна."));
    }
    QString error;
    if (allocation) {
        const QString refunds=advanceCorrections(db);
        if (!exec(q,"SELECT a.client_uid,a.cents-COALESCE((SELECT SUM(b.cents) FROM main.cash_advance_events b WHERE b.source_uid=a.uid),0)"+refunds+" "
                    "FROM main.cash_advance_events a WHERE a.uid=? AND a.source_uid IS NULL",{c.advanceUid}) || !q.next())
            return fail(QStringLiteral("Не знайдено джерело авансу."));
        const QString client=q.value(0).toString();
        const qint64 available=q.value(1).toLongLong();
        q.finish();
        const auto snapshot=CrmFinancialService::readOrder(db,c.orderUid);
        if (!snapshot.error.isEmpty()) return fail(snapshot.error);
        if (snapshot.orders.size()!=1 || !snapshot.documentedOrderReceipts)
            return fail(QStringLiteral("Історія боргу наряду потребує звірки."));
        const auto applied=CustomerCash::allocateAdvance(client.toStdString(),c.clientUid.toStdString(),available,
            snapshot.orders.front().balanceCents(),c.cents,CustomerCash::BalanceAuthority::Confirmed);
        if (!std::holds_alternative<CustomerCash::Allocation>(applied)) return fail(QStringLiteral("Перевірте клієнта, борг та доступний залишок авансу."));
        if (!OrderClosureJournal::ensureSchema(db,&error)) return fail(error);
    }
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (!exec(q,"INSERT INTO main.cash_advance_events(uid,client_uid,order_uid,source_uid,method,cents,day,note,actor,recorded_ms,shift_uid) "
                "VALUES(?,?,NULLIF(?,''),NULLIF(?,''),?,?,?,?,?,?,?)",{c.requestUid,c.clientUid,c.orderUid,c.advanceUid,c.method,c.cents,day,
                c.note.isNull()?QStringLiteral(""):c.note,actor,now,shift})) return fail(q.lastError().text());
    if (allocation && !exec(q,"INSERT INTO main.client_payments(payment_uid,request_uid,order_uid,amount_cents,method,paid_on,actor,recorded_ms) "
                "VALUES(?,?,?,?,'advance_allocation',?,?,?)",{c.requestUid,c.requestUid,c.orderUid,c.cents,day,actor,now})) return fail(q.lastError().text());
    if (!ActivityAuditService::record(db,"cash",allocation?"allocate":"advance",allocation?QStringLiteral("Зарахування авансу"):QStringLiteral("Отримання авансу"),
                                     0,"cash_advance",c.requestUid,c.clientUid,"{}",&error)) return fail(error);
    if (!db.commit()) return fail(db.lastError().text());
    rollback.dismiss();
    return {c.requestUid,{},false};
}
}
