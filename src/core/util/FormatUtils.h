#pragma once

#include <QString>

namespace FormatUtils {
    // PDF presentation only: half away from zero, accounting retains kopecks.
    QString pdfMoney(double value);
    QString moneyCents(qint64 cents);
    QString formatNumberSpaces(double value, int maxDecimals = 2);
    QString money(double value, int maxDecimals = 2);
    QString smartQty(double qty, int maxDecimals = 2);
    double roundMoneyValue(double value);
    double inferDiscountPercent(double discountRaw, double worksSum);
    double discountAmount(double value, const QString& mode, double worksSum);
    double discountPercent(double value, const QString& mode, double worksSum);
}
