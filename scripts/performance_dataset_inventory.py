"""Read-only schema/count inventory; never emits row contents."""
import json, sqlite3, sys
from pathlib import Path

def quote(value):
    return '"' + value.replace('"', '""') + '"'

def inventory(root):
    out = {}
    for path in sorted(Path(root).glob('*.db')):
        with sqlite3.connect(path.resolve().as_uri() + '?mode=ro', uri=True) as db:
            tables = {}
            for name, sql in db.execute("SELECT name,sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").fetchall():
                tables[name] = {'count': db.execute('SELECT COUNT(*) FROM ' + quote(name)).fetchone()[0],
                    'columns': db.execute('PRAGMA table_info(' + quote(name) + ')').fetchall(),
                    'foreign_keys': db.execute('PRAGMA foreign_key_list(' + quote(name) + ')').fetchall(),
                    'sql': sql}
            out[path.name] = {'bytes':path.stat().st_size, 'tables':tables}
    return out

if __name__ == '__main__':
    data = inventory(sys.argv[1])
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({file:{name:t['count'] for name,t in db['tables'].items() if t['count']} for file,db in data.items()}, indent=2))
