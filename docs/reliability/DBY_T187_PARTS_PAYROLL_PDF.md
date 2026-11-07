# DBY-T187 — parts PDF and payroll supplementary reads

Base 3167226; desktop reports all three previous financial PDF Windows slots PASS with a configured report folder. Fresh weekly remaining 56%, stop <=5%. Synthetic sources only.

## Changes and reproduced behavior

Parts PDF collection previously ignored primary/stock/material/policy SQL failures and could render partial or empty results. The collector now checks the current financial-report permission and unrestricted order scope, reads materials without schema initialization, propagates query/iteration errors and discards partial groups. The public renderer obtains a successful collection before allocating the PDF path. Explicit pricing-policy reads no longer reopen a closed handle or fall back to the application database when given an invalid handle.

Payroll rendering previously read notes, personnel and admin breakdown/configuration without checked diagnostics, including admin reads after opening QPdfWriter. All reads now complete in one read transaction before path creation/writer opening, with explicit errors for supplementary expense columns, settings, personnel and admin totals. Read failure leaves the preceding good file untouched. Payroll collection and supplementary financial reads enforce the existing full-financial-report boundary: a restricted master cannot receive a global financial PDF, even with an explicit nav.reports grant. No role grants or owner matching rules were expanded.

Actual generated parts and payroll PDFs lost cents before the formatting fix: pages-before.log recorded missing 19.35, 12.60, 31.95 and 12.25 (exit 1). Monetary cells and summaries now retain two decimal places; extracted PDF text passes. Quantities/counters retain their existing formatting. Financial calculation formulas are unchanged.

## Reproducible cloud evidence

```sh
bash scripts/run_parts_pdf_reads.sh
bash scripts/run_payroll_supplement_reads.sh
bash scripts/run_parts_payroll_pdf_pages.sh
python -B scripts/run_change_gates.py --static-only
```

Actual Qt/SQLite production parts collector PASS: empty success; purchase 30.10, client 45.25, margin 15.15; five missing dependencies (attachments, spare movements, inventory, settings, orders); repair/repeat; restricted master denied; closed explicit pricing handle stays closed. Admin supplementary aggregate independent oracle: one order, purchase 49.45, client 77.20, margin 27.75 including two outsource rows; missing outsource dependency clears totals and repaired retry succeeds.

Actual supplementary expense/configuration readers PASS: empty values/notes, 12.25 + 7.10 = 19.35, note text, malformed column, absent settings, malformed admin-source JSON, repaired retry and denied restricted master despite explicit grant. During development a retry retained the prior Parameter count mismatch diagnostic; the public config reader now clears its error on entry and the repeat passes. Actual Qt PDF pages + pdftotext PASS for both report kinds. Production renderer/helper/page and AdditionalExpenseTests syntax PASS. Unchanged static gates PASS. No stubs or replacement SQL used in these checks.

Logs: build-single-instance-audit/db-parts-payroll-pdf/{parts-read,read,pages-before,pages-after,syntax,static}.log. Portable scripts use the existing cloud Qt SDK. Full Windows-specific application/renderer storage integration is NOT_RUN in Linux, not inferred from collector/page checks.

## Windows handoff — NOT_RUN here

AdditionalExpenseTests exact slots:

- partsPdfDependenciesScopeAndRepeat
- payrollPdfSupplementFailuresScopeAndRepeat

Both configure an isolated report directory and call the public renderer. They require an initial good PDF, injected read errors, unchanged previous PDF bytes, successful repair, current restricted-scope refusal and restored owner access. Parts verifies exact 30.10/45.25/15.15 and empty successful output. Payroll verifies 100.50 x 50% - 12.10 = 38.15, supplementary-note and inventory failures, inactive-owner refusal, repair and empty future period. Rerun the previous financial PDF slots and all AdditionalExpenseTests/report regression plus the normal full gate. Two-decimal cell wrapping and native font pagination need desktop inspection.

## Limits

DBY-02 remains open until final current-binary acceptance. This increment does not establish Windows behavior, attachment enrichment coverage elsewhere, every cache/retry boundary or complete DBY-06 ownership of older payroll queries. The unused legacy periodExpensesTotal helper is not part of the payroll renderer path and remains outside these checked supplementary entry points. No private profile, distribution, registry migration, protected metadata or gate budgets were changed.
