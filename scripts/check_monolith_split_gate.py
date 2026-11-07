#!/usr/bin/env python3
from __future__ import annotations
import sys
import hashlib
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
CPP_LIMIT = 1500
HEADER_LIMIT = 500
IPP_LIMIT = 1300

# Unmodified vendored MIT library; application size limits apply to owned code.
# Pin the complete file, including license, so edits require another review.
VENDORED_HEADERS = {
    'src/thirdparty/qrcodegen/qrcodegen.hpp':
        'b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793',
}

def size_error(path: Path, relative: str) -> str | None:
    if relative in VENDORED_HEADERS:
        if hashlib.sha256(path.read_bytes()).hexdigest() != VENDORED_HEADERS[relative]:
            return f'vendored header changed; review required: {relative}'
        return None
    limit = {'.cpp': CPP_LIMIT, '.h': HEADER_LIMIT, '.hpp': HEADER_LIMIT,
             '.ipp': IPP_LIMIT}.get(path.suffix)
    if limit is not None:
        count = line_count(path)
        if count > limit:
            return f'large {path.suffix} over {limit} lines: {relative} ({count})'
    return None

REQUIRED_SPLIT_MARKERS = {
    "src/ui/tabs/orders/WorkPartsInlineEditorControllerImpl.cpp": [
        "WorkPartsEditorRowHelpers.ipp",
        "WorkPartsEditorNavigation.ipp",
        "WorkPartsEditorCommitGuard.ipp",
        "WorkPartsEditorDelegateState.ipp",
    ],
    "src/core/import/InvoiceImportEngine.cpp": [
        "PdfTextExtractionStage.ipp",
        "ExcelExtractionStage.ipp",
        "InvoiceVendorDetector.ipp",
        "InvoiceRowsNormalizer.ipp",
        "InvoicePriceColumnResolver.ipp",
        "InvoiceImportPersistenceStage.ipp",
        "InvoiceImportDiagnostics.ipp",
    ],
    "src/app/update/UpdateServiceSupport.cpp": [
        "UpdateReleaseResolver.ipp",
        "UpdateDownloader.ipp",
        "UpdateInstallerRunner.ipp",
        "UpdateCleanupGuard.ipp",
        "UpdateVersionDetector.ipp",
    ],
    "src/ui/tabs/orders/WorkPartsDialogSupport.cpp": [
        "WorkPartsDialogBackupAndImport.ipp",
        "WorkPartsDialogRowState.ipp",
        "WorkPartsDialogPersistence.ipp",
    ],
    "src/ui/tabs/settings/SettingsPageLocalSupport.cpp": [
        "SettingsLocalLegacyPreview.ipp",
        "SettingsLocalBackupRestore.ipp",
        "SettingsLocalUiActions.ipp",
    ],
    "src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp": [
        "DbArchiveRoutingRegistryMirror.ipp",
        "DbArchiveRoutingShardImport.ipp",
        "DbArchiveRoutingWorkspaceState.ipp",
    ],
    "src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp": [
        "DbImportCatalogRunSetup.ipp",
        "DbImportCatalogRows.ipp",
        "DbImportCatalogPersistence.ipp",
    ],
}

def rel(p: Path) -> str:
    return p.relative_to(ROOT).as_posix()

def line_count(p: Path) -> int:
    with p.open(encoding="utf-8") as stream:
        return sum(1 for _ in stream)

def main() -> int:
    errors: list[str] = []
    for p in SRC.rglob("*"):
        if not p.is_file():
            continue
        r = rel(p)
        error = size_error(p, r)
        if error:
            errors.append(error)


    header = ROOT / "src/ui/tabs/orders/WorkPartsInlineEditorController.h"
    if header.exists():
        header_text = header.read_text(encoding="utf-8", errors="ignore")
        if ".ipp" in header_text or "WorkPartsEditor" in header_text and "#include" in header_text:
            errors.append("WorkPartsInlineEditorController.h must stay interface-only and must not include implementation fragments")

    for owner, fragments in REQUIRED_SPLIT_MARKERS.items():
        owner_path = ROOT / owner
        if not owner_path.exists():
            errors.append(f"missing split owner: {owner}")
            continue
        text = owner_path.read_text(encoding="utf-8", errors="ignore")
        for frag in fragments:
            if frag not in text:
                errors.append(f"split owner does not include required fragment {frag}: {owner}")
            if not list((owner_path.parent / "internal").glob(frag)) and not list((owner_path.parent / "../internal").glob(frag)):
                # The include path may point to a sibling internal directory through src-root includes;
                # fall back to global lookup to avoid false negatives after future owner moves.
                if not list(SRC.rglob(frag)):
                    errors.append(f"required split fragment missing: {frag} for {owner}")

    if errors:
        print("[monolith_split_gate] ERROR: monolith split acceptance failed", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("[monolith_split_gate] OK: .cpp/.h size limits and required P1/P2 fragments are enforced")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
