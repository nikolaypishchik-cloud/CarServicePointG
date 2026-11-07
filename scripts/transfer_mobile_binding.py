"""Explicit, audited transfer of one device; never issues or prints access tokens."""
import argparse
import secrets
from datetime import datetime
from uuid import uuid4
from sqlalchemy import select
from app.db.session import SessionLocal
from app.models.core import Device, MobilePairing, User
from app.services.auth_security_service import lock_auth
from app.api.v1.mobile import _audit, _pairing_hash

parser = argparse.ArgumentParser()
parser.add_argument('--device', required=True)
parser.add_argument('--from-employee', required=True)
parser.add_argument('--to-employee', required=True)
args = parser.parse_args()
with SessionLocal.begin() as db:
    lock_auth(db)
    device = db.scalar(select(Device).where(Device.device_uid == args.device).with_for_update())
    assert device and device.device_type == 'mobile' and device.status == 'active', 'Active mobile device required'
    previous = db.scalar(select(MobilePairing).where(MobilePairing.claimed_device_uid == args.device,
        MobilePairing.claimed_at.is_not(None)).order_by(MobilePairing.claimed_at.desc(), MobilePairing.id.desc()).limit(1))
    assert previous, 'No current binding'
    if previous.employee_uid == args.to_employee:
        print('Already bound to requested employee; no changes')
    else:
        assert previous.employee_uid == args.from_employee, 'Source binding changed; refusing transfer'
        target = db.scalar(select(MobilePairing).where(MobilePairing.employee_uid == args.to_employee,
            MobilePairing.role == 'master').order_by(MobilePairing.id.desc()).limit(1))
        assert target, 'Owner-created target pairing required'
        owner = db.scalar(select(User).where(User.user_uid == target.created_by))
        assert owner and owner.status == 'active' and owner.role in {'owner','admin','manager'}, 'Active authorizing administrator required'
        user = db.scalar(select(User).where(User.user_uid == args.to_employee))
        assert user is None or (user.role == 'master' and user.status == 'active'), 'Target user cannot be elevated or inactive'
        now = datetime.utcnow()
        transfer = MobilePairing(pairing_uid='transfer-'+uuid4().hex,
            secret_hash=_pairing_hash(secrets.token_urlsafe(48)), employee_uid=target.employee_uid,
            employee_name=target.employee_name, role='master', server_url=target.server_url,
            expires_at=now, claimed_at=now, claimed_device_uid=args.device, created_by=owner.user_uid)
        db.add(transfer)
        _audit(db, owner.user_uid, 'mobile_device_transferred', 'device', args.device,
            {'from_employee_uid':args.from_employee, 'to_employee_uid':args.to_employee,
             'authorization':'Explicit owner confirmation in support chat; new QR claim required'},
            actor_role=owner.role)
        print('Device binding transferred; old employee token is revoked; claim a fresh target QR on phone')
