#include "core/services/ReportsPdfRenderer.h"

#include "core/services/ReportsPdfSupport.h"
#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsPdfPartsSupport.h"
#include "core/services/ReportsPdfPartsPageSupport.h"
#include "core/services/ReportsPdfPeriodSupport.h"
#include "core/services/ReportsPdfPeriodPageSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPayrollSupport.h"
#include "core/services/ReportsPdfExpensesPageSupport.h"
#include "core/services/ReportsPdfElectricityPageSupport.h"
#include "core/services/ReportsPdfPayrollPageSupport.h"
#include "core/services/ReportsPdfOutsourcePageSupport.h"
#include "core/services/ReportsSyncSupport.h"
#include "core/services/ReportsPdfRendererSupport.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSqlQuery>
#include <QtMath>

namespace {

using ReportsPdfLayoutSupport::PdfTheme;
using ReportsPdfLayoutSupport::buildTheme;
using ReportsPdfLayoutSupport::ensurePdfOutputPath;
using ReportsPdfLayoutSupport::finalizePdf;
using ReportsPdfLayoutSupport::mmToPx;
using ReportsPdfLayoutSupport::newPdfPage;
using ReportsPdfPaintingSupport::drawCell;
using ReportsPdfPaintingSupport::drawCenteredText;
using ReportsPdfPaintingSupport::drawIssueTextWithUnderline;
using ReportsPdfPaintingSupport::drawLeftText;
using ReportsPdfPaintingSupport::drawRightText;
using ReportsPdfPaintingSupport::drawSingleLineFittedCell;
using ReportsPdfPaintingSupport::kPdfTablePenWidth;
using ReportsPdfPaintingSupport::wrapCellText;
using ReportsPdfPaintingSupport::wrappedHeight;
using namespace ReportsPdfRendererSupport;


} // namespace

namespace ReportsPdfRenderer {

bool renderElectricityPdf(ExportResult* result)
{
    const QString path = makeReportPdfPath(QString::fromUtf8("Електроенергія"), QString::fromUtf8("Електроенергія"));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("electricity");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("electricity") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("electricity") << path;
    QString readError;const auto rows=ReportsSyncSupport::loadElectricityPeriods(&readError);
    if(!readError.isEmpty()){if(result){result->success=false;result->path=path;result->reportKind="electricity";result->recordCount=0;result->errorText=readError;}return false;}
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    const PdfTheme th = buildTheme(pdf);

    const QFont reportTitleF = titleFont(28);
    const QFont reportSubtitleF = titleFont(16);
    const QFont headerFont = bodyFont(9, true);
    const QFont cellFont = bodyFont(9, false);


    if (result) {
        result->reportKind = QStringLiteral("electricity");
        result->recordCount = rows.size();
    }

    ReportsPdfElectricityPageSupport::renderElectricityPage(
        pdf,
        p,
        th,
        rows,
        reportTitleF,
        reportSubtitleF,
        headerFont,
        cellFont);

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("electricity") << path << "records" << rows.size() << "bytes" << QFileInfo(path).size() << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("electricity") << path;
    }
    return ok;
}

bool renderPeriodPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result)
{
    const QString df = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString dt = toDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString path = makeReportPdfPath(QString::fromUtf8("Звіт за період"), reportDateRangeStem(fromDate, toDate));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("period");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("period") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    double partsTotal = 0.0, worksTotal = 0.0, partsMargin = 0.0, outsourceMargin = 0.0, salaryTotal = 0.0, expensesTotal = 0.0;
    QString readError;
    const auto rows = ReportsPdfSupport::collectPeriodPdfRows(df, dt, partsTotal, worksTotal, partsMargin, outsourceMargin, salaryTotal, expensesTotal,&readError);
    if(!readError.isEmpty()){if(result){result->success=false;result->path=path;result->errorText=readError;result->reportKind="period";result->recordCount=0;}return false;}
    if (result) {
        result->reportKind = QStringLiteral("period");
        result->recordCount = rows.size();
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("period") << path << "records" << rows.size();

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const PdfTheme th = buildTheme(pdf);

    const QString title = QStringLiteral("CarService PointG");
    const QString subtitle = QString::fromUtf8("ЗВІТ ЗА ПЕРІОД: %1 — %2")
        .arg(fromDate.toString(QStringLiteral("dd.MM.yy")), toDate.toString(QStringLiteral("dd.MM.yy")));

    const auto visibleTotals = ReportsPdfPeriodSupport::collectVisibleTotals(rows, outsourceMargin, salaryTotal, expensesTotal);
    const auto rawRowCells = ReportsPdfPeriodSupport::buildPeriodRowCells(rows);
    QList<QStringList> formattedRowCells;
    formattedRowCells.reserve(rawRowCells.size());
    for (const auto& rawCells : rawRowCells) {
        QStringList cells = rawCells;
        for (int i : {7, 8, 9, 10}) cells[i] = money0(cells[i].toLongLong());
        formattedRowCells.push_back(cells);
    }

    ReportsPdfPeriodPageSupport::renderPeriodPage(
        pdf,
        p,
        th,
        title,
        subtitle,
        formattedRowCells,
        visibleTotals,
        titleFont(27, false),
        titleFont(13, true),
        bodyFont(7, true),
        bodyFont(8, true),
        bodyFont(8, false),
        bodyFont(8, false),
        bodyFont(7, false),
        bodyFont(11, false),
        bodyFont(11, true),
        bodyFont(12, true));

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("period") << path
                << "records" << rows.size()
                << "parts_sum_column" << visibleTotals.partsTotal
                << "works_sum_column" << visibleTotals.worksTotal
                << "margin_sum_column" << visibleTotals.partsMargin
                << "grand_sum_column" << visibleTotals.grandTotal
                << "bytes" << QFileInfo(path).size()
                << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("period") << path;
    }
    return ok;
}


