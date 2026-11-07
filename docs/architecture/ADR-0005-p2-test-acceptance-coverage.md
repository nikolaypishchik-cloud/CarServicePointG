# ADR-0005: P2 test and acceptance coverage gate

## Status
Accepted

## Context
The P2 test/acceptance stage requires explicit coverage for unit-level services, integration-level flows, UI smoke scenarios, and final readiness criteria before later cleanup/refactoring continues.

## Decision
The repository contains a dedicated P2 acceptance layer:

- `tests/unit/P2AcceptanceUnitTests.cpp`
- `tests/integration/P2AcceptanceIntegrationTests.cpp`
- `tests/smoke/P2AcceptanceSmokeTests.cpp`
- `tests/acceptance/P2AcceptanceCriteriaTests.cpp`
- `scripts/check_p2_acceptance_tests.py`

The gate verifies that the acceptance tests are present, registered in CMake, and cover the required P2 checklist.

## Unit coverage requirements
The unit acceptance test guards the following areas:

1. `PartDatePolicyService`
2. `PartRowPayloadBuilderService`
3. import parsers: `ElitParser`, `AvtonovaParser`, `GenericXlsParser`, `GenericPdfParser`
4. WorkParts dirty-state
5. `OrderAttachmentService`
6. `UpdateService` hash/path logic

## Integration coverage requirements
The integration acceptance test guards the following flows:

1. SQLite transaction rollback
2. imported parts edit/save/reopen
3. invoice import without Excel
4. backup/recovery after save
5. update package apply/rollback

## UI smoke coverage requirements
The UI smoke acceptance test guards the following P2 p3 scenarios:

1. Excel-like navigation
2. editing imported date
3. editing imported price/name/brand
4. rapid Enter/Tab/Arrow
5. close dialog during import
6. open PartsPage during refresh

## Readiness criteria requirements
The readiness acceptance test guards the following P2 p4 criteria:

1. no direct SQL/`Database::db()` in `src/ui`
2. import does not block the UI thread
3. attachment file operations are staged outside DB transactions
4. attachment failure keeps dirty-state and reports failure
5. imported date editing remains crash-safe
6. imported parts are transferred into the order-parts view model
7. Enter/Tab commits through one canonical editor path
8. queued editor actions are cancelled after refresh/sort revision changes
9. update applies atomically or rolls back
10. P0 regression pack remains registered for the 20-repeat stability run before release

## Enforcement
`p2_acceptance_tests_gate` is wired into the CMake build graph next to the existing architecture/logging/legacy gates. The CI workflow also runs the gate directly.

## Notes
These tests are intentionally source-level acceptance guards in this archive because the current execution environment does not include the Windows Qt toolchain needed to compile and run the full application test suite. They are designed to fail fast if the required implementation or test registration disappears.
