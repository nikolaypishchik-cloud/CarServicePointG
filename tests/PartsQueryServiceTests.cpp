#include "core/services/PartOverduePolicy.h"
#include "core/services/PartsQueryService.h"
#include "TestSourceAssertions.h"

#include <QtTest/QtTest>

class PartsQueryServiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void sortRowsUsesOverdueRankAndVisibleDateTieBreakers();
    void paidInstalledAndMissingDeliveryRowsUsePolicy();
    void manualRowsUseVisibleOrderedDateAsOverdueAnchor();
    void debtBannerUsesPurchaseAmountAndOdometerStartsBlank();
    void allRanksAndDeterministicTies();
    void closedUnpaidPartsPrecedeAllSettledParts();
};

namespace {
PartRowSnapshot row(int id, int orderNumber, const QDate& delivery, const QDate& ordered, bool paid, bool installed, const QString& status = QStringLiteral("open"))
{
    PartRowSnapshot r;
    r.id = id;
    r.orderNumber = orderNumber;
    r.orderDisplay = QString::number(orderNumber);
    r.dateRaw = ordered.isValid() ? ordered.toString(Qt::ISODate) : QString();
    r.deliveryDateRaw = delivery.isValid() ? delivery.toString(Qt::ISODate) : QString();
    r.installedAtRaw = installed ? QStringLiteral("2026-04-19") : QString();
    r.paid = paid;
    r.hasInvoiceAttachment = true;
    r.orderStatus = status;
    PartsQueryService::refreshDerivedFields(&r, QDate(2026, 4, 20));
    return r;
}
}

void PartsQueryServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "PartOverduePolicy::evaluate");
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "PARTS_SORT_REFRESH totalRows=");
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "PART_DELIVERY_CHANGED partId=");
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersQueryService.cpp", "applyCurrentOrdersOverduePolicy");
    cs::tests::requireContains("src/infrastructure/db/sqlite/DbSettingsRepository.cpp", "PART_PAID_CHANGED partId=");
    cs::tests::requireContains("src/infrastructure/db/sqlite/DbSettingsRepository.cpp", "PART_INSTALLED_CHANGED partId=");
    cs::tests::requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "openPurchasePayment");
    cs::tests::requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "applyIncrementalPaidToggle");
    cs::tests::requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "case ColPaid:");
    cs::tests::requireContains("src/core/services/MutationPostCommitService.cpp", "router_cache_work_order_parts");
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "router_work_order_parts");
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersQueryService.cpp", "router_work_order_parts");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartEditServices.h", "manual_delivery_date_from_ordered_at");
    cs::tests::requireContains("src/core/services/orders/PartRowPayloadBuilderService.cpp", "payload_delivery_date_from_ordered_at");
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "PART_MANUAL_EFFECTIVE_DELIVERY");
    cs::tests::requireNotContains("src/core/services/orders/CurrentOrdersQueryService.cpp", "date('now')");
    cs::tests::requireNotContains("src/core/services/orders/CurrentOrdersQueryService.cpp", "julianday");
    cs::tests::requireNotContains("src/core/services/orders/CurrentOrdersQueryService.cpp", "COALESCE(delivery_date, ordered_at)");
}

