#!/usr/bin/env python3
from __future__ import annotations

import sys
from project_source_scope import project_paths
sys.dont_write_bytecode = True

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / 'CMakeLists.txt'
QRC = ROOT / 'resources' / 'resources.qrc'
FORBIDDEN_SUFFIXES = {'.tmp', '.bak', '.orig', '.rej', '.log'}
# Local build, deployment and installer packaging may create service folders in the
# project root. They must not block structural sanity checks.
ALLOWED_WORKFLOW_ROOT_DIRS = {'build-release', 'dist', 'logs', 'Output'}
CRITICAL_CMAKE_SOURCES = [
    'src/main.cpp',
    'src/runtime/AsyncTaskHandle.h',
    'src/runtime/AsyncThreadScope.h',
    'src/core/services/OrderRoutingLookupService.cpp',
    'src/core/services/CatalogImportTextUtils.cpp',
    'src/core/backup/BackupManager.cpp',
    'src/core/backup/BackupJobQueue.cpp',
    'src/app/DeferredStartupMaintenance.cpp',
    'src/app/IdleMaintenanceScheduler.cpp',
]


def fail(message: str) -> None:
    print(f'[build_sanity_gate] ERROR: {message}', file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    if not QRC.exists():
        fail('resources/resources.qrc is missing')

    root_main = ROOT / 'src' / 'main.cpp'
    if not root_main.exists():
        fail('src/main.cpp is missing')

    main_candidates = [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').rglob('main.cpp') if 'build' not in p.parts and 'dist' not in p.parts]
    if main_candidates != ['src/main.cpp']:
        fail(f'expected a single main entrypoint at src/main.cpp, found: {main_candidates}')

    cmake_files = [CMAKE] + sorted((ROOT / 'cmake' / 'modules').glob('*.cmake'))
    cmake_text = '\n'.join(
        re.sub(r'#.*', '', path.read_text(encoding='utf-8', errors='replace'))
        for path in cmake_files
        if path.exists()
    )
    missing_sources = [rel for rel in CRITICAL_CMAKE_SOURCES if rel not in cmake_text]
    if missing_sources:
        fail('critical sources are not wired in CMakeLists.txt:\n  - ' + '\n  - '.join(missing_sources))

    junk = []
    for path in project_paths(ROOT):
        rel = path.relative_to(ROOT)
        if rel.parts and rel.parts[0] in ALLOWED_WORKFLOW_ROOT_DIRS:
            continue
        if not path.is_file():
            continue
        if path.suffix.lower() in FORBIDDEN_SUFFIXES:
            junk.append(rel.as_posix())
        if path.name.startswith('~$'):
            junk.append(rel.as_posix())
    if junk:
        fail('temporary/junk artifacts found:\n  - ' + '\n  - '.join(sorted(set(junk))))

    print('[build_sanity_gate] OK: qrc/main/source wiring/temp artifact checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
