"""Create a consistent SQLite audit set; production connections are read-only."""
import argparse
import hashlib
import json
import sqlite3
from contextlib import ExitStack
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    destination = args.destination.resolve()
    if destination == source or source in destination.parents:
        raise ValueError("Audit output must be outside the production directory")
    destination.mkdir(parents=True, exist_ok=False)
    with ExitStack() as stack:
        connections = []
        for path in sorted(source.glob("*.db")):
            connection = sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)
            stack.callback(connection.close)
            connection.execute("PRAGMA query_only=ON")
            connections.append((path, connection, connection.execute("PRAGMA data_version").fetchone()[0]))
        if not connections:
            raise ValueError("No source databases")
        manifest = []
        for path, connection, version in connections:
            target = destination / path.name
            with sqlite3.connect(target) as copied:
                connection.backup(copied)
                if copied.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
                    raise ValueError("Copy integrity check failed: " + path.name)
            manifest.append({"file": path.name, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
        # Refuse a mixed-time set if another process committed during copying.
        for path, connection, version in connections:
            if connection.execute("PRAGMA data_version").fetchone()[0] != version:
                raise RuntimeError("Source changed during copying; do not use this set: " + path.name)
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
