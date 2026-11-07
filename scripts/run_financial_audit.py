"""Run isolated financial regression cases; never launch the desktop distribution."""
import json, os, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('FINANCIAL_AUDIT_OUTPUT', str(ROOT / 'build-single-instance-audit/financial-audit')))
EXE = ROOT / 'build-single-instance-audit/tests/PurchasePaymentTests.exe'
CASES = '''moneyPrecisionAndDiscountModes
financialIndependentDecimalOracle
financialReturnsRefundAndCredit
historyReopenPreservesPaymentsAndInstalledParts
supplierInvoiceLedgerIsolation
elitPurchaseAcrossInvoiceLines
elitPurchaseAcrossDocuments
elitSharedDebtSummary
invoiceGroupsOrdersAndSettlesAllPositions
invoicePaymentRejectsChangesAndRollsBack
invoiceNumbersDoNotMergeDifferentSuppliersOrDates
invoiceEvidenceMismatchBlocksPayment
partialStockInvoiceDistribution
stockOnlyInvoiceRegistry
orderPartsPaymentIsAtomicAndPreservesCustomerStatus
groupedPartialPaymentsAndHistory
orderPartsPaymentRollsBackAndRejectsStaleSnapshot
warehousePurchaseIsNotPaidTwiceFromOrder
legacyStatesAndPartialSettlement
inventoryAndCombinedDebts
paymentRollsBackOnAuditFailureAndMasterDenied
zeroAndDeferredPayments
inventoryDeferralAndSummary
debtAnalysisUsesOrderDateForUnpaidClosedOrders
debtAgeIgnoresDeliveryDeadlineAndContinuesAfterClosure
debtAgingBucketsAreExclusiveAndHeadersSeparateTimelines
debtAnalysisModesAndFiltersUseOneSnapshot
debtAnalysisExpensePeriodDoesNotDuplicateLiabilities
debtTimelinesAndLinksKeepUnknownValuesSeparate
supplierReconciliationPreservesPaymentsAndRejectsStaleOrUnauthorizedEdits
reconciliationSuppliersComeOnlyFromFinance
supplierReconciliationBatchRollsBackAndListsPaidSuppliers
reconciledTotalsFollowSubsequentLedgerChanges
oldPaymentHistoryMigration
paymentHistoryInStatistics
spareBatchMixedInvoiceIsAtomic
paymentPersistsAfterReopen
concurrentPreflightConnections
debtSummaryWorkerUsesIndependentConnection
totalExpenseDashboardMetrics'''.split()

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('CS_'):
            del env[key]
    env.update(CS_ELIT_DISABLE_STARTUP='1', CS_FINANCIAL_AUDIT='1',
               CS_FINANCIAL_ORACLE=str(OUT/'oracle.json'),
               CS_FINANCIAL_ACTUAL=str(OUT/'actual.json'),
               QT_QPA_PLATFORM='offscreen',
               QT_PLUGIN_PATH=r'C:\Qt\6.10.2\msvc2022_64\plugins')
    env['PATH'] = r'C:\Qt\6.10.2\msvc2022_64\bin;' + env['PATH']
    results = []
    for case in CASES:
        log = OUT / (case + '.txt')
        with (OUT/(case+'.stderr.txt')).open('wb') as stream:
            try:
                completed = subprocess.run([str(EXE),case,'-o',str(log)+',txt'],
                    cwd=ROOT, env=env, stdout=stream, stderr=stream, timeout=45)
                status = completed.returncode
            except subprocess.TimeoutExpired:
                status = 'timeout_45s'
        results.append(dict(case=case, exit=status))
        (OUT/'case-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print(case, status, flush=True)
