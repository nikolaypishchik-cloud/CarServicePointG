#include "core/services/ReportsPdfRendererSupport.h"

#include "core/services/ReportsDateUtils.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/util/FormatUtils.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace ReportsPdfRendererSupport {

QString makeAutoPdfPath(const QString& baseName)
{
    QString safeBase = baseName.trimmed();
    if (safeBase.isEmpty()) safeBase = QStringLiteral("report");
    if (safeBase.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        safeBase.chop(4);
    }
    safeBase.replace(QRegularExpression(QStringLiteral(R"([\/:*?"<>|]+)")), QStringLiteral("_"));

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return makeReportPdfPath(QString::fromUtf8("Інші звіти"), safeBase + QStringLiteral("_") + stamp);
}

QString makeReportPdfPath(const QString& reportFolder, const QString& fileStem)
{
    QString pathError;
    const QString path = StorageDirectoriesGuard::managedReportFilePath(reportFolder, fileStem, &pathError);
    if (!path.trimmed().isEmpty()) return path;

    qWarning().noquote() << QStringLiteral("REPORT_PDF_PATH_FAILED report=%1 file=%2 error=%3")
                            .arg(reportFolder, fileStem, pathError);
    return QString();
}

QString reportDateRangeStem(const QDate& fromDate, const QDate& toDate)
{
    return QStringLiteral("%1-%2").arg(fromDate.toString(QStringLiteral("dd.MM.yy")),
                                      toDate.toString(QStringLiteral("dd.MM.yy")));
}

QString dmy(const QString& iso)
{
    return ReportsDateUtils::displayDate(iso);
}

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

QFont titleFont(int pt)
{
    QFont f(QStringLiteral("Times New Roman"));
    f.setPointSize(pt);
    f.setBold(false);
    return f;
}

QFont titleFont(int pt, bool bold)
{
    QFont f(QStringLiteral("Times New Roman"));
    f.setPointSize(pt);
    f.setBold(bold);
    return f;
}

QFont bodyFont(int pt, bool bold)
{
    QFont f(QStringLiteral("Times New Roman"));
    f.setPointSize(pt);
    f.setBold(bold);
    return f;
}

} // namespace ReportsPdfRendererSupport
