#pragma once

#include <QDateTime>
#include <QString>
#include <QJsonObject>

namespace DateFormats {
QDate parseDate(const QString& value);
QDateTime parseDateTime(const QString& value);
QString date(const QString& value);
QString dateTime(const QString& value, bool seconds = false);
QString storage(const QString& value);
QString isoSql(const QString& expression);
QJsonObject protocolDates(QJsonObject value);
}
