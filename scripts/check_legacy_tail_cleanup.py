#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys
from project_source_scope import project_paths
from collections import defaultdict

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADR2 = ROOT / 'docs' / 'architecture' / 'ADR-0002-legacy-fallback-inventory.md'
ADR3 = ROOT / 'docs' / 'architecture' / 'ADR-0003-legacy-tail-cleanup-gate.md'
SOURCE_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx'}
TEXT_SUFFIXES = SOURCE_SUFFIXES | {'.py', '.cmake', '.txt', '.md', '.qrc', '.ui', '.iss', '.ps1', '.cmd', '.bat', '.json', '.yml', '.yaml'}
EXCLUDED_PREFIXES = (
    'build-single-instance-audit/',
    'dist/',
    'resources/python_vendor/',
    'resources/windows/ocr-layer/python/site-packages/',
    'docs/',
    'tests/',
)
ANNOTATION_BLOCKING = 'CS_INTERNAL_BLOCKING_SYNC_ALLOWED'
ANNOTATION_SYNC_FALLBACK = 'CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY'
WAIT_RE = re.compile(r'\bwaitForFinished\s*\(')


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        return path.read_text(encoding='utf-8', errors='ignore')


def rel(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_text(path: pathlib.Path) -> bool:
    return path.name == 'CMakeLists.txt' or path.suffix.lower() in TEXT_SUFFIXES


def iter_project_text_files():
    for path in sorted(project_paths(ROOT)):
        if path.is_file() and is_text(path):
            yield path


def previous_window(lines: list[str], index: int, radius: int = 4) -> str:
    start = max(0, index - radius)
    end = min(len(lines), index + 2)
    return '\n'.join(lines[start:end])


def check_required_docs() -> list[str]:
    errors: list[str] = []
    for path in (ADR2, ADR3):
        if not path.exists():
            errors.append(f'missing required cleanup document: {rel(path)}')
    if errors:
        return errors
    adr2 = read_text(ADR2)
    adr3 = read_text(ADR3)
    required_adr2 = [
        'Follow-up rules',
        'compile-check',
        'grep/call-graph verification',
        'deprecated but used',
    ]
    required_adr3 = [
        'P2 legacy-tail cleanup gate',
        'grep/call-graph verification',
        'compile-check policy',
        'No runtime deletion was performed',
        'CS_INTERNAL_BLOCKING_SYNC_ALLOWED',
        'CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY',
    ]
    for phrase in required_adr2:
        if phrase not in adr2:
            errors.append(f'ADR-0002 missing required phrase: {phrase}')
    for phrase in required_adr3:
        if phrase not in adr3:
            errors.append(f'ADR-0003 missing required phrase: {phrase}')
    return errors


def check_unannotated_blocking_sync() -> list[str]:
    errors: list[str] = []
    for path in iter_project_text_files():
        r = rel(path)
        if r.startswith(EXCLUDED_PREFIXES) or r.startswith('scripts/'):
            continue
        text = read_text(path)
        if not WAIT_RE.search(text):
            continue
        lines = text.splitlines()
        for i, line in enumerate(lines):
            if not WAIT_RE.search(line):
                continue
            context = previous_window(lines, i)
            if ANNOTATION_BLOCKING not in context:
                errors.append(f'unannotated blocking sync wait at {r}:{i + 1}')
    return errors


def check_sync_fallback_public_headers() -> list[str]:
    errors: list[str] = []
    for path in iter_project_text_files():
        r = rel(path)
        if not r.startswith('src/') or path.suffix.lower() not in {'.h', '.hpp'}:
            continue
        text = read_text(path)
        if 'synchronous external-process fallback' in text and ANNOTATION_SYNC_FALLBACK not in text:
            errors.append(f'synchronous fallback header lacks internal-only marker: {r}')
    return errors


def check_deleted_candidate_policy() -> list[str]:
    errors: list[str] = []
    adr2 = read_text(ADR2)
    adr3 = read_text(ADR3)
    delete_candidate_rows = [line for line in adr2.splitlines() if '| `src/' in line and '| delete candidate |' in line]
    if delete_candidate_rows and 'DELETE-CANDIDATE-EVIDENCE:' not in adr3:
        errors.append('runtime delete candidates exist, but ADR-0003 has no DELETE-CANDIDATE-EVIDENCE section')
    if 'No runtime deletion was performed' in adr3:
        return errors
    for row in delete_candidate_rows:
        m = re.search(r'`([^`]+)`', row)
        if not m:
            continue
        candidate = m.group(1)
        if candidate not in adr3:
            errors.append(f'delete candidate lacks ADR-0003 evidence entry: {candidate}')
    return errors


def build_simple_reference_index() -> dict[str, set[str]]:
    symbols: dict[str, set[str]] = defaultdict(set)
    for path in iter_project_text_files():
        r = rel(path)
        if not r.startswith('src/') or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        text = read_text(path)
        for match in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_:]*)\s*\(', text):
            token = match.group(1).split('::')[-1]
            if len(token) >= 4:
                symbols[token].add(r)
    return symbols


def check_static_callgraph_probe() -> list[str]:
    errors: list[str] = []
    adr3 = read_text(ADR3)
    if 'Static reference probe: PASS' not in adr3:
        errors.append('ADR-0003 must record static grep/call-graph probe status')
    index = build_simple_reference_index()
    if not index:
        errors.append('static reference index is empty; grep/call-graph probe did not run')
    return errors


def main() -> int:
    errors: list[str] = []
    errors.extend(check_required_docs())
    if not errors:
        errors.extend(check_unannotated_blocking_sync())
        errors.extend(check_sync_fallback_public_headers())
        errors.extend(check_deleted_candidate_policy())
        errors.extend(check_static_callgraph_probe())
    if errors:
        print('[legacy_tail_cleanup] ERROR', file=sys.stderr)
        for error in errors:
            print(f'  - {error}', file=sys.stderr)
        return 1
    print('[legacy_tail_cleanup] OK')
    print('  - deletion policy: evidence-gated')
    print('  - blocking sync paths: annotated/internal-only')
    print('  - static grep/call-graph probe: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
