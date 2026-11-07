#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class HistoryPageInteractionTests final : public QObject {
    Q_OBJECT
private slots:
    void historyPageKeepsContextMenuOnTableAndViewport() {
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.cpp", "m_table->setContextMenuPolicy(Qt::CustomContextMenu);");
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.cpp", "connect(m_table, &QWidget::customContextMenuRequested");
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.cpp", "connect(m_table->viewport(), &QWidget::customContextMenuRequested");
    }

    void historySearchAcceptsPartialPlateAndDigitQueries() {
        cs::tests::requireContains("src/core/services/HistoryQueryService.cpp", "compactPrefixLiteral");
        cs::tests::requireContains("src/core/services/HistoryQueryService.cpp", "platePrefixCandidate");
        cs::tests::requireContains("src/core/services/HistoryQueryService.cpp", "q.__rank < 100");
        cs::tests::requireContains("src/core/services/HistoryQueryService.cpp", "q.__order_uid IN (%1)");
    }

    void historyPageSwallowsRightButtonDoubleClick() {
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.h", "bool eventFilter(QObject* watched, QEvent* event) override;");
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.cpp", "event->type() == QEvent::MouseButtonDblClick");
        cs::tests::requireContains("src/ui/tabs/history/HistoryPage.cpp", "me->button() == Qt::RightButton");
    }

    void newOrderAutoPrefillsVehicleDataFromCompletedHistory() {
        cs::tests::requireContains("src/ui/common/dialogs/OrderDialogBuildUi.cpp", "prefillFromCompletedVehicleByPlateIfAvailable()");
        cs::tests::requireContains("src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp", "loadLatestClosedVehicleSnapshotByPlate");
        cs::tests::requireContains("src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp", "odometer and master must stay empty");
        cs::tests::requireContains("src/core/services/orders/OrderDialogReadQueryService.cpp", "router_work_orders");
        cs::tests::requireContains("src/core/services/orders/OrderDialogReadQueryService.cpp", "LOWER(TRIM(COALESCE(status,''))) IN ('closed'");
    }
};

QTEST_MAIN(HistoryPageInteractionTests)
#include "HistoryPageInteractionTests.moc"