void PartsQueryServiceTests::sortRowsUsesOverdueRankAndVisibleDateTieBreakers()
{
    const QDate today(2026, 4, 20);
    QVector<PartRowSnapshot> rows;
    rows << row(10, 170, today.addDays(-3), today.addDays(-10), false, false); // rank 2
    rows << row(11, 172, today.addDays(-5), today.addDays(-9), false, false);  // rank 0
    rows << row(12, 171, today.addDays(-5), today.addDays(-8), false, true);   // rank 1
    rows << row(13, 169, today.addDays(-3), today.addDays(-7), false, true);   // rank 3
    rows << row(14, 200, today.addDays(-1), today.addDays(-1), false, false);  // rank 4

    PartsQueryService::sortRowsForTable(&rows, today);
    QCOMPARE(rows.at(0).overdueRank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));
    QCOMPARE(rows.at(1).overdueRank, int(PartOverduePolicy::RankCriticalInstalledNotPaid));
    QCOMPARE(rows.at(2).overdueRank, int(PartOverduePolicy::RankWarningUnpaidNotInstalled));
    QCOMPARE(rows.at(3).overdueRank, int(PartOverduePolicy::RankWarningInstalledNotPaid));
    QCOMPARE(rows.at(4).overdueRank, int(PartOverduePolicy::RankActiveUnpaidNotOverdue));

    // Regression from the parts archive video: finalized/closed rows must be
    // ordered by the visible "Дата" column first, then by order number DESC.
    // Hidden closed_at values are used only when a part date is missing.
    PartRowSnapshot visibleApr07Order101 = row(20, 101, QDate(), QDate(2026, 4, 7), true, false, QStringLiteral("closed"));
    visibleApr07Order101.orderClosedAtRaw = QStringLiteral("2026-04-07 09:03:36");
    PartRowSnapshot visibleApr06Order150 = row(21, 150, QDate(), QDate(2026, 4, 6), true, false, QStringLiteral("closed"));
    visibleApr06Order150.orderClosedAtRaw = QStringLiteral("2026-04-07 12:42:13");
    PartRowSnapshot visibleApr01Order134 = row(22, 134, QDate(), QDate(2026, 4, 1), true, false, QStringLiteral("closed"));
    visibleApr01Order134.orderClosedAtRaw = QStringLiteral("2026-03-31 10:07:35");
    PartRowSnapshot visibleApr01Order126 = row(23, 126, QDate(), QDate(2026, 4, 1), true, false, QStringLiteral("closed"));
    visibleApr01Order126.orderClosedAtRaw = QStringLiteral("2026-04-03 13:34:34");
    QVector<PartRowSnapshot> closedRows{visibleApr06Order150, visibleApr01Order126, visibleApr07Order101, visibleApr01Order134};
    PartsQueryService::sortRowsForTable(&closedRows, today);
    QCOMPARE(closedRows.at(0).orderNumber, 101);
    QCOMPARE(closedRows.at(1).orderNumber, 150);
    QCOMPARE(closedRows.at(2).orderNumber, 134);
    QCOMPARE(closedRows.at(3).orderNumber, 126);
}


void PartsQueryServiceTests::manualRowsUseVisibleOrderedDateAsOverdueAnchor()
{
    const QDate today(2026, 5, 4);

    PartRowSnapshot manual;
    manual.id = 1730;
    manual.orderNumber = 173;
    manual.dateRaw = QStringLiteral("2026-04-30");
    manual.deliveryDateRaw = QStringLiteral("2026-05-04"); // old bad save-default
    manual.paid = false;
    manual.orderStatus = QStringLiteral("in_progress");
    manual.hasInvoiceAttachment = false;
    PartsQueryService::refreshDerivedFields(&manual, today);
    QCOMPARE(manual.deliveryDate, QDate(2026, 4, 30));
    QCOMPARE(manual.overdueRank, int(PartOverduePolicy::RankWarningUnpaidNotInstalled));
    QCOMPARE(manual.overdueSeverity, int(PartOverduePolicy::SeverityWarning));

    PartRowSnapshot imported = manual;
    imported.id = 1731;
    imported.hasInvoiceAttachment = true;
    PartsQueryService::refreshDerivedFields(&imported, today);
    QCOMPARE(imported.deliveryDate, QDate(2026, 5, 4));
    QCOMPARE(imported.overdueRank, int(PartOverduePolicy::RankActiveUnpaidNotOverdue));
}


void PartsQueryServiceTests::debtBannerUsesPurchaseAmountAndOdometerStartsBlank()
{
    cs::tests::requireContains("src/core/services/PartsQueryService.h", "purchaseAmount");
    cs::tests::requireContains("src/core/services/PartsQueryService.cpp", "COALESCE(p.purchase_amount");
    cs::tests::requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "SupplierDebtReadService::prepareBanner()");
    cs::tests::requireContains("src/core/services/SupplierDebtReadService.cpp", "PurchasePaymentService::debts(db");
    cs::tests::requireContains("src/core/services/SupplierDebtReadService.cpp", "remainingCents()");
    cs::tests::requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "debts[store] += rowData.amount");
    cs::tests::requireContains("src/ui/common/dialogs/OrderDialogBuildUi.cpp", "m_odometer->setSpecialValueText(QString())");
    cs::tests::requireContains("src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp", "m_odometer->clear()");
}

