"""Add only empty bay tables; verify every pre-existing table inside one transaction.

Run with the API's configured engine after a verified backup. Deliberately does
not advance alembic_version or run unrelated pending migrations.
"""
import json
from sqlalchemy import inspect, text
from app.db.session import engine
from app.models.bay_occupancy import BAY_TABLES


def install(connection):
    if connection.dialect.name != "postgresql":
        raise RuntimeError("This guarded installer requires PostgreSQL")
    connection.execute(text("SET LOCAL lock_timeout = '5s'"))
    connection.execute(text("SET LOCAL statement_timeout = '60s'"))
    existing = sorted(inspect(connection).get_table_names())
    new_names = {table.name for table in BAY_TABLES}
    if new_names.intersection(existing):
        raise RuntimeError("Bay tables already exist; stop for schema review, never overwrite")
    quote = connection.dialect.identifier_preparer.quote
    # Prevent concurrent writers changing the verification baseline during this short transaction.
    if existing:
        connection.execute(text("LOCK TABLE " + ",".join(quote(name) for name in existing) + " IN SHARE MODE"))

    def fingerprint():
        result = {}
        for name in existing:
            result[name] = tuple(connection.execute(text(
                "SELECT count(*), md5(coalesce(string_agg(digest, '' ORDER BY digest),'')) "
                "FROM (SELECT md5(row_to_json(t)::text) AS digest FROM " + quote(name) + " t) rows"
            )).one())
        return result

    before = fingerprint()
    for table in BAY_TABLES:
        table.create(connection, checkfirst=False)
    after = fingerprint()
    if before != after:
        raise RuntimeError("Existing data changed; rolling back schema addition")
    added = set(inspect(connection).get_table_names()) - set(existing)
    if added != new_names:
        raise RuntimeError("Unexpected schema change")
    for name in sorted(added):
        if connection.scalar(text("SELECT count(*) FROM " + quote(name))) != 0:
            raise RuntimeError("New tables must remain empty at installation")
    return {"existing_tables_unchanged": len(existing), "new_empty_tables": sorted(added),
            "alembic_version_unchanged": True}


if __name__ == "__main__":
    with engine.begin() as connection:
        result = install(connection)
    print(json.dumps(result, sort_keys=True))
