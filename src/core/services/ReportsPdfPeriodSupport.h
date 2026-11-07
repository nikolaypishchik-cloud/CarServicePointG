#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/services/ReportsPdfSupport.h"

namespace ReportsPdfPeriodSupport {

struct PeriodVisibleTotals {
    qint64 partsTotal = 0;
    qint64 worksTotal = 0;
    qint64 partsMargin = 0;
    qint64 grandTotal = 0;
    qint64 outsourceMargin = 0;
    qint64 salaryTotal = 0;
    qint64 expensesTotal = 0;
    qint64 totalMargin = 0;
};

struct PeriodSummaryLine {
    QString label;
    qint64 value = 0;
    bool bold = false;
};

PeriodVisibleTotals collectVisibleTotals(const QList<ReportsPdfSupport::PeriodPdfRow>& rows,
                                         double outsourceMargin,
                                         double salaryTotal,
                                         double expensesTotal);

QStringList periodHeaders();
QVector<QStringList> buildPeriodRowCells(const QList<ReportsPdfSupport::PeriodPdfRow>& rows);
QList<PeriodSummaryLine> buildPeriodSummaryLines(const PeriodVisibleTotals& totals);

} // namespace ReportsPdfPeriodSupport
