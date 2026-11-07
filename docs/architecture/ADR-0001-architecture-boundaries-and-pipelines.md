# ADR-0001: Architecture boundaries and critical pipelines

Status: accepted

## Context

CarServicePointG is being stabilized around explicit layer ownership. The immediate risks are hidden DB calls from UI, synchronous external process waits, service/repository UI side effects, and fragile update/backup handoff behavior.

## Decision

### Layer rule

- `src/ui` owns widgets, views, delegates, and view-controller glue only.
- UI must call ViewModel/ApplicationService/facade APIs; it must not call `Database::db()`, `QSqlQuery`, or repositories directly.
- `src/core/services` owns use-case orchestration and returns structured success/error results to UI.
- `src/core/repository` owns persistence only. It must not show UI, invalidate UI cache, emit presentation notifications, or trigger post-commit recalculation as a side effect.
- `src/infrastructure` owns low-level adapters such as SQLite/file/process implementations.

### Transaction boundary

All write mutations must enter through a single service/mutation boundary. File operations are staged outside the DB transaction. Metadata is committed only after the staged file operation succeeds. On failure, the dirty state remains dirty and the service returns a structured error.

### Import pipeline

Invoice import is treated as an asynchronous job: prepare request, run parser worker, stream stdout/stderr incrementally, produce a structured DTO, then insert rows through a ViewModel/service API. UI must not call `QProcess::waitForFinished()` or write imported rows directly into table widgets.

### Update pipeline

The update flow is atomic: download/stage package, verify SHA256, start helper, keep DB open until helper start is confirmed, stop app, backup current installation, replace files, verify installed exe/resources, and rollback on failure. `.cmd` and PowerShell paths must use literal/quoted escaping and be safe for Cyrillic, spaces, parentheses, `%`, `!`, and other special path characters.

### Backup/recovery pipeline

Backup is a separate recovery pipeline with explicit state: preflight, staged copy, verification, activation, and rollback/recovery metadata. UI can initiate backup/recovery but must not perform the low-level copy or database mutation itself.

## Enforcement

`architecture_gate` runs `scripts/check_architecture.py` as a build target and as a dependency of `CarServiceLib`, `CarService`, and release archive creation. The gate blocks:

- `Database::db()` in `src/ui`;
- Qt SQL primitives in `src/ui`;
- `waitForFinished()` in `src/ui`;
- direct repository includes from `src/ui`;
- UI/cache/notification side effects in repositories;
- `QMessageBox` in services/repositories.
