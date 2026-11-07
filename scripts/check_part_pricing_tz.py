#!/usr/bin/env python3
from __future__ import annotations
import re
import sys
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []

def read(rel: str) -> str:
    p = ROOT / rel
    if not p.exists():
        errors.append(f"missing file: {rel}")
        return ""
    text = p.read_text(encoding="utf-8", errors="ignore")
    # P1/P2 monolith split keeps original translation-unit semantics through ordered .ipp fragments.
    if rel == 'src/ui/tabs/orders/WorkPartsDialogSupport.cpp':
        for fragment in sorted((ROOT / 'src/ui/tabs/orders/internal').glob('WorkPartsDialog*.ipp')):
            text += '\n' + fragment.read_text(encoding="utf-8", errors="ignore")
    return text

def must(rel: str, *needles: str) -> None:
    text = read(rel)
    for needle in needles:
        if needle not in text:
            errors.append(f"{rel}: missing required marker: {needle}")

def forbid(rel: str, *needles: str) -> None:
    text = read(rel)
    for needle in needles:
        if needle in text:
            errors.append(f"{rel}: forbidden legacy marker still present: {needle}")

# TZ v2.2: canonical pure engine, no stateful input price modes.
must('src/core/services/orders/PartPricingEngine.h',
     'double purchasePrice = 0.0;',
     'bool manualMarkupOverride = false;',
     'double manualMarkupPercent = 0.0;',
     'PartPricingResult calculate(const PartPricingInput& input, const PartMarkupPolicyService::PolicySnapshot& policy)')
forbid('src/core/services/orders/PartPricingEngine.h',
       'inputUnitPrice', 'priceInputMode', 'preserveStoredPurchasePrice',
       'storedPurchasePrice', 'storedClientPrice', 'storedMarkupPercent',
       'static PartPricingResult calculate(const PartPricingInput& input);',
       'calculateWithPolicyPercent')
must('src/core/services/orders/PartPricingEngine.cpp',
     'PartMarkupPolicyService::effectivePartMarkupPercent(input.partName, input.store, policy)',
     'result.clientPrice = FormatUtils::roundMoneyValue(result.purchasePrice * (1.0 + result.markupPercent / 100.0));',
     'PART_PRICING op=calculate')
forbid('src/core/services/orders/PartPricingEngine.cpp', 'calculateWithPolicyPercent', 'priceInputMode', 'inputUnitPrice')


# Engine must be called with an explicit policy snapshot; no implicit DB-load overload is allowed.
for rel in ['src/core/services/orders/PartMarkupPolicyService.cpp',
            'src/core/services/orders/PartRowPayloadBuilderService.cpp']:
    t = read(rel)
    if re.search(r'PartPricingEngine::calculate\s*\(\s*[^,;]+\s*\)', t):
        errors.append(f'{rel}: PartPricingEngine::calculate must receive explicit PolicySnapshot')

# Payload/repository/query contracts do not use legacy input mode.
must('src/core/services/orders/PartRowPayloadBuilderService.h', 'double purchasePrice = 0.0;', 'manualMarkupOverride', 'manualMarkupPercent')
forbid('src/core/services/orders/PartRowPayloadBuilderService.h', 'inputUnitPrice', 'priceInputMode', 'storedPurchasePrice', 'storedClientPrice')
must('src/core/services/orders/PartRowPayloadBuilderService.cpp', 'PartMarkupPolicyService::loadPolicySnapshot()', 'PartPricingEngine::calculate(pricingInput, policySnapshot)', 'row.purchasePrice = pricing.purchasePrice', 'row.clientPrice = pricing.clientPrice')
forbid('src/core/services/orders/PartRowPayloadBuilderService.cpp', 'row.priceInputMode', 'inputUnitPrice', 'storedClientPrice')
forbid('src/core/repository/PartRepository.h', 'priceInputMode')
forbid('src/core/repository/PartRepository.cpp', 'price_input_mode')
forbid('src/core/services/orders/WorkPartsQueryService.cpp', 'price_input_mode', 'priceInputMode')

# UI roles and one refresh path.
for rel in ['src/ui/tabs/orders/WorkPartsDialogModulePrelude.h', 'src/ui/tabs/orders/WorkPartsDialog.cpp']:
    must(rel, 'RolePurchasePrice', 'RoleMarkupPercent', 'RoleManualMarkupOverride')
    forbid(rel, 'RoleDisplayPrice', 'RolePriceInputMode', 'PriceInputModePurchaseImport', 'PriceInputModeClientDisplay')
text = read('src/ui/tabs/orders/WorkPartsDialogSupport.cpp')
if text.count('void refreshPartRowClientPricing(QTableWidget* table, int row, int changedColumn)') != 1:
    errors.append('WorkPartsDialogSupport.cpp: refreshPartRowClientPricing must have exactly one definition')
