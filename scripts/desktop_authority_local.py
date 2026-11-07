"""Close only explicitly reconciled queues after a committed server receipt."""
import json
import shutil
import sqlite3
import sys
from pathlib import Path
from desktop_authority_snapshot import digest, rows

TABLES = ['work_orders', 'work_order_services', 'work_order_parts', 'sync_conflicts', 'sync_inbox', 'sync_outbox']


def reconcile(db, plan, receipt):
    assert receipt['plan_hash'] == digest(plan)
    assert receipt['orders'] == len(plan['canonical'])
    assert receipt['retired_orders'] == len(plan['obsolete_orders'])
    assert len(receipt['published_events']) >= receipt['orders'] + receipt['retired_orders']
    db.execute('BEGIN IMMEDIATE')
    marker = 'desktop-authority-' + digest(plan)[:32]
    if db.execute("SELECT 1 FROM sync_audit_log WHERE action='desktop_authority_reconciliation' AND entity_uid=?", (marker,)).fetchone():
        db.rollback()
        return {'already_applied': True}
    assert digest({t: rows(db, t) for t in TABLES}) == plan['desktop_fingerprint'], 'Desktop changed; take a fresh snapshot'
    business_before = digest({t: rows(db, t) for t in TABLES[:3]})
    reason = 'Resolved by explicit desktop authority; original payload retained; server receipt ' + receipt['plan_hash']
    for identifier in plan['local_conflict_ids']:
        result = db.execute("UPDATE sync_conflicts SET resolution='keep_local',status='resolved',resolved_by=?,"
                            "resolved_at=datetime('now'),resolver_policy='explicit_desktop_authority' WHERE id=? AND status='pending'",
                            (marker, identifier))
        assert result.rowcount == 1
    for identifier in plan['local_inbox_ids']:
        result = db.execute("UPDATE sync_inbox SET status='resolved_authoritative',apply_error=?,error=?,last_error=?,"
                            "applied_at=datetime('now') WHERE id=? AND status IN ('pending','failed','conflict','applying')",
                            (reason, reason, reason, identifier))
        assert result.rowcount == 1
    for identifier in plan['local_outbox_ids']:
        result = db.execute("UPDATE sync_outbox SET status='local_only',last_error=?,updated_at=datetime('now'),next_retry_at='' "
                            "WHERE id=? AND status IN ('pending','failed','sending')", (reason, identifier))
        assert result.rowcount == 1
    db.execute("INSERT INTO sync_audit_log(created_at,action,entity_type,entity_uid,conflict_uid,resolution,result,actor,reason,details_json) "
               "VALUES(datetime('now'),'desktop_authority_reconciliation','work_order',?,'','keep_local','ok','authorized-operator',?,?)",
               (marker, reason, json.dumps(receipt)))
    assert digest({t: rows(db, t) for t in TABLES[:3]}) == business_before
    assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    db.commit()
    return {'resolved_conflicts': len(plan['local_conflict_ids']), 'superseded_inbox': len(plan['local_inbox_ids']),
            'superseded_outbox': len(plan['local_outbox_ids']), 'business_rows_unchanged': True}


def main():
    mode, folder = sys.argv[1], Path(sys.argv[2])
    plan = json.loads((folder / 'plan.json').read_text())
    if mode == 'test':
        destination = folder / 'desktop-test.db'
        shutil.copyfile(folder / 'desktop-before.db', destination)
        receipt = {'plan_hash': digest(plan), 'orders': len(plan['canonical']), 'retired_orders': len(plan['obsolete_orders']),
                   'published_events': ['test'] * (len(plan['canonical']) + len(plan['obsolete_orders']))}
    else:
        assert mode == 'apply'
        destination = Path(sys.argv[3])
        receipt = json.loads((folder / 'server-receipt.json').read_text())
    db = sqlite3.connect(destination)
    db.row_factory = sqlite3.Row
    calendar_before = digest([dict(r) for r in db.execute('SELECT * FROM planning_calendar ORDER BY rowid')])
    if mode == 'test':
        try:
            reconcile(db, dict(plan, desktop_fingerprint='wrong'), dict(receipt, plan_hash=digest(dict(plan, desktop_fingerprint='wrong'))))
            raise RuntimeError('Missing concurrency guard')
        except AssertionError:
            db.rollback()
    result = reconcile(db, plan, receipt)
    assert digest([dict(r) for r in db.execute('SELECT * FROM planning_calendar ORDER BY rowid')]) == calendar_before
    assert reconcile(db, plan, receipt) == {'already_applied': True}
    print(json.dumps(result))
    print(json.dumps({'remaining_failed': [dict(r) for r in db.execute("SELECT entity_type,count(*) AS count FROM sync_inbox WHERE status='failed' GROUP BY entity_type")],
                      'remaining_outbox': [dict(r) for r in db.execute("SELECT entity_type,count(*) AS count FROM sync_outbox WHERE status IN ('pending','failed','sending') GROUP BY entity_type")],
                      'calendar_unchanged': True}))
    db.close()


if __name__ == '__main__':
    main()
