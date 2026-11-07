"""Read-only identity and delivery diagnostics, excluding credentials and contacts."""
import json
import sqlite3
import sys
from pathlib import Path

db = sqlite3.connect(Path(sys.argv[1]).resolve().as_uri() + "?mode=ro", uri=True)
db.row_factory = sqlite3.Row
queries = {
    "open_orders": "SELECT number,order_uid,status,master_id FROM work_orders WHERE lower(status) NOT IN ('completed','closed','закритий','закрито','done','виконано','paid','archived','deleted','planned')",
    "pending_outbox": "SELECT entity_type,entity_uid,json_extract(payload_json,'$.payload.number') number,last_error FROM sync_outbox WHERE status IN ('pending','failed')",
    "recent_mobile": "SELECT event_uid,entity_type,entity_uid,status,apply_error,acked_at FROM sync_inbox WHERE source_device_uid='mobile-panel' ORDER BY id DESC LIMIT 15",
    "unacked": "SELECT status,count(*) FROM sync_inbox WHERE coalesce(acked_at,'')='' GROUP BY status",
    "conflict_fields": "SELECT entity_type,entity_uid,local_payload_json,remote_payload_json FROM sync_conflicts WHERE status='pending' AND entity_type='work_order'",
}
for name, sql in queries.items():
    rows = [dict(row) for row in db.execute(sql)]
    if name == "conflict_fields":
        for row in rows:
            local = json.loads(row.pop("local_payload_json") or "{}")
            remote = json.loads(row.pop("remote_payload_json") or "{}")
            remote = remote.get("payload", remote)
            row["number"] = local.get("number", remote.get("number"))
            row["differing_fields"] = sorted(k for k in remote if k in local and local[k] != remote[k])
    print(json.dumps({name: rows}, ensure_ascii=True))
