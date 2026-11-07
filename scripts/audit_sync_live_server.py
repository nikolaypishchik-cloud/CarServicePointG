"""Read-only operational diagnostics; never selects credentials or client contacts."""
import json
from collections import Counter
from sqlalchemy import select, text
from app.db.session import SessionLocal
from app.models.core import Order, OrderService, OrderPart, SyncEvent, MobilePairing, SyncConflict
from app.api.v1.mobile import _is_master_allowed

with SessionLocal() as db:
    db.execute(text("SET TRANSACTION READ ONLY"))
    pairings = db.scalars(select(MobilePairing).where(MobilePairing.claimed_at.is_not(None))).all()
    identities = sorted({(p.employee_uid, p.role) for p in pairings})
    orders = db.scalars(select(Order)).all()
    parts = db.scalars(select(OrderPart)).all()
    services = db.scalars(select(OrderService)).all()
    print(json.dumps({"counts": {"orders": len(orders), "parts": len(parts), "services": len(services)},
                      "paired_identities": identities}, ensure_ascii=True))
    terminal = {"completed", "closed", "закритий", "закрито", "done", "виконано", "paid", "archived", "deleted", "planned"}
    for order in orders:
        payload = json.loads(order.payload_json or "{}")
        if (order.status or "").lower() in terminal:
            continue
        print(json.dumps({"open_order": {"uid": order.order_uid, "number": order.number, "status": order.status,
            "master_id": payload.get("master_id"), "master": payload.get("master"), "assigned_to": payload.get("assigned_to"),
            "visible_for": [uid for uid, role in identities if _is_master_allowed(uid, role, payload)],
            "services": [{"uid": s.service_uid, "status": json.loads(s.payload_json or "{}").get("status"),
                          "assigned_to": json.loads(s.payload_json or "{}").get("assigned_to")}
                         for s in services if s.order_uid == order.order_uid]}}, ensure_ascii=True))
    recent = db.scalars(select(SyncEvent).where(SyncEvent.source_device_uid == "mobile-panel").order_by(SyncEvent.id.desc()).limit(12)).all()
    print(json.dumps({"recent_mobile": [{"event_uid": e.event_uid, "type": e.entity_type, "entity_uid": e.entity_uid,
                      "operation": e.operation, "status": e.status, "created": str(e.created_at)} for e in recent]}))
    for conflict in db.scalars(select(SyncConflict).where(SyncConflict.resolution == "pending")):
        print(json.dumps({"pending_conflict": {"uid": conflict.entity_uid, "type": conflict.conflict_type}}, ensure_ascii=True))
    print(json.dumps({"order_graphs": [{"uid": o.order_uid,
        "parts": [{"uid": p.part_uid, "values": {k: json.loads(p.payload_json or "{}").get(k) for k in ("name", "qty", "installed")}} for p in parts if p.order_uid == o.order_uid],
        "services": [{"uid": s.service_uid, "values": {k: json.loads(s.payload_json or "{}").get(k) for k in ("name", "qty", "status", "completed_at")}} for s in services if s.order_uid == o.order_uid]}
        for o in orders if str(o.number) in {"9", "15"}]}, ensure_ascii=True))
    db.rollback()
