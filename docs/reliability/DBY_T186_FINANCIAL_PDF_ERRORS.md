# DBY-T186 — expense and outsource PDF read boundaries

Started from remote 247bb1f, verified descendant of requested b52e203. User reports ten targeted Windows scenarios passed on prior work; that does not accept this increment. Fresh desktop quota 63% in 10080-minute window; stop threshold 5%. Synthetic data only.

## Actual entry paths and semantics

ReportsPage::exportOutsourcePdf calls ReportsPdfRenderer::renderOutsourcePdf, which calls collectOutsourceGroups. The collector previously ignored failed SQL and the renderer proceeded with an empty PDF. It now returns an explicit diagnostic for SQL/iteration/open/date/permission failure, clears all totals, and the renderer aborts before path allocation or QPdfWriter. Required router order source errors are not hidden by an empty selection. Existing sort, period and financial formulas are retained. Physical-number ownership in the existing outsource join is a separate DBY-06 limitation, not changed or accepted here.

ReportsPage::exportExpensesPdf captures ExpensesFormData from the UI. Its PDF page performs no SQL: it prints the form, including unsaved edits, rather than the total-expenses ledger. The renderer now verifies current nav.reports permission/unrestricted order scope and revalidates the expense source using the existing checked readonly reader before publication. It does not silently replace captured form values with saved SQL values. Missing settings or malformed expense columns abort export, with an explicit error and zero result records. This is source validation, not a new expense aggregation or inclusion of additional-expense rows.

Both pages formatted amounts with zero decimal places. An actual Qt-generated expense PDF plus pdftotext failed the 12.25 assertion before the patch (exit 1, pages-before.log). Both templates now print monetary amounts with two decimal places; actual PDF text checks pass for expense 12.25 + 7.10 = 19.35 and outsource repair 19.35 / client 31.95 / margin 12.60. The initial run also emitted a fontconfig cache-directory diagnostic; the script now uses a writable development cache and the successful repeat is clean.

## Reproducible cloud checks

```sh
bash scripts/run_financial_pdf_reads.sh
bash scripts/run_financial_pdf_pages.sh
python -B scripts/run_change_gates.py --static-only
```

Actual Qt/SQLite production collector and expense-source validator: PASS empty success, populated sums/order, missing outsource table and router dependency, malformed expense column, missing settings, repair/repeat, disabled account, absent/revoked admin nav.reports grant and successful retry after grant, closed handle without implicit reopening. These are the production service functions used by the renderer, with real AccessControlService and ActivityAuditService; no mocks/stubs. Actual production page rendering and extracted PDF text: PASS. The whole public renderer/storage pipeline is NOT_RUN on Linux; it is explicitly covered by the Windows slots below, not inferred from the portable checks. SQL swallowing on the old outsource path was identified in source; the pre-patch executable reproduction recorded here is the lost-cent PDF defect.

Production renderer/service/page and AdditionalExpenseTests syntax PASS; unchanged static gates PASS. Logs under build-single-instance-audit/db-financial-pdf: read.log, pages-before.log, pages-after.log, syntax.log, static.log. Full Linux application remains constrained by Windows platform dependencies. No application binaries, private data, gate budgets or desktop-owned registry/metadata were changed.

## Windows handoff — new tests NOT_RUN here

AdditionalExpenseTests exact slots:

- financialPdfReadFailuresAndRecovery — same production service/permission fixture as cloud.
- expensePdfFailuresRightsAndRepeat — real renderExpensesPdf with captured form values; successful PDF, malformed column, missing settings, revoked current account, repaired repeat. Failed export must preserve bytes of the preceding good PDF.
- outsourcePdfFailuresRightsAndRepeat — real renderOutsourcePdf with two rows; SQL and router dependency failures, current account revocation, repaired repeat and valid empty PDF. Failed export preserves the preceding PDF.

Run the full AdditionalExpenseTests/report regression and normal gate afterward. Desktop should inspect final typography/pagination, especially two-decimal amounts, in the native font environment. Full renderer and user-visible access/error/retry acceptance remains Windows RETEST_REQUIRED. DBY-02 stays OPEN for PDF parts, payroll supplementary queries, attachment enrichment and remaining cache/error scope; this increment does not close all reports.