must('src/ui/tabs/orders/WorkPartsDialogSupport.cpp',
     'priceItem->setData(RolePurchasePrice, pricing.purchasePrice)',
     'priceItem->setData(RoleMarkupPercent, pricing.manualMarkupOverride ? pricing.markupPercent : 0.0)',
     'priceItem->setData(RoleManualMarkupOverride, pricing.manualMarkupOverride)',
     'QSet<QString> pricingGuard',
     'cachedPricingPolicySnapshot()')
forbid('src/ui/tabs/orders/WorkPartsDialogSupport.cpp', 'RoleDisplayPrice', 'RolePriceInputMode', 'PriceInputMode')

# Manual override and batch/import flows.
must('src/ui/tabs/orders/WorkPartsPricingSupport.cpp',
     'Повернути глобальну націнку',
     'Індивідуальна 0% означає',
     'priceItem->setData(RoleManualMarkupOverride, false)',
     'priceItem->setData(RoleMarkupPercent, 0.0)',
     'PART_PRICING op=recalc')
forbid('src/ui/tabs/orders/WorkPartsPricingSupport.cpp', 'PART_PRICING_MANUAL_OVERRIDE_SET', 'PART_PRICING_MANUAL_OVERRIDE_RESET')
must('src/ui/tabs/orders/WorkPartsCrudController.cpp',
     'confirmExplicitZeroPartMarkup',
     'manualMarkupOverride = hasManualMarkup',
     'snapshot.manualMarkupOverride && snapshot.markupPercent > 0.0',
     'PART_PRICING op=save')
must('src/ui/tabs/orders/WorkPartsImportCoordinator.cpp',
     'applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColPrice, uiRow.priceText)',
     'priceItem->setData(RolePurchasePrice',
     'priceItem->setData(RoleManualMarkupOverride, false)',
     'priceItem->setData(RoleMarkupPercent, 0.0)',
     'refreshPartRowClientPricing(guard->m_partsTable, row, PartColPrice)',
     'PART_PRICING op=import')
forbid('src/ui/tabs/orders/WorkPartsImportCoordinator.cpp', 'RoleDisplayPrice', 'RolePriceInputMode', 'PriceInputModePurchaseImport', 'PART_PRICING_IMPORT_APPLIED')
must('src/core/services/orders/CurrentOrdersInvoiceImportService.cpp',
     'input.purchasePrice = FormatUtils::roundMoneyValue(price)',
     'input.sourceType = QStringLiteral("import")',
     'PART_PRICING op=import')
forbid('src/core/services/orders/CurrentOrdersInvoiceImportService.cpp', 'inputUnitPrice', 'priceInputMode', 'PART_PRICING_IMPORT_CURRENT_ORDERS', 'INVOICE_IMPORT_DIRECT')

# Settings/recalc/report paths.
must('src/core/services/settings/FinanceSettingsService.h', 'class MarkupSettingsBroadcaster final : public QObject', 'Q_OBJECT', 'markupSettingsRevision', 'notifyMarkupSettingsChanged', 'markupSettingsBroadcaster')
must('src/core/services/settings/FinanceSettingsService.cpp', 'void MarkupSettingsBroadcaster::broadcastMarkupSettingsChanged()', 'notifyMarkupSettingsChanged();', 'g_markupSettingsRevision', 'MarkupSettingsBroadcaster', 'broadcastMarkupSettingsChanged')
must('src/ui/tabs/orders/WorkPartsDialog.cpp', 'FinanceSettingsService::markupSettingsBroadcaster()', 'refreshPartRowClientPricing(m_partsTable, row, -1)')
must('src/ui/tabs/orders/CurrentOrdersPage.cpp', 'FinanceSettingsService::markupSettingsBroadcaster()', 'queueRefreshAll(30)')
must('src/ui/tabs/reports/ReportsPage.cpp', 'FinanceSettingsService::markupSettingsBroadcaster()', 'queueRefreshAll(30)')
must('src/core/services/orders/PricingRecalculationService.cpp',
     'WHERE COALESCE(manual_markup_override,0)=0',
     'PartPricingEngine::calculate(pricingInput, policySnapshot)',
     'pricing_calc_version=2',
     'PART_PRICING op=recalc')
forbid('src/core/services/orders/PricingRecalculationService.cpp', 'PART_PRICING_RECALC_SKIPPED_MANUAL_OVERRIDE', 'calculateWithPolicyPercent', 'price_input_mode')
must('src/core/services/orders/PricingRecalculationService.h', 'PricingIntegrityCheckResult', 'checkPartPricingIntegrity')
must('src/core/services/orders/PricingRecalculationService.cpp', 'PART_PRICING_INVARIANT_VIOLATION op=integrity_dry_run', 'PART_PRICING op=integrity_dry_run')
must('src/core/repository/PartRepository.cpp', 'validatePartPricingInvariant', 'PART_PRICING_INVARIANT_VIOLATION op=%1', 'PartPricingEngine::calculate(input, policy)')
must('src/ui/tabs/settings/SettingsIntegrationsSection.cpp', 'Перевірити цілісність націнок', 'checkPartPricingIntegrity', 'recalcPartMarkupsFromSettings')
must('src/core/services/ReportsPdfSupportSnapshotFlow.cpp', 'PartPricingEngine::calculate(input, snapshot.policy)', 'explicitClientAmount > 0.009')
must('src/core/services/ReportsDataLoaderSnapshotFlow.cpp', 'PartPricingEngine::calculate(pricingInput, policySnapshot)')
forbid('src/core/services/ReportsPdfSupportLocalSupport.cpp', 'effectiveMarkupPercentForReports')
forbid('src/core/services/ReportsDataLoaderLocalSupport.cpp', 'effectiveMarkupPercentForReportsFromMaps')

