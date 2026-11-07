"""Exercise installed pairing and replication modules against an isolated SQLite DB."""
import json
from datetime import datetime, timedelta
from sqlalchemy import BigInteger, create_engine, select
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.orm import Session
from fastapi import HTTPException
from app.db.base import Base
from app.models.core import MobilePairing, OrderService, SyncEvent
from app.models.auth_security import AuthSecurity
from app.api.v1.mobile import claim_mobile_pairing, _pairing_hash, my_jobs, complete_service
from app.schemas.common import MobilePairingClaimRequest
from app.services.sync_service import store_events, pull_events


@compiles(BigInteger, "sqlite")
def sqlite_integer(element, compiler, **kw):
    return "INTEGER"


engine = create_engine("sqlite://")
Base.metadata.create_all(engine)
with Session(engine) as db:
    db.add(MobilePairing(pairing_uid="test-pair", secret_hash=_pairing_hash("test-secret-for-isolated-check"),
        employee_uid="test-master", employee_name="Test", role="master",
        server_url="https://example.invalid", expires_at=datetime.utcnow()+timedelta(minutes=10)))
    db.commit()
    request = MobilePairingClaimRequest(pairing_uid="test-pair", secret="test-secret-for-isolated-check",
        device_uid="test-phone", device_name="Test")
    try:
        claim_mobile_pairing(request.model_copy(update={"secret":"wrong-secret-for-isolated-check"}), db)
        raise AssertionError("Invalid secret accepted")
    except HTTPException as error:
        assert error.status_code == 401
        db.rollback()
    token = claim_mobile_pairing(request, db)
    assert token["access_token"]
    assert db.scalar(select(AuthSecurity)) is not None
    try:
        claim_mobile_pairing(request, db)
        raise AssertionError("Pairing was reused")
    except HTTPException as error:
        assert error.status_code == 410
        db.rollback()
    for number, master in ((15,"test-master"),(474,"test-master"),(475,"other")):
        store_events(db,"test-desktop","","",[{
            "event_uid":f"test-{number}", "entity_type":"work_order", "entity_uid":f"order-{number}",
            "operation":"upsert", "payload":{"number":str(number),"status":"open","master_id":master},
            "related":{"services":[{"service_uid":f"service-{number}","title":"Oil","price":123}]}}])
    db.commit()
    identity={"sub":"test-master","role":"master","device_uid":"test-phone"}
    jobs=my_jobs(db=db,identity=identity)
    assert {str(item["order_number"]) for item in jobs["items"]} == {"15","474"}
    complete_service("service-474",{},db,identity)
    service=db.scalar(select(OrderService).where(OrderService.service_uid=="service-474"))
    assert json.loads(service.payload_json)["status"] == "completed"
    assert json.loads(service.payload_json)["price"] == 123
    received=pull_events(db,"test-desktop")
    assert any(item["entity_uid"]=="service-474" for item in received)
print("Installed server: pairing, wrong secret, replay protection, orders >11, master visibility, completion return passed")
