#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

CODE_SUFFIXES = {".cpp", ".h", ".hpp", ".cxx", ".cc", ".ipp"}
WAIT_ALLOWED_FILES = {}
REQUIRED_MARKERS = {
    "src/core/io/SafeFileOps.h": "namespace SafeFileOps",
    "src/infrastructure/db/sqlite/DbDiagnostics.cpp": "ensureSchemaDestructiveOperationSnapshot",
}


def iter_code_files():
    for path in SRC.rglob("*"):
        if path.is_file() and path.suffix.lower() in CODE_SUFFIXES:
            yield path


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    errors: list[str] = []

    for req_path, marker in REQUIRED_MARKERS.items():
        path = ROOT / req_path
        if not path.exists():
            errors.append(f"missing required P0 safety file: {req_path}")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if marker not in text:
            errors.append(f"required P0 marker missing in {req_path}: {marker}")

    for path in iter_code_files():
        r = rel(path)
        text = path.read_text(encoding="utf-8", errors="ignore")

        if "processEvents" in text:
            errors.append(f"forbidden UI reentrancy primitive remains: {r}")

        if r != "src/core/io/SafeFileOps.h":
            if re.search(r"\bQFile::remove\s*\(", text):
                errors.append(f"raw QFile::remove outside SafeFileOps: {r}")
            if re.search(r"\.removeRecursively\s*\(", text):
                errors.append(f"raw removeRecursively outside SafeFileOps: {r}")

        if re.search(r"\bwaitForFinished\s*\(", text):
            expected_guard = WAIT_ALLOWED_FILES.get(r)
            if not expected_guard:
                errors.append(f"waitForFinished outside guarded allowlist: {r}")
            elif expected_guard not in text:
                errors.append(f"guard marker missing for bounded waitForFinished in {r}: {expected_guard}")

    diagnostics = (SRC / "infrastructure/db/sqlite/DbDiagnostics.cpp").read_text(encoding="utf-8", errors="ignore")
    destructive_rebuild_tables = ("work_order_services", "outsource_repairs", "outsource_jobs", "work_order_parts")
    for table in destructive_rebuild_tables:
        marker = f'ensureSchemaDestructiveOperationSnapshot(QStringLiteral("{table}")'
        if marker not in diagnostics:
            errors.append(f"destructive schema rebuild lacks pre-migration snapshot guard for {table}")

    if errors:
        print("[p0_safety_gate] ERROR: unsafe P0 patterns detected", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("[p0_safety_gate] OK: no processEvents, no raw destructive file deletes, schema snapshot guards present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
