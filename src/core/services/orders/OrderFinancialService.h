#pragma once

#include <QString>

class QSqlDatabase;

namespace OrderFinancialService {

// A missing order and a failed query are never successful zero totals.
enum class TotalsStatus { Ok, NotFound, DatabaseError, InvalidData };
struct TotalsResult {
    TotalsStatus status = TotalsStatus::DatabaseError;
    qint64 worksCents = 0;
    qint64 partsCents = 0;
    QString error;
    bool ok() const { return status == TotalsStatus::Ok; }
};
TotalsResult readTotals(QSqlDatabase db, int internalOrderNumber);

bool refreshOrderFinancialSums(QSqlDatabase db, int orderNumber, QString* errorText = nullptr);
bool refreshOrderFinancialSums(int orderNumber, QString* errorText = nullptr);
bool refreshOrderFinancialSumsByUid(QSqlDatabase db, const QString& orderUid, QString* errorText = nullptr);
bool refreshAllOrderFinancialSums(QSqlDatabase db, QString* errorText = nullptr);
double servicesTotal(QSqlDatabase db, int orderNumber);
double partsTotal(QSqlDatabase db, int orderNumber);
int visibleOrderNumber(QSqlDatabase db, int internalOrderNumber, int fallbackValue);

} // namespace OrderFinancialService
