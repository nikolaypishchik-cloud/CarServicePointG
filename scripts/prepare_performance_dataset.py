"""Clone the local audit business graph; never connect to production for writing.

Fivefold means rows in the business tables below, not file bytes. Configuration,
staff, external supplier snapshots and historical synchronization logs are shared.
"""
import argparse, hashlib, json, sqlite3
from contextlib import closing
from pathlib import Path
from performance_dataset_inventory import inventory, quote

TABLES = set('work_orders work_order_services work_order_parts service_masters work_order_service_masters outsource_jobs outsource_repairs work_order_part_attachments order_registry part_registry workspace_order_state order_search_v1 parts_catalog parts_catalog_vehicle_map parts_catalog_sources catalog_source_map crm_clients crm_vehicles crm_client_events crm_tasks payroll_order_adjustments workshop_inventory_purchases workshop_inventory_movements workshop_spare_orders workshop_spare_movements workshop_spare_attachments workshop_spare_payment_allocations invoice_part_evidence purchase_payment_balances purchase_payment_terms purchase_payment_events order_workshop_posts additional_expenses expenses'.split())
INTEGER_REFS = set('number order_number visible_number visible_order_number last_order_number source_order_number id service_id parent_id part_id source_part_id part_ref catalog_id catalog_item_id spare_id purchase_id issue_id'.split())
UID_COLUMNS = set('order_uid source_order_uid source_order_uid_first source_order_uid_last last_order_uid part_uid service_uid client_uid vehicle_uid event_uid task_uid'.split())
UNIQUE_TEXT = set('entry_key source_signature catalog_match_key dedupe_signature'.split())
STRIDE = 1_000_000

def mapped(table, col, value, copy):
    if value is None or value == '': return value
    if col in INTEGER_REFS and isinstance(value, int) and value > 0:
        return value + copy * STRIDE
    if col in UID_COLUMNS or col in UNIQUE_TEXT:
        return f'audit{copy}~{value}'
    if col == 'payment_key':
        pieces = value.split(':', 2)
        if len(pieces) >= 2 and pieces[1].isdigit():
            pieces[1] = str(int(pieces[1]) + copy * STRIDE)
            if len(pieces) == 3 and pieces[2]: pieces[2] = f'audit{copy}~{pieces[2]}'
            return ':'.join(pieces)
        raise ValueError('Unsupported payment key format')
    # Keep invoice grouping separate for each replicated business history.
    if col in ('invoice_number', 'document') and value:
        return f'{value}~audit{copy}'
    return value

def prepare(source, dest, factor):
    source, dest = source.resolve(strict=True), dest.resolve()
    if source == dest or dest.exists(): raise ValueError('Destination must be a new isolated directory')
    if 'build-single-instance-audit' not in dest.parts: raise ValueError('Development output required')
    dest.mkdir(parents=True)
    before = inventory(source)
    report = {'factor':factor, 'stride':STRIDE, 'files':{}, 'shared':'configuration, staff, supplier snapshots, sync and audit logs; calendar dates'}
    for path in source.glob('*.db'):
        target = dest / path.name
        with closing(sqlite3.connect(path.as_uri()+'?mode=ro',uri=True)) as src, closing(sqlite3.connect(target)) as db:
            src.backup(db)
            db.execute('PRAGMA foreign_keys=OFF')
            base_fk = len(db.execute('PRAGMA foreign_key_check').fetchall())
            triggers = db.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger'").fetchall()
            for name, _ in triggers: db.execute('DROP TRIGGER '+quote(name))
            scaled = {}
            for table, info in before[path.name]['tables'].items():
                base = table.removeprefix('router_cache_')
                if base not in TABLES or not info['count']: continue
                if (info['sql'] or '').upper().startswith('CREATE VIRTUAL'): continue
                cols = [r[1] for r in info['columns']]
                rows = db.execute('SELECT '+','.join(map(quote,cols))+' FROM '+quote(table)).fetchall()
                for col in INTEGER_REFS.intersection(cols):
                    if any(isinstance(r[cols.index(col)],int) and r[cols.index(col)] >= STRIDE for r in rows):
                        raise ValueError(f'Stride too small: {table}.{col}')
                sql = 'INSERT INTO '+quote(table)+'('+','.join(map(quote,cols))+') VALUES('+','.join('?' for _ in cols)+')'
                for copy in range(1,factor):
                    db.executemany(sql, [tuple(mapped(base,c,v,copy) for c,v in zip(cols,row)) for row in rows])
                count = db.execute('SELECT COUNT(*) FROM '+quote(table)).fetchone()[0]
                if count != info['count']*factor: raise ValueError('Wrong scale: '+table)
                scaled[table] = {'before':info['count'],'after':count}
            for _, sql in triggers: db.execute(sql)
            for name in ('work_orders_fts','work_order_parts_fts'):
                if name in before[path.name]['tables']:
                    db.execute('INSERT INTO '+quote(name)+'('+quote(name)+") VALUES('rebuild')")
            # All storage routing points to this isolated set, including registry DBs.
            for table,info in before[path.name]['tables'].items():
                for col in ('storage_path','source_db'):
                    if col not in [r[1] for r in info['columns']]: continue
                    vals=db.execute('SELECT DISTINCT '+quote(col)+' FROM '+quote(table)+" WHERE "+quote(col)+"<>''").fetchall()
                    for (old,) in vals:
                        filename=Path(old.replace('\\','/')).name
                        if filename not in before: raise ValueError('Unknown routed database: '+filename)
                        db.execute('UPDATE '+quote(table)+' SET '+quote(col)+'=? WHERE '+quote(col)+'=?',(str(dest/filename),old))
            db.commit()
            integrity=db.execute('PRAGMA integrity_check').fetchall()
            if integrity != [('ok',)]: raise ValueError('Integrity failed: '+path.name)
            after_fk=len(db.execute('PRAGMA foreign_key_check').fetchall())
            if after_fk > base_fk*factor: raise ValueError(f'New FK problems: {path.name}, {base_fk} -> {after_fk}')
            report['files'][path.name]={'scaled_tables':scaled,'baseline_fk_violations':base_fk,'scaled_fk_violations':after_fk,'integrity':'ok'}
            if db.execute('PRAGMA wal_checkpoint(TRUNCATE)').fetchone()[0]:
                raise ValueError('Dataset checkpoint incomplete: '+path.name)
        report['files'][path.name]['bytes']=target.stat().st_size
        report['files'][path.name]['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
    (dest/'dataset-manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);p.add_argument('--factor',type=int,default=5)
    a=p.parse_args();prepare(a.source,a.destination,a.factor)
