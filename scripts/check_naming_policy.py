#!/usr/bin/env python3
from __future__ import annotations

import sys
sys.dont_write_bytecode = True
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOW = ROOT / 'scripts' / 'p7_naming_allowlist.txt'
allowed = set()
if ALLOW.exists():
    allowed = {line.strip() for line in ALLOW.read_text(encoding='utf-8').splitlines() if line.strip() and not line.startswith('#')}
pattern = re.compile(r'(MissingMethods|LocalSupport|FlowSupport|RuntimeSupport|BuildUiSupport)', re.I)
excluded_prefixes = (
    'build/',
    'build-crm-transfer-check/',
    'build-release/',
    'build-single-instance-audit/',
    'dist/',
    'logs/',
    'Output/',
)
errors = []
for p in ROOT.rglob('*'):
    if not p.is_file():
        continue
    rel = p.relative_to(ROOT).as_posix()
    if any(rel.startswith(prefix) for prefix in excluded_prefixes):
        continue
    if pattern.search(rel) and rel not in allowed:
        errors.append(rel)
if errors:
    print('Naming policy check failed:')
    for e in errors:
        print(' -', e)
    sys.exit(1)
print('Naming policy check: OK')
