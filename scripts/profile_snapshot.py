"""Offline, verified profile snapshots; restore ONLY to a new directory.

The caller must close the application and include all profile-owned files beneath
profile_root (settings, keys, registry, archives, attachments, recovery journals).
External paths and OS credential/registry stores are NOT silently captured.
No automatic restore over live data or application launch is performed.
"""
from __future__ import annotations
import argparse
from contextlib import ExitStack, closing
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sqlite3
import stat
import time
import uuid
from urllib.parse import quote


def checked(path: Path) -> Path:
    native = os.path.abspath(path)
    if os.name == 'nt' and not native.startswith('\\\\?\\'):
        native = ('\\\\?\\UNC\\' + native[2:]) if native.startswith('\\\\') else '\\\\?\\' + native
    path = Path(native)
    for item in [path, *path.parents]:
        if item.exists() or item.is_symlink():
            info = item.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError(f'Link/reparse point is not allowed: {item}')
    return path


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root: Path) -> dict[str, Path]:
    root = checked(root)
    result = {}
    for folder, dirs, files in os.walk(root, followlinks=False):
        for name in dirs:
            checked(Path(folder) / name)
        for name in files:
            item = checked(Path(folder) / name)
            if not item.is_file():
                raise ValueError(f'Not a regular file: {item}')
            result[item.relative_to(root).as_posix()] = item
    return result


def database(path: Path) -> bool:
    with path.open('rb') as stream:
        return stream.read(16) == b'SQLite format 3\x00'


def connect(path: Path, mode='ro'):
    # SQLite's Windows VFS needs the extended path for staging paths over MAX_PATH.
    # Encode the entire native path: Path.as_uri() treats '?' as a URI authority.
    native = str(path.absolute())
    if os.name == 'nt':
        if not native.startswith('\\\\?\\'):
            native = ('\\\\?\\UNC\\' + native[2:]) if native.startswith('\\\\') else '\\\\?\\' + native
        uri = 'file:' + quote(native, safe=':')
    else:
        uri = path.as_uri()
    return sqlite3.connect(uri + '?mode=' + mode, uri=True, timeout=1)


def integrity(path: Path):
    with closing(connect(path)) as conn:
        rows = conn.execute('PRAGMA integrity_check').fetchall()
        if rows != [('ok',)]:
            raise ValueError(f'Invalid SQLite database {path}: {rows}')
        return conn.execute('PRAGMA user_version').fetchone()[0]


def new_target(path: Path, source: Path) -> Path:
    path = checked(path)
    if path.exists():
        raise FileExistsError(f'Refusing to replace an existing directory: {path}')
    if path.is_relative_to(source) or source.is_relative_to(path):
        raise ValueError('Source and destination trees must not overlap')
    if not path.parent.is_dir():
        raise ValueError('Destination parent must exist')
    return path


def write_json(path, data):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())


def snapshot(profile_root, destination, *, _step=lambda phase: None):
    root = checked(Path(profile_root))
    if not root.is_dir():
        raise ValueError('Profile directory is missing')
    target = new_target(Path(destination), root)
    source = inventory(root)
    databases = {name for name, path in source.items() if database(path)}
    for name in source:
        if Path(name).suffix.lower() in {'.db', '.sqlite', '.sqlite3'} and name not in databases:
            raise ValueError(f'Encrypted or unsupported DB requires its native backup provider: {name}')
    sidecars = {name + suffix for name in databases for suffix in ('-wal', '-shm', '-journal')}
    stage = target.with_name(target.name + '.partial-' + uuid.uuid4().hex)
    stage.mkdir()
    (stage / 'files').mkdir()
    # Staging is deliberately retained on failure; it is never a valid snapshot.
    with ExitStack() as stack:
        for name in sorted(databases):
            guard = connect(source[name], 'rw')
            stack.callback(guard.close)
            guard.execute('BEGIN IMMEDIATE')
        before = {name: digest(path) for name, path in source.items() if name not in databases | sidecars}
        _step('locked')
        entries = []
        for name, path in sorted(source.items()):
            if name in sidecars:
                continue
            copy = stage / 'files' / name
            copy.parent.mkdir(parents=True, exist_ok=True)
            kind = 'sqlite' if name in databases else 'file'
            if kind == 'sqlite':
                started = time.monotonic()
                def progress(status, remaining, total):
                    if time.monotonic() - started > 60:
                        raise TimeoutError('SQLite snapshot exceeded 60 seconds')
                reader = connect(path)
                writer = connect(copy, 'rwc')
                try:
                    reader.backup(writer, pages=256, progress=progress, sleep=0.05)
                    writer.execute('PRAGMA journal_mode=DELETE')
                finally:
                    writer.close()
                    reader.close()
                version = integrity(copy)
            else:
                shutil.copyfile(path, copy)
                version = None
                if digest(copy) != before[name]:
                    raise ValueError(f'File changed during snapshot: {name}')
            entries.append(dict(path=name, kind=kind, size=copy.stat().st_size,
                                sha256=digest(copy), user_version=version))
            _step('copied')
        current = inventory(root)
        if set(current) - sidecars != set(source) - sidecars:
            raise ValueError('Profile file list changed during snapshot')
        for name, expected in before.items():
            if digest(current[name]) != expected:
                raise ValueError(f'File changed during snapshot: {name}')
        manifest = dict(format='carservice-profile-v1', state='complete',
                        created_utc=datetime.now(timezone.utc).isoformat(), files=entries,
                        source_root=str(root),
                        scope='Files beneath source_root; no external files or OS credential stores')
        write_json(stage / 'manifest.json', manifest)
        verify(stage)
        _step('verified')
        os.rename(stage, target)
    return target


