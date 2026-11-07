# DB-YEAR-212 cloud checkpoint

See CLOUD_HANDOFF.md for the latest validated fixture and next action. The private Windows workspace retains the detailed historical logs. Full real-profile migration and release acceptance are pending.

## DBY-T161 — Windows migration service validation, 2026-11-07

The strict Qt runner on an isolated profile passed the inconsistent parent refusal in 1673 ms (3 passed including setup and cleanup). The complete migrationServiceRoundTrip passed all eight scenario rows in 18723 ms (10 passed including setup and cleanup, zero failed or skipped). Binary ReportsOrdersTests.exe SHA-256 E29870CD122F8D12C9F705C30D882C14EB008CF42A51C13A44DC567C10CD1EBD. Private evidence is retained under build-single-instance-audit/db-year-212/cloud-handoff-validation-01. This supersedes the outcome of the collision fixture on the tested binary and preserves its historical failure. Cloud static gates passed on ca73056. The full Windows build and registered suite have a separate log, full-gates.log. Release and real-profile acceptance remain pending.
