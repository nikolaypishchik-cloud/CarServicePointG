"""Guarded one-off reconciliation; audit mode never changes business data."""
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from sqlalchemy import select, text
from app.db.session import SessionLocal
from app.models.core import Order, OrderService, OrderPart, SyncConflict, SyncEvent, AuditLog


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=True).encode()).hexdigest()


def snapshot(db):
    result = {}
    for model in (Order, OrderService, OrderPart, SyncConflict, SyncEvent):
        result[model.__tablename__] = [
            {column.name: (value.isoformat() if hasattr(value, 'isoformat') else value)
             for column in model.__table__.columns
             for value in [getattr(row, column.name)]}
            for row in db.scalars(select(model).order_by(model.id))]
    result['fingerprint'] = digest(result)
    return result


def reconcile(db, plan):
    from app.services.sync_service import materialize_event, ack_event
    from app.services.replication_lock import lock_replication
    from app.services.sync_emit_service import emit_server_event
    lock_replication(db)
    if db.bind.dialect.name == 'postgresql':
        db.execute(text("SET LOCAL lock_timeout = '5s'"))
        db.execute(text('LOCK TABLE orders, order_services, order_parts, sync_events, sync_conflicts '
                        'IN SHARE ROW EXCLUSIVE MODE'))
    marker = 'desktop-authority-' + digest(plan)[:32]
    previous = db.scalar(select(AuditLog).where(AuditLog.audit_uid == marker))
    if previous:
        return json.loads(previous.after_json)
    assert snapshot(db)['fingerprint'] == plan['server_fingerprint'], 'Server changed; take a fresh snapshot'
    org, branch = plan['organization_uid'], plan['branch_uid']
    source = plan['device_uid']
    before = snapshot(db)
    for event in db.scalars(select(SyncEvent).where(SyncEvent.id.in_(plan['server_event_ids']))):
        assert event.organization_uid in ('', org) and event.branch_uid in ('', branch), 'Scope mismatch'
        event.status = 'superseded'
    # Historical pre-scope events stay untouched for other devices. This desktop
    # explicitly rejected those old versions, so they are no longer competing edits.
    for event in db.scalars(select(SyncEvent).where(SyncEvent.id.in_(plan['legacy_event_ids']))):
        assert event.organization_uid == '' and event.branch_uid == 'local-branch'
        ack_event(db, event.event_uid, source)
    for conflict in db.scalars(select(SyncConflict).where(SyncConflict.id.in_(plan['server_conflict_ids']))):
        assert conflict.resolution == 'pending'
        conflict.resolution = 'desktop_authoritative'
        conflict.resolved_by = marker
        conflict.resolved_at = datetime.now(timezone.utc).replace(tzinfo=None)
    published = []
    for uid in plan['obsolete_orders']:
        materialize_event(db, {'entity_type': 'work_order', 'entity_uid': uid, 'operation': 'delete'}, org, branch)
        published.append(emit_server_event(db, 'work_order', uid, {'order_uid': uid, 'status': 'deleted', 'deleted': True},
            operation='delete', organization_uid=org, branch_uid=branch, source_device_uid=source))
    for item in plan['canonical']:
        uid, payload, related = item['uid'], item['payload'], item['related']
        for relation, model, field in [('services', OrderService, 'service_uid'), ('parts', OrderPart, 'part_uid')]:
            desired = {r[field] for r in related[relation]}
            assert len(desired) == len(related[relation]), 'Duplicate child UID'
            for row in related[relation]:
                existing = db.scalar(select(model).where(getattr(model, field) == row[field]))
                assert not existing or existing.order_uid == uid, 'Cross-order child collision'
            for extra in db.scalars(select(model).where(model.order_uid == uid)):
                if getattr(extra, field) not in desired:
                    child_type = 'work_order_service' if relation == 'services' else 'work_order_part'
                    published.append(emit_server_event(db, child_type, getattr(extra, field),
                        {field: getattr(extra, field), 'order_uid': uid}, operation='delete',
                        organization_uid=org, branch_uid=branch, source_device_uid=source))
                    db.delete(extra)
        existing = db.scalar(select(Order).where(Order.order_uid == uid))
        operation = 'restore' if existing and existing.status == 'deleted' else 'upsert'
        materialize_event(db, {'entity_type': 'work_order', 'entity_uid': uid, 'operation': operation,
                              'payload': payload, 'related': related}, org, branch)
        db.flush()
        order = db.scalar(select(Order).where(Order.order_uid == uid))
        # This is an explicit operator choice, not timestamp-based conflict merging.
        order.payload_json = json.dumps(payload, ensure_ascii=True)
        for relation, model, field in [('services', OrderService, 'service_uid'), ('parts', OrderPart, 'part_uid')]:
            for row in related[relation]:
                child = db.scalar(select(model).where(getattr(model, field) == row[field]))
                child.payload_json = json.dumps(row, ensure_ascii=True)
        published.append(emit_server_event(db, 'work_order', uid, payload, related=related, operation=operation,
            organization_uid=org, branch_uid=branch, source_device_uid=source))
    db.flush()
    verify(db, plan)
    receipt = {'plan_hash': digest(plan), 'orders': len(plan['canonical']),
               'retired_orders': len(plan['obsolete_orders']), 'published_events': published,
               'server_conflicts_resolved': len(plan['server_conflict_ids'])}
    db.add(AuditLog(audit_uid=marker, actor_role='owner', actor_uid='authorized-operator',
        action='desktop_authority_reconciliation', entity_type='work_order', entity_uid=marker,
        before_json=json.dumps(before, ensure_ascii=True), after_json=json.dumps(receipt),
        payload_json=json.dumps({'reason': 'User explicitly selected desktop orders as truth', 'plan_hash': digest(plan)}),
        source='maintenance'))
    db.flush()
    return receipt


