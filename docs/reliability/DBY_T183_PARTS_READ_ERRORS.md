# DBY-T183 — parts report read failures

Base 0d233a0. DBY-02 remains open. Public buildPartsSnapshot previously opened writable SQLite, ignored failure of its primary detail query, and could retain rows/totals after a stock subquery failed. Actual SQLite primary-query reproduction on an empty synthetic source reports missing attachment table; this is not an empty successful selection.

Parts reads now use a read-only transaction, checked primary prepare/execute/iteration, explicit policy/material/spare read errors, and discard the complete snapshot on failure. Optional initialization flags keep schema creation available to existing stock operations, while the report reads existing schemas only. Incomplete profiles require schema preparation rather than being silently repaired by a report. The shared cache evicts failed parts results; UI can retry and prevents parts PDF export until a successful snapshot. Existing mixed report fixture now explicitly initializes stock schemas and requires an error-free parts result; prior partial results are no longer treated as success.

Cloud commands/evidence:

- `bash scripts/run_parts_report_query.sh > build-single-instance-audit/db-parts-errors/query.log 2>&1` — PASS. Actual production query/open helper and cache; missing/directory path, SQL failure on empty input, repaired empty success, populated repeat preserving exact 30/45 amounts and invoice attachment, unchanged source bytes, failed-cache eviction/recovery.
- `python -B scripts/run_change_gates.py --static-only > build-single-instance-audit/db-parts-errors/static.log 2>&1` — PASS; no gate/budget changes.
- Qt6 `g++ -std=c++17 -fPIC -DQT_WIDGETS_LIB -DQT_GUI_LIB -fsyntax-only` for changed production services/UI and AdditionalExpenseTests, with Qt Core/Sql/Gui/Widgets/Network/PrintSupport/Test headers and generated moc — PASS; log syntax.log. Same include/prefix recipe as DBY-T182.

Initial helper test stopped with `No query Unable to fetch row`: exec replaced the earlier prepare diagnostic. The helper now checks prepare separately and the identical test passes. This was a diagnostic correction, not a relaxed assertion. Full public parts worker and UI are not executed in Linux; the portable query subset is not full acceptance.

Windows `AdditionalExpenseTests` slots:

- `partsReadFailuresAndRecovery`: public worker missing/unusable path; empty success; populated data; six independent dependent-table failures; no partial rows/tiles; repair and retry; unchanged source on read.
- `partsUiRecoversAfterReadFailure`: actual async UI error, disabled PDF, repaired empty result, enabled PDF on retry.

Also run `ReportsOrdersTests::orderListAndPrintData`: its existing parts invoice-link fixture now explicitly initializes dependent schemas and requires an error-free result. Full stock/report and Windows acceptance RETEST_REQUIRED. No personal profile/dist or desktop registry work changed. Remaining work: period PDF rights/errors, bounded UID detail queries and SQL-count oracle, followed by other PDF/detail/electricity errors. Peripheral credential/remote-ledger diagnostics are not claimed complete by this subset.
