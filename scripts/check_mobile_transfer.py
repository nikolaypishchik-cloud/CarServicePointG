"""Test revocation after transfer without accessing the configured production DB."""
from datetime import datetime, timedelta
from sqlalchemy import BigInteger, create_engine
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.orm import Session
from fastapi import HTTPException
from app.db.base import Base
from app.models.core import Device, User, MobilePairing
from app.models.auth_security import AuthSecurity
from app.core.security import current_identity, create_access_token


@compiles(BigInteger, 'sqlite')
def sqlite_integer(element, compiler, **kw):
    return 'INTEGER'


engine = create_engine('sqlite://')
Base.metadata.create_all(engine)
with Session(engine) as db:
    for uid in ('first', 'second'):
        db.add(User(user_uid=uid, email=uid+'@example.invalid', role='master', status='active'))
    for uid in ('phone', 'other-phone'):
        db.add(Device(device_uid=uid, device_type='mobile', status='active'))
        db.add(MobilePairing(pairing_uid='binding-'+uid, employee_uid='first', role='master',
            claimed_device_uid=uid, claimed_at=datetime.utcnow()-timedelta(minutes=1),
            expires_at=datetime.utcnow(), secret_hash='not-a-usable-secret'))
    db.commit()
    old = create_access_token('first', {'device_uid':'phone'})
    other = create_access_token('first', {'device_uid':'other-phone'})
    assert current_identity(old, db)['sub'] == 'first'
    db.add(MobilePairing(pairing_uid='transfer', employee_uid='second', role='master',
        claimed_device_uid='phone', claimed_at=datetime.utcnow(), expires_at=datetime.utcnow(),
        secret_hash='not-a-usable-secret'))
    db.commit()
    try:
        current_identity(old, db)
        raise AssertionError('Old phone token still accepted')
    except HTTPException as error:
        assert error.status_code == 401
    assert current_identity(other, db)['sub'] == 'first'
    new = create_access_token('second', {'device_uid':'phone'})
    assert current_identity(new, db)['sub'] == 'second'
print('Transfer: old phone token rejected; new owner accepted; other phone unaffected')
