"""Run in the API container only after a PostgreSQL backup has been saved."""
from sqlalchemy import text
from app.db.session import engine

with engine.begin() as connection:
    connection.execute(text("SET LOCAL lock_timeout = '5s'"))
    connection.execute(text("ALTER TABLE sync_events ALTER COLUMN operation TYPE TEXT"))
    kind = connection.execute(text(
        "SELECT data_type FROM information_schema.columns "
        "WHERE table_name='sync_events' AND column_name='operation'"
    )).scalar_one()
    assert kind == "text", kind
    connection.execute(text("CREATE TEMP TABLE sync_operation_probe ON COMMIT DROP "
                            "AS SELECT operation FROM sync_events WITH NO DATA"))
    operation = "rewrite_legacy_database_to_canonical_layout"
    connection.execute(text("INSERT INTO sync_operation_probe VALUES (:operation)"),
                       {"operation": operation})
    assert connection.execute(text("SELECT operation FROM sync_operation_probe")).scalar_one() == operation
print("operation=TEXT; long-operation round-trip passed")
