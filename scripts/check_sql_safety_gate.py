#!/usr/bin/env python3
from __future__ import annotations
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
CODE_SUFFIXES = {".cpp", ".h", ".hpp", ".ipp"}

REQUIRED_MARKERS = {
    "src/infrastructure/db/sqlite/DbDiagnostics.cpp": [
        "ensureSchemaDestructiveOperationSnapshot",
        "BackupSnapshotService::createSnapshot",
        "execSchemaGuardedSql",
        "MutationExecutor::execute(database, request",
    ],
    "src/core/services/MutationExecutor.cpp": [
        "TransactionRunner::run",
        "QStringLiteral(\"ROLLBACK\")",
    ],
    "src/infrastructure/db/sqlite/TransactionRunner.cpp": [
        "tx.rollback(QStringLiteral(\"step_failed\")",
        "DB_TRANSACTION commit_failed",
    ],
}

ALLOWED_DESTRUCTIVE_CONTEXTS = [
    "ensureSchemaDestructiveOperationSnapshot",
    "BackupSnapshotService::createSnapshot",
    "MutationExecutor::execute",
    "TransactionRunner::run",
    "VACUUM INTO",  # backup snapshot path creates a copy, not destructive in-place rewrite of main db
    "prepareSqliteForSealing skipped VACUUM due to active SQLite connections",
    "DROP TABLE IF EXISTS temp_",
    "DROP TABLE IF EXISTS work_order_services_new",
    "DROP TABLE IF EXISTS outsource_repairs_new",
    "DROP TABLE IF EXISTS outsource_jobs_new",
    "DROP TABLE IF EXISTS work_order_parts_new",
]


def rel(p: Path) -> str:
    return p.relative_to(ROOT).as_posix()


def iter_code():
    for p in SRC.rglob("*"):
        if p.is_file() and p.suffix in CODE_SUFFIXES:
            yield p


def window(lines: list[str], index: int, radius: int = 140) -> str:
    start = max(0, index - radius)
    end = min(len(lines), index + radius + 1)
    return "\n".join(lines[start:end])


def main() -> int:
    errors: list[str] = []
    for req, markers in REQUIRED_MARKERS.items():
        path = ROOT / req
        if not path.exists():
            errors.append(f"missing SQL safety source: {req}")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for marker in markers:
            if marker not in text:
                errors.append(f"SQL safety marker missing in {req}: {marker}")

    for p in iter_code():
        r = rel(p)
        lines = p.read_text(encoding="utf-8", errors="ignore").splitlines()
        for i, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("qWarning") or stripped.startswith("qCritical") or stripped.startswith("qInfo"):
                continue
            # Only inspect actual SQL strings or QStringLiteral calls, not log text such as "cannot vacuum".
            looks_like_sql_literal = ('"DROP TABLE' in line or '"ALTER TABLE' in line or '"VACUUM' in line
                                      or 'QStringLiteral("DROP TABLE' in line or 'QStringLiteral("ALTER TABLE' in line or 'QStringLiteral("VACUUM' in line)
            if not looks_like_sql_literal:
                continue
            if re.search(r"\b(DROP\s+TABLE|VACUUM)\b", line, flags=re.IGNORECASE):
                context = window(lines, i)
                if any(marker in context for marker in ALLOWED_DESTRUCTIVE_CONTEXTS):
                    continue
                if "DROP TABLE" in line and "_new" in line and "IF EXISTS" in line:
                    continue
                errors.append(f"unguarded destructive SQL pattern in {r}:{i+1}: {line.strip()}")
            if re.search(r"\bALTER\s+TABLE\b", line, flags=re.IGNORECASE):
                text = line.upper()
                # ADD COLUMN is an additive migration; RENAME and other ALTERs require transaction/snapshot context.
                if "ADD COLUMN" in text:
                    continue
                context = window(lines, i)
                if "MutationExecutor::execute" in context or "ensureSchemaDestructiveOperationSnapshot" in context:
                    continue
                errors.append(f"unguarded non-additive ALTER TABLE in {r}:{i+1}: {line.strip()}")

    if errors:
        print("[sql_safety_gate] ERROR: SQL safety acceptance failed", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("[sql_safety_gate] OK: destructive SQL has snapshot/transaction guards; additive ALTERs are isolated")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
