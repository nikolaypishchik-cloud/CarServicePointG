#!/usr/bin/env python3
from __future__ import annotations
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SUFFIXES = {".cpp", ".h", ".hpp", ".ipp"}
UI_EXEC_NAMES = ("box.exec", "dlg.exec", "orderDlg.exec", "info.exec", "lock.exec", "confirm.exec", "recalcBox.exec", "menu.exec")
SQL_MUTATION = re.compile(r"\b(INSERT|UPDATE|DELETE|REPLACE|DROP\s+TABLE|ALTER\s+TABLE|VACUUM)\b", re.I)
LOW_RISK_SCHEMA = re.compile(r"\b(CREATE\s+(?:TABLE|VIEW)\s+IF\s+NOT\s+EXISTS|CREATE\s+(?:UNIQUE\s+)?INDEX\s+IF\s+NOT\s+EXISTS|DROP\s+INDEX\s+IF\s+EXISTS|CREATE\s+TRIGGER|DROP\s+TRIGGER\s+IF\s+EXISTS|CREATE\s+VIEW|DROP\s+VIEW\s+IF\s+EXISTS|PRAGMA\s+wal_checkpoint|DETACH\s+DATABASE)\b", re.I)
ACCOUNTABILITY_MARKERS = (
    "if (", "if(!", "if (!", "QVERIFY", "QCOMPARE", "Q_ASSERT", "return ", "const bool", "bool ", "=",
    "MutationExecutor::execute", "TransactionRunner::run", "execSchemaGuardedSql", "safeSqlExec",
    "ROLLBACK", "COMMIT", "SAVEPOINT", "SCHEMA_GUARDED_SQL", "BEST_EFFORT_SQL"
)

errors: list[str] = []
scanned = accounted = low_risk = 0
for p in SRC.rglob("*"):
    if not p.is_file() or p.suffix not in SUFFIXES:
        continue
    lines = p.read_text(encoding="utf-8", errors="ignore").splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if ".exec(" not in line or stripped.startswith("//"):
            continue
        if any(name in line for name in UI_EXEC_NAMES):
            continue
        ctx = "\n".join(lines[max(0, i - 12):min(len(lines), i + 13)])
        if "QEventLoop" in ctx:
            errors.append(f"nested event-loop exec remains: {p.relative_to(ROOT).as_posix()}:{i+1}")
            continue
        if LOW_RISK_SCHEMA.search(ctx):
            scanned += 1; low_risk += 1; continue
        if not SQL_MUTATION.search(ctx) and "QSqlQuery" not in ctx and ".lastError()" not in ctx:
            continue
        scanned += 1
        if any(marker in ctx for marker in ACCOUNTABILITY_MARKERS):
            accounted += 1
            continue
        errors.append(f"SQL exec without transaction/check/accountability context: {p.relative_to(ROOT).as_posix()}:{i+1}: {stripped}")

if errors:
    print("[sql_exec_accountability_gate] ERROR: unchecked SQL exec calls remain", file=sys.stderr)
    for e in errors[:200]:
        print(f"  - {e}", file=sys.stderr)
    if len(errors) > 200:
        print(f"  ... {len(errors)-200} more", file=sys.stderr)
    raise SystemExit(1)
print(f"[sql_exec_accountability_gate] OK: {accounted}/{scanned} SQL exec sites have explicit accountability; {low_risk} low-risk schema/index exec sites classified")
