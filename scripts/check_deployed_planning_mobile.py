"""Check installed planning modules using temporary data, never the live DB."""
import tempfile
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path
from threading import Barrier

from fastapi import HTTPException
from sqlalchemy import BigInteger, create_engine
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.orm import Session

from app.db.base import Base
from app.models.core import PlanningDay
from app.services.planning_sync_service import apply_day, planning_jobs, reserve


@compiles(BigInteger, "sqlite")
def sqlite_integer(element, compiler, **kw):
    return "INTEGER"


with tempfile.TemporaryDirectory() as folder:
    engine = create_engine("sqlite:///" + str(Path(folder) / "planning.sqlite"))
    Base.metadata.create_all(engine)
    barrier = Barrier(2)

    def book(master):
        with Session(engine) as db:
            barrier.wait(timeout=10)
            try:
                reserve(db, datetime(2030, 10, 1, 9), {"master_id": master, "source": "mobile"})
                db.commit()
                return master
            except HTTPException as error:
                db.rollback()
                assert error.status_code == 409
                return None

    with ThreadPoolExecutor(max_workers=2) as executor:
        winners = [result for result in executor.map(book, ["m1", "m2"]) if result]
    assert len(winners) == 1
    with Session(engine) as db:
        assert len(planning_jobs(db, winners[0])["items"]) == 1
        assert not planning_jobs(db, "m2" if winners[0] == "m1" else "m1")["items"]
        assert not planning_jobs(db)["items"]
        row = db.get(PlanningDay, "2030-10-01")
        before = row.note
        try:
            apply_day(db, "2030-10-01", {"note": "09:00 Synthetic conflicting car"})
        except HTTPException as error:
            assert error.status_code == 409
            db.rollback()
        else:
            raise AssertionError("Desktop overwrite was accepted")
        assert db.get(PlanningDay, "2030-10-01").note == before
    engine.dispose()
print("Installed planning: concurrent reservation, ownership and desktop conflict checks passed")
