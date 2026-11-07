#include "core/services/ReportsPdfPeriodSupport.h"

#include "core/services/orders/PartImportTextSupport.h"

#include <QtGlobal>

namespace ReportsPdfPeriodSupport {

PeriodVisibleTotals collectVisibleTotals(const QList<ReportsPdfSupport::PeriodPdfRow>& rows,
                                         double outsourceMargin,
                                         double salaryTotal,
                                         double expensesTotal)
{
    PeriodVisibleTotals totals;
    for (const auto& row : rows) {
        totals.partsTotal += qRound64(row.partsPrice);
        totals.worksTotal += qRound64(row.worksPrice);
        totals.partsMargin += qRound64(row.added);
        totals.grandTotal += qRound64(row.total);
    }
    totals.outsourceMargin = qRound64(outsourceMargin);
    totals.salaryTotal = qRound64(salaryTotal);
    totals.expensesTotal = qRound64(expensesTotal);
    totals.totalMargin = (totals.worksTotal + totals.partsMargin + totals.outsourceMargin)
        - (totals.salaryTotal + totals.expensesTotal);
    return totals;
}

QStringList periodHeaders()
{
    return {
        QString::fromUtf8("Дата"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Авто"),
        QString::fromUtf8("Держ.\nномер"),
        QString::fromUtf8("Робота"),
        QString::fromUtf8("Запчастина"),
        QString::fromUtf8("Кіл-\nть"),
        QString::fromUtf8("Ціна\nзапчастин"),
        QString::fromUtf8("Доданий\n%"),
        QString::fromUtf8("Ціна\nробіт"),
        QString::fromUtf8("Підсумок")
    };
}

QVector<QStringList> buildPeriodRowCells(const QList<ReportsPdfSupport::PeriodPdfRow>& rows)
{
    QVector<QStringList> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back({
            row.date,
            row.orderNumber,
            row.car,
            row.plate,
            PartImportTextSupport::translateTechnicalRuToUa(row.workTitle),
            PartImportTextSupport::normalizePartName(row.partName),
            row.qty,
            QString::number(qRound64(row.partsPrice)),
            QString::number(qRound64(row.added)),
            QString::number(qRound64(row.worksPrice)),
            QString::number(qRound64(row.total))
        });
    }
    return out;
}

QList<PeriodSummaryLine> buildPeriodSummaryLines(const PeriodVisibleTotals& totals)
{
    return {
        {QString::fromUtf8("Запчастини"), totals.partsTotal, false},
        {QString::fromUtf8("Роботи"), totals.worksTotal, false},
        {QString::fromUtf8("Маржа запчастин"), totals.partsMargin, false},
        {QString::fromUtf8("Маржа аутсорсу"), totals.outsourceMargin, false},
        {QString::fromUtf8("Зарплата"), totals.salaryTotal, false},
        {QString::fromUtf8("Витрати"), totals.expensesTotal, false}
    };
}

} // namespace ReportsPdfPeriodSupport
