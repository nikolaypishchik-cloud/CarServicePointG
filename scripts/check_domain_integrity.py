#!/usr/bin/env python3
from __future__ import annotations

import sys
sys.dont_write_bytecode = True

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_FILES = {
    "order_create_edit": [
        "src/core/services/OrderWorkspaceService.cpp",
        "src/core/services/orders/OrderDialogCoordinator.cpp",
        "src/core/services/orders/OrderMutationFacade.cpp",
    ],
    "work_parts": [
        "src/core/services/orders/WorkPartsMutationService.cpp",
        "src/core/services/orders/WorkPartsReadQueryService.cpp",
        "src/core/services/orders/PartMutationService.cpp",
    ],
    "invoice_import": [
        "src/core/services/orders/CurrentOrdersInvoiceImportService.cpp",
        "src/core/services/orders/PartImportTextSupport.cpp",
    ],
    "attachments_dirty_state": [
        "src/core/services/orders/OrderAttachmentService.cpp",
        "src/core/services/orders/OrderDialogStateSignature.cpp",
        "src/ui/tabs/orders/WorkPartsEditorController.cpp",
    ],
    "outsource": [
        "src/core/services/orders/OutsourceJobService.cpp",
        "src/core/services/settings/OutsourceDirectorySettingsService.cpp",
    ],
    "planning_to_order": [
        "src/core/services/planning/PlanningRepository.cpp",
        "src/core/services/planning/PlanningQueryService.cpp",
    ],
    "archive_search": [
        "src/core/services/OrderArchiveRotationService.cpp",
        "src/core/services/OrderSearchAggregator.cpp",
        "src/core/services/OrderIdentityResolver.cpp",
    ],
    "reports_pdf": [
        "src/core/services/ReportsDataLoader.cpp",
        "src/core/services/ReportsPdfRenderer.cpp",
        "src/core/services/ReportsPdfLayoutSupport.cpp",
        "src/core/services/ReportsPdfPartsSupport.cpp",
    ],
    "backup_restore": [
        "src/core/backup/BackupManager.cpp",
        "src/core/backup/RestoreCoordinator.cpp",
        "src/core/services/NativeBackupRestoreService.cpp",
    ],
    "legacy_import_pipeline": [
        "src/core/services/legacyimport/LegacyImportSourceResolver.cpp",
        "src/core/services/legacyimport/MigrationStagingCoordinator.cpp",
        "src/core/services/legacyimport/MigrationVerifyCoordinator.cpp",
        "src/core/services/legacyimport/MigrationPromoteCoordinator.cpp",
        "src/core/services/legacyimport/LegacyImportExecutionSupport.cpp",
    ],
}

REQUIRED_SYMBOLS = {
    "src/core/services/OrderWorkspaceService.cpp": [
        "OrderWorkspaceService::",
    ],
    "src/core/services/orders/OrderMutationFacade.cpp": [
        "OrderMutationFacade::",
    ],
    "src/core/services/orders/CurrentOrdersInvoiceImportService.cpp": [
        "CurrentOrdersInvoiceAttachmentSpec",
        "importRowsDetailed",
        "OrderAttachmentService::setPartAttachment",
    ],
    "src/core/services/orders/OrderAttachmentService.cpp": [
        "namespace OrderAttachmentService",
        "setPartAttachment",
        "setServiceAttachment",
        "invoice_number",
        "invoice_date",
    ],
    "src/core/services/orders/WorkPartsMutationService.cpp": [
        "OrderAttachmentService::setPartAttachment",
    ],
    "src/core/services/orders/WorkPartsReadQueryService.cpp": [
        "OrderAttachmentService::partAttachmentPath",
    ],
    "src/ui/tabs/orders/WorkPartsEditorController.cpp": [
        "renumberServiceItems();",
        "renumberPartItems();",
        "RoleAttachmentDocumentNumber",
        "RoleAttachmentDocumentDate",
        "const bool hasAttachment = !path.trimmed().isEmpty() || !invoiceNumber.trimmed().isEmpty();",
    ],
    "src/core/services/orders/OutsourceJobService.cpp": [
        "OutsourceJobService::saveJob",
        "OutsourceJobService::loadServiceDisplayNames",
    ],
    "src/core/services/ReportsPdfRenderer.cpp": [
        "namespace ReportsPdfRenderer",
        "finalizePdf",
    ],
    "src/ui/tabs/reports/ReportsPdfPresentation.cpp": [
        "openExportedPdf",
        "QDesktopServices::openUrl",
        "QMessageBox::warning",
    ],
    "src/core/services/ReportsPdfPartsSupport.cpp": [
        "invoiceNumber",
        "cells << row.invoiceNumber",
    ],
    "src/core/backup/RestoreCoordinator.cpp": [
        "RestoreCoordinator::restoreSnapshot",
    ],
    "src/core/services/NativeBackupRestoreService.cpp": [
        "NativeBackupRestoreService::",
    ],
    "src/core/services/legacyimport/MigrationStagingCoordinator.cpp": [
        "MigrationStagingCoordinator::",
    ],
    "src/core/services/legacyimport/MigrationVerifyCoordinator.cpp": [
        "MigrationVerifyCoordinator::",
    ],
    "src/core/services/legacyimport/MigrationPromoteCoordinator.cpp": [
        "MigrationPromoteCoordinator::",
    ],
    "src/core/backup/CrashRecoveryManager.cpp": [
        "dirtyState",
    ],
}

PROHIBITED_ARTIFACTS = [
    ".zip",
    "P0_EXECUTION_REPORT.txt",
    "P10_AUDIT_REPORT.txt",
    "FIX_BUILD_REPORT.txt",
    "LINK_FIX_REPORT.txt",
]


def fail(msg: str) -> None:
    print(f"[domain-integrity] ERROR: {msg}")
    sys.exit(1)


def main() -> int:
    for scenario, files in REQUIRED_FILES.items():
        missing = [path for path in files if not (ROOT / path).exists()]
        if missing:
            fail(f"scenario '{scenario}' is missing required files: {', '.join(missing)}")

    for rel_path, needles in REQUIRED_SYMBOLS.items():
        path = ROOT / rel_path
        if not path.exists():
            fail(f"required source missing while checking symbols: {rel_path}")
        text = path.read_text(encoding='utf-8', errors='ignore')
        for needle in needles:
            if needle not in text:
                fail(f"required symbol/text '{needle}' was not found in {rel_path}")

    root_entries = [p.name for p in ROOT.iterdir() if p.is_file()]
    bad_root = [name for name in root_entries if name.endswith('.zip') or name in PROHIBITED_ARTIFACTS]
    if bad_root:
        fail(f"unexpected packaging/report artifacts found in project root: {', '.join(sorted(bad_root))}")

    print("[domain-integrity] OK: critical P5 domain scenarios are structurally present.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
