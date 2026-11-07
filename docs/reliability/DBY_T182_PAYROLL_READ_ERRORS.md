# DBY-T182 — payroll read errors and recovery

Base: 4a99835, after desktop historical-version and deleted-order-event changes. DBY-02 stays IN_PROGRESS. This increment does not touch InvoiceRegistryService, registry reconciliation, protected source inventory, private data or dist.

## Reproduction

On the base, calling public `ReportsDataLoader::buildPayrollSnapshot` for a nonexistent synthetic path creates an SQLite database, then attempts schema creation and silently treats failed payroll queries as empty/zero data. The actual compiled service regression returned exit 1: `FAIL: payroll read created missing database`. Log: `build-single-instance-audit/db-payroll-errors/before.log`.

The reader now opens read-only, performs its dependent reads in one read transaction, and returns `PayrollSnapshot.error` for open/transaction/SQL/employment-policy failures. Bootstrap already owns payroll adjustment schema creation. Personnel, settings, admin totals, shared expenses and per-master query failures cannot produce a partial successful payroll. Failures clear rows, produce one explicit diagnostic, evict the cache entry and leave the tab eligible for retry. UI disables payroll PDF while loading/failed; a repaired read restores the table and export availability. Other tabs retain their normal export behavior. Existing financial formulas are unchanged.

## Evidence and commands

```bash
bash scripts/run_payroll_read_failures.sh > build-single-instance-audit/db-payroll-errors/after.log 2>&1
python -B scripts/run_change_gates.py --static-only > build-single-instance-audit/db-payroll-errors/static.log 2>&1
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
/workspace/qt-sdk/root/usr/lib/qt6/libexec/moc tests/AdditionalExpenseTests.cpp -o build-single-instance-audit/db-payroll-errors/AdditionalExpenseTests.moc
g++ -std=c++17 -fPIC -DQT_WIDGETS_LIB -DQT_GUI_LIB -fsyntax-only -Isrc -Ibuild-single-instance-audit/db-payroll-errors -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" -I"$qt/QtPrintSupport" -I"$qt/QtTest" tests/AdditionalExpenseTests.cpp src/ui/tabs/reports/ReportsPageExportFlow.cpp src/ui/tabs/reports/ReportsPageRefreshFlow.cpp src/ui/tabs/reports/ReportsPageBuildUi.cpp > build-single-instance-audit/db-payroll-errors/ui-syntax.log 2>&1
```

Actual Qt/SQLite public-reader + cache regression PASS, no mocks. Cases: missing path (no creation); directory path; corrupt SQLite (unchanged bytes); POSIX permission denial; valid empty result; SQL error even when the selection is empty; nine required tables independently unavailable; repair and retry after each; corrupt employment history and payroll JSON; cached previous success evicted after failure; no leaked connections; unchanged file bytes after successful read. Independent expected wages: master 100 × 50% − 10 = 40, admin ((50 − 30) + (20 − 10)) × 10% = 3.

Static gates PASS without changed rules/budgets. UI and updated Windows test source syntax PASS with Qt Widgets compile definitions. Initial standalone syntax invocation lacked those definitions and failed on existing QtTest mouseClick/QApplication declarations; corrected command above, no source workaround. No application binaries delivered.

Windows target `AdditionalExpenseTests`, exact slots:

- `payrollReadFailuresAndRecovery`
- `payrollUiRecoversAfterReadFailure`

First reuses the same public-service checks (POSIX permission case is platform-specific; native Windows ACL denial remains unverified). Second runs the actual async report UI, verifies visible diagnostic/empty table/disabled PDF, repairs the schema, refreshes and requires two personnel rows and enabled PDF. Windows behavior NOT_RUN here. Rerun full AdditionalExpenseTests, ReportsOrdersTests/payroll regression and full strict gate. Linux whole application remains limited by Windows dependencies; syntax is not UI behavior acceptance.

## Public report-read review: outstanding DBY-02 work

This is a source-level inventory, not a claim of all-service behavioral acceptance.

| Public entry points | Current evidence / next gap |
|---|---|
| ReportsDataLoader period/outsource | Read-only and query/iteration diagnostics present; prior Windows T169 covers missing/schema/empty/populated. Period has stats-only error and caches results without a typed failure guard: retry/cache scenario remains to reproduce. |
| ReportsDataLoader orders | Read-only transaction and clientPaymentError path present; empty selection deliberately avoids unrelated invoice enrichment. Cache failure behavior still requires targeted acceptance; InvoiceRegistryService belongs to desktop. |
| ReportsDataLoader parts | Opens default writable SQLite; stock/part subreads report diagnostics separately. Missing-file creation and partial results/failed-cache behavior remain to reproduce and fix. |
| ReportsDataLoader payroll | This increment: public reader/cache synthetic PASS; async UI Windows NOT_RUN. |
| ReportsDataLoader expenses/total expenses | Read-only, explicit error fields; T169/T173 and AdditionalExpenseTests cover existing failure/retry cases. Latest binary acceptance remains separate. |
| ReportsPdfSupport collectPeriodPdfRows/collectPartsPdfGroups/collectOutsourceGroups/collectPayrollDetails and financial collectors | Several unchecked exec paths and APIs without diagnostics remain; failure must not export a plausible empty/partial PDF. Payroll button guard here covers a known failed snapshot, not a later PDF-specific read failure. |
| ReportsSyncSupport payroll detail/electricity readers | Several failed queries return empty rows; payroll detail reader also calls an adjustment-schema ensure helper. Needs independent error/read-only contract and UI acceptance. |
| StatisticsSnapshotService collectFromPath/collectCurrent | collectFromPath uses writable SQLite open; missing-file case needs reproduction. StatisticsReadService has an explicit error result but all failure/partial-data paths still need targeted tests. |
| InvoiceReportService context/load/orderSummary | Path/permission checks and diagnostics exist; uses scoped connection and registry. Reserved for concurrent desktop work; no claim of complete coverage. |
| BayLoadReport calculate | Pure calculation, no database opening; persistent BaySnapshot acquisition is a separate service boundary. |

Do not move DBY-02 to PASS or skip to DBY-06/07 on this partial coverage. Next independent target: parts reader missing-path/partial result and retry, followed by PDF/detail diagnostics. Real profile and registry work stay with desktop.
