#pragma once

#include <QDate>
#include <QString>
#include <QtGlobal>


namespace ReportsPdfRenderer {

struct ExportResult {
    bool success = false;
    QString path;
    QString errorText;
    QString reportKind;
    qint64 fileSizeBytes = -1;
    int recordCount = -1;
};

struct ExpensesFormData {
    double elecPrev = 0.0;
    double elecCurr = 0.0;
    double elecTariff = 0.0;
    double elecKwh = 0.0;
    double elecCost = 0.0;
    double cleaning = 0.0;
    QString cleaningNote;
    double software = 0.0;
    QString softwareNote;
    double heating = 0.0;
    QString heatingNote;
    double amortization = 0.0;
    QString amortizationNote;
    double rent = 0.0;
    QString rentNote;
    double other = 0.0;
    QString otherNote;
    double total = 0.0;
};


bool renderElectricityPdf(ExportResult* result);
bool renderPeriodPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result);
bool renderPartsPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result);
bool renderOutsourcePdf(const QDate& fromDate, const QDate& toDate, ExportResult* result);
bool renderExpensesPdf(const QDate& fromDate, const QDate& toDate, const ExpensesFormData& data, ExportResult* result);
bool renderPayrollPdf(const QDate& fromDate, const QDate& toDate, ExportResult* result);

} // namespace ReportsPdfRenderer