def verify(db, plan):
    for item in plan['canonical']:
        order = db.scalar(select(Order).where(Order.order_uid == item['uid']))
        assert order and json.loads(order.payload_json) == item['payload']
        assert order.number == str(item['payload']['number']) and order.status == item['payload']['status']
        for relation, model, field in [('services', OrderService, 'service_uid'), ('parts', OrderPart, 'part_uid')]:
            actual = {getattr(r, field): json.loads(r.payload_json)
                      for r in db.scalars(select(model).where(model.order_uid == item['uid']))}
            assert actual == {r[field]: r for r in item['related'][relation]}
    for uid in plan['obsolete_orders']:
        assert db.scalar(select(Order).where(Order.order_uid == uid)).status == 'deleted'


def selftest(folder):
    from sqlalchemy import create_engine, BigInteger
    from sqlalchemy.orm import Session
    from sqlalchemy.ext.compiler import compiles
    from app.models.core import Base, PlanningDay
    compiles(BigInteger, 'sqlite')(lambda element, compiler, **kw: 'INTEGER')
    engine = create_engine('sqlite://')
    Base.metadata.create_all(engine)
    original = json.loads((folder / 'server-snapshot.json').read_text())
    plan = json.loads((folder / 'plan.json').read_text())
    with Session(engine) as db:
        for model in (Order, OrderService, OrderPart, SyncConflict, SyncEvent):
            for raw in original[model.__tablename__]:
                data = dict(raw)
                for column in model.__table__.columns:
                    if column.type.__class__.__name__ == 'DateTime' and data[column.name]:
                        data[column.name] = datetime.fromisoformat(data[column.name])
                db.add(model(**data))
        db.add(PlanningDay(day='2026-05-28', note='preserve calendar', details_json='{}'))
        db.commit()
        assert snapshot(db)['fingerprint'] == plan['server_fingerprint']
        changed = dict(plan, server_fingerprint='wrong')
        try:
            reconcile(db, changed)
            raise RuntimeError('Missing concurrent-change guard')
        except AssertionError:
            db.rollback()
        assert snapshot(db)['fingerprint'] == plan['server_fingerprint']
        reconcile(db, plan)
        db.rollback()
        assert snapshot(db)['fingerprint'] == plan['server_fingerprint']
        receipt = reconcile(db, plan)
        db.commit()
        verify(db, plan)
        assert db.get(PlanningDay, '2026-05-28').note == 'preserve calendar'
        from app.services import sync_service
        if hasattr(sync_service, '_record_conflict_if_needed'):
            for item in plan['canonical']:
                event = {'entity_type': 'work_order', 'entity_uid': item['uid'], 'operation': 'upsert',
                         'payload': dict(item['payload'], status='open')}
                assert sync_service._record_conflict_if_needed(db, event, plan['device_uid']) is None
        count = db.scalar(select(text('count(*)')).select_from(SyncEvent))
        assert reconcile(db, plan) == receipt
        db.commit()
        assert db.scalar(select(text('count(*)')).select_from(SyncEvent)) == count
        print(json.dumps({'isolated_checks': ['concurrency_guard', 'transaction_rollback', 'canonical_graphs',
                                              'calendar_preserved', 'next_desktop_edit', 'idempotent_repeat'], 'passed': 6}))


