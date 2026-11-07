# DBY-T188 — parts PDF owner and child identity

Classification: active reliability evidence, synthetic fixtures only. Base 9bc94a6. User-reported weekly remaining 54%, stop <=5%. Payroll fixture/headcount is owned by desktop and is not edited in this increment.

## Reproduction and change

The actual collectPartsPdfGroups regression on the previous implementation failed with six findings: foreign child invoice borrowed; unknown UID silently omitted; ambiguous numeric owner accepted; foreign order stock issue borrowed; foreign child stock issue borrowed; receipt treated as issue. Before log exited 1. No private database was used.

PDF collection now calls the existing PartsReportQuery. A nonempty order UID must resolve uniquely; only an absent UID can use a unique physical-number owner. Unknown/ambiguous owners relevant to the period, including undated unresolved parts, return an error rather than disappearing or multiplying amounts. An explicitly outside-period row remains excluded. The query already matches the active child by physical ID, physical order number, order UID and part_uid when that column exists; the PDF now uses that exact match for invoice enrichment too. Mixed old/current child schemas do not infer a match. When both child UID columns are absent, the existing exact tuple match remains supported.

An optional PDF projection in PartsReportQuery batches distinct stock issue keys in a materialized CTE and joins them through the same matched active child and order number. A receipt is not an issue; a reused physical child ID cannot borrow another owner's issue or an issue belonging to a different child UID. Distinct issue keys prevent duplicate movements multiplying report rows. Existing non-PDF query columns and default behavior are unchanged. No per-row application SELECT was added.

Public effective-date precedence, date filtering, visible number, OEM/name/invoice fields, pricing formulas and PDF sort keys are retained. Assertions cover ordered date and closed-date fallback, stable UID despite stale physical number, visible number 7 and exact purchase30.10/client45.25/margin15.15. The remaining limitation is the source stock table itself: it records part_id/order_number rather than a historical part UID. The report only associates it through the exactly matched current child; it does not invent a historical movement identity.

## Cloud evidence and commands

```sh
bash scripts/run_parts_pdf_identity.sh
bash scripts/run_parts_report_query.sh
bash scripts/run_parts_pdf_reads.sh
python -B scripts/run_change_gates.py --static-only
```

Actual Qt/SQLite public collector covers positive/negative UID and numeric owners; matching/nonmatching child invoices; stock order/child/operation matching; both child UID columns absent and mixed schemas; exact fields/cents; SQL failure and repair/repeat. The existing shared-query and parts/dependency regressions are rerun. Production and combined AdditionalExpenseTests syntax are checked. Logs: build-single-instance-audit/db-parts-pdf-identity/{before,after,shared-query,regression,syntax,static}.log. No gate rules or budgets changed. Source hashes are recorded for this increment; the document is classified as active evidence, not a production compatibility exception.

## Windows handoff

AdditionalExpenseTests exact slot: partsPdfIdentityOwnersAttachmentsAndStock.

It reuses the synthetic public-collector fixture, then renders the actual PDF into the configured isolated report directory, requires one row, injects an unknown owner, verifies a failed render with unchanged preceding PDF bytes, repairs the owner and renders again. Windows behavior is NOT_RUN in cloud. Also rerun partsPdfDependenciesScopeAndRepeat and the existing parts report/query regressions. The payroll fixture is untouched. Full DBY-02/06 and current-binary acceptance remain open; no private profile, registry migration or distribution changed.
