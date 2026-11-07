# DBY-T190 — payroll ownership and row rounding

Task base 6d21394; desktop f13e439 integrated before publication, including its statistics and invoice changes. Last confirmed weekly remaining 48%, stop <=5%. Synthetic databases only. Statistics source/tests, gate budgets, private profiles and dist were not edited by this increment.

## Confirmed defects

`bash scripts/reproduce_payroll_identity_previous.sh` compiles the unchanged ReportsSyncSupport and ReportsDataLoaderSnapshotFlow from 6d21394 against synthetic SQLite. Expected exit 1, four failures:

1. Two work rows of 0.05 at 50% paid 0.05 in the aggregate, although the work text's per-row half-up model pays 0.03 + 0.03 = 0.06.
2. The payroll table displayed `0` for a nonzero kopeck amount.
3. Different order UIDs sharing internal number 1 produced an extra order through the numeric join.
4. Two distinct service UIDs sharing physical service ID 10 silently borrowed an assignment with no UID.

## Change and semantics

PayrollIdentityRead loads headers, services, assignments, adjustments and outsourcing references with checked SQL and iteration errors. PDF and background payroll snapshots load this graph once for the employee batch. Detail and text reads use the same calculation. Header/service UID is authoritative when present; a missing UID is supported only when its numeric reference resolves uniquely across the source. Unknown or duplicate explicit identities do not borrow a numeric owner. A numeric-only standalone work-text request rejects multiple header owners. Numeric adjustment writes also reject an ambiguous/missing current order before creating/updating adjustment data.

Assignments with distinct masters remain legal when their shares total at most 100%. Repeated assignments for one master, conflicting owner UID, invalid shares, ambiguous outsourcing references and ambiguous adjustment owners return errors, not partial wages. Distinct header owners stay separate even when their internal numbers collide. A confirmed absent adjustment zeroes only its owner's earnings; a deleted adjustment removes only its owner's detail.

The existing work-text formula is the common model: round each work's shared, discounted base to kopecks; round that row's employee percentage to kopecks; sum integer kopecks. PDF order/master totals and payroll snapshots use these sums rather than applying the percentage again to grouped totals. Shared expenses are deducted afterward. Salary table/detail text now keeps two decimal places; no global money formatter changed. Admin salary-source formulas are unchanged and are not claimed as newly accepted here.

The existing report window rejects returned read errors before opening a partial detail dialog. The PDF renderer already collects before creating its writer; new native checks exercise that path. No numeric alias is invented for an ambiguous assignment. Sources requiring reconciliation remain blocked until the owner is established.

## Commands and results

```bash
bash scripts/reproduce_payroll_identity_previous.sh  # expected exit 1: four failures
bash scripts/run_payroll_identity.sh               # PASS
bash scripts/run_payroll_read_failures.sh           # PASS
bash scripts/run_report_detail_reads.sh             # PASS
python -B scripts/run_change_gates.py --static-only # PASS
```

Actual Linux Qt/SQLite public detail, work-text and payroll snapshot functions are executed, without doubles. Checks cover correct UID despite numeric collision; unconfirmed assignment rejection; repair using service UID; unknown assignment UID rejection; numeric adjustment ambiguity; explicit foreign/absent adjustment ownership; legal 50/50 split; discount and per-row half-up; distinct owner rows; empty period; dependency failures and repeated repair. Snapshot amounts and displayed kopecks are checked. Existing read regressions retain successful old schemas without UID columns, missing/denied/corrupt sources, nine dependency failures, cache rejection and exact totals.

Syntax verification (after integrating desktop):

```bash
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
/workspace/qt-sdk/root/usr/lib/qt6/libexec/moc -Isrc -I"$qt" -I"$qt/QtCore" tests/AdditionalExpenseTests.cpp -o build-single-instance-audit/AdditionalExpenseTests.moc
g++ -std=c++17 -fPIC -DQT_WIDGETS_LIB -DQT_GUI_LIB -fsyntax-only -Isrc -Ibuild-single-instance-audit -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" -I"$qt/QtPrintSupport" -I"$qt/QtTest" tests/AdditionalExpenseTests.cpp src/core/services/ReportsPdfSupportSnapshotFlow.cpp src/core/services/ReportsSyncSupport.cpp src/core/services/ReportsDataLoaderSnapshotFlow.cpp src/ui/tabs/reports/ReportsDialogSupport.cpp
```

## Windows handoff and remaining acceptance

AdditionalExpenseTests exact new slots:

- `payrollIdentityOwnersSplitAndRepeat`: shared synthetic fixture, public snapshot repeat, visible kopecks, numeric adjustment/text ambiguity rejection.
- `payrollPdfIdentityRoundingAndFilePreservation`: real workspace/router views; PDF collector and renderer; 0.06 total and two 0.03 work rows; actual detail dialog cell; archived service-ID collision rejection; previous good PDF preservation; dependency repair/repeat and current account revocation/recovery.

Also rerun `payrollReadFailuresAndRecovery`, `reportDetailAndElectricityReadFailures` and `payrollPdfSupplementFailuresScopeAndRepeat` when present in the current native binary. Full suite RETEST_REQUIRED. New native PDF/UI slots NOT_RUN in Linux; Windows platform dependencies still limit the complete runner. No full-runtime performance result is claimed: the identity graph requires global reference counts, is read once per PDF/snapshot employee batch, and must be measured on the Windows dataset. DBY-02/06 remain IN_PROGRESS pending native and wider acceptance.
