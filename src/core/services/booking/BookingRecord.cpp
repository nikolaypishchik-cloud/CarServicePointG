#include "core/services/booking/BookingRecord.h"

#include <QStringList>
#include <QRegularExpression>

namespace {
QString cleanPlanningValue(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral(R"([\r\n\t]+)")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return value.trimmed();
}

QString withUnitSuffix(QString value, const QString& suffix)
{
    value = cleanPlanningValue(value);
    if (value.isEmpty()) return value;
    if (value.endsWith(suffix, Qt::CaseInsensitive)) return value;
    return QStringLiteral("%1 %2").arg(value, suffix);
}
}

QString bookingStatusNormalized(const QString& status)
{
    const QString s = status.trimmed().toLower();
    if (s == QStringLiteral("confirmed")) return s;
    if (s == QStringLiteral("cancelled")) return s;
    if (s == QStringLiteral("completed")) return s;
    if (s == QStringLiteral("no_show")) return s;
    return QStringLiteral("pending");
}

bool bookingStatusBlocksPlanning(const QString& status)
{
    const QString s = bookingStatusNormalized(status);
    return s == QStringLiteral("pending") || s == QStringLiteral("confirmed");
}

QString bookingDisplayTitle(const BookingRecord& booking)
{
    QStringList parts;
    if (booking.date.isValid()) parts << booking.date.toString(QStringLiteral("dd.MM.yyyy"));
    if (booking.timeFrom.isValid()) parts << booking.timeFrom.toString(QStringLiteral("HH:mm"));
    if (!booking.clientName.trimmed().isEmpty()) parts << booking.clientName.trimmed();
    if (!booking.carMakeModel.trimmed().isEmpty()) parts << booking.carMakeModel.trimmed();
    if (!booking.serviceType.trimmed().isEmpty()) parts << booking.serviceType.trimmed();
    return parts.join(QStringLiteral(" | "));
}

QString bookingPlanningText(const BookingRecord& booking)
{
    QStringList parts;
    parts << QString::fromUtf8("[Online]");

    if (booking.timeFrom.isValid()) {
        QString timeText = booking.timeFrom.toString(QStringLiteral("HH:mm"));
        if (booking.timeTo.isValid()) {
            timeText += QStringLiteral("-") + booking.timeTo.toString(QStringLiteral("HH:mm"));
        }
        parts << timeText;
    }

    const QString carText = !cleanPlanningValue(booking.carMakeModel).isEmpty()
        ? cleanPlanningValue(booking.carMakeModel)
        : QStringLiteral("%1 %2").arg(cleanPlanningValue(booking.carMake), cleanPlanningValue(booking.carModel)).simplified();

    if (!carText.isEmpty()) parts << carText;
    if (!cleanPlanningValue(booking.carNumber).isEmpty()) parts << cleanPlanningValue(booking.carNumber);
    if (!cleanPlanningValue(booking.serviceType).isEmpty()) parts << cleanPlanningValue(booking.serviceType);

    QString text = parts.join(QStringLiteral(" ")).simplified();
    if (!text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1Char('!')) && !text.endsWith(QLatin1Char('?'))) {
        text += QLatin1Char('.');
    }
    return text;
}
