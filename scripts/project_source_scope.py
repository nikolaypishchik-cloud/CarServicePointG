"""Owned project inputs; prune runtime/private/generated trees before traversal."""
from pathlib import Path
import os

SOURCE_DIRS = {'.github', 'cmake', 'docs', 'resources', 'scripts', 'server',
               'src', 'tests', 'web-booking', 'web-crm', 'mobile-android'}
SERVICE_DIRS = {'.codex', '.codex-attachments', 'build-crm-transfer-check',
                'build-release', 'build-single-instance-audit', 'dist', 'logs',
                'Output', 'private-data', 'tmp', 'design'}
# Exact locations only: src/node_modules and src/build must still be checked.
GENERATED = {'web-crm/node_modules', 'web-crm/dist', 'web-booking/node_modules',
             'web-booking/dist', 'mobile-android/.gradle', 'mobile-android/.kotlin',
             'mobile-android/build', 'mobile-android/app/build'}

def project_paths(root: Path):
    def failure(error):
        raise error
    for child in sorted(root.iterdir()):
        if child.is_file():
            yield child
        elif child.name in SOURCE_DIRS:
            for base, directories, files in os.walk(child, onerror=failure):
                parent = Path(base)
                directories[:] = sorted(d for d in directories
                    if (parent / d).relative_to(root).as_posix() not in GENERATED)
                for name in directories + sorted(files):
                    yield parent / name
