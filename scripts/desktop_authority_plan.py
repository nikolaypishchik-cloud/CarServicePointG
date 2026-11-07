"""Build a bounded reconciliation plan from private immutable snapshots."""
import json
import sys
from datetime import datetime
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

DATE_FIELDS = {'opened_at', 'closed_at', 'ordered_at', 'delivery_date', 'installed_at',
               'requested_at', 'due_date'}


def protocol(row):
    row = {k: ('' if v is None else v) for k, v in row.items() if k != 'id'}
    for key in DATE_FIELDS:
        value = row.get(key)
        if not isinstance(value, str) or len(value) < 8 or value[2] != '.':
            continue
        parsed = None
        for fmt in ('%d.%m.%y', '%d.%m.%Y', '%d.%m.%y/%H.%M',
                    '%d.%m.%y %H:%M:%S', '%d.%m.%y %H:%M', '%d.%m.%Y %H:%M:%S'):
            try:
                parsed = datetime.strptime(value, fmt)
                break
            except ValueError:
                pass
        if parsed is None:
            raise ValueError('Unrecognized desktop date field: ' + key)
        row[key] = parsed.date().isoformat() if len(value) <= 10 else parsed.isoformat()
    return row


def main(folder):
    folder = Path(folder)
    d = json.loads((folder / 'desktop-snapshot.json').read_text())
    s = json.loads((folder / 'server-snapshot.json').read_text())
    orders = {r['order_uid']: r for r in d['work_orders']}
    numbers = {str(r['number']): r['order_uid'] for r in d['work_orders']}
    server_orders = {r['order_uid']: r for r in s['orders']}
    parts = {r['part_uid']: r for r in d['work_order_parts']}
    targets, obsolete = set(), set()
    conflicts = [r for r in d['sync_conflicts'] if r['status'] == 'pending'
                 and r['entity_type'] in {'work_order', 'work_order_part'}]
    for r in conflicts:
        if r['entity_type'] == 'work_order_part':
            targets.add(parts[r['entity_uid']]['order_uid'])
        elif r['entity_uid'] in orders:
            targets.add(r['entity_uid'])
        else:
            remote = server_orders[r['entity_uid']]
            assert r['conflict_type'] == 'order_number_uid_collision'
            targets.add(numbers[str(remote['number'])])
            obsolete.add(r['entity_uid'])
    outbox = [r for r in d['sync_outbox'] if r['status'] in {'pending', 'failed', 'sending'}
              and r['entity_type'] == 'work_order']
    targets.update(r['entity_uid'] for r in outbox)
    assert targets and targets.issubset(orders)
    devices = {r['device_uid'] for r in outbox}
    assert len(devices) == 1
    device = devices.pop()
    scopes = {(r['organization_uid'], r['branch_uid']) for r in s['sync_events']
              if r['source_device_uid'] == device}
    assert len(scopes) == 1, 'Ambiguous organization/branch'
    org, branch = scopes.pop()
    assert org and branch
    master_names = {str(r['id']): r['name'] for r in d.get('masters', [])}
    balances = {r['payment_key']: r['paid_cents'] for r in d.get('purchase_payment_balances', [])}
    terms = {r['payment_key']: r['due_date'] for r in d.get('purchase_payment_terms', [])}
    canonical = []
    for uid in sorted(targets):
        payload = protocol(orders[uid])
        master = str(payload['master_id'])
        payload.update(assigned_to=master, master=master,
                       assigned_to_name=master_names.get(master, ''), organization_uid=org, branch_uid=branch)
        payload['total_amount'] = float(payload['works_sum'] or 0) + float(payload['parts_sum'] or 0)
        state = json.loads(payload.get('mobile_state_json') or '{}')
        if isinstance(state, dict):
            payload.update(state)
        # The authoritative SQLite row takes precedence over any embedded state.
        payload.update(protocol(orders[uid]))
        related = {'services': [], 'parts': []}
        for raw in d['work_order_services']:
            if raw['order_uid'] != uid:
                continue
            row = protocol(raw)
            service_masters = {str(r['master_id']) for r in d.get('service_masters', [])
                               if r['service_id'] == raw['id']}
            if not row.get('assigned_to') and len(service_masters) > 1:
                raise ValueError('Multiple service masters require explicit mapping')
            service_master = next(iter(service_masters), master)
            row['assigned_to'] = str(row.get('assigned_to') or service_master)
            row['assigned_to_name'] = row.get('assigned_to_name') or master_names.get(row['assigned_to'], '')
            assert row['service_uid']
            related['services'].append(row)
        for raw in d['work_order_parts']:
            if raw['order_uid'] != uid:
                continue
            row = protocol(raw)
            assert row['part_uid']
            amount = Decimal(str(row['purchase_amount'] or 0))
            if amount <= 0:
                amount = Decimal(str(row['qty'] or 0)) * Decimal(str(row['purchase_price'] or 0))
            total = max(0, int((amount * 100).quantize(Decimal('1'), rounding=ROUND_HALF_UP)))
            key = 'part:' + str(raw['id']) + ':' + uid
            due = protocol({'due_date': terms.get(key, '')})['due_date']
            row['purchase_payment'] = {'version': 1, 'total_cents': total,
                                      'paid_cents': max(0, int(balances.get(key, total if row['paid'] else 0))),
                                      'due_date': due}
            related['parts'].append(row)
        canonical.append({'uid': uid, 'payload': payload, 'related': related})
    affected = targets | obsolete
    child_uids = {r[field] for data, field in [(d['work_order_services'], 'service_uid'),
                                              (d['work_order_parts'], 'part_uid'),
                                              (s['order_services'], 'service_uid'),
                                              (s['order_parts'], 'part_uid')]
                  for r in data if r['order_uid'] in affected}
    def covered(r):
        return (r['entity_type'] == 'work_order' and r['entity_uid'] in affected) or (
            r['entity_type'] in {'work_order_service', 'work_order_part'} and r['entity_uid'] in child_uids)
    plan = {'version': 1, 'desktop_fingerprint': d['fingerprint'], 'server_fingerprint': s['fingerprint'],
            'device_uid': device, 'organization_uid': org, 'branch_uid': branch,
            'canonical': canonical, 'obsolete_orders': sorted(obsolete),
            'local_conflict_ids': [r['id'] for r in conflicts],
            'local_inbox_ids': [r['id'] for r in d['sync_inbox'] if covered(r) and r['status'] in {'pending', 'failed', 'conflict', 'applying'}],
            'local_outbox_ids': [r['id'] for r in d['sync_outbox'] if covered(r) and r['status'] in {'pending', 'failed', 'sending'}],
            'server_conflict_ids': [r['id'] for r in s['sync_conflicts'] if covered(r) and r['resolution'] == 'pending'],
            'legacy_event_ids': [r['id'] for r in s['sync_events'] if covered(r)
                                 and r['organization_uid'] == '' and r['branch_uid'] == 'local-branch'],
            'server_event_ids': [r['id'] for r in s['sync_events'] if covered(r)
                                 and r['organization_uid'] in ('', org) and r['branch_uid'] in ('', branch)]}
    (folder / 'plan.json').write_text(json.dumps(plan, ensure_ascii=True), encoding='utf-8')
    print(json.dumps({'canonical_order_numbers': sorted(int(r['payload']['number']) for r in canonical),
                      'obsolete_server_orders': len(obsolete),
                      'local_conflicts': len(plan['local_conflict_ids']), 'local_inbox': len(plan['local_inbox_ids']),
                      'local_outbox': len(plan['local_outbox_ids']), 'server_conflicts': len(plan['server_conflict_ids'])}))


if __name__ == '__main__':
    main(sys.argv[1])
