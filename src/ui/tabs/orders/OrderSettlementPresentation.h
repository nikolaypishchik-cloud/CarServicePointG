#pragma once
#include "core/services/orders/OrderDialogReadQueryService.h"
#include <QLocale>

namespace OrderSettlementPresentation {
inline QString amount(qint64 cents)
{
    const QLocale ua(QLocale::Ukrainian, QLocale::Ukraine);
    return ua.toString(cents / 100) + QStringLiteral(",%1 грн").arg(cents % 100, 2, 10, QLatin1Char('0'));
}

inline QString summary(int number, const QString& uid)
{
    const auto snapshot = OrderDialogReadQueryService::settlement(number, uid);
    if (!snapshot.ok()) return snapshot.error;
    const auto& row = snapshot.orders.first();
    // No historical cash receipts can be inferred from supplier payments or order status.
    const bool documented = snapshot.documentedOrderReceipts;
    const QString unknown = QStringLiteral("немає даних");
    return QStringLiteral("Разом: %1     Отримано від клієнта: %2     %3: %4")
        .arg(amount(row.totalCents), documented ? amount(row.receivedCents) : unknown,
             row.balanceCents() < 0 ? QStringLiteral("Переплата") : QStringLiteral("Залишок до сплати"),
             documented ? amount(qAbs(row.balanceCents())) : unknown);
}
}
