# DBY-T172 — reserve archived routing keys

Base: publication recovery 9e4ca8ffc198b3dbbb9c2909ee50293409c54151; desktop metadata 05cbae41dc1d309c3631b63743246de1b0c84805 fast-forwarded before saving. SOURCE_SHA256.json and ADR-0002 were not edited by cloud.

The staging planner previously reserved only main.work_orders keys. An unhydrated order in the local order_registry could therefore lose its physical routing key to the incoming source. The planner now reserves the union of both tables, deduplicates mirrored active keys, and allocates above both destination and source maxima. Invalid registry schema and exhausted integer key space fail explicitly.

Synthetic real Qt/SQLite planner/projection coverage: mirrored active key 7 and archive-only high key 40 force source 7 to 41; removing active 7 still reserves registry 7. Projected child owners retain source UID and cents 3850/125; old registry UID remains unchanged. Maximum registry key 2147483647 refuses allocation. Existing child projection/import/correspondence scenarios remain passing.

Exact commands (repository root):

```sh
bash scripts/run_migration_child_projection.sh > build-single-instance-audit/db-child-collision/registry-before.log 2>&1
bash scripts/run_migration_child_projection.sh > build-single-instance-audit/db-child-collision/registry-after.log 2>&1
python3 scripts/run_change_gates.py --static-only > build-single-instance-audit/db-child-collision/registry-static.log 2>&1
```

First command ran with the new fixture and old planner: exit 134 (assertion failure). Second ran with the fix: PASS, exit 0. Static gates PASS before desktop metadata fast-forward; no gate changes. Full application build/service execution not claimed on Linux. Windows slot `migrationReservesArchivedOrderKeys` is added but NOT_RUN; it also checks malformed registry rejection. Rerun `migrationServiceRoundTrip` after this planner change. Publication crash slots remain separately pending Windows validation.

This is a routing prerequisite, not DBY-05 acceptance. Prior sealed receipt custody does not yet establish availability of archived-only order graphs. Next work: reconcile old archive/legacy/registry sources into the staged generation, preserve UID and full dependent graphs, verify runtime routing and crash/rollback/repeat through the real service. Do not merely copy historical files and call them accessible. Existing target consistency checks must remain strict.

Stopped at user's freshly reported 5% weekly remaining (threshold <=5%). No further code changes or test runs after that reading; only saving this existing increment and checkpoint. Dist and personal profiles untouched.
