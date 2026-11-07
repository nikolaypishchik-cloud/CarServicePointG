# Backup / Recovery / Restore acceptance checklist p11-p14

This document is a technical guardrail for the data protection layer.
It records the implemented acceptance criteria for the professional autobackup, recovery draft and restore flow.

## p11 — Behavioral regression coverage

Covered scenarios:
- custom backup folder safety: snapshot temp files are created under the configured backup root `.tmp` folder and only renamed after validation;
- pending backup on shutdown: pending events are persisted to `pending_backup_journal.json` and replayed as `startup-pending` before cleanup;
- restore while backup is running: `DataProtectionCoordinator` serializes backup, cleanup, recovery autosave and restore operations;
- restore failure rollback: restore replacement and reopen failures switch to `RestoreRollback` and attempt rollback DB restore;
- orphan recovery drafts: manifest entries are merged with actual JSON file scan, so orphan files are visible;
- recovery conflict: changed committed version marks a draft as conflict and prevents automatic apply;
- clean shutdown autosave failure: session remains recoverable and is not marked clean;
- cleanup retention: at least five newest valid snapshots, daily keeper snapshots and emergency snapshots are protected;
- busy/locked DB: sqlite backup uses timeout, retry budget and degraded/failure reporting;
- UI status: user-facing status labels, protection modes and manual actions remain wired.

## p12 — Acceptance criteria

Autobackup:
- starts after committed DB changes;
- uses debounce/coalescing;
- does not run during restore/migration/DB close-reopen;
- uses validated snapshot + manifest append flow;
- persists pending shutdown backup marker when flush cannot finish.

Recovery:
- protects unsaved UI changes separately from DB snapshots;
- writes drafts through safe JSON files;
- detects orphan draft files;
- refuses clean shutdown when draft autosave fails;
- detects conflicts before automatic recovery apply.

Restore:
- acquires exclusive data-protection gate;
- performs preflight validation;
- creates emergency/rollback protection;
- rolls back after replace/reopen failure;
- logs restart-required state after success.

UI:
- section name: “Автобекап та відновлення”;
- recommended mode: “Авто — рекомендовано”;
- advanced technical settings are collapsed under “Додатково”;
- cleanup actions have confirmation dialogs;
- status block displays current protection, pending backup, paths, size, queue, engine and errors.

## p13 — Implementation order applied

1. Safe point fixes: temp path, status formatting, orphan drafts, pending journal, restore lock.
2. Coordinator: central `DataProtectionCoordinator` gate for conflicting operations.
3. Restore transaction: preflight, emergency backup, rollback, reopen, restart-required audit.
4. UI refinement: protection modes, status, manual actions, collapsed advanced parameters.
5. Tests: acceptance/regression guard coverage added in `BackupRecoveryAcceptanceTests`.

## p14 — Main implementation principle

The implementation follows this model:

`DB commit -> backup event -> coordinator gate -> coalesced snapshot -> validation -> manifest append`

Restore and recovery drafts are separate flows:
- restore is exclusive and transactional;
- recovery drafts never replace DB snapshots;
- any restore failure after DB replacement must try rollback;
- no snapshot is visible in the manifest before successful validation.
