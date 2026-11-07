"""Private, consistent desktop backup for explicitly authorized reconciliation."""
import hashlib
import json
import sqlite3
import sys
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=True).encode()).hexdigest()


def rows(db, table):
    return [dict(row) for row in db.execute('SELECT * FROM "' + table + '" ORDER BY rowid')]


def main():
    source, destination = map(Path, sys.argv[1:3])
    destination.mkdir(parents=True, exist_ok=False)
    db = sqlite3.connect(source.resolve().as_uri() + '?mode=ro', uri=True)
    db.row_factory = sqlite3.Row
    backup = sqlite3.connect(destination / 'desktop-before.db')
    db.backup(backup)
    assert backup.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    backup.close()
    db.execute('PRAGMA query_only=ON')
    db.execute('BEGIN')
    tables = ['work_orders', 'work_order_services', 'work_order_parts',
              'sync_conflicts', 'sync_inbox', 'sync_outbox']
    data = {table: rows(db, table) for table in tables}
    data['schemas'] = {table: [dict(r) for r in db.execute('PRAGMA table_info("' + table + '")')]
                       for table in tables + ['sync_audit_log', 'masters', 'service_masters',
                                              'purchase_payment_balances', 'purchase_payment_terms']}
    for table in ['masters', 'service_masters', 'purchase_payment_balances', 'purchase_payment_terms']:
        if data['schemas'][table]:
            data[table] = [dict(r) for r in db.execute('SELECT * FROM "' + table + '"')]
    data['fingerprint'] = digest({k: data[k] for k in tables})
    (destination / 'desktop-snapshot.json').write_text(json.dumps(data, ensure_ascii=True), encoding='utf-8')
    db.rollback()
    db.close()
    print(json.dumps({'backup_integrity': 'ok', 'counts': {t: len(data[t]) for t in tables},
                      'schemas': {t: [r['name'] for r in fields] for t, fields in data['schemas'].items()}}))


if __name__ == '__main__':
    main()
