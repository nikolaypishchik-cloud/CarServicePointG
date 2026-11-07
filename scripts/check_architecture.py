#!/usr/bin/env python3
from __future__ import annotations

import sys
import json
import hashlib
import argparse
sys.dont_write_bytecode = True

import pathlib
import platform
import re
from collections import defaultdict

root = pathlib.Path(__file__).resolve().parents[1]
src_root = root / 'src'
cmake_root = root / 'cmake' / 'modules'

if platform.system().lower().startswith('win') and sys.maxsize <= 2**32:
    print('[architecture_gate] ERROR: 32-bit Python/environment detected; use x64 build tools.', file=sys.stderr)
    raise SystemExit(1)

FILE_GLOBS = ('*.h', '*.hpp', '*.cpp', '*.cxx', '*.cc', '*.ipp')

DISALLOWED_RULES = [
    {
        'scope': 'app/',
        'pattern': r'#include\s+["<](?:\.\./)*infrastructure/',
        'message': 'App layer must not include infrastructure directly; route through core/runtime facades.',
    },
    {
        'scope': 'runtime/',
        'pattern': r'#include\s+["<](?:\.\./)*infrastructure/',
        'message': 'Runtime layer must not include infrastructure directly; route through core/runtime facades.',
    },
    {
        'scope': 'ui/',
        'pattern': r'#include\s+["<](?:\.\./)*infrastructure/db/sqlite/',
        'message': 'UI layer must not include sqlite infrastructure directly.',
    },
    {
        'scope': 'ui/',
        'pattern': r'#include\s+["<](?:\.\./)*core/repository/',
        'message': 'UI layer must not include repositories directly; route through ViewModel/ApplicationService.',
    },
    {
        'scope': 'ui/',
        'pattern': r'\bQSql(?:Database|Query|TableModel|RelationalTableModel)\b',
        'message': 'UI layer must not use Qt SQL query/database primitives directly; route via services/repositories.',
    },
    {
        'scope': 'ui/',
        'pattern': r'\bDatabase::db\s*\(',
        'message': 'UI layer must not call Database::db() directly; route via an application service.',
    },
    {
        'scope': 'ui/',
        'pattern': r'\bwaitForFinished\s*\(',
        'message': 'UI layer must not block on QProcess::waitForFinished(); use async workers/incremental process IO.',
    },
    {
        'scope': 'core/',
        'pattern': r'#include\s+["<](?:\.\./)*ui/',
        'message': 'Core layer must not include UI headers directly.',
    },
    {
        'scope': 'infrastructure/',
        'pattern': r'#include\s+["<](?:\.\./)*ui/',
        'message': 'Infrastructure layer must not include UI headers directly.',
    },
    {
        'scope': 'core/repository/',
        'pattern': r'#include\s+["<](?:\.\./)*(?:ui/|core/cache/)',
        'message': 'Repository layer must not know about UI/cache; post-commit side effects belong to services.',
    },
    {
        'scope': 'core/repository/',
        'pattern': r'\bQMessageBox\b|\bemit\s+\w+Changed\b|\bnotify\w+\s*\(',
        'message': 'Repository layer must not perform UI/notification side effects.',
    },
    {
        'scope': 'core/services/',
        'pattern': r'\bQMessageBox\b',
        'message': 'Service layer must not show QMessageBox directly; return structured errors to UI.',
    },
]

CMAKE_LAYER_MODULES = [
    'CarServiceSourcesCore.cmake',
    'CarServiceSourcesInfrastructure.cmake',
    'CarServiceSourcesIntegrations.cmake',
    'CarServiceSourcesUi.cmake',
    'CarServiceTests.cmake',
]

