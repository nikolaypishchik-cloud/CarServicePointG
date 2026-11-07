# DBY-T216 — admin payroll detail ownership

Evidence document (synthetic development verification, not release acceptance).
Base: 544d214641900aa507b6235c805e0e83d81c5d2c. DBY-06 remains open for full acceptance.

## Defect and correction

ReportsPage::openPayrollOrdersDialog calls ReportsSyncSupport::collectPayrollOrdersForMaster. Its admin branch joined service captions only by internal order number. A child with another known order UID leaked into the selected order's text. This changed captions, not the header financial total.

The reader now uses the same PayrollIdentityRead::Index rules as master payroll: nonempty UID is authoritative; a numeric-only historical row needs exactly one parent. Unknown UID or ambiguous identity affecting a selected order returns an explicit error and no partial rows. A valid UID can retain a historical numeric key different from the current parent's internal key. A known other owner is excluded, not relabelled. Duplicate parent UIDs are rejected. NULL captions retain GROUP_CONCAT's omission semantics; service traversal order, header SQL/date/order/display fields and header-based admin money calculation remain unchanged. Caption indexes are built once per read, with no per-header SELECT and no persistent cache. Caller transaction ownership is unchanged. The existing UI displays the propagated error.

## Reproduction and Linux results

New shared fixture calls the production public reader with synthetic SQLite only. It covers numeric-only schema, known foreign UID with colliding numeric key, valid historical UID with a different numeric key, empty child UID with a unique parent, unknown UID refusal, ambiguous numeric owner, duplicate UID, missing SQL column, repair/repeat, valid empty period and unchanged database bytes. It asserts 100.05 gross and 10.01 admin amount at 10 percent.

- RED: compile tests/AdminPayrollIdentityTests.cpp with HEAD (544d214) ReportsSyncSupport.cpp instead of the working source, using the same source/link list in scripts/run_admin_payroll_identity.sh. Exit 1: `Foreign UID leaked into admin detail:`. Exact old source extraction: `git show 544d214:src/core/services/ReportsSyncSupport.cpp > /tmp/AdminPayrollBefore.cpp`; replace that source argument and output directory in the runner. Log: build-single-instance-audit/admin-payroll-before.log.
- GREEN: `bash scripts/run_admin_payroll_identity.sh`, exit 0. Log: build-single-instance-audit/admin-payroll.log.
- `bash scripts/run_payroll_identity.sh`, exit 0; existing master public detail/text/snapshot, split, rounding and retry cases. Log: build-single-instance-audit/admin-master-regression.log.
- `bash scripts/run_report_detail_reads.sh`: exit 0; detail/electricity compatibility. Log: build-single-instance-audit/admin-detail-regression.log.
- `python3 -B scripts/run_change_gates.py --static-only`, exit 0. No gate/budget/PerformanceAuditScenarios.h changes. Log: build-single-instance-audit/admin-payroll-static.log.

## Windows handoff

AdditionalExpenseTests slots: **adminPayrollIdentityOwnersAndRepeat** (new), **payrollIdentityOwnersSplitAndRepeat**, **reportDetailAndElectricityReadFailures**. Also rerun **payrollPdfIdentityRoundingAndFilePreservation** for downstream presentation acceptance. Native Windows/full gate RETEST_REQUIRED; Linux production-reader compilation is not Windows UI/PDF acceptance. Frozen 544d214 endurance results remain evidence for that revision, not this new source. No private profile, application binary or dist was touched. SOURCE_SHA256.json and ADR-0002 were left untouched to avoid parallel desktop edits; exact changed-file hashes follow.

```text
bffe6a1f8a378b43c9ac5104c4cea62e7a77b6c4b5064105c03699efa9d5d8ef  src/core/services/PayrollIdentityRead.h
7d41c03ab6b5cbb1830912f025588492a3a327789c9a660805caccf84360f377  src/core/services/ReportsSyncSupport.cpp
730f5aad8083fe8deddb888a0a6f26b2744592ac8fe66cf47b2a79f10600c98c  tests/AdditionalExpenseTests.cpp
e725536d36a167e9fdfe841530992be8e2c1e046d9a31db620f0b3644b6436b5  tests/AdminPayrollIdentityFixture.h
aa674e1cfb614402e2c8c67996abe82976273d2555c916772f0aa2955863a165  tests/AdminPayrollIdentityTests.cpp
1808fe8ea16e8f2d253a0b75b365ff15e8416e54f910d94b06894adc3ddc7993  scripts/run_admin_payroll_identity.sh
```

Integrated desktop b5452ec by fast-forward before publication; its PerformanceAuditScenarios.h change is preserved verbatim and is not part of this patch.
