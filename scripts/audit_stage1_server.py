"""Offline audit: synthetic SQLite only; no production credentials or network."""
import json
import math
import statistics
import time
from sqlalchemy import BigInteger, create_engine, select
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.orm import Session
from sqlalchemy.pool import StaticPool
from fastapi.testclient import TestClient
from app.db.base import Base
from app.db.session import get_db
from app.core.security import current_identity
from app.main import app
from app.models.core import Order


@compiles(BigInteger, "sqlite")
def integer_type(element, compiler, **kwargs):
    return "INTEGER"


engine = create_engine("sqlite://", connect_args={"check_same_thread": False}, poolclass=StaticPool)
Base.metadata.create_all(engine)
db = Session(engine, autoflush=False)
identity = {"sub": "audit-owner", "role": "owner", "device_uid": "audit-device", "claims": {}}
app.dependency_overrides[get_db] = lambda: db
app.dependency_overrides[current_identity] = lambda: identity
client = TestClient(app)
result = {"scope": "in-process HTTP, synthetic SQLite, authenticated identity substituted; not login/network/desktop timing", "checks": {}, "timings": {}}


def post(path, payload):
    response = client.post("/api/v1" + path, json=payload)
    assert response.status_code == 200, (path, response.status_code, response.text)
    return response


def measure(label, operation):
    samples = []
    for i in range(33):
        start = time.perf_counter()
        operation(i)
        elapsed = (time.perf_counter() - start) * 1000
        if i >= 3:
            samples.append(elapsed)
    result["timings"][label] = {"n": len(samples), "median_ms": round(statistics.median(samples), 3),
        "p95_ms": round(sorted(samples)[math.ceil(.95 * len(samples)) - 1], 3)}


measure("server_create_order", lambda i: post("/orders", {"order_uid": f"bench-{i}", "number": str(i), "total_amount": 1000}))
measure("server_close_order", lambda i: post("/orders", {"order_uid": f"bench-{i}", "status": "closed"}))
post("/approvals", {"approval_uid": "audit-approval", "order_uid": "bench-1", "total": 1000})
measure("staff_approval_decision_not_customer_flow", lambda i: post("/approvals/audit-approval/decision", {"status": "approved"}))

post("/orders", {"order_uid": "zero-test", "total_amount": 1000})
post("/orders", {"order_uid": "zero-test", "total_amount": 0})
db.expire_all()
result["checks"]["setting_order_total_to_zero"] = {"expected": 0, "actual": db.scalar(select(Order).where(Order.order_uid == "zero-test")).total_amount}

identity.update(sub="audit-master", role="master")
db.close()
Base.metadata.drop_all(engine)
Base.metadata.create_all(engine)
db = Session(engine, autoflush=False)
db.add(Order(order_uid="permission-test", total_amount=1000))
db.commit()
direct = client.post("/api/v1/orders", json={"order_uid": "permission-test", "total_amount": 7})
sync = post("/sync/push", {"device_uid": "claimed-other-device", "organization_uid": "claimed-other-org", "events": [
    {"event_uid": "audit-permission-event", "entity_type": "work_order", "entity_uid": "permission-test", "operation": "upsert",
     "created_at": "2026-09-30T10:00:00", "payload": {"total_amount": 7}}]})
db.expire_all()
result["checks"]["master_finance_via_sync"] = {"direct_order_http": direct.status_code, "sync_http": sync.status_code,
    "actual_total_after_sync": db.scalar(select(Order).where(Order.order_uid == "permission-test")).total_amount,
    "expected": "deny unauthorized financial mutation and mismatched device/org"}
pull = client.get("/api/v1/sync/pull", params={"device_uid": "audit-reader", "organization_uid": "claimed-other-org"})
result["checks"]["caller_selected_sync_scope"] = {"http": pull.status_code,
    "includes_other_org_event": any(e.get("event_uid") == "audit-permission-event" for e in pull.json().get("events", []))}

identity.update(sub="audit-client", role="client")
cabinet = client.get("/api/v1/client-cabinet/audit-client/service-book")
result["checks"]["client_cabinet_permission"] = {"http": cabinet.status_code, "expected": "own-data client access requires implementation"}
print(json.dumps(result, indent=2))
db.close()
engine.dispose()
