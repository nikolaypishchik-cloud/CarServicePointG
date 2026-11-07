#include "core/util/FormatUtils.h"

#include <QtMath>
#include <QStringList>
#include <cmath>
#include <limits>

static QString groupThousands(const QString& digits)
{
    QString out;
    out.reserve(digits.size() + digits.size() / 3);

    int n = digits.size();
    for (int i = 0; i < n; ++i) {
        out.append(digits[i]);
        int left = n - i - 1;
        if (left > 0 && (left % 3) == 0) {
            out.append(' ');
        }
    }
    return out;
}

QString FormatUtils::moneyCents(qint64 cents)
{
    QString digits = QString::number(cents);
    const bool negative = digits.startsWith('-');
    if (negative) digits.remove(0, 1);
    digits = digits.rightJustified(3, '0');
    const QString result = groupThousands(digits.left(digits.size() - 2))
        + ',' + digits.right(2);
    return negative ? '-' + result : result;
}

QString FormatUtils::formatNumberSpaces(double value, int maxDecimals)
{
    if (!qIsFinite(value)) return QStringLiteral("0");

    const bool neg = value < 0;
    double abs = qAbs(value);

    // Render with fixed maxDecimals, then trim trailing zeros.
    QString s = QString::number(abs, 'f', qMax(0, maxDecimals));
    int dot = s.indexOf('.');
    QString intPart = (dot >= 0) ? s.left(dot) : s;
    QString fracPart = (dot >= 0) ? s.mid(dot + 1) : QString();

    // Trim trailing zeros in frac
    while (!fracPart.isEmpty() && fracPart.endsWith('0')) fracPart.chop(1);

    QString grouped = groupThousands(intPart);
    QString out = grouped;
    if (!fracPart.isEmpty()) {
        out += '.' + fracPart;
    }
    if (neg) out.prepend('-');
    return out;
}

QString FormatUtils::money(double value, int maxDecimals)
{
    // In the python app most sums are displayed as integers (грн), but keep optional decimals.
    return formatNumberSpaces(value, maxDecimals);
}

QString FormatUtils::smartQty(double qty, int maxDecimals)
{
    // Display integer quantities without decimals; keep fractional when needed.
    const double q = qty;
    if (!qIsFinite(q)) return QStringLiteral("0");
    const double r = qRound64(q);
    if (qFabs(q - r) < 1e-9) {
        return QString::number((qint64)r);
    }
    return formatNumberSpaces(q, maxDecimals);
}


double FormatUtils::roundMoneyValue(double value)
{
    if (!qIsFinite(value)) return 0.0;
    // Monetary boundary: integer kopecks, half away from zero. Compensate only
    // for the binary representation error at a decimal half-kopeck (1.005).
    // Inventory unit costs must not pass this boundary before multiplication.
    const long double scaled = std::fabs(static_cast<long double>(value)) * 100.0L;
    const long double tolerance = std::numeric_limits<double>::epsilon() * scaled;
    const long double cents = std::floor(scaled + 0.5L + tolerance);
    return std::copysign(static_cast<double>(cents / 100.0L), value);
}


static double _clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double FormatUtils::discountAmount(double value, const QString& mode, double worksSum)
{
    if (mode == QStringLiteral("percent"))
        return roundMoneyValue(qMax(0.0, worksSum) * _clamp(value, 0.0, 100.0) / 100.0);
    // Legacy order totals stored an amount. Do not reinterpret historical rows.
    return roundMoneyValue(qMax(0.0, value));
}

double FormatUtils::discountPercent(double value, const QString& mode, double worksSum)
{
    if (mode == QStringLiteral("percent")) return _clamp(value, 0.0, 100.0);
    if (mode == QStringLiteral("uah"))
        return worksSum > 0 ? _clamp(value / worksSum * 100.0, 0.0, 100.0) : 0.0;
    return inferDiscountPercent(value, worksSum);
}

double FormatUtils::inferDiscountPercent(double discountRaw, double worksSum)
{
    const double d = double(discountRaw);
    const double w = double(worksSum);
    if (d <= 0.0) return 0.0;

    // Likely percent in normal business range
    if (d <= 30.0) return _clamp(d, 0.0, 100.0);

    // Ambiguous 31..100: could be percent OR old UAH.
    if (d <= 100.0) {
        // If works are big enough, a 31..100 UAH discount is common, while 31..100% is rare.
        if (w >= 500.0 && d < (w * 0.30)) {
            return _clamp((w > 0.0) ? (d / w) * 100.0 : 0.0, 0.0, 100.0);
        }
        return _clamp(d, 0.0, 100.0);
    }

    // >100 is almost certainly old UAH
    return _clamp((w > 0.0) ? (d / w) * 100.0 : 0.0, 0.0, 100.0);
}


QString FormatUtils::pdfMoney(double value)
{
    return formatNumberSpaces(std::round(value), 0);
}
