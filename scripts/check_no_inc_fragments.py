#!/usr/bin/env python3
from __future__ import annotations
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
CHECK_ROOTS = [SRC, ROOT / "scripts", ROOT / "cmake"]
TEXT_SUFFIXES = {".cpp", ".h", ".hpp", ".ipp", ".cmake", ".py", ".txt", ".md"}
INCLUDE_INC = re.compile(r'#include\s+["<][^">]+\.inc[">]')

errors: list[str] = []
for p in SRC.rglob("*.inc"):
    errors.append(f"old .inc fragment remains: {p.relative_to(ROOT).as_posix()}")

files = [ROOT / "CMakeLists.txt"]
for root in CHECK_ROOTS:
    if root.exists():
        files.extend(path for path in root.rglob("*") if path.is_file())

for p in files:
    if not p.is_file() or (p.suffix not in TEXT_SUFFIXES and p.name != "CMakeLists.txt"):
        continue
    text = p.read_text(encoding="utf-8", errors="ignore")
    if INCLUDE_INC.search(text):
        errors.append(f"old .inc include reference remains: {p.relative_to(ROOT).as_posix()}")

if errors:
    print("[no_inc_fragments_gate] ERROR: old .inc pseudo-fragments remain", file=sys.stderr)
    for e in errors:
        print(f"  - {e}", file=sys.stderr)
    raise SystemExit(1)
print("[no_inc_fragments_gate] OK: no .inc files or .inc include references remain; P2 fragments use .ipp")
