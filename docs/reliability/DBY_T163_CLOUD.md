# DBY-T163: populated destination child keys

Base: a076e54d (resolve the full parent with `git rev-parse HEAD^` on this patch commit).
Only synthetic fixtures were used. No private Windows evidence was uploaded.

The staging planner previously remapped only order keys. A source service/part
could retain an ID already used by the destination, causing primary import to
fail. The patch allocates IDs above both source and target maxima, projects
service parent links, part service links, service-master links and outsource
service links, and persists the mapping for final original-source correspondence.
Source bytes and canonical UIDs are not rewritten. Unknown tables/triggers still
block projection; no gate exceptions were added.

## Actual cloud commands

From `/workspace/CarServicePointG`:

```sh
git fetch --no-tags origin refs/heads/codex/db-optimization-2.1.2:refs/remotes/origin/codex/db-optimization-2.1.2
git merge --ff-only origin/codex/db-optimization-2.1.2
git merge-base --is-ancestor a076e54 HEAD
bash scripts/run_migration_child_projection.sh
python3 -B scripts/run_change_gates.py --static-only
export PATH=/workspace/carservice-tools/bin:$PATH
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
cmake -S . -B build-single-instance-audit -G Ninja -DCMAKE_PREFIX_PATH=/workspace/qt-sdk/root/usr -DBUILD_TESTING=ON -DCS_ENFORCE_QUALITY_GATES=ON -DCS_REMEDIATION_TEST_BUILD=OFF
```

Focused runner: PASS, exit 0. Uses real DbFacadeSupport,
DbCanonicalRewritePrimaryImportSupport, DbCanonicalRewriteSupport and
LegacyOrderChildMapper, plus the production staging planner/projector header.
No alternate implementation or migration mocks. GNU 14.2 / Qt 6.8.2 / QSQLITE.
The test imports source order 7 into a target already containing order 7 and
children 20/21. It checks remapped keys, original UIDs, owner links, source amounts
125 and 3850, destination part sum 1500, two projections from the same mapping,
and twice recording correspondence with exactly four records. Ancillary import,
full service repeat and multi-file publication are outside this focused runner.

Before the patch, the same projection fixture linked against
`git show a076e54:src/infrastructure/db/sqlite/MigrationOrderKeys.h` failed
at the expected remapped child IDs (exit 134, `Projection check failed`). The
prepatch fixture and executable remain in `/tmp/child-before`; core dumps were
disabled. The patched fixture additionally tests primary import and provenance.
Build logs are in `build-single-instance-audit/db-child-collision/`.

Static acceptance: PASS, 34 Python self-tests. Correspondence translation unit
also passed `g++ -fsyntax-only` with the same Qt includes as the focused runner.
Full CMake configure: exit 1, missing Qt6CorePrivateConfig.cmake in the Debian SDK.
This is SDK discovery, not evidence of a Windows API compiler failure.

## Windows handoff

Rebuild with unchanged mandatory gates and run the strict Qt runner first with
`migrationServiceRoundTrip:populated-child-key-collision`, then all nine rows.
Check full-service repetition separately; repeated projection/correspondence is
not proof of full migration idempotency. Run the complete acceptance suite on the
new binary with the existing approved invoice fixtures. Historical 8-row and
122/122 passes do not certify this patch. Do not change dist before acceptance.

## Short independent boundary checks

After publishing 2121c1c90abb890bb289300b7d169ccfcd40173b, the focused runner
also exercised child-only collisions with an empty order-key map: source order
7 stayed 7 while its service/part IDs became 22 and their link stayed intact.
An existing maximum service ID 2147483647 caused an explicit allocation failure;
the synthetic transaction was rolled back. Command:
`bash scripts/run_migration_child_projection.sh` (exit 0).
This does not close archive-retention or full-service-repeat acceptance.