void PartsQueryServiceTests::paidInstalledAndMissingDeliveryRowsUsePolicy()
{
    const QDate today(2026, 4, 20);
    auto paid = row(1, 100, today.addDays(-5), today.addDays(-30), true, false);
    QCOMPARE(paid.overdueRank, int(PartOverduePolicy::RankActivePaidNotInstalled));
    QCOMPARE(paid.overdueSeverity, int(PartOverduePolicy::SeverityNormal));

    auto installed = row(2, 101, today.addDays(-5), today.addDays(-30), false, true);
    QCOMPARE(installed.overdueRank, int(PartOverduePolicy::RankCriticalInstalledNotPaid));

    auto clearedDelivery = row(3, 102, QDate(), today.addDays(-30), false, false);
    // An invoice without a delivery date uses its ordered date as the existing policy's anchor.
    QCOMPARE(clearedDelivery.deliveryDate, today.addDays(-30));
    QCOMPARE(clearedDelivery.overdueRank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));
    QCOMPARE(clearedDelivery.overdueSeverity, int(PartOverduePolicy::SeverityCritical));
}

void PartsQueryServiceTests::allRanksAndDeterministicTies()
{
    const QDate today(2026,4,20);
    QVector<PartRowSnapshot> rows{
        row(7,100,today.addDays(-10),today.addDays(-10),true,true),
        row(6,100,today.addDays(-10),today.addDays(-10),true,false),
        row(5,100,today,today,false,false),
        row(4,100,today.addDays(-3),today.addDays(-3),false,true),
        row(3,100,today.addDays(-3),today.addDays(-3),false,false),
        row(2,100,today.addDays(-5),today.addDays(-5),false,true),
        row(1,100,today.addDays(-5),today.addDays(-5),false,false)};
    rows.last().paidAmount=50; // Partial payment retains the outstanding-debt rank.
    PartsQueryService::sortRowsForTable(&rows,today);
    for(int i=0;i<rows.size();++i) {
        QCOMPARE(rows.at(i).id,i+1);
        QCOMPARE(rows.at(i).overdueRank,i==6 ? int(PartOverduePolicy::RankFinalizedOrClosed) : i);
    }
    auto missing=row(10,100,{}, {},false,false);
    auto future=row(11,100,today.addDays(1),today,false,false);
    rows={missing,future};
    PartsQueryService::sortRowsForTable(&rows,today);
    QCOMPARE(rows.first().id,11);
    QCOMPARE(rows.last().id,10);
    QCOMPARE(rows.first().overdueRank,4);
    QCOMPARE(rows.last().overdueRank,4);

    rows={row(20,100,today.addDays(-5),today.addDays(-5),false,false),
          row(21,101,today.addDays(-5),today.addDays(-5),false,false),
          row(22,101,today.addDays(-5),today.addDays(-5),false,false)};
    PartsQueryService::sortRowsForTable(&rows,today);
    QCOMPARE(rows.at(0).id,22); QCOMPARE(rows.at(1).id,21); QCOMPARE(rows.at(2).id,20);
    auto closed=rows.first(); closed.id=30; closed.orderStatus="closed";
    rows.prepend(closed);
    PartsQueryService::sortRowsForTable(&rows,today);
    QCOMPARE(rows.last().id,30); QCOMPARE(rows.last().overdueRank,int(PartOverduePolicy::RankFinalizedOrClosed));
}

void PartsQueryServiceTests::closedUnpaidPartsPrecedeAllSettledParts()
{
    const QDate today(2026,9,17);
    auto unpaid=row(1,11,QDate(2026,7,27),QDate(2026,7,27),false,false,"closed");
    auto paid=row(2,11,QDate(2026,7,27),QDate(2026,7,27),true,false,"closed");
    auto partial=unpaid; partial.id=3; partial.paidAmount=4000;
    auto paidActive=row(4,12,today,today,true,false);
    QVector<PartRowSnapshot> rows{paid,paidActive,unpaid,partial};
    PartsQueryService::sortRowsForTable(&rows,today);
    QCOMPARE(rows.at(0).id,3);
    QCOMPARE(rows.at(1).id,1);
    QCOMPARE(rows.at(2).id,4);
    QCOMPARE(rows.at(3).id,2);
    // Only the archive payment ordering changes, not the closed-order highlighting.
    QCOMPARE(rows.first().overdueRank,int(PartOverduePolicy::RankFinalizedOrClosed));
    QCOMPARE(rows.first().overdueSeverity,int(PartOverduePolicy::SeverityNormal));
}

QTEST_MAIN(PartsQueryServiceTests)
#include "PartsQueryServiceTests.moc"