bool renderPartsPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result)
{
    const QString df = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString dt = toDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString path = makeReportPdfPath(QString::fromUtf8("Звіт по запчастинам"), reportDateRangeStem(fromDate, toDate));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("parts");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("parts") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    const auto groups = ReportsPdfSupport::collectPartsPdfGroups(df, dt);
    int totalRows = 0;
    for (auto it = groups.begin(); it != groups.end(); ++it) totalRows += it.value().size();
    if (result) {
        result->reportKind = QStringLiteral("parts");
        result->recordCount = totalRows;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("parts") << path << "records" << totalRows;

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const PdfTheme th = buildTheme(pdf);

    const QFont storeTitleF = titleFont(16, true);
    const QFont reportTitleF = titleFont(8, true);
    const QFont periodF = bodyFont(10, false);
    const QFont sectionTitleF = bodyFont(8, true);
    const QFont headerF = bodyFont(7, true);
    const QFont cellF = bodyFont(8, false);
    const QFont summaryLabelF = bodyFont(10, false);
    const QFont summaryValueF = bodyFont(10, false);
    const QFont summaryBoldF = bodyFont(10, true);
    const QFont oilReportTitleF = bodyFont(10, true);

    const bool hasInvoiceColumn = ReportsPdfPartsSupport::hasInvoiceColumn(groups);

    ReportsPdfPartsPageSupport::renderPartsPages(
        pdf,
        p,
        th,
        fromDate,
        toDate,
        groups,
        hasInvoiceColumn,
        storeTitleF,
        reportTitleF,
        periodF,
        sectionTitleF,
        headerF,
        cellF,
        summaryLabelF,
        summaryValueF,
        summaryBoldF,
        oilReportTitleF);

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("parts") << path << "records" << (result ? result->recordCount : -1) << "bytes" << QFileInfo(path).size() << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("parts") << path;
    }
    return ok;
}

bool renderOutsourcePdf(const QDate& fromDate, const QDate& toDate, ExportResult* result)
{
    const QString df = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString dt = toDate.toString(QStringLiteral("yyyy-MM-dd"));
    QString readError;int count=0;double repairTotal=0,clientTotal=0;
    const auto groups=ReportsPdfSupport::collectOutsourceGroups(df,dt,count,repairTotal,clientTotal,&readError);
    if(!readError.isEmpty()){if(result){*result={};result->reportKind="outsource";result->recordCount=0;result->errorText=readError;}return false;}
    const QString path = makeReportPdfPath(QString::fromUtf8("Аутсорс"), reportDateRangeStem(fromDate, toDate));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("outsource");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("outsource") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    if (result) {
        result->reportKind = QStringLiteral("outsource");
        result->recordCount = count;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("outsource") << path << "records" << count;
    const double marginTotal = clientTotal - repairTotal;

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    const PdfTheme th = buildTheme(pdf);

    ReportsPdfOutsourcePageSupport::renderOutsourcePages(
        pdf,
        p,
        th,
        fromDate,
        toDate,
        groups,
        count,
        repairTotal,
        marginTotal,
        titleFont(24),
        titleFont(12),
        bodyFont(12, false),
        bodyFont(8, false),
        bodyFont(11, false),
        bodyFont(12, false),
        bodyFont(12, false));

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("outsource") << path << "records" << count << "bytes" << QFileInfo(path).size() << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("outsource") << path;
    }
    return ok;
}

