#include "core/services/orders/OrderFinancialService.h"

#include "core/services/OrderIdentityResolver.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "core/util/FormatUtils.h"

#include "core/finance/ExactMoney.h"
#include <limits>
#include <QScopeGuard>
#include <QUuid>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool refreshNumbers(QSqlDatabase db, const QList<int>& numbers, QString* errorText)
{
    const QString savepoint=QStringLiteral("financial_totals_")+QUuid::createUuid().toString(QUuid::Id128);
    QSqlQuery boundary(db);
    if(!boundary.exec("SAVEPOINT "+savepoint)) {if(errorText)*errorText=boundary.lastError().text();return false;}
    auto rollback=qScopeGuard([&]{
        QSqlQuery rollbackQuery(db);
        if(!rollbackQuery.exec("ROLLBACK TO "+savepoint)) {
            if(errorText) *errorText+=QStringLiteral("; rollback: ")+rollbackQuery.lastError().text();
            return; // Do not release an unrolled-back savepoint.
        }
        if(!rollbackQuery.exec("RELEASE "+savepoint) && errorText)
            *errorText+=QStringLiteral("; release: ")+rollbackQuery.lastError().text();
    });
    for(const auto number:numbers) {
        const auto totals=OrderFinancialService::readTotals(db,number);
        if(!totals.ok()) {if(errorText)*errorText=totals.error;return false;}
        // Existing cache columns are REAL, unlike the new exact cents domain.
        // Reject totals outside their reliable cent range rather than silently narrowing.
        constexpr qint64 legacyCacheLimit=99999999999999LL;
        if(totals.worksCents>legacyCacheLimit || totals.worksCents<-legacyCacheLimit
            || totals.partsCents>legacyCacheLimit || totals.partsCents<-legacyCacheLimit) {
            if(errorText)*errorText=QStringLiteral("Підсумок перевищує точність старого сховища. Потрібна міграція до цілих копійок.");
            return false;
        }
        QSqlQuery update(db);
        if(!update.prepare("UPDATE work_orders SET works_sum=?,parts_sum=? WHERE number=?")) {if(errorText)*errorText=update.lastError().text();return false;}
        update.addBindValue(totals.worksCents/100.0);update.addBindValue(totals.partsCents/100.0);update.addBindValue(number);
        if(!update.exec()) {if(errorText)*errorText=update.lastError().text();return false;}
        if(update.numRowsAffected()!=1) {if(errorText)*errorText=QStringLiteral("Financial totals header was not written");return false;}
        update.finish();
        if(DbThreadConnectionSupport::isRuntimeConnection(db.connectionName()) && !queueOrderRegistryPublication(db,number,errorText)) return false;
    }
    if(!boundary.exec("RELEASE "+savepoint)) {if(errorText)*errorText=boundary.lastError().text();return false;}
    rollback.dismiss();
    return true;
}
}