def verify(snapshot_root):
    root = checked(Path(snapshot_root))
    manifest = json.loads(checked(root / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('format') != 'carservice-profile-v1' or manifest.get('state') != 'complete':
        raise ValueError('Incomplete or unsupported snapshot')
    seen = set()
    for entry in manifest['files']:
        name = entry['path']; relative = PurePosixPath(name)
        if relative.as_posix() != name or relative.is_absolute() or not relative.parts or any(p in {'..', '.'} or ':' in p or '\\' in p for p in relative.parts):
            raise ValueError('Unsafe manifest path')
        if name.casefold() in seen:
            raise ValueError('Duplicate manifest path')
        seen.add(name.casefold())
        path = checked(root / 'files' / name)
        if not path.is_file() or path.stat().st_size != entry['size'] or digest(path) != entry['sha256']:
            raise ValueError(f'Hash/size verification failed: {name}')
        if entry['kind'] == 'sqlite':
            if integrity(path) != entry['user_version']:
                raise ValueError('Schema version mismatch')
        elif entry['kind'] != 'file':
            raise ValueError('Unknown entry type')
    if {name.casefold() for name in inventory(root / 'files')} != seen:
        raise ValueError('Unlisted/missing files in snapshot')
    return manifest


def relocate_profile(stage, old_root, target):
    """Rebase complete path values only, inside the unpublished restored tree.

    Binary/DPAPI blobs and external paths are untouched. A profile-owned missing
    target fails restoration instead of publishing a profile with dangling links.
    """
    def portable(value):
        value = value.replace('\\', '/')
        if value.startswith('//?/UNC/'):
            value = '//' + value[8:]
        elif value.startswith('//?/'):
            value = value[4:]
        return value.rstrip('/')

    old = portable(str(old_root))
    new = portable(str(target))
    def rebase(value):
        if not isinstance(value, str):
            return value
        path = portable(value)
        comparison = path.casefold() if os.name == 'nt' else path
        prefix = old.casefold() if os.name == 'nt' else old
        if comparison != prefix and not comparison.startswith(prefix + '/'):
            return value
        relative = path[len(old):].lstrip('/')
        if any(part in {'.', '..'} for part in relative.split('/')):
            raise ValueError('Noncanonical profile reference; restore was not published')
        destination = checked(stage / relative)
        if not destination.is_relative_to(stage) or not destination.exists():
            raise ValueError('Missing profile-owned reference; restore was not published: ' + relative)
        return new + ('/' + relative if relative else '')

    def tree(value):
        if isinstance(value, dict):
            return {key: tree(item) for key, item in value.items()}
        if isinstance(value, list):
            return [tree(item) for item in value]
        return rebase(value)

    def text_value(value):
        updated = rebase(value)
        if updated != value:
            return updated
        if value.lstrip().startswith(('{', '[')):
            try:
                obj = json.loads(value)
            except (ValueError, TypeError):
                return value
            changed = tree(obj)
            if changed != obj:
                return json.dumps(changed, ensure_ascii=False, separators=(',', ':'))
        return value

    def ident(name):
        return '"' + name.replace('"', '""') + '"'

    changes = 0
    for name, path in inventory(stage).items():
        if database(path):
            with closing(connect(path, 'rw')) as conn:
                with conn:
                    tables = conn.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").fetchall()
                    for (table,) in tables:
                        info = conn.execute('PRAGMA table_info(' + ident(table) + ')').fetchall()
                        columns = [row[1] for row in info]
                        keys = [row[1] for row in sorted(info, key=lambda row: row[5]) if row[5]]
                        selectors = keys or ['rowid']
                        selected = ','.join(ident(c) for c in selectors + columns)
                        rows = conn.execute('SELECT ' + selected + ' FROM ' + ident(table)).fetchall()
                        for row in rows:
                            updates = {}
                            for col, value in zip(columns, row[len(selectors):]):
                                if isinstance(value, str):
                                    updated = text_value(value)
                                    if updated != value:
                                        updates[col] = updated
                            if updates:
                                sql = 'UPDATE ' + ident(table) + ' SET ' + ','.join(ident(c) + '=?' for c in updates)
                                sql += ' WHERE ' + ' AND '.join(ident(c) + ' IS ?' for c in selectors)
                                cursor = conn.execute(sql, list(updates.values()) + list(row[:len(selectors)]))
                                if cursor.rowcount != 1:
                                    raise ValueError('Ambiguous relocation row')
                                changes += len(updates)
            integrity(path)
        elif path.suffix.lower() == '.json':
            original = path.read_text(encoding='utf-8-sig')
            obj = json.loads(original)
            updated = tree(obj)
            if obj != updated:
                path.write_text(json.dumps(updated, ensure_ascii=False, indent=2), encoding='utf-8')
                changes += 1
        elif path.suffix.lower() == '.ini':
            # Preserve QSettings sections, key names and opaque @ByteArray/@Variant values.
            lines = path.read_text(encoding='utf-8-sig').splitlines(keepends=True)
            output = []
            for line in lines:
                if '=' in line and not line.lstrip().startswith((';', '#')):
                    key, value = line.split('=', 1)
                    ending = '\n' if value.endswith('\n') else ''
                    raw = value.rstrip('\r\n')
                    quoted = raw.startswith('"') and raw.endswith('"')
                    decoded = raw[1:-1] if quoted else raw
                    # Try the literal path before QSettings unescaping: a raw
                    # Windows extended/UNC prefix must not lose its first slash.
                    updated = rebase(decoded)
                    if updated == decoded:
                        unescaped = decoded.replace('\\\\', '\\')
                        candidate = rebase(unescaped)
                        if candidate != unescaped:
                            updated = candidate
                    if updated != decoded:
                        line = key + '=' + ('"' + updated + '"' if quoted else updated) + ending
                        changes += 1
                output.append(line)
            if ''.join(output) != ''.join(lines):
                path.write_text(''.join(output), encoding='utf-8')
    return changes


def restore(snapshot_root, destination, *, _step=lambda phase: None):
    source = checked(Path(snapshot_root)); manifest = verify(source)
    target = new_target(Path(destination), source)
    stage = target.with_name(target.name + '.partial-' + uuid.uuid4().hex)
    stage.mkdir()
    for entry in manifest['files']:
        copy = stage / entry['path']; copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / 'files' / entry['path'], copy)
        if digest(copy) != entry['sha256']:
            raise ValueError('Restore hash mismatch')
        if entry['kind'] == 'sqlite':
            integrity(copy)
        _step('copied')
    relocate_profile(stage, manifest["source_root"], target)
    _step("relocated")
    # All mutations stay unpublished until every database and reference passes.
    _step('verified')
    os.rename(stage, target)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('snapshot', 'verify', 'restore'))
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path, nargs='?')
    args = parser.parse_args()
    if args.operation == 'verify':
        print(json.dumps(verify(args.source), ensure_ascii=False, indent=2))
    elif args.destination is None:
        parser.error('destination is required')
    else:
        print(snapshot(args.source, args.destination) if args.operation == 'snapshot' else restore(args.source, args.destination))
