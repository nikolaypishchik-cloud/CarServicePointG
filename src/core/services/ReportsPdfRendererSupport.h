#pragma once

#include <QFont>
#include <QDate>
#include <QString>

namespace ReportsPdfRendererSupport {

QString makeAutoPdfPath(const QString& baseName);
QString makeReportPdfPath(const QString& reportFolder, const QString& fileStem);
QString reportDateRangeStem(const QDate& fromDate, const QDate& toDate);
QString dmy(const QString& iso);
QString money0(double value);
QFont titleFont(int pt);
QFont titleFont(int pt, bool bold);
QFont bodyFont(int pt, bool bold = false);

} // namespace ReportsPdfRendererSupport
