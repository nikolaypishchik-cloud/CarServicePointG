# DBY-T184 — period PDF UID batching and checked report reads

Synthetic evidence only. DBY-02 remains open; full Windows application acceptance is RETEST_REQUIRED. No private profiles or distribution changed.

## Reproduction and change

The former period collector selected children by physical order_number inside each header loop. The synthetic fixture deliberately gives an outside-period order the same physical number and selected children stale physical numbers. The former SELECT returns `Wrong owner`; the production UID loader returns the selected owner's Service/Part. Two selected owners may share physical child IDs without sharing UID ownership.

The collector now reads selected headers and two batched child sets by stable order_uid. Missing/duplicate identities or a part pointing to a service outside its owner fail explicitly. No aliases are inferred. Header date/number ordering, child ID ordering, service-first layout, first-part work charge, leftover parts, header-only rows and pricing calculations are preserved. Detail graphs use 1 actual SQLite statement when empty and 3 when populated, measured with sqlite3_trace_v2 at 10 and 50 orders. This count covers the graph, not the entire PDF pipeline: financial/personnel queries remain. Period salary calculation skips per-order work descriptions and issued-payroll history that it does not use.

The period collector checks report permission and unrestricted order scope, holds a read transaction, propagates policy/aggregate/payroll/expense errors, and clears every total on failure. Renderer aborts before QPdfWriter on read failure. Payroll detail and electricity readers now expose SQL errors and do not repair schemas while reading; detail UI shows the error. Payroll PDF propagates its main collector error. PDF workers explicitly own a read-only connection to the captured source; missing files are not created, and handles are removed on exit.

## Reproducible cloud commands and observed results

Run from repository root with the existing Qt SDK:

```sh
bash scripts/run_period_report_graph.sh
bash scripts/run_report_detail_reads.sh
python -B scripts/run_change_gates.py --static-only
```

All PASS on Debian Qt6/SQLite. Graph oracle: each selected order has works 100.00, sale 45.25, purchase 30.10, margin 15.15 and total 145.25; exact row ordering and visible number 7 checked at 10/50 orders. Empty, SQL failure, repair/repeat, duplicate/missing UID rejection and multi-part layout checked. Detail oracle: work 100 and master 50; electricity 20 x 4.25 = 85. Closed source, empty success, missing table/column, repair/repeat, unchanged database bytes and actual worker connection lifecycle checked. Production/UI/test syntax also PASS with Qt Core/Sql/Gui/Widgets/Network/PrintSupport/Test includes and generated AdditionalExpenseTests.moc. Logs: build-single-instance-audit/db-period-pdf/{graph,detail,syntax,static}.log. Initial development compile failures (missing Network/PlateUtils/date/logging dependencies and const scope-guard dismissal) were corrected with real dependencies; no test stubs or gate changes.

## Windows handoff — NOT_RUN here

```powershell
.\build-single-instance-audit\AdditionalExpenseTests.exe periodPdfFailuresAndUidRows reportDetailAndElectricityReadFailures
```

Use the actual configuration output path if the generator places executables in Release. Then run all AdditionalExpenseTests and ReportsOrdersTests and the normal full gate. New slots cover collector totals/permission denial/error clearing/retry and no PDF file on read failure; electricity and payroll detail cover empty/error/repeat. Desktop should verify final period PDF at 1x/5x, layout and all six financial totals against its independent oracle, including empty period and worker lifecycle. No Windows PASS is inferred from syntax checks.

## Remaining limits

UID-less/ambiguous legacy graphs are rejected, not guessed; real-profile compatibility needs acceptance. Full Linux runner is constrained by existing Windows APIs. Full pipeline performance and PDF rendering are unmeasured here. Other PDF parts/outsource/expense collectors, payroll expense-breakdown/notes and statistics still need their own error-boundary review. The reader changes do not establish complete DBY-02 or release acceptance. Desktop metadata and registry work remains independent.
