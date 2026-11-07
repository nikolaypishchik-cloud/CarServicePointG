# DBY-T180 — existing parent must not hide historical child ambiguity

Base: desktop 0338c2f, including Windows full acceptance of the preceding 40f7c61 revision (122/122, 607.66 s). User prioritizes historical child UIDs; latest user-reported remaining is 100%, stop at <=5%.

## Reproduction and temporary safety increment

A canonical historical order and active order have the same stable parent UID. The historical part has UID old-part; the active part has UID current-part at the same physical key, identical fields and the same amount. The old merger returned success and skipped the historical graph. A new synthetic regression expected refusal and failed on the old code (exit 134).

Before accepting an already-present parent, the merger now verifies source and target child UID uniqueness/ownership. It never aliases by number or equal fields. An absent historical child UID, changed paid value, or changed stable service relation requires an explicit decision before publication. A service physical ID change with the same stable service UID remains valid. Current amount edits with stable child identity remain authoritative under the existing contract. Source path, parent UID, child table/UID and reason identify the unresolved history; retained originals are not rewritten.

Snapshot restore previously rewound the migration journal to before its snapshot attachment. Failure handling now reattaches the verified backup after restore and reports journal/provenance write errors rather than silently losing the backup link. The failed run retains snapshot_dir and its source-specific decision note.

This is a TEMPORARY SAFETY INCREMENT, NOT completion of historical versions or DBY-05. Separate version storage, read-by-UID/view support without double billing, and parts_catalog reconciliation are the next authorized work. The user's private rehearsal found an unreconciled parts_catalog; only synthetic fixtures are used here.

## Commands and results

```sh
bash scripts/run_migration_archive_workspace.sh > build-single-instance-audit/db-archive-workspace/child-history-before.log 2>&1
bash scripts/run_migration_archive_workspace.sh > build-single-instance-audit/db-archive-workspace/child-history-after.log 2>&1
bash scripts/run_migration_repeat_journal.sh > build-single-instance-audit/db-archive-workspace/child-history-journal.log 2>&1
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -fsyntax-only -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" src/core/services/legacyimport/MigrationStagingCoordinator.cpp src/core/services/legacyimport/LegacyImportExecutionSupport.cpp > build-single-instance-audit/db-archive-workspace/child-history-syntax.log 2>&1
python3 scripts/run_change_gates.py --static-only > build-single-instance-audit/db-archive-workspace/child-history-static.log 2>&1
```

First command used the new UID fixture with old production code: exit 134. After: PASS. Portable checks cover repeated missing/different UID rejection; no alias by equal keys/fields; unchanged source and active amounts; rollback of an earlier inserted header/state; payment conflict; service linkage by stable UID (including a valid physical-key change); previous coverage regressions. Journal contract test simulates the journal rewind and verifies reattachment, failed status, snapshot path, and retry eligibility. Production syntax and unchanged static gates PASS. Full Windows service behavior is NOT_RUN here.

## Windows slots

- `migrationServiceRoundTrip:populated-historical-child-uid`
- `migrationServiceRoundTrip:populated-historical-paid`
- `migrationServiceRoundTrip:populated-historical-service-link`

Each executes the real service twice and requires refusal before execute_reopen. It checks exact old sealed archive restoration, durable failure/snapshot provenance, normal current UID access after restart, original historical values in the archive, no incoming order publication, and unchanged current sums (1250/3850 cents). No new history UID is substituted for the current one.

Then rerun `migrationServiceRoundTrip` and `migrationPublicationCrashRecovery`. Full Windows acceptance of earlier SHA remains historical evidence, not acceptance of this change. Personal data, dist, SOURCE_SHA256.json and ADR-0002 were not edited.
