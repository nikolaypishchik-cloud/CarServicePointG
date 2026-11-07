#include "core/services/ReportsPdfPartsSupport.h"

#include "core/services/orders/PartImportTextSupport.h"
#include "core/util/FormatUtils.h"

#include <QRegularExpression>

namespace {

QString formatQty(double value)
{
    return FormatUtils::smartQty(value);
}

QString displayOilGrade(QString grade)
{
    grade = grade.trimmed().toUpper();
    grade.remove(QChar::Space);
    grade.replace(QStringLiteral("-"), QString());
    return grade;
}

} // namespace

namespace ReportsPdfPartsSupport {

bool hasInvoiceColumn(const QMap<QString, QList<ReportsPdfSupport::PartPdfRow>>& groups)
{
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        for (const auto& row : it.value()) {
            if (!row.invoiceNumber.trimmed().isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

QString displayPartName(QString name)
{
    name = PartImportTextSupport::normalizePartName(name.trimmed());
    if (name.contains(QStringLiteral("0W-")) ||
        name.contains(QStringLiteral("5W-")) ||
        name.contains(QStringLiteral("10W-"))) {
        name.replace(QStringLiteral("0W-"), QStringLiteral("0W"));
        name.replace(QStringLiteral("5W-"), QStringLiteral("5W"));
        name.replace(QStringLiteral("10W-"), QStringLiteral("10W"));
    }
    return name;
}

QString oilSectionTitleForRow(const ReportsPdfSupport::PartPdfRow& row)
{
    QString grade = displayOilGrade(row.oilGrade);
    if (grade.isEmpty()) {
        const QString normalizedName = displayPartName(row.name).toUpper();
        const QRegularExpression re(QStringLiteral(R"((\d{1,2}W-?\d{2}))"), QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(normalizedName);
        if (match.hasMatch()) {
            grade = displayOilGrade(match.captured(1));
        }
    }
    return grade.isEmpty()
        ? QString::fromUtf8("Мастило без в'язкості")
        : QString::fromUtf8("Мастило %1").arg(grade);
}

QMap<QString, QList<ReportsPdfSupport::PartPdfRow>> groupMotorOilRowsBySection(const QList<ReportsPdfSupport::PartPdfRow>& rows)
{
    QMap<QString, QList<ReportsPdfSupport::PartPdfRow>> oilGroups;
    for (const auto& row : rows) {
        oilGroups[oilSectionTitleForRow(row)].append(row);
    }
    return oilGroups;
}

QStringList buildPartCells(const ReportsPdfSupport::PartPdfRow& row, int seq, bool oilMode, bool includeInvoiceColumn)
{
    const double displayedPurchasePrice = oilMode
        ? (row.purchaseUnitPrice > 0.0 ? row.purchaseUnitPrice : (row.qtyValue > 0.0 ? row.purchase / row.qtyValue : row.purchase))
        : row.purchase;
    QStringList cells = {
        QString::number(seq),
        row.date,
        row.orderNumber,
        displayPartName(row.name),
        FormatUtils::money(displayedPurchasePrice, 0)
    };
    if (!oilMode) {
        cells << FormatUtils::money(row.added, 0);
    }
    cells << formatQty(row.qtyValue)
          << FormatUtils::money(row.clientPrice, 0);
    if (!oilMode && includeInvoiceColumn) {
        cells << row.invoiceNumber;
    }
    return cells;
}

QList<PartsSummaryLine> buildSummaryLines(double qtyTotal, bool oilUnits,
                                          double purchaseTotal, double addedTotal,
                                          double clientTotal, bool oilMode)
{
    QList<PartsSummaryLine> lines = {
        {
            QString::fromUtf8("Кількість"),
            QString::fromUtf8("%1 %2").arg(formatQty(qtyTotal), oilUnits ? QString::fromUtf8("л.") : QString::fromUtf8("од.")),
            false
        }
    };
    if (!oilMode) {
        lines.append({
            QString::fromUtf8("Вартість"),
            FormatUtils::money(purchaseTotal, 0) + QString::fromUtf8(" грн"),
            true
        });
        lines.append({
            QString::fromUtf8("Доданий %"),
            FormatUtils::money(addedTotal, 0) + QString::fromUtf8(" грн"),
            false
        });
    }
    lines.append({
        QString::fromUtf8("Разом"),
        FormatUtils::money(clientTotal, 0) + QString::fromUtf8(" грн"),
        true
    });
    return lines;
}

} // namespace ReportsPdfPartsSupport
