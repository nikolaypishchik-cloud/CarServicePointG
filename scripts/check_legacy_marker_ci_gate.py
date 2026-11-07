#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys
from project_source_scope import project_paths
from collections import Counter

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADR2 = ROOT / 'docs' / 'architecture' / 'ADR-0002-legacy-fallback-inventory.md'
ALLOWLIST = ROOT / 'scripts' / 'p3_transitional_allowlist.txt'

CONTROLLED_TERMS = ('legacy', 'fallback', 'MissingMethods')
CONTROLLED_RE = re.compile('|'.join(re.escape(term) for term in CONTROLLED_TERMS), re.IGNORECASE)
TEXT_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.cmake', '.txt'}
SOURCE_PREFIXES = ('src/', 'cmake/')
SPECIAL_SOURCE_FILES = {'CMakeLists.txt'}

# P2 p5: budget is intentionally lower than the previous baseline.
MAX_TRANSITIONAL_ALLOWLIST_ENTRIES = 1
FORBIDDEN_REINTRODUCED_ALLOWLIST_PATHS = {
    'src/ui/common/dialogs/OrderDialogFlowSupport.cpp',
    'src/ui/common/dialogs/OrderDialogFlowSupport.h',
}


def rel(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        return path.read_text(encoding='utf-8', errors='ignore')


def marker_counts(text: str) -> Counter[str]:
    counts: Counter[str] = Counter()
    for term in CONTROLLED_TERMS:
        counts[term] = len(re.findall(re.escape(term), text, flags=re.IGNORECASE))
    return counts


def parse_inventory_rows() -> dict[str, dict[str, object]]:
    if not ADR2.exists():
        raise RuntimeError(f'missing ADR inventory: {ADR2}')
    rows: dict[str, dict[str, object]] = {}
    for line in read_text(ADR2).splitlines():
        if not line.startswith('| `'):
            continue
        parts = [part.strip() for part in line.strip().strip('|').split('|')]
        if len(parts) < 4:
            continue
        path_cell, markers_cell, status_cell, reason_cell = parts[:4]
        match = re.search(r'`([^`]+)`', path_cell)
        if not match:
            continue
        file_path = match.group(1)
        marker_budget: dict[str, int] = {}
        controlled = False
        for term in CONTROLLED_TERMS:
            if re.search(rf'{re.escape(term)}\s*:\s*controlled', markers_cell, flags=re.IGNORECASE):
                controlled = True
            m = re.search(rf'{re.escape(term)}\s*[:=]\s*(\d+)', markers_cell, flags=re.IGNORECASE)
            if m:
                marker_budget[term] = int(m.group(1))
        rows[file_path] = {
            'budget': marker_budget,
            'controlled': controlled,
            'status': status_cell,
            'reason': reason_cell,
        }
    return rows


def iter_source_text_files():
    for path in sorted(project_paths(ROOT)):
        if not path.is_file():
            continue
        r = rel(path)
        if r in SPECIAL_SOURCE_FILES:
            yield path
            continue
        if not r.startswith(SOURCE_PREFIXES):
            continue
        if path.suffix.lower() in TEXT_SUFFIXES:
            yield path


def load_allowlist_entries() -> list[str]:
    if not ALLOWLIST.exists():
        return []
    entries: list[str] = []
    for raw in read_text(ALLOWLIST).splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        entries.append(line.replace('\\', '/'))
    return entries


def check_transitional_allowlist_budget() -> list[str]:
    errors: list[str] = []
    entries = load_allowlist_entries()
    if len(entries) > MAX_TRANSITIONAL_ALLOWLIST_ENTRIES:
        errors.append(
            f'transitional allowlist has {len(entries)} entries; maximum allowed after P2 p5 is {MAX_TRANSITIONAL_ALLOWLIST_ENTRIES}'
        )
    for forbidden in FORBIDDEN_REINTRODUCED_ALLOWLIST_PATHS:
        if forbidden in entries:
            errors.append(f'forbidden reintroduced transitional allowlist entry: {forbidden}')
    for entry in entries:
        if not (ROOT / entry).exists():
            errors.append(f'transitional allowlist entry does not exist: {entry}')
    return errors


def check_controlled_marker_inventory() -> list[str]:
    errors: list[str] = []
    inventory = parse_inventory_rows()
    for path in iter_source_text_files():
        r = rel(path)
        text = read_text(path)
        counts = marker_counts(text)
        active_counts = {term: count for term, count in counts.items() if count > 0}
        if not active_counts:
            continue
        row = inventory.get(r)
        if row is None:
            errors.append(f'new controlled marker without ADR-0002 explanation: {r} -> {active_counts}')
            continue
        if len(str(row.get('reason', '')).strip()) < 24:
            errors.append(f'ADR-0002 explanation is too short for controlled marker file: {r}')
        if row.get('controlled'):
            # Governance/test rows may use a controlled marker bucket, but production source rows must carry numeric budgets.
            errors.append(f'production source/cmake file must use numeric marker counts, not controlled bucket: {r}')
            continue
        budget = row.get('budget', {})
        assert isinstance(budget, dict)
        for term, count in active_counts.items():
            allowed = int(budget.get(term, 0))
            if count > allowed:
                errors.append(f'controlled marker budget exceeded for {r}: {term} actual={count}, ADR-0002={allowed}')
    return errors


def main() -> int:
    errors: list[str] = []
    errors.extend(check_transitional_allowlist_budget())
    errors.extend(check_controlled_marker_inventory())
    if errors:
        print('[legacy_marker_ci_gate] ERROR', file=sys.stderr)
        for error in errors:
            print(f'  - {error}', file=sys.stderr)
        return 1
    print('[legacy_marker_ci_gate] OK')
    print(f'  - transitional allowlist entries <= {MAX_TRANSITIONAL_ALLOWLIST_ENTRIES}')
    print('  - new legacy/fallback/MissingMethods source markers require ADR-0002 explanation')
    print('  - existing source marker counts may not grow without ADR update')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
