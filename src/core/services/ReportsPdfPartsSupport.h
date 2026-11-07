#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "core/services/ReportsPdfSupport.h"

namespace ReportsPdfPartsSupport {

struct PartsSummaryLine {
    QString label;
    QString value;
    bool emphasize = false;
};

bool hasInvoiceColumn(const QMap<QString, QList<ReportsPdfSupport::PartPdfRow>>& groups);
QString displayPartName(QString name);
QString oilSectionTitleForRow(const ReportsPdfSupport::PartPdfRow& row);
QMap<QString, QList<ReportsPdfSupport::PartPdfRow>> groupMotorOilRowsBySection(const QList<ReportsPdfSupport::PartPdfRow>& rows);
QStringList buildPartCells(const ReportsPdfSupport::PartPdfRow& row, int seq, bool oilMode, bool includeInvoiceColumn);
QList<PartsSummaryLine> buildSummaryLines(double qtyTotal, bool oilUnits,
                                          double purchaseTotal, double addedTotal,
                                          double clientTotal, bool oilMode);

} // namespace ReportsPdfPartsSupport