# DB migration policy: schema may keep legacy column for compatibility, but fallback purchase migration must distinguish manual rows.
must('src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp',
     'COALESCE(manual_markup_override,0) = 0',
     'AND (COALESCE(markup_percent,0) <= 0 OR COALESCE(manual_markup_override,0) <> 0)',
     'purchase_amount = ROUND(purchase_price * COALESCE(NULLIF(qty,0),1), 2)')
forbid('src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp',
       'purchase_price = CASE WHEN purchase_price <= 0 THEN price ELSE purchase_price END',
       'purchase_amount = CASE WHEN purchase_amount <= 0 THEN amount ELSE purchase_amount END')
must('src/infrastructure/db/sqlite/DbDiagnostics.cpp',
     'firstAvailableExpr(cols, {"purchase_price", "cost_price"}, "0")',
     'firstAvailableExpr(cols, {"purchase_amount", "cost_amount"}, "0")')
# Preserve historical evidence; runtime pricing paths above still forbid its use.
must('src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp',
     "price_input_mode TEXT NOT NULL DEFAULT 'unknown'",
     'ensureColumn("work_order_parts", "price_input_mode"')
forbid('src/infrastructure/db/sqlite/DbSchemaAuditSupport.cpp', 'price_input_mode')
forbid('src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp', 'p.price_input_mode', 'c.price_input_mode')
must('src/infrastructure/db/sqlite/DbDiagnostics.cpp', 'cols.contains(QStringLiteral("price_input_mode"))', 'pricing_calc_version INTEGER NOT NULL DEFAULT 2')
must('src/core/services/orders/CurrentOrdersQueryService.cpp', 'tableOrViewUsableForSelect(QStringLiteral("router_work_order_parts")', 'CURRENT_ORDERS_SOURCE_RECOVERY reason=queryable_check_failed')
must('src/core/services/PartsQueryService.cpp', 'tableOrViewUsableForSelect(db, QStringLiteral("router_work_order_parts")', 'PARTS_SOURCE_RECOVERY reason=queryable_check_failed')

# Direct work-order part pricing math must only be in the engine.
write_paths = [
    'src/core/services/orders/PartRowPayloadBuilderService.cpp',
    'src/core/services/orders/CurrentOrdersInvoiceImportService.cpp',
    'src/ui/tabs/orders/WorkPartsPartPersistence.cpp',
    'src/ui/tabs/orders/WorkPartsCrudController.cpp',
    'src/ui/tabs/orders/WorkPartsImportCoordinator.cpp',
    'src/core/services/ReportsDataLoaderSnapshotFlow.cpp',
    'src/core/services/ReportsPdfSupportSnapshotFlow.cpp',
]
for rel in write_paths:
    t = read(rel)
    suspicious = re.findall(r'purchasePrice\s*\*\s*\(\s*1\.0\s*\+|markupPercent\s*/\s*100\.0|markup_percent\s*/\s*100\.0', t)
    if suspicious:
        errors.append(f"{rel}: direct work-part pricing formula outside engine: {suspicious[:3]}")


# Compile-safety guard for known high-risk regressions found during independent audit.
for rel in ['src/ui/tabs/orders/WorkPartsDialog.cpp', 'src/ui/tabs/orders/CurrentOrdersPage.cpp', 'src/ui/tabs/reports/ReportsPage.cpp']:
    forbid(rel, 'SIGNAL(markupSettingsChanged())')
    must(rel, '&FinanceSettingsService::MarkupSettingsBroadcaster::markupSettingsChanged')
for rel in ['src/ui/tabs/orders/WorkPartsDialogSupport.cpp']:
    t = read(rel)
    if re.search(r'int\s+row\s*,\s*\n\s*int\s+row\s*,', t):
        errors.append(f'{rel}: duplicate int row parameter detected')

# Test contract markers.
must('tests/regression/RegressionMarkupRecalculationTests.cpp',
     'manualOverrideHasPriorityOverPolicy',
     'manualOverrideZeroMeansPurchaseEqualsClient',
     'globalPolicyRecalcMustPreserveManualOverrideContract',
     'sameInputCannotBeMarkedUpTwiceAfterReopen')

if errors:
    print('[part_pricing_tz_gate] FAILED')
    for err in errors:
        print(' -', err)
    sys.exit(1)
print('[part_pricing_tz_gate] OK: canonical part pricing v2.2 contract is wired and guarded')