def scan_occurrences(source_root, rules):
    found = []
    counts = defaultdict(int)
    for path in sorted(p for p in source_root.rglob('*') if p.is_file() and p.suffix in {'.h','.hpp','.cpp','.cxx','.cc','.ipp'}):
        rel = path.relative_to(source_root).as_posix()
        counts[rel.split('/',1)[0] if '/' in rel else '(root)'] += 1
        text = path.read_text(encoding='utf-8', errors='replace')
        for rule in rules:
            if not rel.startswith(rule['scope']): continue
            rule_id = hashlib.sha256((rule['scope']+'|'+rule['pattern']).encode()).hexdigest()[:16]
            duplicates = defaultdict(int)
            for match in re.finditer(rule['pattern'],text,re.MULTILINE):
                start=text.rfind('\n',0,match.start())+1
                end=text.find('\n',match.end())
                line=text[start:end if end>=0 else len(text)].strip()
                normalized=' '.join(line.split())
                duplicates[normalized]+=1
                fingerprint=hashlib.sha256((rule_id+'|'+rel+'|'+normalized+'|'+str(duplicates[normalized])).encode()).hexdigest()
                found.append(dict(rule_id=rule_id,path=rel,line=text.count('\n',0,match.start())+1,
                    fingerprint=fingerprint,source=line,message=rule['message']))
    return found,counts

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--baseline',type=pathlib.Path,default=root/'docs/architecture-baseline.json')
    parser.add_argument('--write-baseline',action='store_true',help='Create once from explicitly approved existing debt; never overwrite')
    parser.add_argument('--json-output',type=pathlib.Path)
    args=parser.parse_args()
    occurrences,layer_counts=scan_occurrences(src_root,DISALLOWED_RULES)
    violations=[]
    # Root CMake must include the layer modules and must not own the long source lists directly.
    cmake_text = (root / 'CMakeLists.txt').read_text(encoding='utf-8', errors='replace')
    for module in CMAKE_LAYER_MODULES:
        if f'include(cmake/modules/{module})' not in cmake_text:
            violations.append(f'CMakeLists.txt: missing layer module include cmake/modules/{module}')
        if not (cmake_root / module).exists():
            violations.append(f'cmake/modules/{module}: missing CMake layer module')
    
    if re.search(r'^set\s*\(\s*SRC_FILES\s*\n\s*src/', cmake_text, flags=re.MULTILINE):
        violations.append('CMakeLists.txt: root source list owns raw src/* entries; keep lists in cmake/modules/')
    if re.search(r'^set\s*\(\s*HDR_FILES\s*\n\s*src/', cmake_text, flags=re.MULTILINE):
        violations.append('CMakeLists.txt: root header list owns raw src/* entries; keep lists in cmake/modules/')
    
    P2_P5_REQUIRED_RULE_MARKERS = [
        'UI layer must not call Database::db() directly',
        'UI layer must not use Qt SQL query/database primitives directly',
        'UI layer must not block on QProcess::waitForFinished()',
        'Service layer must not show QMessageBox directly',
        'Repository layer must not perform UI/notification side effects',
    ]
    
    _missing_markers = [marker for marker in P2_P5_REQUIRED_RULE_MARKERS if marker not in '\n'.join(rule['message'] for rule in DISALLOWED_RULES)]
    if _missing_markers:
        for marker in _missing_markers:
            violations.append(f'scripts/check_architecture.py: missing required P2 p5 gate marker: {marker}')
    

    if args.write_baseline:
        entries=[dict(item,owner_role='architecture maintainer',justification='Existing R0 debt; no whole-file exemption',target_stage='R3/R4',review_by='2026-11-28') for item in occurrences]
        args.baseline.parent.mkdir(parents=True,exist_ok=True)
        with args.baseline.open('x',encoding='utf-8') as stream:
            json.dump({'schema':1,'entries':entries},stream,ensure_ascii=False,indent=2)
    try:
        baseline=json.loads(args.baseline.read_text(encoding='utf-8'))
        assert baseline['schema']==1
        entries=baseline['entries']
        assert all(all(e.get(k) for k in ('fingerprint','rule_id','path','owner_role','justification','target_stage','review_by')) for e in entries)
        known={e['fingerprint'] for e in entries}
        assert len(known)==len(entries)
    except (OSError,ValueError,KeyError,AssertionError):
        print('[architecture_gate] Invalid or missing reviewed baseline',file=sys.stderr);return 1
    actual={e['fingerprint'] for e in occurrences}
    new=[e for e in occurrences if e['fingerprint'] not in known]
    removed=[e for e in entries if e['fingerprint'] not in actual]
    report=dict(schema=1,raw_count=len(occurrences),accepted_count=len(occurrences)-len(new),
                new=new,resolved_baseline_entries=removed,structural_errors=violations,occurrences=occurrences,layer_counts=dict(layer_counts))
    if args.json_output:args.json_output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    for item in new:print(f"{item['path']}:{item['line']}: {item['message']}",file=sys.stderr)
    for item in violations:print(item,file=sys.stderr)
    if removed:print('[architecture_gate] Remove resolved fingerprints from baseline to prevent reintroduction',file=sys.stderr)
    print(f'[architecture_gate] raw={len(occurrences)} accepted={len(occurrences)-len(new)} new={len(new)} resolved={len(removed)} structural={len(violations)}')
    return int(bool(new or removed or violations))

if __name__=='__main__':raise SystemExit(main())
