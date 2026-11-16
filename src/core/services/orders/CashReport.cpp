#include "CashReport.h"
#include "CashOperations.h"
#include "CashBankService.h"
#include "CashShiftService.h"
#include "ClientReceiptReader.h"
#include "core/services/AccessControlService.h"
#include "core/finance/ExactMoney.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QMap>
#include <algorithm>

namespace CashReport {
QString title(Type type)
{
    switch(type) {
    case Type::Shift: return QStringLiteral("Звіт за касовою зміною");
    case Type::Account: return QStringLiteral("Рух за касою або рахунком");
    case Type::Period: return QStringLiteral("Зведений рух за календарними днями");
    case Type::Advances: return QStringLiteral("Клієнтські аванси");
    case Type::Orders: return QStringLiteral("Розрахунки за нарядами");
    case Type::Refunds: return QStringLiteral("Повернення і сторнування");
    case Type::Transfers: return QStringLiteral("Внутрішні переміщення");
    case Type::Discrepancies: return QStringLiteral("Розбіжності та рішення");
    case Type::Acquiring: return QStringLiteral("Еквайринг та банківська звірка");
    case Type::Employees: return QStringLiteral("Касові дії працівників");
    }
    return {};
}
QList<QPair<QString,QString>> shifts(QString& error)
{
    auto db=Database::db();
    if (!AccessControlService::currentUserHasPermission(db,"cash.view")) {error=QStringLiteral("Немає доступу до каси.");return {};}
    if (!db.tables().contains("cash_shifts")) return {};
    QSqlQuery query(db);
    if (!query.exec("SELECT shift_uid,operator_uid,datetime(opened_ms/1000,'unixepoch') FROM main.cash_shifts ORDER BY opened_ms DESC,rowid DESC")) {
        error=query.lastError().text();return {};
    }
    QList<QPair<QString,QString>> result;
    while(query.next()) result.append({query.value(0).toString(),query.value(2).toString()+" UTC · "+query.value(1).toString()+" · "+query.value(0).toString()});
    error=query.lastError().text();
    return result;
}
Table read(Type type,QDate first,QDate last,const QString& selector)
{
    Table result;
    result.title=title(type);
    result.scope=type==Type::Shift ? QStringLiteral("Зміна ")+selector : first.toString(Qt::ISODate)+" — "+last.toString(Qt::ISODate);
    if(type==Type::Account) result.scope+=QStringLiteral(" · Рахунок ")+selector;
    auto db=Database::db();
    if (!AccessControlService::currentUserHasPermission(db,"cash.view") || !first.isValid() || !last.isValid() || first>last ||
        ((type==Type::Account || type==Type::Shift) && selector.isEmpty())) {
        result.error=QStringLiteral("Перевірте доступ, вибір рахунку чи зміни та період.");return result;
    }
    QSqlQuery boundary(db);
    if (!boundary.exec("SAVEPOINT cash_report_read")) {result.error=boundary.lastError().text();return result;}
    auto finish=qScopeGuard([&]{boundary.exec("RELEASE cash_report_read");});
    const auto tables=db.tables();
    const QVariantList dates{first.toString(Qt::ISODate),last.toString(Qt::ISODate)};
    QSet<int> money, additive;
    QMap<int,qint64> sums;
    QString reportAccount;
    QMap<QPair<QString,QString>,qint64> dailyAccounts;
    auto append=[&](const QString& sql,const QVariantList& args) {
        if (!result.error.isEmpty()) return;
        QSqlQuery q(db);
        if (!q.prepare(sql)) {result.error=q.lastError().text();return;}
        for(const auto& arg:args)q.addBindValue(arg);
        if (!q.exec()) {result.error=q.lastError().text();return;}
        while(q.next()) {
            QStringList row;
            for(int i=0;i<result.columns.size();++i) {
                const auto value=q.value(i);
                if (value.isNull()) {row<<"—";continue;}
                if (!money.contains(i)) {row<<value.toString();continue;}
                row<<ClientReceiptReader::money(value.toLongLong());
                if(additive.contains(i)){
                    const auto sum=ExactMoney::sum({sums.value(i),value.toLongLong()});
                    if (auto cents=std::get_if<ExactMoney::Cents>(&sum)) sums[i]=*cents;
                    else {result.error=QStringLiteral("Переповнення підсумку.");return;}
                }
            }
            if(type==Type::Period){
                const auto key=qMakePair(row.front(),reportAccount);
                const auto value=ExactMoney::sum({dailyAccounts.value(key),q.value(4).toLongLong()});
                if(auto cents=std::get_if<ExactMoney::Cents>(&value))dailyAccounts[key]=*cents;
                else {result.error=QStringLiteral("Переповнення руху за день.");return;}
            } else result.rows.append(row);
        }
        result.error=q.lastError().text();
    };
    if(type==Type::Shift){
        result.columns={"Час UTC","Документ","Подія","Працівник","Обліково","Перераховано","Коригування"};money={4,5,6};additive={6};
        if(tables.contains("cash_shifts"))
            append("SELECT datetime(opened_ms/1000,'unixepoch'),shift_uid,'Відкриття',operator_uid,opening_cents,NULL,NULL FROM main.cash_shifts WHERE shift_uid=?",{selector});
        if(tables.contains("cash_shift_transitions"))
            append("SELECT datetime(recorded_ms/1000,'unixepoch'),request_uid,message,actor,expected,counted,delta FROM main.cash_shift_transitions WHERE shift_uid=? ORDER BY rowid",{selector});
        qint64 closing=0;
        result.error= result.error.isEmpty() ? CashShiftService::balance(db,selector,closing) : result.error;
        if(result.error.isEmpty())result.totals<<QStringLiteral("Обліковий залишок зміни ")+ClientReceiptReader::money(closing);
    } else if(type==Type::Account || type==Type::Period){
        result.columns={"Дата","Документ","Операція","Працівник","Рух за рахунком"};money={4};additive={4};
        auto readAccount=[&](const QString& accountUid){
            if(tables.contains("cash_account_moves"))
                append("SELECT day,uid,CASE WHEN src IS NULL THEN 'Початковий залишок' ELSE 'Переміщення' END,actor,CASE WHEN dest=? THEN cents ELSE -cents END FROM main.cash_account_moves WHERE (src=? OR dest=?) AND day BETWEEN ? AND ?",QVariantList{accountUid,accountUid,accountUid}+dates);
            if(tables.contains("cash_treasury_entries"))
                append("SELECT day,uid,reason,actor,delta FROM main.cash_treasury_entries WHERE account=? AND day BETWEEN ? AND ?",QVariantList{accountUid}+dates);
            if(tables.contains("cash_bank_events"))
                append("SELECT day,uid,'Зарахування банком',actor,net FROM main.cash_bank_events WHERE action='credit' AND account=? AND day BETWEEN ? AND ?",QVariantList{accountUid}+dates);
            if(accountUid=="primary-cash"){
                if(tables.contains("cash_advance_events")) append("SELECT day,uid,'Аванс',actor,cents FROM main.cash_advance_events WHERE method='cash' AND source_uid IS NULL AND shift_uid IS NOT NULL AND day BETWEEN ? AND ?",dates);
                if(tables.contains("cash_shift_receipts")) append("SELECT p.paid_on,p.payment_uid,'Оплата',p.actor,p.amount_cents FROM main.cash_shift_receipts r JOIN main.client_payments p ON p.payment_uid=r.payment_uid WHERE p.paid_on BETWEEN ? AND ?",dates);
                if(tables.contains("cash_outflows")) append("SELECT day,uid,reason,actor,CASE WHEN reversed_uid IS NULL THEN -cents ELSE cents END FROM main.cash_outflows WHERE shift_uid IS NOT NULL AND day BETWEEN ? AND ?",dates);
                if(tables.contains("cash_opening_events")) append("SELECT effective_on,uid,reason,actor,delta FROM main.cash_opening_events WHERE delta<>0 AND effective_on BETWEEN ? AND ?",dates);
                if(tables.contains("cash_shift_transitions")) append("SELECT date(recorded_ms/1000,'unixepoch','localtime'),request_uid,reason,actor,delta FROM main.cash_shift_transitions WHERE delta<>0 AND date(recorded_ms/1000,'unixepoch','localtime') BETWEEN ? AND ?",dates);
            }
        };
        if(type==Type::Account) readAccount(selector);
        else {
            const auto accounts=CashBankService::accounts(result.error,true);
            for(const auto& account:accounts){
                reportAccount=account.name+" · "+account.uid;
                readAccount(account.uid);
                if(!result.error.isEmpty())return result;
            }
            result.columns={"Календарний день","Рахунок","Чистий рух за рахунком"};
            for(auto it=dailyAccounts.cbegin();it!=dailyAccounts.cend();++it)
                result.rows.append({it.key().first,it.key().second,ClientReceiptReader::money(it.value())});
            result.totals<<QStringLiteral("Чистий рух усіх рахунків ")+ClientReceiptReader::money(sums.value(4));
            sums.clear();
        }
    } else if(type==Type::Discrepancies){
        result.columns={"Дата","Документ","Стан","Автор","Обліково","Перераховано","Різниця","Причина"};money={4,5,6};
        if(tables.contains("cash_opening_events"))append("SELECT effective_on,uid,outcome,actor,expected,counted,counted-expected,reason FROM main.cash_opening_events WHERE (outcome IN ('discrepancy','adjusted') OR source_uid IS NOT NULL) AND effective_on BETWEEN ? AND ?",dates);
        if(tables.contains("cash_shift_transitions"))append("SELECT date(recorded_ms/1000,'unixepoch','localtime'),request_uid,outcome,actor,expected,counted,counted-expected,reason FROM main.cash_shift_transitions WHERE outcome IN ('discrepancy','adjusted') AND date(recorded_ms/1000,'unixepoch','localtime') BETWEEN ? AND ?",dates);
    } else if(type==Type::Acquiring){
        result.columns={"Дата","Документ","Операція","Підтверджено карткою","Зараховано банком","Комісія","Звірено"};money={3,4,5,6};additive=money;
        if(tables.contains("client_payments"))append("SELECT paid_on,payment_uid,'Оплата карткою',amount_cents,NULL,NULL,NULL FROM main.client_payments WHERE method='card' AND paid_on BETWEEN ? AND ?",dates);
        if(tables.contains("cash_advance_events"))append("SELECT day,uid,'Картковий аванс',cents,NULL,NULL,NULL FROM main.cash_advance_events WHERE method='card' AND source_uid IS NULL AND day BETWEEN ? AND ?",dates);
        if(tables.contains("cash_outflows"))append("SELECT day,uid,reason,CASE WHEN reversed_uid IS NULL THEN -cents ELSE cents END,NULL,NULL,NULL FROM main.cash_outflows WHERE method='card' AND day BETWEEN ? AND ?",dates);
        if(tables.contains("cash_bank_events"))append("SELECT day,reference,reason,NULL,CASE WHEN action='credit' THEN net END,CASE WHEN action='credit' THEN fee END,CASE WHEN action='match' THEN matched END FROM main.cash_bank_events WHERE action IN ('credit','match') AND day BETWEEN ? AND ?",dates);
    } else if(type==Type::Advances){
        result.columns={"Дата","Операція","Аванс","Клієнт","Наряд","Зміна вільного авансу","Рух коштів клієнта"};money={5,6};additive=money;
        if(tables.contains("cash_advance_events"))append("SELECT day,CASE WHEN source_uid IS NULL THEN 'Отримання' ELSE 'Зарахування' END,COALESCE(source_uid,uid),client_uid,order_uid,CASE WHEN source_uid IS NULL THEN cents ELSE -cents END,CASE WHEN source_uid IS NULL THEN cents ELSE 0 END FROM main.cash_advance_events WHERE day BETWEEN ? AND ?",dates);
        if(tables.contains("cash_outflows") && tables.contains("cash_advance_events"))append("SELECT v.day,v.reason,COALESCE(a.source_uid,a.uid),a.client_uid,a.order_uid,CASE WHEN v.kind IN ('undo_advance','void_allocation') THEN v.cents ELSE -v.cents END,CASE WHEN v.kind='void_allocation' THEN 0 WHEN v.kind='undo_advance' THEN v.cents ELSE -v.cents END FROM main.cash_outflows v JOIN main.cash_advance_events a ON a.uid=v.source WHERE v.kind IN ('advance','undo_advance','void_advance','void_allocation') AND v.day BETWEEN ? AND ?",dates);
    } else if(type==Type::Transfers){
        result.columns={"Дата","Документ","З рахунку","На рахунок","Працівник","Переміщено","Підстава"};money={5};additive={5};
        if(tables.contains("cash_account_moves"))append("SELECT day,uid,src,dest,actor,cents,reason FROM main.cash_account_moves WHERE src IS NOT NULL AND day BETWEEN ? AND ?",dates);
    } else if(type==Type::Employees){
        result.columns={"Час UTC","Працівник","Дія","Документ","Опис"};
        if(tables.contains("activity_audit_events"))append("SELECT occurred_at,actor_name,action,entity_id,title FROM main.activity_audit_events WHERE category='cash' AND date(occurred_at,'localtime') BETWEEN ? AND ?",dates);
    } else {
        result.columns={"Дата","Документ","Дія","Клієнт / наряд","Спосіб","Сума документа","Рух коштів клієнта"};
        qint64 total=0;
        int offset=0;
        for(;;){
            const auto page=CashOperations::read(first,last,{},{},{},offset);
            if(!page.error.isEmpty()){result.error=page.error;return result;}
            for(const auto& row:page.rows){
                if(type==Type::Orders && (row.order.isEmpty() || row.kind=="supplier"))continue;
                if(type==Type::Refunds && row.kind!="refund" && row.kind!="void")continue;
                const auto sum=ExactMoney::sum({total,row.net});
                if(!std::holds_alternative<ExactMoney::Cents>(sum)){result.error=QStringLiteral("Переповнення підсумку.");return result;}
                total=std::get<ExactMoney::Cents>(sum);
                result.rows.append({row.day,row.uid,CashOperations::title(row.kind),(row.name+" "+row.order).trimmed(),CashOperations::methodTitle(row.method),ClientReceiptReader::money(row.amount),ClientReceiptReader::money(row.net)});
            }
            if(!page.more)break;
            offset+=page.rows.size();
        }
        result.totals<<QStringLiteral("Рух коштів клієнтів ")+ClientReceiptReader::money(total);
    }
    std::stable_sort(result.rows.begin(),result.rows.end(),[](const QStringList& a,const QStringList& b){return a.front()<b.front();});
    for(auto i=sums.cbegin();i!=sums.cend();++i)result.totals<<result.columns.at(i.key())+" · "+ClientReceiptReader::money(i.value());
    return result;
}
}
