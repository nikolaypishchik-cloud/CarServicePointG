#!/usr/bin/env python3
from __future__ import annotations

import sys
sys.dont_write_bytecode = True

from pathlib import Path
from project_source_scope import project_paths, SOURCE_DIRS, SERVICE_DIRS

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_ROOT_FILES = {
    'AGENTS.md',
    'CarServicePointG.lnk',
    'CMakeLists.txt',
    'CarService_PointG_v2_1.iss',
    'SHA256SUMS.txt',
    'github_release_manifest.json',
    'GITHUB_RELEASE_UPLOAD_LIST_2.1.txt',
}
ALLOWED_ROOT_DIRS = {
    # Codex user attachments: permit the root, retain recursive artifact checks.
    '.codex-attachments',
    '.github',
    'cmake',
    'docs',
    'resources',
    'scripts',
    'server',
    'src',
    'tests',
    'web-booking',
    'web-crm',
}

# Runtime/build/packaging workflow may create these service folders in the
# project root during local configure/build/run. They are allowed as
# temporary working directories, but their internal contents are intentionally
# not audited here.
ALLOWED_WORKFLOW_ROOT_DIRS = {
    'build-crm-transfer-check',
    'build-release',
    'build-single-instance-audit',
    'dist', # Runtime contents are checked by check_runtime_package.py.

    'logs',
    'Output',
}
WORKFLOW_ROOT_DIRS_LOWER = {name.lower() for name in ALLOWED_WORKFLOW_ROOT_DIRS}

# Release/support artifacts intentionally shipped with the project archive.
# They are source-controlled inputs for the GitHub update workflow, not
# accidental build leftovers, so the layout gate must allow them in root.
FORBIDDEN_FILE_SUFFIXES = ('.log', '.tmp', '.bak', '.orig', '.rej')
FORBIDDEN_FILE_NAMES = {
    '.DS_Store',
    'Thumbs.db',
}
FORBIDDEN_DIR_NAMES = {
    '__pycache__', '.git', '.vs', '.idea', 'build', 'dist'
}

ALLOWED_ROOT_DIRS |= SOURCE_DIRS | SERVICE_DIRS

errors: list[str] = []
for child in ROOT.iterdir():
    name = child.name
    if child.is_file() and name not in ALLOWED_ROOT_FILES:
        errors.append(f'unexpected root file: {name}')
    elif child.is_dir() and name not in ALLOWED_ROOT_DIRS and name.lower() not in WORKFLOW_ROOT_DIRS_LOWER:
        errors.append(f'unexpected root directory: {name}')

for path in project_paths(ROOT):
    rel = path.relative_to(ROOT)
    if rel.parts and rel.parts[0].lower() in WORKFLOW_ROOT_DIRS_LOWER:
        continue
    name = path.name
    if any(part in FORBIDDEN_DIR_NAMES for part in rel.parts):
        errors.append(f'forbidden directory artifact: {rel}')
        continue
    if path.is_file():
        if name in FORBIDDEN_FILE_NAMES:
            errors.append(f'forbidden file artifact: {rel}')
        if path.suffix.lower() in FORBIDDEN_FILE_SUFFIXES:
            errors.append(f'forbidden temp/log artifact: {rel}')
        upper = name.upper()
        if upper.endswith('_REPORT.TXT') or upper.endswith('_FIX.TXT') or upper.endswith('_AUDIT.TXT'):
            errors.append(f'forbidden report artifact: {rel}')
        if path.suffix.lower() == '.zip':
            errors.append(f'nested zip artifact: {rel}')

if errors:
    print('Project layout check failed:')
    for e in errors:
        print(' -', e)
    sys.exit(1)
print('Project layout check: OK')
