#pragma once

#include <QDate>
#include <QString>

namespace ReportsDateUtils {

QDate parseFlexibleDate(QString value);
QDate effectiveDate(const QString& raw, const QString& fallback = {});
QString displayDate(const QString& raw, const QString& fallback = {});
QString dmy(const QString& raw, const QString& fallback = {});

} // namespace ReportsDateUtils