if __name__ == '__main__' and sys.argv[1] == 'test':
    selftest(Path(sys.argv[2]))
elif __name__ == '__main__' and sys.argv[1] == 'verify':
    plan = json.loads(Path(sys.argv[2]).read_text())
    from app.api.v1.mobile import _is_master_allowed
    with SessionLocal() as db:
        db.execute(text('SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY'))
        verify(db, plan)
        affected = {r['uid'] for r in plan['canonical']} | set(plan['obsolete_orders'])
        child_uids = {r.service_uid for r in db.scalars(select(OrderService).where(OrderService.order_uid.in_(affected)))}
        child_uids |= {r.part_uid for r in db.scalars(select(OrderPart).where(OrderPart.order_uid.in_(affected)))}
        replay = [e for e in db.scalars(select(SyncEvent))
                  if e.source_device_uid != plan['device_uid']
                  and e.organization_uid in ('', plan['organization_uid'])
                  and e.branch_uid in ('', plan['branch_uid'])
                  and e.status in ('received', 'materialized', 'acked', 'delivered')
                  and (e.entity_type == 'work_order' and e.entity_uid in affected
                       or e.entity_type in ('work_order_service', 'work_order_part') and e.entity_uid in child_uids)]
        assert not replay, 'Old versions can still replay to desktop'
        for item in plan['canonical']:
            subject = str(item['payload'].get('master_id') or '')
            if subject:
                assert _is_master_allowed(subject, 'master', item['payload'])
                assert not _is_master_allowed('unrelated-master-check', 'master', item['payload'])
        remaining = sum(r.resolution == 'pending' for r in db.scalars(select(SyncConflict)))
        print(json.dumps({'canonical_graphs_verified': len(plan['canonical']), 'stale_replay_events': len(replay),
                          'server_pending_conflicts': remaining, 'master_visibility_verified': True}))
        db.rollback()
elif __name__ == '__main__' and sys.argv[1] == 'apply':
    plan = json.loads(Path(sys.argv[2]).read_text())
    with SessionLocal() as db:
        receipt = reconcile(db, plan)
        db.commit()
        verify(db, plan)
        Path(sys.argv[3]).write_text(json.dumps(receipt), encoding='utf-8')
        print(json.dumps({k: v for k, v in receipt.items() if k != 'published_events'}))
elif __name__ == '__main__' and sys.argv[1] == 'audit':
    with SessionLocal() as db:
        db.execute(text('SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY'))
        data = snapshot(db)
        Path(sys.argv[2]).write_text(json.dumps(data, ensure_ascii=True), encoding='utf-8')
        print(json.dumps({'counts': {key: len(value) for key, value in data.items() if isinstance(value, list)},
                          'pending_conflicts': sum(r['resolution'] == 'pending' for r in data['sync_conflicts'])}))
        db.rollback()
