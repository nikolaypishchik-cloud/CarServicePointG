#include "core/util/DateFormats.h"
#include <QRegularExpression>
#include <QSet>

namespace DateFormats {
QDate parseDate(const QString& value)
{
    const QString text = value.trimmed();
    static const QRegularExpression iso("^(\\d{4})[-/](\\d{2})[-/](\\d{2})(?:$|[ T])");
    static const QRegularExpression local("^(\\d{2})[./](\\d{2})[./](\\d{4}|\\d{2})(?:$|[ T/])");
    auto match = iso.match(text);
    if (match.hasMatch()) return QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
    match = local.match(text);
    if (!match.hasMatch()) return {};
    int year = match.captured(3).toInt();
    if (match.captured(3).size() == 2) year += 2000;
    return QDate(year, match.captured(2).toInt(), match.captured(1).toInt());
}

QDateTime parseDateTime(const QString& value)
{
    const QString text = value.trimmed();
    auto iso = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!iso.isValid()) iso = QDateTime::fromString(text, Qt::ISODate);
    if (iso.isValid()) return iso;
    const QDate day = parseDate(text);
    if (!day.isValid()) return {};
    if (text.size() == 8 || text.size() == 10) return QDateTime(day, QTime(0, 0));
    static const QRegularExpression local("^\\d{2}[./]\\d{2}[./](?:\\d{4}|\\d{2})(?:[ T/](.*))?$");
    const auto match = local.match(text);
    if (!match.hasMatch()) return {};
    QString clock = match.captured(1);
    if (clock.isEmpty()) return QDateTime(day, QTime(0, 0));
    if (clock.size() >= 5 && clock[2] == '.') clock[2] = ':';
    if (clock.size() >= 8 && clock[5] == '.') clock[5] = ':';
    return QDateTime::fromString(day.toString(Qt::ISODate) + 'T' + clock, Qt::ISODateWithMs);
}

QString date(const QString& value)
{
    const auto parsed = parseDate(value);
    return parsed.isValid() ? parsed.toString("dd.MM.yy") : value.trimmed();
}

QString dateTime(const QString& value, bool seconds)
{
    const auto parsed = parseDateTime(value);
    return parsed.isValid() ? parsed.toString(seconds ? "dd.MM.yy HH:mm:ss" : "dd.MM.yy HH:mm") : value.trimmed();
}

QString storage(const QString& value)
{
    const QString text = value.trimmed();
    const auto parsed = parseDateTime(text);
    if (!parsed.isValid()) return text;
    if (parsed.date().year() < 2000 || parsed.date().year() > 2099) return text;
    if (text.size() == 8 || text.size() == 10) return parsed.date().toString("dd.MM.yy");
    const QString iso = parsed.toString(Qt::ISODateWithMs);
    return parsed.date().toString("dd.MM.yy") + ' ' + iso.mid(11);
}

QString isoSql(const QString& expression)
{
    return QString("CASE WHEN substr(trim(%1),3,1)='.' AND substr(trim(%1),6,1)='.' THEN "
        "CASE WHEN substr(trim(%1),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(%1),7,4) ELSE '20'||substr(trim(%1),7,2) END"
        "||'-'||substr(trim(%1),4,2)||'-'||substr(trim(%1),1,2)||"
        "CASE WHEN substr(trim(%1),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN replace(substr(trim(%1),11),'/',' ') ELSE replace(substr(trim(%1),9),'/',' ') END "
        "ELSE %1 END").arg(expression);
}

QJsonObject protocolDates(QJsonObject value)
{
    // API dates stay ISO; database presentation must not alter sync identifiers or versions.
    static const QSet<QString> fields = {
        "opened_at", "closed_at", "ordered_at", "delivery_date", "installed_at",
        "sent_date", "return_date", "invoice_date", "active_from", "active_to",
        "purchased_on", "spent_on", "due_date", "due_at", "date", "planning_day",
        "desired_start_at", "desired_end_at", "act_date", "accepted_at", "planned_return_at"
    };
    for (const QString& key : fields) {
        const QString text = value.value(key).toString();
        if (text.size() < 8 || text.at(2) != '.' || text.at(5) != '.') continue;
        const auto parsed = parseDateTime(text);
        if (parsed.isValid()) value.insert(key, text.size() <= 10
            ? parsed.date().toString(Qt::ISODate) : parsed.toString(Qt::ISODateWithMs));
    }
    return value;
}
}
