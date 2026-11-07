# DBY-T181 — independent historical versions and catalogue

Increment based on desktop 4af416f. DBY-05 remains IN_PROGRESS; no delivery or real-profile acceptance.

## Behavior

Previous canonical archive/legacy/shard snapshots now have an immutable source-scoped record store in the staged workspace. Identity is SHA-256(origin + NUL + snapshot digest), not an alias inferred from numeric keys or matching fields. Each original table row retains column names, SQLite storage classes, exact scalar values and duplicate multiplicity; original schema is retained as data, never replayed from this store. Prior copies remain outside live shard discovery. Existing version stores are inherited with source/count/payload validation, without re-keying earlier records. Incoming audited source rows are captured too.

When an authoritative active order already exists, its current children, paid/service references and amounts remain authoritative. Different historical child UIDs or values stay in their source version. Current financial queries do not join the historical tables. Catalogue rows and their physical links remain scoped to their own source; matching part IDs do not establish ownership. Supplemental history is read from the version store, not inserted into current accounting. Orphans, conflicting parent ownership/registry identities and contradictory old graphs without an authoritative current graph still stop before publication; these unresolved cases are not feature completion.

The order context menu and catalogue page provide a read-only historical versions viewer with origin/digest/time, source/table selection, exact child UID lookup, pagination and full field viewing. Core checks permissions on every read; master actors are denied. Unknown order-linked tables require security.manage. Generic non-order tables are preserved but are not exposed by the catalogue viewer. No personal database or dist changed.

## Reproduction and evidence

All inputs are synthetic. Actual Qt/SQLite implementation, no substitute SQL engine:

```bash
bash scripts/run_historical_versions.sh > build-single-instance-audit/db-historical-versions/records.log 2>&1
bash scripts/run_migration_archive_workspace.sh > build-single-instance-audit/db-historical-versions/strict-regression.log 2>&1
python -B scripts/run_change_gates.py --static-only > build-single-instance-audit/db-historical-versions/static.log 2>&1
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -fsyntax-only -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" src/core/services/legacyimport/MigrationStagingCoordinator.cpp src/ui/tabs/orders/CurrentOrdersPageBuildUi.cpp src/ui/tabs/catalog/CatalogPageBuildUi.cpp > build-single-instance-audit/db-historical-versions/syntax.log 2>&1
```

All four commands PASS. New portable regression covers historical/current child IDs at identical physical keys, different paid/amount values, source-scoped catalogue with colliding keys, duplicate supplemental rows, BLOBs, repeat, new source version, failed merge rollback, reader restart, inheritance into the next migration, invalid UTF-8 rejection and corrupt historical payload refusal. Current part sum remains 4000, historic version remains 3850; current catalogue remains 40, historical 38.5. Existing strict archive materializer/coverage regression remains PASS. Static-only is not behavioral/release acceptance.

Full ReportsOrdersTests Linux syntax still fails at the pre-existing `openedUrl = {}` QUrl overload in invoiceLinkOpensAttachedFile; original diagnostics retained in windows-test-syntax.log. Production/UI syntax passes. Full Linux application/service execution remains blocked by Windows dependencies; Windows behavior is NOT_RUN on this increment.

## Windows handoff

Run exact Qt data slots:

- `migrationServiceRoundTrip:populated-historical-child-uid`
- `migrationServiceRoundTrip:populated-historical-paid`
- `migrationServiceRoundTrip:populated-historical-service-link`
- `migrationServiceRoundTrip:populated-historical-child-uid-refusal`
- `migrationServiceRoundTrip:populated-legacy-routing`
- `migrationServiceRoundTrip:populated-legacy-conflict`

First three now require successful migration, historical/current reads by exact UID, separate catalogue values, two ordinary startups, unchanged active sums, read-only dialog switching, permission denial and repeat with unchanged historical row count. The new refusal row deliberately removes the source at execute_import, repeats the failure and retains the strict current-child assertions at every restart stage from desktop. Legacy routing checks the original supplemental cents/blob in the version reader and its absence from current accounting tables. Also rerun all migrationServiceRoundTrip and migrationPublicationCrashRecovery rows, then the strict full gate.

Preserve the Windows 59ae6c6 FAIL: after a refused import, startup exposed existing-target-part instead of current-target-part. Desktop owns and supplied 4af416f for this; integrated unchanged production code. This cloud increment does not claim the old Windows result passed, nor weaken the current UID expectation. Full service/crash acceptance RETEST_REQUIRED.

## Remaining limits

Independent versions do not resolve genuinely uncertain ownership, physical-key conflicts while materializing an otherwise missing historical graph, or conflicting old-only authoritative graphs. Normal non-order domain workflows beyond catalogue, arbitrary unlinked table viewing, and physical attachment files require further adapters/acceptance. UTF-16 or malformed text needs a lossless adapter; originals remain retained on refusal. Large real-profile cost/performance and multi-file process-crash behavior require Windows acceptance. Per-row fingerprints detect accidental payload corruption, not a malicious rewrite of all metadata. No absolute reliability or complete DBY-05 claim.
