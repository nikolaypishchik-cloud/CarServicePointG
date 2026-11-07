#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys
from project_source_scope import project_paths
from collections import Counter

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[1]
INVENTORY = ROOT / 'docs' / 'architecture' / 'ADR-0002-legacy-fallback-inventory.md'
TERMS = ('legacy', 'fallback', 'MissingMethods', 'transitional', 'allowlist')
TERM_RE = re.compile('|'.join(re.escape(term) for term in TERMS), re.IGNORECASE)
TEXT_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.py', '.cmake', '.txt', '.md', '.qrc', '.ui', '.iss', '.ps1', '.cmd', '.bat', '.json', '.yml', '.yaml'}
EXCLUDED = {
    'SHA256SUMS.txt',
    'docs/architecture/ADR-0002-legacy-fallback-inventory.md',
}
EXCLUDED_PREFIXES = (
    '.github/',
    'build/',
    'build-crm-transfer-check/',
    'build-release/',
    'build-single-instance-audit/',
    'dist/',
    'logs/',
    'Output/',
    'docs/architecture/ADR-',
)


def is_text_file(path: pathlib.Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    if rel in EXCLUDED:
        return False
    if any(rel.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
        return False
    return path.name == 'CMakeLists.txt' or path.suffix.lower() in TEXT_SUFFIXES


def classify(rel: str, text: str) -> str:
    if rel.startswith('tests/'):
        return 'test-only'
    if rel in {'scripts/p3_transitional_allowlist.txt', 'scripts/p7_naming_allowlist.txt'}:
        return 'deprecated but used'
    if 'WorkPartsMissingMethods' in rel:
        return 'deprecated but used'
    if 'P2 architecture migration: implementation module converted from legacy .ipp' in text:
        return 'deprecated but used'
    if rel.startswith('resources/python_vendor/') or rel.startswith('resources/windows/ocr-layer/python/site-packages/'):
        return 'active'
    if rel.startswith(('resources/windows/ocr-layer/', 'scripts/', 'cmake/', 'docs/', 'src/', 'server/', 'web-booking/', 'web-crm/')) or rel == 'CMakeLists.txt':
        return 'active'
    return 'delete candidate'


def main() -> int:
    if not INVENTORY.exists():
        print(f'[legacy_tail_inventory] ERROR: missing inventory doc: {INVENTORY}', file=sys.stderr)
        return 1
    inventory_text = INVENTORY.read_text(encoding='utf-8')
    required_phrases = [
        'Status legend',
        'Inventory table',
        'deprecated but used',
        'delete candidate',
        'test-only',
        'No runtime code is deleted in this pass',
    ]
    missing = [phrase for phrase in required_phrases if phrase not in inventory_text]
    if missing:
        for phrase in missing:
            print(f'[legacy_tail_inventory] ERROR: inventory doc missing phrase: {phrase}', file=sys.stderr)
        return 1

    status_counts: Counter[str] = Counter()
    unlisted: list[str] = []
    for path in sorted(project_paths(ROOT)):
        if not path.is_file() or not is_text_file(path):
            continue
        rel = path.relative_to(ROOT).as_posix()
        try:
            text = path.read_text(encoding='utf-8')
        except UnicodeDecodeError:
            text = path.read_text(encoding='utf-8', errors='ignore')
        if not TERM_RE.search(text):
            continue
        status = classify(rel, text)
        status_counts[status] += 1
        if f'`{rel}`' not in inventory_text:
            unlisted.append(f'{rel} [{status}]')

    if unlisted:
        print('[legacy_tail_inventory] ERROR: marker-bearing files are not classified in ADR-0002:', file=sys.stderr)
        for rel in unlisted:
            print(f'  - {rel}', file=sys.stderr)
        return 1

    print('[legacy_tail_inventory] OK')
    for status, count in sorted(status_counts.items()):
        print(f'  - {status}: {count}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
