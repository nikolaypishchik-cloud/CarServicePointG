"""Add a separate empty receipt ledger without rewriting existing financial data."""
from sqlalchemy import inspect, text
from app.db.session import engine
from app.models.core import Order  # Register the FK target.
from app.models.client_payment import ClientPayment
import json


def install(connection):
    if connection.dialect.name != "postgresql":
        raise RuntimeError("Guarded installer requires PostgreSQL")
    connection.execute(text("SET LOCAL lock_timeout='5s'"))
    connection.execute(text("SET LOCAL statement_timeout='60s'"))
    names = sorted(inspect(connection).get_table_names())
    quote = connection.dialect.identifier_preparer.quote
    if "client_payments" in names:
        actual = {c["name"] for c in inspect(connection).get_columns("client_payments")}
        if actual != set(ClientPayment.__table__.columns.keys()):
            raise RuntimeError("Existing receipt schema differs; no automatic overwrite")
        return {"already_installed": True}
    connection.execute(text("LOCK TABLE " + ",".join(quote(n) for n in names) + " IN SHARE MODE"))
    def fingerprints():
        return {name: tuple(connection.execute(text(
            "SELECT count(*), md5(coalesce(string_agg(digest,'' ORDER BY digest),'')) "
            "FROM (SELECT md5(row_to_json(t)::text) digest FROM " + quote(name) + " t) rows")).one()) for name in names}
    before = fingerprints()
    ClientPayment.__table__.create(connection)
    if fingerprints() != before:
        raise RuntimeError("Pre-existing data changed")
    return {"existing_tables_unchanged": len(names), "new_empty_table": "client_payments"}


if __name__ == "__main__":
    with engine.begin() as connection:
        print(json.dumps(install(connection)))
