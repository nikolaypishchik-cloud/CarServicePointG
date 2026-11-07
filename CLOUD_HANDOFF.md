# CarServicePG 2.1.2 database migration handoff

Continue docs/reliability/DB_YEAR_212.md with PLAN.md, STATUS.md and MATRIX.csv. The latest code includes work beyond the last written milestone. The isolated internal-key collision scenario passed with source and destination both using physical key 7 and distinct canonical UIDs. Original order, work and part ownership and provenance were checked after reopening. A subsequent change added a refusal test for inconsistent parent references. Its build was started locally before the goal was paused. Verify the final build and run the full migration service scenarios before making further changes.

This is a public, sanitized source snapshot for the DB task. It is not a complete build distribution. Resources, binary and document fixtures, private data, captures, build directories and desktop distribution are excluded. Restore reviewed build resources separately for a clean full build. Windows-specific DPAPI, EFS, ACL, process recovery and installer acceptance require a suitable Windows host. Use isolated test profiles. Do not migrate the working profile before acceptance of an independently reconciled copy.

The latest owner-conflict test build linked locally. Windows validation now passes this refusal case and all eight migrationServiceRoundTrip rows; see DBY-T161 in docs/reliability/STATUS.md. Continue the remaining DB_YEAR_212 acceptance criteria. Preserve at least 20% weekly Codex usage and stop at 21% remaining; check the 10080-minute account window before substantive work. If usage cannot be verified, stop. Keep historical private evidence on its Windows host.

The public matrix contains scenario identifiers, names and historical status. Local evidence and fixture details are retained privately.
