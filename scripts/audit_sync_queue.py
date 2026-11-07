"""Read-only sync diagnostics. Never prints credentials or business payloads."""
import argparse
import json
import sqlite3
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("database")
args = parser.parse_args()
db = sqlite3.connect(Path(args.database).resolve().as_uri() + "?mode=ro", uri=True)
queries = {
    "outbox": "select entity_type,status,count(*),substr(max(last_error),1,500) from sync_outbox group by 1,2",
    "inbox": "select entity_type,status,count(*),substr(max(apply_error),1,250) from sync_inbox group by 1,2",
    "conflict_schema": "select name from pragma_table_info('sync_conflicts')",
    "conflicts": "select entity_type,conflict_type,status,count(*) from sync_conflicts group by 1,2,3",
    "state": "select key,value from settings where key in ('settings.cloudsync.last_error','settings.cloudsync.last_push_at','settings.cloudsync.server_url')",
    "outbox_shape": "select entity_type,operation,json_extract(payload_json,'$.payload_source'),count(*) from sync_outbox where status<>'acked' group by 1,2,3",
}
for name, query in queries.items():
    try:
        print(json.dumps({name: db.execute(query).fetchall()}, ensure_ascii=True))
    except sqlite3.Error as error:
        print(json.dumps({name: str(error)}))
