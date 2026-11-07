# DBY-T174 — archived graphs in the migration workspace

Based on 6791b8c, fast-forwarded desktop cc2088b before publication. Desktop reported the 9e4ca8f crash boundaries PASS (5/5 including setup/cleanup, 18.217 s) and migration service PASS (15/15, 41.091 s); these precede this production change.

## Change

Before capturing the destination baseline and planning incoming keys, staging enumerates the previous primary archive, canonical archive shards and local registry archive references. The previous primary is copied consistently with VACUUM INTO (or decrypted if runtime is absent), SHA-256 checked, and retained under the protected security/migration_retained_archives directory. This is deliberately outside live shard discovery, avoiding a second physical routing source for the same UID after ordinary archive maintenance. Existing snapshot inventory covers this directory for rollback/crash recovery.

Missing historical order graphs are materialized into the isolated working copy using the existing complete graph copier and its bidirectional field/storage-class/multiplicity verifier. This covers headers, services, parts, service-master links, supported attachment rows and outsource rows. Existing working copies remain authoritative for newer edits. Newly materialized duplicate archive copies must agree exactly; conflicting identities, child-key collisions or missing service masters refuse publication rather than silently overwriting an owner or inventing a master. Workspace routing state for these complete graphs becomes native; ordinary archive maintenance can subsequently move them to the existing canonical shard path. Incoming source collision planning then sees their reserved keys and children. The target consistency gate is unchanged.

## Cloud evidence

Commands from repository root:

```sh
bash scripts/run_migration_archive_workspace.sh > build-single-instance-audit/db-archive-workspace/materialize.log 2>&1
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -fsyntax-only -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" src/core/services/legacyimport/MigrationStagingCoordinator.cpp > build-single-instance-audit/db-archive-workspace/syntax.log 2>&1
python3 scripts/run_change_gates.py --static-only > build-single-instance-audit/db-archive-workspace/static.log 2>&1
```

Portable actual Qt/SQLite implementation PASS: archived-only graph appears in the target; stable order/service/part UID and owner links; amounts 1250/3850; exact attachment blob 0001FF; unchanged source file; repeat preserves a newer working amount 4000; conflicting archived copy rejects; a child-key insertion failure rolls back the new header; conflicting header identity rejects. Initial helper test failed because SQLite inherited READONLY into its attached destination; corrected by using a connection whose main schema is only read by the copier. First syntax command had an incorrect include prefix; corrected command above PASS. These development failures are not service acceptance evidence.

## Windows handoff

New slot: `migrationServiceRoundTrip:populated-archive-routing`.
It first archives the existing synthetic order through the production archive operation and proves the active header/parts are absent. It also places the same canonical archive into the primary archive slot. The real migration imports a distinct order with colliding keys. Two normal startup sequences run migration checks, archive maintenance and registry refresh, then open old/new UIDs through the normal availability API. Existing checks cover identities, owner links and amounts; the added graph check covers the old service/part UIDs and cents after startup.

Then run `migrationServiceRoundTrip` and `migrationPublicationCrashRecovery`. All are NOT_RUN on this cloud revision; Windows execution is required. Full Linux runner remains blocked by Qt private package discovery and Windows crypto/runtime dependencies; the focused executable is not a substitute for the service or full gate.

## Remaining limits

DBY-05 remains OPEN. The new service/ordinary-startup path awaits Windows. Ambiguous historical identities and conflicting child IDs between prior archives still require reconciliation rather than automatic remapping in this increment. Missing canonical identity columns or absent required graph tables reject migration. An independent legacy-only database and arbitrary additional domain tables are still only covered by retained custody, not proven live routing. Full payment/inventory/file-content acceptance and real-profile acceptance remain pending. No personal databases, dist, gates, SOURCE_SHA256.json or ADR-0002 were edited by cloud.

The user corrected the current 10080-minute remaining value to 13%; the stop threshold remains <=5%.
