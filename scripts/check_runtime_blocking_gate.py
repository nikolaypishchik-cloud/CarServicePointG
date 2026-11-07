#!/usr/bin/env python3
from __future__ import annotations
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SUFFIXES = {".cpp", ".h", ".hpp", ".cxx", ".cc", ".inc", ".ipp"}
FORBIDDEN = {
    "waitForFinished": re.compile(r"\bwaitForFinished\s*\("),
    "QThread::msleep": re.compile(r"\bQThread::msleep\s*\("),
    "processEvents": re.compile(r"\bprocessEvents\s*\("),
    "QEventLoop": re.compile(r"\bQEventLoop\b"),
    "nested event-loop exec": re.compile(r"\.exec\s*\(\s*QEventLoop::"),
}

def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()

def iter_code():
    for path in SRC.rglob("*"):
        if path.is_file() and path.suffix in SUFFIXES:
            yield path

def main() -> int:
    errors: list[str] = []
    for path in iter_code():
        text = path.read_text(encoding="utf-8", errors="ignore")
        for name, rx in FORBIDDEN.items():
            for match in rx.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{name} remains in runtime source: {rel(path)}:{line}")
    if errors:
        print("[runtime_blocking_gate] ERROR: blocking/reentrant runtime primitives detected", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("[runtime_blocking_gate] OK: no waitForFinished, QThread::msleep, processEvents, or nested QEventLoop in src")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
