# DBY-T185 — statistics read failures and expense coverage

Base e5a21aa. Synthetic sources only; no profile, registry, dist or gate changes.

## Reproduced defect and implementation

`StatisticsSnapshotService::collectFromPath` on an absent path created a SQLite file. The new portable regression failed before the fix with exit 1 and `FAIL: public statistics reader created missing database` (before.log). This public legacy financial collector is distinct from the current StatsPage worker; its test is not UI acceptance.

The legacy collector now opens read-only, holds a read transaction and returns a typed error plus a visible problem message for open/SQL/iteration failures. Every aggregate is discarded after a failure. collectCurrent obtains only an existing handle instead of initializing a runtime. The independent fixture expects labor 100, parts 50, purchase 30, payroll 50, outsource margin 10, periodic expense 10 plus additional 2.50: net 67.50. Missing directory/file, valid empty, seven broken required tables, malformed optional additional-expense table, repair/repeat, unchanged bytes and no leaked handles are checked.

Current StatsPage source inspection also found writable worker opens, swallowed primary part-query errors, partial performance results and stale tables left on failure. The worker now opts into read-only ScopedSqliteConnection (other users retain the default), rechecks nav.statistics on the opened source, resolves current ownership scope, propagates primary part-query/iteration errors, discards partial results and clears stale UI models/properties. A half-present stock schema fails. Permission and current-reader behavior are covered by a Windows test, NOT_RUN in cloud. Invoice/attachment enrichment still has unchecked dependencies and is explicitly not closed by this patch. Performance view retains its existing access policy; no new role grants were introduced.

## Commands and results

```sh
bash scripts/run_statistics_read_failures.sh
python -B scripts/run_change_gates.py --static-only
```

Portable actual Qt/SQLite legacy statistics and expense-reader regression PASS. Production/UI/test syntax PASS (Qt6 Core/Sql/Gui/Widgets/Network/PrintSupport/Test with generated AdditionalExpenseTests.moc). Logs in build-single-instance-audit/db-statistics-errors: before.log, after.log, syntax.log, static.log. A development compile found a leftover default argument in an internal helper; removed it and reran. Full Linux application remains constrained by Windows APIs; no Windows or binary acceptance is claimed.

Expense coverage invokes the existing public buildExpensesSnapshot/buildTotalExpensesSnapshot: missing source, empty future period, populated totals 1000/250 cents, missing cleaning column with cleared total rows, repaired repeat. These cases pass without production expense changes; no new defect is demonstrated for them. Existing native expenseFormDistinguishesEmptyPeriodFromReadFailure and expenseFormRecoversAfterBrokenSchema cover form error/retry and should be rerun. This does not prove expense PDF helper paths or role enforcement throughout all report entry points.

## Windows handoff

AdditionalExpenseTests slots:

- statisticsReadFailuresAndRecovery — shared actual legacy statistics plus expenses fixture.
- statisticsWorkerFailuresAndCurrentRights — current capture/run with valid empty source, broken part column, repair, account disabled after capture, re-enabled retry, absent source and denied request without file creation.
- expenseFormDistinguishesEmptyPeriodFromReadFailure
- expenseFormRecoversAfterBrokenSchema

Run full AdditionalExpenseTests, PurchasePaymentTests and normal acceptance gate after these. Current StatsPage UI failure display and authorized populated rendering require Windows acceptance; syntax is not behavior evidence. DBY-02 remains OPEN: PDF parts/outsource/expenses, payroll supplementary queries, attachment enrichment diagnostics, period cache/retry and complete current-view scope/error coverage remain. Desktop owns registry migration and Windows acceptance.