namespace OrderFinancialService {

bool refreshOrderFinancialSums(int orderNumber, QString* errorText)
{
    return refreshOrderFinancialSums(Database::db(),orderNumber,errorText);
}

bool refreshOrderFinancialSums(QSqlDatabase db, int orderNumber, QString* errorText)
{
    return refreshNumbers(db,{orderNumber},errorText);
}

bool refreshOrderFinancialSumsByUid(QSqlDatabase db, const QString& orderUid, QString* errorText)
{
    QSqlQuery query(db);query.prepare("SELECT number FROM work_orders WHERE TRIM(order_uid)=?");
    query.addBindValue(orderUid.trimmed());
    if(!query.exec()) {if(errorText)*errorText=query.lastError().text();return false;}
    if(!query.next()) {if(errorText)*errorText=QStringLiteral("Наряд за UID не знайдено.");return false;}
    const auto number=query.value(0).toInt();
    if(orderUid.trimmed().isEmpty() || query.next()) {if(errorText)*errorText=QStringLiteral("Неоднозначний UID наряду.");return false;}
    query.finish();
    return refreshNumbers(db,{number},errorText);
}

bool refreshAllOrderFinancialSums(QSqlDatabase db, QString* errorText)
{
    QSqlQuery query(db);
    if(!query.exec("SELECT number FROM work_orders ORDER BY number")) {if(errorText)*errorText=query.lastError().text();return false;}
    QList<int> numbers;while(query.next())numbers.append(query.value(0).toInt());query.finish();
    return refreshNumbers(db,numbers,errorText);
}

TotalsResult readTotals(QSqlDatabase db, int internalOrderNumber)
{
    TotalsResult result;
    if (!db.isValid() || !db.isOpen()) {
        result.error = QStringLiteral("З'єднання з БД недоступне.");
        return result;
    }
    // One statement provides a consistent SQLite read snapshot for both totals.
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
        "SELECT rows.kind, rows.id, rows.qty, rows.price, rows.amount "
        "FROM work_orders o LEFT JOIN ("
        "SELECT 0 AS kind,id,order_uid,order_number,qty,price,amount FROM work_order_services "
        "UNION ALL SELECT 1 AS kind,id,order_uid,order_number,qty,price,amount FROM work_order_parts"
        ") rows ON ((TRIM(COALESCE(rows.order_uid,''))<>'' AND TRIM(rows.order_uid)=TRIM(o.order_uid)) "
        "OR (TRIM(COALESCE(rows.order_uid,''))='' AND rows.order_number=o.number)) "
        "WHERE o.number=?"))) { result.error=query.lastError().text(); return result; }
    query.addBindValue(internalOrderNumber);
    if (!query.exec()) { result.error=query.lastError().text(); return result; }
    if (!query.next()) {
        result.status=TotalsStatus::NotFound;
        result.error=QStringLiteral("Наряд із внутрішнім номером %1 не знайдено.").arg(internalOrderNumber);
        return result;
    }
    do {
        if (query.value(0).isNull()) continue; // Existing empty order: a genuine zero.
        // Preserve a recorded row amount. Reconstruct only when it is absent.
        const auto amount=query.value(4).isNull()
            ? ExactMoney::lineAmount(query.value(2).toString().toStdString(),query.value(3).toString().toStdString())
            : ExactMoney::payment(query.value(4).toString().toStdString());
        if (std::holds_alternative<ExactMoney::Error>(amount)) {
            result.status=TotalsStatus::InvalidData;
            result.error=QStringLiteral("Перевірте суму або кількість рядка %1 (%2). Історичні дані не змінено.")
                .arg(query.value(1).toString(),query.value(0).toInt()==0?QStringLiteral("роботи"):QStringLiteral("запчастини"));
            return result;
        }
        auto& total=query.value(0).toInt()==0?result.worksCents:result.partsCents;
        const auto added=ExactMoney::sum({total,std::get<ExactMoney::Cents>(amount)});
        if(std::holds_alternative<ExactMoney::Error>(added)) {
            result.status=TotalsStatus::InvalidData;result.error=QStringLiteral("Переповнення фінансового підсумку.");return result;
        }
        total=std::get<ExactMoney::Cents>(added);
    } while(query.next());
    if(query.lastError().isValid()) {result.error=query.lastError().text();return result;}
    result.status=TotalsStatus::Ok;
    return result;
}

double servicesTotal(QSqlDatabase db, int orderNumber)
{
    const auto result=readTotals(db,orderNumber);
    return result.ok()?result.worksCents/100.0:std::numeric_limits<double>::quiet_NaN();
}

double partsTotal(QSqlDatabase db, int orderNumber)
{
    const auto result=readTotals(db,orderNumber);
    return result.ok()?result.partsCents/100.0:std::numeric_limits<double>::quiet_NaN();
}

int visibleOrderNumber(QSqlDatabase db, int internalOrderNumber, int fallbackValue)
{
    if (internalOrderNumber <= 0) return fallbackValue;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(order_number, number) FROM work_orders WHERE number=?"));
    q.addBindValue(internalOrderNumber);
    return (q.exec() && q.next()) ? q.value(0).toInt() : fallbackValue;
}

} // namespace OrderFinancialService
