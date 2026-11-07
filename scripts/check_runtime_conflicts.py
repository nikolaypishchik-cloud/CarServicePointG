#!/usr/bin/env python3
from __future__ import annotations

import sys
import os
sys.dont_write_bytecode = True

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
ALLOWLIST_FILE = ROOT / "scripts" / "p3_transitional_allowlist.txt"

PLACEHOLDER_PATTERNS = (
    "styles placeholder",
    "not yet ported",
    "temporary placeholder",
)

SUSPICIOUS_NAME_PARTS = (
    "MissingMethods",
    "FlowSupport",
    "RuntimeSupport",
    "BuildUiSupport",
)

EXEMPT_NAME_PARTS = {
    "MainWindowRuntime.ipp",
    "MainWindowBuildUi.ipp",
}

UNWANTED_ARTIFACT_PARTS = {"__pycache__", ".pytest_cache"}
UNWANTED_SUFFIXES = {".pyc", ".pyo"}
UNWANTED_NAME_PATTERNS = ("_REPORT.txt", "AUDIT_REPORT", "FIX_REPORT", "LINK_FIX")


def load_allowlist() -> set[str]:
    if not ALLOWLIST_FILE.exists():
        return set()
    entries: set[str] = set()
    for raw in ALLOWLIST_FILE.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        entries.add(line.replace("\\", "/"))
    return entries


def iter_files():
    for path in SRC.rglob("*"):
        if path.is_file() and path.suffix.lower() in {".h", ".hpp", ".cpp", ".ipp", ".qss"}:
            yield path


def main() -> int:
    issues: list[str] = []
    allowlist = load_allowlist()

    # Build/test output is intentionally retained under the prescribed build root.
    # Distribution integrity is checked by check_runtime_package.py, not source lint.
    source_entries = []
    for directory, dirs, files in os.walk(ROOT):
        if Path(directory) == ROOT:
            dirs[:] = [d for d in dirs if d not in {"build-single-instance-audit", "dist", ".git"}]
        source_entries.extend(Path(directory) / name for name in dirs + files)
    for path in source_entries:
        rel = path.relative_to(ROOT).as_posix()
        if any(part in UNWANTED_ARTIFACT_PARTS for part in path.parts):
            issues.append(f"Unwanted cache/artifact directory remains in project tree: {rel}")
            continue
        if path.is_file():
            if path.suffix.lower() in UNWANTED_SUFFIXES:
                issues.append(f"Compiled Python artifact remains in project tree: {rel}")
            if any(pattern in path.name for pattern in UNWANTED_NAME_PATTERNS):
                issues.append(f"Unexpected report/fix artifact remains in project tree: {rel}")

    for path in iter_files():
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        low = text.lower()
        for marker in PLACEHOLDER_PATTERNS:
            if marker in low:
                issues.append(f"Placeholder marker remains in {rel}: {marker}")
        name = path.name
        if any(part in name for part in SUSPICIOUS_NAME_PARTS) and name not in EXEMPT_NAME_PARTS:
            if path.suffix.lower() != ".ipp" and rel not in allowlist:
                issues.append(f"Suspicious support artifact naming outside allowed scope: {rel}")

    if issues:
        print("P3 runtime/support conflict gate failed:", file=sys.stderr)
        for issue in issues:
            print(f" - {issue}", file=sys.stderr)
        return 1
    print("P3 runtime/support conflict gate passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