bool renderExpensesPdf(const QDate& fromDate, const QDate& toDate, const ExpensesFormData& data, ExportResult* result)
{
    const QString df = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString dt = toDate.toString(QStringLiteral("yyyy-MM-dd"));
    QString readError;
    if(!ReportsPdfSupport::validateExpensesPdfSource(fromDate,toDate,&readError)){if(result){*result={};result->reportKind="expenses";result->recordCount=0;result->errorText=readError;}return false;}
    const QString path = makeReportPdfPath(QString::fromUtf8("Витрати"), reportDateRangeStem(fromDate, toDate));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("expenses");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("expenses") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    if (result) {
        result->reportKind = QStringLiteral("expenses");
        result->recordCount = 6;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("expenses") << path << "records" << 6;
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    const PdfTheme th = buildTheme(pdf);

    const QFont reportTitleF = titleFont(28);
    const QFont reportSubtitleF = titleFont(16);
    const QFont rowFont = bodyFont(11, false);
    const QFont rowBoldFont = bodyFont(11, true);

    ReportsPdfExpensesPageSupport::renderExpensesPage(
        pdf,
        p,
        th,
        df,
        dt,
        data,
        reportTitleF,
        reportSubtitleF,
        rowFont,
        rowBoldFont);

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("expenses") << path << "records" << 6 << "bytes" << QFileInfo(path).size() << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("expenses") << path;
    }
    return ok;
}

bool renderPayrollPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result)
{
    const QString df = fromDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString dt = toDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString path = makeReportPdfPath(QString::fromUtf8("Зарплата"), reportDateRangeStem(fromDate, toDate));
    QString pathError;
    if (!ensurePdfOutputPath(path, &pathError)) {
        if (result) {
            result->success = false;
            result->path = path;
            result->errorText = pathError;
            result->reportKind = QStringLiteral("payroll");
        }
        qWarning() << "Report PDF export failed before start:" << QStringLiteral("payroll") << pathError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();

    QString readError;const auto masters = ReportsPdfSupport::collectPayrollDetails(df, dt,&readError);
    if(!readError.isEmpty()){if(result){result->success=false;result->path=path;result->reportKind="payroll";result->recordCount=0;result->errorText=readError;}return false;}
    const auto expenseBreakdown = ReportsPdfExpenseSupport::periodExpenseBreakdown(df, dt);
    if (result) {
        result->reportKind = QStringLiteral("payroll");
        result->recordCount = masters.size();
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export started:" << QStringLiteral("payroll") << path << "records" << masters.size();
    const auto expenseNotes = ReportsPdfExpenseSupport::periodExpenseNotes(df, dt);
    int activeMasterCount = 0;
    for (const auto& master : Database::mastersForPeriod(df, dt)) {
        if (master.role == QStringLiteral("master")) ++activeMasterCount;
    }

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, false);
    const PdfTheme th = buildTheme(pdf);

    const auto adminSources = ReportsPdfExpenseSupport::loadAdminSalarySources();
    const ReportsPdfExpenseSupport::AdminSalaryTotals adminTotals = ReportsPdfExpenseSupport::collectAdminSalaryTotals(df, dt);

    ReportsPdfPayrollPageSupport::PayrollFonts fonts;
    fonts.title = titleFont(20);
    fonts.subtitle = titleFont(10);
    fonts.person = bodyFont(13, true);
    fonts.period = bodyFont(9, false);
    fonts.tableHeader = bodyFont(6, false);
    fonts.tableCell = bodyFont(7, false);
    fonts.pairLabel = bodyFont(12, false);
    fonts.pairValue = bodyFont(12, false);
    fonts.pairEmphasis = bodyFont(12, true);
    fonts.adminCell = bodyFont(9);
    fonts.summary = bodyFont(8, true);
    fonts.expenseTitle = bodyFont(11, true);
    fonts.expenseBody = bodyFont(8);

    ReportsPdfPayrollPageSupport::renderPayrollPages(
        pdf,
        p,
        th,
        fromDate,
        toDate,
        masters,
        adminSources,
        adminTotals,
        expenseBreakdown,
        expenseNotes,
        activeMasterCount,
        fonts);

    const bool ok = finalizePdf(p, path, result);
    if (ok) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Report PDF export finished:" << QStringLiteral("payroll") << path << "records" << masters.size() << "bytes" << QFileInfo(path).size() << "elapsed_ms" << timer.elapsed();
    } else {
        qWarning() << "Report PDF export failed:" << QStringLiteral("payroll") << path;
    }
    return ok;
}

} // namespace ReportsPdfRenderer
