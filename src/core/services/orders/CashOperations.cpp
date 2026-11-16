#include "CashOperations.h"
#include "core/services/AccessControlService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QHash>

namespace CashOperations {
QString title(const QString& kind)
{
    static const QHash<QString,QString> labels{{"payment",QStringLiteral("Оплата наряду")},{"advance",QStringLiteral("Отримання авансу")},
        {"allocation",QStringLiteral("Зарахування авансу")},{"refund",QStringLiteral("Повернення")},{"void",QStringLiteral("Сторно")},
        {"expense",QStringLiteral("Видаток")},{"credit",QStringLiteral("Зарахування банком")},{"match",QStringLiteral("Банківська звірка")},
        {"move",QStringLiteral("Переміщення")},{"opening",QStringLiteral("Початковий залишок")},{"deposit",QStringLiteral("Службове внесення")},
        {"withdrawal",QStringLiteral("Службове вилучення")},{"supplier",QStringLiteral("Оплата постачальнику")}};
    return labels.value(kind,kind);
}
QString methodTitle(const QString& method)
{
    if(method=="cash")return QStringLiteral("Готівка");
    if(method=="card")return QStringLiteral("Картка");
    if(method=="transfer" || method=="bank_transfer")return QStringLiteral("Переказ");
    if(method=="allocation" || method=="internal")return QStringLiteral("Без руху коштів клієнта");
    return method.trimmed().isEmpty()?QStringLiteral("Історично не визначено"):method;
}
static Page selectRows(QDate first,QDate last,const QString& search,const QString& operation,const QString& method,int offset,bool all)
{
    Page result;auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view") || !first.isValid() || !last.isValid() || first>last || offset<0){
        result.error=QStringLiteral("Перевірте доступ та межі пошуку.");return result;
    }
    const auto tables=db.tables();QStringList sources;
    if(tables.contains("client_payments")) {
        const bool details = tables.contains("cash_payment_details");
        const QString customer = details ? "d.client_uid" : "NULL";
        const QString note = details ? "d.note" : "NULL";
        QString payments = "SELECT p.paid_on,p.recorded_ms,p.payment_uid,'payment'," + customer
            + ",p.order_uid,p.method,p.amount_cents,p.amount_cents," + note + ",NULL,p.actor FROM main.client_payments p";
        if (details) payments += " LEFT JOIN main.cash_payment_details d ON d.payment_uid=p.payment_uid";
        sources << payments + " WHERE p.method<>'advance_allocation'";
    }
    if(tables.contains("cash_advance_events"))
        sources<<"SELECT day,recorded_ms,uid,CASE WHEN source_uid IS NULL THEN 'advance' ELSE 'allocation' END,client_uid,order_uid,method,cents,CASE WHEN source_uid IS NULL THEN cents ELSE 0 END,note,source_uid,actor FROM main.cash_advance_events";
    if(tables.contains("cash_outflows"))
        sources<<"SELECT day,recorded_ms,uid,CASE WHEN kind='expense' THEN 'expense' WHEN kind IN ('payment','advance') THEN 'refund' ELSE 'void' END,client_uid,order_uid,method,cents,"
                 "CASE WHEN kind IN ('expense','void_allocation') THEN 0 WHEN reversed_uid IS NOT NULL THEN cents ELSE -cents END,reason,COALESCE(reversed_uid,source),actor FROM main.cash_outflows";
    if(tables.contains("cash_bank_events"))
        sources<<"SELECT day,recorded_ms,uid,action,NULL,NULL,'bank_transfer',CASE WHEN action='credit' THEN net ELSE matched END,0,reason,reference,actor FROM main.cash_bank_events WHERE action IN ('credit','match')";
    if(tables.contains("cash_account_moves"))
        sources<<"SELECT day,recorded_ms,uid,CASE WHEN src IS NULL THEN 'opening' ELSE 'move' END,NULL,NULL,'internal',cents,0,reason,src,actor FROM main.cash_account_moves";
    if (tables.contains("cash_treasury_entries"))
        sources << "SELECT day,recorded_ms,uid,kind,NULL,order_uid,CASE WHEN account='primary-cash' THEN 'cash' ELSE 'transfer' END,"
                   "ABS(delta),0,reason,source,actor FROM main.cash_treasury_entries";
    if(sources.isEmpty())return result;
    const bool orders=tables.contains("work_orders"),clients=tables.contains("crm_clients");
    const bool orderCustomer = orders && db.record("work_orders").contains("client_uid");
    QStringList identities;
    if (clients) identities << "NULLIF(c.full_name,'')";
    identities << "NULLIF(e.client_uid,'')";
    if (orderCustomer) identities << "NULLIF(o.client_uid,'')";
    const QString client = "COALESCE(" + identities.join(',') + ",'')";
    const QString vehicle=orders?"COALESCE(o.car,'')":"''";
    const QString number=orders?"CAST(o.number AS TEXT)":"''";
    QString sql="WITH e(day,ms,uid,kind,client_uid,order_uid,method,amount,net,reason,source,actor) AS ("+sources.join(" UNION ALL ")+") "
        "SELECT e.day,e.uid,e.kind,"+client+","+vehicle+",e.order_uid,"+number+",e.method,e.reason,e.source,e.actor,e.amount,e.net FROM e";
    if(orders)sql+=" LEFT JOIN main.work_orders o ON o.order_uid=e.order_uid";
    if(clients)sql+=orderCustomer?" LEFT JOIN main.crm_clients c ON c.client_uid=COALESCE(e.client_uid,o.client_uid)":" LEFT JOIN main.crm_clients c ON c.client_uid=e.client_uid";
    sql+=" WHERE e.day BETWEEN ? AND ?";
    QVariantList args{first.toString(Qt::ISODate),last.toString(Qt::ISODate)};
    if(!operation.isEmpty()){sql+=" AND e.kind=?";args.append(operation);}
    if(!method.isEmpty()){sql+=" AND (CASE WHEN e.method='bank_transfer' THEN 'transfer' ELSE e.method END)=?";args.append(method);}
    if(!search.trimmed().isEmpty()){
        sql+=" AND instr("+client+"||' '||"+vehicle+"||' '||COALESCE("+number+",'')||' '||COALESCE(e.reason,'')||' '||e.uid||' '||COALESCE(e.source,'')||' '||COALESCE(e.order_uid,''),?)>0";
        args.append(search.trimmed());
    }
    sql+=" ORDER BY e.day DESC,e.ms DESC,e.kind,e.uid";
    if (!all) { sql+=" LIMIT 101 OFFSET ?";args.append(offset); }
    QSqlQuery q(db);
    if(!q.prepare(sql)){result.error=q.lastError().text();return result;}
    for(const auto& arg:args)q.addBindValue(arg);
    if(!q.exec()){result.error=q.lastError().text();return result;}
    while(q.next()){
        if(!all && result.rows.size()==100){result.more=true;break;}
        result.rows.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toString(),
            q.value(6).toString(),q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toLongLong(),q.value(12).toLongLong()});
    }
    result.error=q.lastError().text();return result;
}
Page read(QDate first,QDate last,const QString& search,const QString& operation,const QString& method,int offset)
{
    return selectRows(first,last,search,operation,method,offset,false);
}
Page exportSelection(QDate first,QDate last,const QString& search,const QString& operation,const QString& method)
{
    if (!AccessControlService::currentUserHasPermission("cash.export"))
        return {{},false,QStringLiteral("Немає права експорту каси.")};
    return selectRows(first,last,search,operation,method,0,true);
}
}
