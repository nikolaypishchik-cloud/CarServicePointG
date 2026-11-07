# DBY-T171 — rollback intent covers migration publication

The previous service took a verified snapshot but armed the restore intent only
when rollback itself began. A process stopping between sealed-file replacements
could therefore reopen a mixed generation. This change arms the existing verified
snapshot intent before staging/publication. Normal startup restores that snapshot.
The live publisher alone opens its new generation through an entry point requiring
the exact pending snapshot; ordinary startup recovery is unchanged. The intent is
removed only after verification, success history and reopening the owner connection.
A crash after success history but before commit still restores the previous profile.

Cloud command: `bash scripts/run_migration_publication_intent.sh`.
Production journal test PASS: a child process arms an intent and exits without stack
unwinding; the next process reads it, accepts same-snapshot rearming, rejects a
conflicting snapshot and changed manifest, then explicitly clears the intent.
Uses production RuntimeRecoveryCoordinator, LegacyImportSafetySupport, AppRuntime
and App, with no substitutes. It does not exercise Windows encryption or restore.

Static command: `python3 -B scripts/run_change_gates.py --static-only`.
Production execution/promote/recovery translation units are also syntax-checked
with GNU C++17 and Qt Core/Sql/Gui/Widgets/Network includes from the retained SDK.

Windows command (NOT RUN in cloud):

```text
python -B scripts/run_qt_test.py build-single-instance-audit/tests/ReportsOrdersTests.exe migrationPublicationCrashRecovery
python -B scripts/run_qt_test.py build-single-instance-audit/tests/ReportsOrdersTests.exe migrationServiceRoundTrip
```

The new crash fixture launches the real `populated-retained-stores` service in a
child process and exits at execute_import (intent armed), execute_reopen (stores
published), or execute_done (success history written, before intent commit).
Parent process validates the exact prior sealed hashes after actual snapshot
recovery, opens the prior target, repeats the same source import, and checks counts
and cleared intent. All data is synthetic and the nested profile is under the
parent's temporary directory. Different cryptographic write boundaries and power
loss remain untested. This is process-interruption recovery, not a power-loss proof.

Prior desktop custody/rollback tests reportedly passed individually; desktop
renamed the Qt-conflicting `slots` identifier to `storePaths`. Those changes were
rebased intact. No SOURCE_SHA256.json, ADR-0002, gates or environment edits here.

DBY-05 stays OPEN: prior sealed custody is not live archived-data routing. A complete
reconciliation must preserve prior archive/registry identities, route archived-only
orders and their children, reserve their keys against incoming data, and validate
all retained rows rather than merely counts. The current publication still uses
incoming archive/registry slots; do not claim this increment closes availability.
