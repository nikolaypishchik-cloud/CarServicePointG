#!/usr/bin/env python3
# Static/fixture acceptance gate for hybrid VAT invoice detector TZ.
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    p = ROOT / path
    if not p.exists():
        raise AssertionError(f"missing required file: {path}")
    return p.read_text(encoding='utf-8', errors='ignore')


def require_file(path: str, min_size: int = 1) -> None:
    p = ROOT / path
    if not p.exists():
        raise AssertionError(f"missing required fixture file: {path}")
    if p.stat().st_size < min_size:
        raise AssertionError(f"fixture file is too small: {path}")

def require_magic(path: str, magic: bytes, min_size: int = 1) -> None:
    p = ROOT / path
    require_file(path, min_size)
    data = p.read_bytes()[:max(len(magic), 16)]
    if not data.startswith(magic):
        raise AssertionError(f"fixture file has wrong magic/header: {path}")

def require_not_text_html_xls(path: str) -> None:
    p = ROOT / path
    head = p.read_bytes()[:512].lower()
    if b'<html' in head or b'<table' in head or b'<?xml' in head:
        raise AssertionError(f"xls fixture must be a binary/OLE workbook, not HTML/XML text: {path}")

def require_no_ui_direct_manual_parse(path: str) -> None:
    text = read(path)
    if 'UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping' in text:
        raise AssertionError(f"UI path still calls parseWithConfirmedVatMapping directly: {path}")
    if 'runConfirmedVatMappingParseAsync' not in text:
        raise AssertionError(f"UI path does not use async confirmed VAT mapping parser: {path}")

def require(path: str, needle: str) -> None:
    text = read(path)
    if needle not in text:
        raise AssertionError(f"missing {needle!r} in {path}")

def require_all(path: str, needles) -> None:
    for n in needles:
        require(path, n)

