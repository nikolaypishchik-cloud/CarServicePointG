"""Read-only inventory; never prints customer data or settings secrets."""
import argparse
import json
import sqlite3
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("databases", nargs="+")
args = parser.parse_args()
for path in map(Path, args.databases):
    result = {"path": str(path), "exists": path.exists()}
    if path.exists():
        with sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True) as db:
            result["integrity"] = db.execute("PRAGMA integrity_check").fetchall()
            result["counts"] = {}
            for table in ("work_orders", "work_order_services", "work_order_parts", "planning_days"):
                if db.execute("SELECT 1 FROM sqlite_master WHERE name=?", (table,)).fetchone():
                    result["counts"][table] = db.execute('SELECT COUNT(*) FROM "' + table + '"').fetchone()[0]
            result["schema"] = db.execute("SELECT name, sql FROM sqlite_master WHERE name IN ('work_orders','work_order_services','work_order_parts')").fetchall()
            result["indices"] = db.execute("SELECT name,sql FROM sqlite_master WHERE type='index' AND tbl_name='work_orders'").fetchall()
            result["pragmas"] = {p: db.execute("PRAGMA " + p).fetchall() for p in ("journal_mode", "page_size", "synchronous", "foreign_keys")}
    print(json.dumps(result, ensure_ascii=True, indent=2))
