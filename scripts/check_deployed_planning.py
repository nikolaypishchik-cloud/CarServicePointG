"""Exercise installed modules using SQLite only, never the configured database."""
from sqlalchemy import create_engine
from sqlalchemy.orm import Session
from fastapi import HTTPException
from app.models.core import Base, PlanningDay
from app.services.planning_sync_service import apply_day

engine = create_engine("sqlite://")
Base.metadata.create_all(engine)
with Session(engine) as db:
    apply_day(db, "2030-10-01", {"note": "09:00 Synthetic car A"})
    db.commit()
    assert db.get(PlanningDay, "2030-10-01").note == "09:00 Synthetic car A"
    try:
        apply_day(db, "2030-10-01", {"note": "09:00 Synthetic car B"})
    except HTTPException as error:
        assert error.status_code == 409
        db.rollback()
    else:
        raise AssertionError("Concurrent calendar edit was not rejected")
print("Installed planning modules: write and conflict protection passed")
