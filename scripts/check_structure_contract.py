"""Strict source dependency contract. No baseline, waivers or write mode."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {'.cpp', '.cc', '.cxx', '.c', '.h', '.hpp', '.ipp'}
LAYERS = {'app', 'core', 'ui', 'runtime', 'infrastructure', 'integrations', 'thirdparty'}
FORBIDDEN = {
    'ui': {'infrastructure'},
    'core': {'ui', 'app'},
    'infrastructure': {'ui', 'app'},
    'integrations': {'ui'},
}

def without_comments(text):
    # Mask comments/raw string contents, preserving line numbers and includes.
    pattern = r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    def replace(match):
        value = match.group()
        return re.sub(r'[^\n]', ' ', value) if value.startswith(('//', '/*', 'R"')) else value
    return re.sub(pattern, replace, text, flags=re.S)

def forbidden(source, target):
    a, b = source.split('/')[0], target.split('/')[0]
    if b in FORBIDDEN.get(a, set()):
        return True
    # Domain and exact arithmetic must not acquire persistence or presentation dependencies.
    if source.startswith(('core/domain/', 'core/finance/')):
        return not target.startswith(('core/domain/', 'core/finance/'))
    return False

def scan(root):
    src = (root / 'src').resolve()
    if not src.is_dir():
        return ['src directory is missing']
    errors = []
    for path in sorted(src.rglob('*')):
        rel = path.relative_to(src).as_posix()
        if path.is_symlink() or getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            errors.append(f'{rel}: source symlinks/junctions are forbidden')
            continue
        if path.is_dir():
            if path.name in {'node_modules', '__pycache__', 'build', 'dist', '.git'}:
                errors.append(f'{rel}: generated/dependency directory inside src')
            continue
        if path.suffix in {'.db', '.sqlite', '.sqlite3', '.log', '.bak', '.orig', '.rej', '.exe', '.dll'}:
            errors.append(f'{rel}: data/build artifact inside src')
        if path.suffix.lower() not in EXTENSIONS:
            continue
        if rel != 'main.cpp' and rel.split('/')[0] not in LAYERS:
            errors.append(f'{rel}: unclassified source location')
        text = without_comments(path.read_text(encoding='utf-8-sig').replace('\\\n', ''))
        if re.search(r'^\s*#\s*include\s+(?![<"\s])\S', text, re.M):
            errors.append(f'{rel}: macro includes cannot be verified; use an explicit include')
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', text, re.M):
            include = match.group(1).replace('\\', '/')
            candidates = [path.parent / include, src / include]
            target = next((p.resolve() for p in candidates if p.is_file()), None)
            if target is None:
                # Qt, standard library and generated headers are resolved by the compiler.
                continue
            line = text.count('\n', 0, match.start()) + 1
            if not target.is_relative_to(src):
                errors.append(f'{rel}:{line}: local include escapes src: {include}')
                continue
            destination = target.relative_to(src).as_posix()
            if forbidden(rel, destination):
                errors.append(f'{rel}:{line}: forbidden dependency -> {destination}')
        if rel.startswith(('core/domain/', 'core/finance/')):
            if re.search(r'^\s*#\s*include\s*[<"]Q(?:Sql|Widget|MessageBox|Network|File|Process)', text, re.M):
                errors.append(f'{rel}: domain/finance must not depend on IO or UI')
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--json-output', type=Path)
    args = parser.parse_args()
    try:
        errors = scan(args.root)
    except (OSError, UnicodeError) as error:
        errors = [f'Incomplete scan: {error}']
    if args.json_output:
        args.json_output.write_text(json.dumps({'errors': errors}, ensure_ascii=False, indent=2), encoding='utf-8')
    for error in errors:
        print(error)
    print(f'STRUCTURE_CONTRACT: {len(errors)} violations; no accepted debt')
    return int(bool(errors))

if __name__ == '__main__':
    raise SystemExit(main())
