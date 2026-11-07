#include "core/services/ReportsDateUtils.h"
#include "core/util/DateFormats.h"

#include <QStringList>

namespace ReportsDateUtils {

QDate parseFlexibleDate(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) return {};
    const QDate canonical = DateFormats::parseDate(value);
    if (canonical.isValid()) return canonical;
    const QString raw10 = value.left(10);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd.MM.yy"), QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("yy-MM-dd"), QStringLiteral("yyyy/MM/dd"), QStringLiteral("dd/MM/yyyy")
    };
    for (const auto& f : formats) {
        QDate d = QDate::fromString(raw10, f);
        if (!f.contains(QStringLiteral("yyyy")) && d.isValid() && d.year() >= 1900 && d.year() < 2000) {
            d = d.addYears(100);
        }
        if (d.isValid() && d.year() >= 2000 && d.year() <= 2100) return d;
    }
    const auto iso = DateFormats::parseDate(raw10);
    if (iso.isValid() && iso.year() >= 2000 && iso.year() <= 2100) return iso;
    return {};
}

QDate effectiveDate(const QString& raw, const QString& fallback)
{
    const QDate d = parseFlexibleDate(raw);
    if (d.isValid()) return d;
    return parseFlexibleDate(fallback);
}

QString displayDate(const QString& raw, const QString& fallback)
{
    const QDate d = parseFlexibleDate(raw);
    if (d.isValid()) return d.toString(QStringLiteral("dd.MM.yy"));
    const QDate fb = parseFlexibleDate(fallback);
    if (fb.isValid()) return fb.toString(QStringLiteral("dd.MM.yy"));
    const QString src = raw.trimmed().isEmpty() ? fallback.trimmed() : raw.trimmed();
    return src.left(10);
}

QString dmy(const QString& raw, const QString& fallback)
{
    return displayDate(raw, fallback);
}

} // namespace ReportsDateUtils
