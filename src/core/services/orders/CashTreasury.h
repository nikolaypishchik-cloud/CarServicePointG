#pragma once
#include "core/services/PurchasePaymentService.h"

namespace CashTreasury {
struct Command {
    QString uid, kind, account, reason;
    qint64 cents;
    QDate day;
    PurchaseDebt debt;
};
struct Result { QString error; bool replay = false; };
QVector<PurchaseDebt> openDebts(QString& error);
Result post(const Command& command);
}