def main() -> int:
    require_all('src/core/import/InvoiceImportTypes.h', [
        'struct UniversalInvoiceDetectionResult',
        'automaticImportAllowed',
        'manualMappingRequired',
        'physicalColumnConfidence',
    ])
    require_all('src/core/import/UniversalInvoiceColumnDetector.h', [
        'class UniversalInvoiceColumnDetector final',
        'analyzeAndParse',
        'parseWithConfirmedVatMapping',
    ])
    require_all('src/core/import/UniversalInvoiceColumnDetector.cpp', [
        'UniversalInvoiceColumnDetector::analyzeAndParse',
        'manualMappingRequired',
        'single-source-of-truth contract',
        'parseWithConfirmedVatMapping(rows,',
    ])
    require_all('src/core/import/ImportJob.cpp', [
        'detecting_columns',
        'UniversalInvoiceColumnDetector::analyzeAndParse',
        'columnDetectionPerformed = detection.performed',
        'manualMappingRequired = detection.manualMappingRequired',
        'rendering_canonical_pdf',
        'ensureInvoicePdfAttachmentFromRows',
        'canonicalPdfAttachmentPath',
    ])
    require_all('src/core/import/InvoiceAttachmentPdfService.cpp', [
        'Canonical visual standard for Excel-originated supplier invoices',
        'invoiceCanonicalTemplateHeaders',
        'buildCanonicalInvoiceRows',
        'Spreadsheet originals are always normalized to the canonical invoice',
        'ensureInvoicePdfAttachmentFromRows',
        'sourceIsSpreadsheetInvoiceFile(ext)',
        'invoiceCanonicalTemplateHasCurrencyColumn',
        'If the source invoice has no currency values, the currency column is not shown',
        'OEM номер',
        'extractInvoiceCompanyNameFromRows',
        'ППФ',
        'Юнік-трейд',
        'return QString::fromUtf8("Постачальник")',
        'Кільк\\n-ть',
        'Сума\\nEURO',
    ])
    require_all('src/core/import/internal/InvoiceImportPersistenceStage.ipp', [
        'Mandatory rule: imported part purchase price must be the VAT-included price',
        'invoiceHeaderHasVatExcludedMarker',
        'invoiceHeaderHasVatIncludedMarker',
        'Ціна з ПДВ',
        'Цена с НДС',
        'Price VAT',
        'VAT Price',
        'Price incl. VAT',
        'Вартість з ПДВ',
        'Ціна без ПДВ',
        'Price excl. VAT',
        'applySupplierExcelPdfVatRules',
        'avtonova_d',
        'invoiceAmountMatchesVatPrice',
        'report.totalConfidence >= 85',
        'draft.name.isEmpty()) continue',
        'qtyText.trimmed().isEmpty() || draft.priceText.trimmed().isEmpty()',
        'persistSupplierInvoiceTemplateFromManualConfirmation',
    ])
    require_all('src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp', [
        'runConfirmedVatMappingParseAsync',
        'invoice_manual_vat_mapping_parse_worker',
        'розбір не має виконуватися в UI-потоці',
    ])
    require_all('src/ui/tabs/orders/WorkPartsImportCoordinator.cpp', [
        'excelData.canonicalPdfAttachmentPath',
        'original Excel will not be stored/opened as invoice attachment',
    ])
    require_all('src/ui/tabs/orders/WorkPartsImportedRowsViewModel.cpp', [
        'isSpreadsheetInvoiceSource',
        'spreadsheetSource ? QString() : filePath',
    ])
    require_all('tests/regression/RegressionHybridInvoiceColumnDetectorTests.cpp', [
        'fixtureRowsAreParsedByDetectorNotOnlyAssertedInSource',
        'UniversalInvoiceColumnDetector::analyzeAndParse',
        'requireFileStartsWith',
        'D0CF11E0A1B11AE1',
    ])
    require_all('src/ui/tabs/orders/WorkPartsImportCoordinator.cpp', [
        'WorkPartsInvoiceColumnMappingDialog',
        'confirmWorkPartsInvoiceColumnMapping',
        'PDF (*.pdf)',
        'priceWithVatConfirmed',
        'import_manual_mapping_confirmed_async',
        'continueImportWithDrafts(parsedDrafts)',
    ])
    require_all('src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp', [
        'CurrentOrdersInvoiceColumnMappingDialog',
        'confirmCurrentOrdersInvoiceColumnMapping',
        'priceWithVatConfirmed',
        'finishLoadWithDrafts(parsedDrafts, manualError)',
    ])
    require_no_ui_direct_manual_parse('src/ui/tabs/orders/WorkPartsImportCoordinator.cpp')
    require_no_ui_direct_manual_parse('src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp')
    fixtures = {
        'tests/fixtures/invoices/elit_pdf_text_layer_vat.txt': ['Еліт', 'Ціна з ПДВ', 'Сума з ПДВ'],
        'tests/fixtures/invoices/avtonova_d_excel_vat.csv': ['Автонова-Д', 'Ціна без ПДВ', 'Ціна, грн з ПДВ'],
        'tests/fixtures/invoices/avtonova_d_excel_pdf_style_price_sum.csv': ['Автонова-Д', 'Ціна', 'EUR: 51,2; USD: 44,35', 'Сума EURO'],
        'tests/fixtures/invoices/unik_trade_excel_vat.csv': ['Юнік-трейд', 'Price incl. VAT'],
        'tests/fixtures/invoices/only_without_vat_blocked.csv': ['Ціна без ПДВ', 'Сума без ПДВ'],
        'tests/fixtures/invoices/ocr_pdf_common_errors.txt': ['Цiна з ПДB', '15O,OO'],
        'tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt': ['Видаткова накладна №А-04154240 від 16.04.26', 'OEM номер', 'Кільк', '-ть Од Ціна Сума Сума EURO', 'Всього до сплати: 11 897,00 грн'],
    }
    for path, needles in fixtures.items():
        require_all(path, needles)
    require_magic('tests/fixtures/invoices/avtonova_d_excel_vat.xlsx', b'PK\x03\x04', 1024)
    require_magic('tests/fixtures/invoices/unik_trade_excel_vat.xls', bytes.fromhex('D0CF11E0A1B11AE1'), 1024)
    require_not_text_html_xls('tests/fixtures/invoices/unik_trade_excel_vat.xls')
    require_magic('tests/fixtures/invoices/elit_pdf_text_layer_vat.pdf', b'%PDF', 256)
    require_magic('tests/fixtures/invoices/ocr_pdf_common_errors_image_only.pdf', b'%PDF', 1024)
    require_magic('tests/fixtures/invoices/avtonova_canonical_pdf_standard.pdf', b'%PDF', 1024)
    print('OK: hybrid VAT invoice detector TZ gate passed')
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except AssertionError as e:
        print(f'FAIL: {e}', file=sys.stderr)
        raise SystemExit(1)
