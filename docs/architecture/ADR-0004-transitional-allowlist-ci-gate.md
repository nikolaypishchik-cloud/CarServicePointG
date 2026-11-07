# ADR-0004 — Transitional allowlist reduction and legacy-marker CI gate

## Status
Accepted — P2 **“Мертві хвости, legacy і fallback” п5–п6**.

## Scope
This pass controls two risks left after the inventory and cleanup-gate stages:

1. transitional allowlists must shrink step-by-step and must not silently grow;
2. new `legacy`, `fallback`, or `MissingMethods` markers in production source/build files must be blocked unless the file has an explicit ADR-0002 explanation.

## P2 p5 decision — allowlist reduction
The runtime transitional allowlist `scripts/p3_transitional_allowlist.txt` was reduced from **3 entries to 1 entry**.

Removed from the transitional allowlist:
- `src/ui/common/dialogs/OrderDialogFlowSupport.cpp`
- `src/ui/common/dialogs/OrderDialogFlowSupport.h`

Replacement:
- the module was renamed to `src/ui/common/dialogs/OrderDialogFlowController.cpp`;
- the namespace was renamed to `OrderDialogFlowController`;
- call sites were updated without changing business logic.

Remaining transitional entry:
- `src/ui/tabs/orders/WorkPartsMissingMethods.cpp`

That entry stays temporarily because it belongs to the large WorkParts dialog compatibility surface and should be renamed only in a dedicated compile-checked pass.

## P2 p6 decision — CI check
Added `scripts/check_legacy_marker_ci_gate.py`.

The gate fails when:
- `scripts/p3_transitional_allowlist.txt` exceeds the current budget of **1 entry**;
- removed transitional entries are reintroduced;
- an allowlist entry points to a missing file;
- a production `src/` or `cmake/` file introduces `legacy`, `fallback`, or `MissingMethods` without an ADR-0002 row;
- a production file increases an existing controlled marker count without updating the ADR-0002 explanation/budget.

## CI wiring
Added `.github/workflows/code-quality-gates.yml` so the source-only gates can run in CI without Qt/Windows build tools.

The workflow runs:
- `scripts/check_architecture.py`;
- `scripts/check_legacy_tail_inventory.py`;
- `scripts/check_legacy_tail_cleanup.py`;
- `scripts/check_legacy_marker_ci_gate.py`.

## Decision
The allowlist is now budgeted and cannot grow silently. New legacy/fallback/MissingMethods markers are blocked unless they are intentionally documented.
