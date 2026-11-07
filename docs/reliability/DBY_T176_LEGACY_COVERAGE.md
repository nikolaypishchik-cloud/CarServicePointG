# DBY-T176 — independent canonical legacy and archive coverage

Base: 9c08341. User reported 11% weekly remaining, stop at <=5%. Desktop is diagnosing the Windows retained-file open failure on 9c08341; cloud does not duplicate that fix or edit concurrent QSettings isolation/production snapshot ownership work.

## Increment

Staging now takes a consistent retained snapshot of both primary archive and independent legacy stores, using runtime VACUUM INTO or sealed export, and routes either prior runtime path to its retained snapshot. Both canonical sources participate in materialization before incoming key planning and publication. Old files are retained, not overwritten in place. Noncanonical legacy sources still require a separate adapter and fail explicitly.

Archive coverage is reconciled inside the same workspace transaction. It rejects orphan service/part rows, conflicting child owners, dangling service-master links, invalid part-service/service-parent links, and recorded registry identity disagreement. A populated graph field absent from the working copy is an explicit unresolved field, not a silent omission.

Additional order/child-linked tables without a destination table are copied with their original CREATE TABLE constraints and all explicit columns, then compared by native values, binary collation, storage class and duplicate multiplicity. Unknown executable schema or explicit indexes require review rather than an incomplete clone. Existing historical tables must already represent the source tuples exactly; conflicting historical values are never updated or replaced. Nonempty supplemental order_uid references must resolve in both source and target; declared destination foreign keys are checked. The inventory distinction is explicit: non-order domains remain in the immutable original and are NOT claimed as live migrated data.

## Evidence

```sh
bash scripts/run_migration_archive_workspace.sh > build-single-instance-audit/db-archive-workspace/coverage.log 2>&1
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -fsyntax-only -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" src/core/services/legacyimport/MigrationStagingCoordinator.cpp > build-single-instance-audit/db-archive-workspace/coverage-syntax.log 2>&1
python3 scripts/run_change_gates.py --static-only > build-single-instance-audit/db-archive-workspace/coverage-static.log 2>&1
```

Portable Qt/SQLite tests PASS, including earlier graph/cents/blob/repeat/rollback cases plus unknown BLOB column preservation, supplemental history cents 7250 and blob 0002FF, conflicting history refusal without replacing 9999, unrepresented field refusal, unresolved supplemental owner, orphan child, conflicting service reference and mismatched registry UID. Production syntax PASS. Static gate initially BLOCKED by ADR-0002 inventory: MigrationArchiveWorkspace.h legacy=1; MigrationStagingCoordinator.cpp legacy=17 instead of 8. Desktop owns concurrent inventory edits; cloud has not changed the gate, ADR-0002 or SOURCE_SHA256.json. Final integrated static outcome must be recorded before publication.

## Windows slots

- `migrationServiceRoundTrip:populated-legacy-routing`: an independent canonical legacy order (UID legacy-only-order, physical key 40) is absent from old active/shard data. Its children, unknown BLOB field and supplemental historical table are migrated alongside the existing archived order and incoming source. Check unknown BLOB in the published workspace before startup, then ordinary startup and stable known UID/child/cents access plus supplemental history.
- `migrationServiceRoundTrip:populated-legacy-conflict`: the independent legacy file claims the same old UID with different historical part amount. Reject before execute_reopen and restore the exact prior sealed legacy file.
- Regression: `migrationServiceRoundTrip`, `migrationPublicationCrashRecovery`.

All new Windows execution is NOT_RUN here. The preceding 9c08341 archive-routing and retained-stores scenarios failed at execute_import with "Previous archive reconciliation: unable to open database file Error opening database"; desktop owns investigation/fix. This is not accepted service behavior.

## Open limitations

DBY-05 remains OPEN. Disjoint historical rows in an existing supplemental table still require a reviewed merge adapter rather than implicit append; structural key conflicts among old archives and noncanonical legacy need durable mapping. Non-order tables and arbitrary executable schema are not proven live-migrated. Ordinary hydration uses a known field list: arbitrary extra graph fields are proven preserved during migration and in the retained originals, but NOT proven available after archive maintenance/rehydration. Do not infer full field acceptance from the new known-UID startup test. Full Windows gate, payment/inventory/file-content reconciliation and real-profile acceptance remain pending. Dist and personal profiles untouched.

## Integration on desktop 8cb7c58

Rebased on 8cb7c5801af722e93404cf7ca4d5641222f73b5e (including 80a7289). Both archive and legacy roles return the verified short staging copy for SQLite reconciliation; the long-term copy is only retained and hashed. Desktop settings-backend isolation and snapshot ownership checks are preserved. User reports the long-TEMP archive routing case and all crash boundaries after bootstrapIdentityConflictsStopMigration now pass on desktop; new legacy rows still need Windows execution.

Repeated commands above using integrated.log, integrated-syntax.log and integrated-static.log: Qt/SQLite PASS and production syntax PASS. Static result remains BLOCKED solely by the same ADR-0002 marker inventory, not behavioral acceptance. Per user's explicit request the prepared increment is published for desktop integration; it is not a full-gate accepted build. No gate or protected metadata was changed. Latest reported remaining 9%, stop threshold <=5%. Only one further bounded test step is authorized before another quota reading.
