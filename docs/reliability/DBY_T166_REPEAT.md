# DBY-T166: repeat on an open populated profile

Base b2ed6eeb188558f709efce7d8cfdccc52f3f5206. Synthetic fixtures only.

## Defined semantics

`LegacyImportExecutionSupport::runExecute` calls
`LegacyImportJournalSupport::ensureCanImportFingerprint` after source audit and
policy/preflight, but before `MigrationRunJournal::begin`, snapshot, handoff and
staging. An exact fingerprint with history status `success` returns false with
an explicit already-imported message. No new run is recorded. This is the current
safe rejection contract, not a successful no-op API. It satisfies the bounded
no-duplicate/no-mutation check; no production change was justified by this review.
Different paths/fingerprints, missing history, partial attempts and multi-file
recovery are separate cases and are not certified by this test.

## New real-service regression

`migrationServiceRoundTrip:populated-repeat-open` first uses the existing child
collision fixture and completes import. It opens/prepares the populated runtime
on its owner thread, captures all rows of orders, services, parts and migration
provenance/key/prepared/archive tables, then calls `executeLegacyImport` again on
a worker while the owner's event loop runs. Expected: explicit duplicate refusal,
no snapshot/import/reopen stage, owner connection remains open, exact row-image
and history/log equality, unchanged original source bytes. It checks two orders,
two services, two parts, totals 3850 part cents and 1250 service cents; earlier
fixture checks establish source/existing UIDs and child ownership. The journal
fingerprint must equal the original-source correspondence identity.

Windows: rebuild ReportsOrdersTests, then use the existing strict runner:

```text
python -B scripts/run_qt_test.py build-single-instance-audit/tests/ReportsOrdersTests.exe migrationServiceRoundTrip:populated-repeat-open
python -B scripts/run_qt_test.py build-single-instance-audit/tests/ReportsOrdersTests.exe migrationServiceRoundTrip
```

Expected ten scenario rows after this change. Full service execution in cloud is
NOT RUN; the published Debian SDK still lacks the separately requested
Qt6CorePrivate package. No production fix or full-service PASS is claimed.

## Actual cloud validation

```sh
bash scripts/run_migration_repeat_journal.sh
python3 -B scripts/run_change_gates.py --static-only
```

Both exit 0. The focused test compiles the real LegacyImportJournalSupport,
AppRuntime and App implementations (no stubs). It creates an isolated Safety
profile, begins/completes a synthetic journal entry, rejects the same fingerprint
twice, and checks byte equality of history and operation log; a different
fingerprint remains allowed. It does NOT execute the migration service or prove
DB preservation. The initial harness missed Qt test mode and stopped at its path
isolation assertion; that harness setup was corrected before journal writes.

Qt moc of ReportsOrdersTests succeeded. A direct GNU C++ syntax check of the full
translation unit was attempted with Qt Core/Gui/Widgets/Sql/Test/Network/
PrintSupport includes and the generated moc directory. It failed on pre-existing
`openedUrl = {}` QUrl overload ambiguity and incomplete QScreen usage in unrelated
UI slots. No gate, SDK or unrelated UI code was changed to bypass these failures.
Logs: build-single-instance-audit/db-repeat/{journal,static,service-syntax}.log.

User-reported Windows evidence for the preceding patch: child collision 3/3,
all nine service rows 11/11 including setup/cleanup, zero failures/skips. Full
Windows gate was running when this step started; its result is not yet known here.
