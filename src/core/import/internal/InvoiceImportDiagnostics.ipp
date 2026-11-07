// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

// performance_tz_100_completion_gate contract after invoice-import split:
// persistSupplierInvoiceTemplateIfUseful lives in InvoiceImportPersistenceStage.ipp.
// !invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue) lives in InvoiceImportParsedDraftStage.ipp.

static QString excelImportPythonScript()
{
    QString script;
    script.reserve(23696);
    script += QString::fromUtf8(R"PYX1(

import csv
import datetime
import html
import io
import json
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    try:
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='strict')
    except Exception:
        pass

NS = {'a': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
      'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'}
XMLSS_NS = 'urn:schemas-microsoft-com:office:spreadsheet'
OLE_MAGIC = bytes.fromhex('D0CF11E0A1B11AE1')
ZIP_MAGIC = bytes.fromhex('504B0304')


def local_name(tag):
    if not isinstance(tag, str):
        return ''
    return tag.rsplit('}', 1)[-1]


def child_elements(node, name):
    if node is None:
        return []
    return [child for child in list(node) if local_name(child.tag) == name]


def first_child(node, name):
    for child in child_elements(node, name):
        return child
    return None


def element_text(node):
    if node is None:
        return ''
    return ''.join(node.itertext()).strip()


def try_decode_text(data):
    for encoding in ('utf-8-sig', 'utf-16', 'utf-16-le', 'utf-16-be', 'cp1251', 'cp866'):
        try:
            text = data.decode(encoding)
            if text:
                return text
        except Exception:
            pass
    return data.decode('latin1', errors='ignore')


class TableHtmlParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.table_depth = 0
        self.row = None
        self.cell = None
        self.rows = []

    def handle_starttag(self, tag, attrs):
        tag = (tag or '').lower()
        if tag == 'table':
            self.table_depth += 1
        elif self.table_depth > 0 and tag == 'tr':
            self.row = []
        elif self.table_depth > 0 and tag in ('td', 'th'):
            self.cell = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data)

    def handle_endtag(self, tag):
        tag = (tag or '').lower()
        if self.table_depth > 0 and tag in ('td', 'th'):
            text = ' '.join(part.strip() for part in (self.cell or []) if part and part.strip())
            if self.row is not None:
                self.row.append(text.strip())
            self.cell = None
        elif self.table_depth > 0 and tag == 'tr':
            if self.row is not None:
                while self.row and not self.row[-1]:
                    self.row.pop()
                if any(cell.strip() for cell in self.row):
                    self.rows.append(self.row)
            self.row = None
        elif tag == 'table' and self.table_depth > 0:
            self.table_depth -= 1


def parse_html_rows(text):
    parser = TableHtmlParser()
    parser.feed(text)
    parser.close()
    return parser.rows


def parse_csv_rows(text):
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines:
        return []
    sample = '\n'.join(lines[:5])
    delimiter = ';'
    try:
        dialect = csv.Sniffer().sniff(sample, delimiters=';,	,')
        delimiter = dialect.delimiter
    except Exception:
        first = lines[0]
        counts = {';': first.count(';'), '	': first.count('	'), ',': first.count(',')}
        delimiter = max(counts, key=counts.get)
    reader = csv.reader(lines, delimiter=delimiter)
    rows = []
    for row in reader:
        cleaned = [str(cell).strip() for cell in row]
        while cleaned and not cleaned[-1]:
            cleaned.pop()
        if any(cleaned):
            rows.append(cleaned)
    return rows


def xmlss_index_attrib(cell):
    return (cell.attrib.get('{%s}Index' % XMLSS_NS) or
            cell.attrib.get('Index') or
            cell.attrib.get('ss:Index'))


def parse_xmlss_rows(text):
    root = ET.fromstring(text)
    worksheets = [node for node in root.iter() if local_name(node.tag) == 'Worksheet']
    if not worksheets:
        table_nodes = [node for node in root.iter() if local_name(node.tag) == 'Table']
    else:
        table_nodes = []
        for ws in worksheets:
            table = first_child(ws, 'Table')
            if table is not None:
                table_nodes.append(table)
    if not table_nodes:
        return []

    rows = []
    for table in table_nodes:
        for row_node in child_elements(table, 'Row'):
            row = []
            current_idx = 0
            for cell in child_elements(row_node, 'Cell'):
                idx_text = xmlss_index_attrib(cell)
                if idx_text:
                    try:
                        target_idx = max(0, int(idx_text) - 1)
                        while current_idx < target_idx:
                            row.append('')
                            current_idx += 1
                    except Exception:
                        pass
                data_node = first_child(cell, 'Data')
                value = element_text(data_node if data_node is not None else cell)
                row.append(value)
                current_idx += 1
            while row and not row[-1]:
                row.pop()
            if any(cell.strip() for cell in row):
                rows.append(row)
    return rows


def cell_value(cell, shared_strings, date_style_indexes):
    cell_type = cell.attrib.get('t', '')
    style_index = -1
    try:
        style_index = int(cell.attrib.get('s', '-1'))
    except Exception:
        style_index = -1

    if cell_type == 's':
        v = cell.find('a:v', NS)
        if v is None or v.text is None:
            return ''
        try:
            idx = int(v.text)
            return shared_strings[idx] if 0 <= idx < len(shared_strings) else ''
        except Exception:
            return ''

    if cell_type == 'inlineStr':
        return ''.join(t.text or '' for t in cell.findall('.//a:t', NS))

    v = cell.find('a:v', NS)
)PYX1");
    script += QString::fromUtf8(R"PYX2(
    raw = '' if v is None or v.text is None else str(v.text)
    if style_index in date_style_indexes and raw != '':
        try:
            number = float(raw)
            if number > 0:
                base = datetime.datetime(1899, 12, 30)
                value = base + datetime.timedelta(days=number)
                return value.strftime('%d.%m.%Y')
        except Exception:
            pass
    return raw


def read_shared_strings(zf):
    if 'xl/sharedStrings.xml' not in zf.namelist():
        return []
    root = ET.fromstring(zf.read('xl/sharedStrings.xml'))
    values = []
    for si in root.findall('a:si', NS):
        values.append(''.join(t.text or '' for t in si.findall('.//a:t', NS)))
    return values


def is_builtin_date_numfmt(num_fmt_id):
    return 14 <= num_fmt_id <= 22 or 45 <= num_fmt_id <= 47


def is_date_format_code(code):
    if not code:
        return False
    lowered = code.lower()
    return ('dd' in lowered and 'mm' in lowered and 'yy' in lowered) or ('yyyy' in lowered and 'mm' in lowered)


def read_date_style_indexes(zf):
    if 'xl/styles.xml' not in zf.namelist():
        return set()
    root = ET.fromstring(zf.read('xl/styles.xml'))
    custom_date_ids = set()
    numfmts = root.find('a:numFmts', NS)
    if numfmts is not None:
        for numfmt in numfmts.findall('a:numFmt', NS):
            try:
                num_fmt_id = int(numfmt.attrib.get('numFmtId', '-1'))
            except Exception:
                continue
            if is_date_format_code(numfmt.attrib.get('formatCode', '')):
                custom_date_ids.add(num_fmt_id)
    result = set()
    cellxfs = root.find('a:cellXfs', NS)
    if cellxfs is not None:
        for idx, xf in enumerate(cellxfs.findall('a:xf', NS)):
            try:
                num_fmt_id = int(xf.attrib.get('numFmtId', '-1'))
            except Exception:
                continue
            if num_fmt_id >= 0 and (is_builtin_date_numfmt(num_fmt_id) or num_fmt_id in custom_date_ids):
                result.add(idx)
    return result


def first_sheet_path(zf):
    workbook = ET.fromstring(zf.read('xl/workbook.xml'))
    rels = ET.fromstring(zf.read('xl/_rels/workbook.xml.rels'))
    rel_map = {rel.attrib.get('Id'): rel.attrib.get('Target') for rel in rels}
    sheets = workbook.find('a:sheets', NS)
    if sheets is None:
        raise RuntimeError('У файлі Excel не знайдено жодного аркуша.')
    sheet = None
    for node in sheets.findall('a:sheet', NS):
        sheet = node
        break
    if sheet is None:
        raise RuntimeError('У файлі Excel не знайдено жодного аркуша.')
    rel_id = sheet.attrib.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id')
    target = rel_map.get(rel_id)
    if not target:
        raise RuntimeError('Не вдалося знайти XML першого аркуша Excel.')
    target = target.lstrip('/')
    if not target.startswith('xl/'):
        target = 'xl/' + target
    return target


def column_index(cell_ref):
    letters = ''.join(ch for ch in cell_ref if ch.isalpha()).upper()
    result = 0
    for ch in letters:
        result = result * 26 + (ord(ch) - ord('A') + 1)
    return max(0, result - 1)


def read_xlsx_rows(path):
    with zipfile.ZipFile(path, 'r') as zf:
        shared_strings = read_shared_strings(zf)
        date_style_indexes = read_date_style_indexes(zf)
        sheet_path = first_sheet_path(zf)
        sheet = ET.fromstring(zf.read(sheet_path))
        rows = []
        for row in sheet.findall('.//a:sheetData/a:row', NS):
            cell_map = {}
            max_index = -1
            for cell in row.findall('a:c', NS):
                ref = cell.attrib.get('r', '')
                idx = column_index(ref)
                max_index = max(max_index, idx)
                cell_map[idx] = cell_value(cell, shared_strings, date_style_indexes)
            if max_index < 0:
                continue
            rows.append([str(cell_map.get(i, '')) for i in range(max_index + 1)])
        return rows


FREESECT = 0xFFFFFFFF
ENDOFCHAIN = 0xFFFFFFFE
NOSTREAM = 0xFFFFFFFF


def codepage_to_encoding(codepage):
    mapping = {
        1200: 'cp1251',
        65001: 'utf-8',
        1250: 'cp1250',
        1251: 'cp1251',
        1252: 'cp1252',
        1253: 'cp1253',
        1254: 'cp1254',
        1255: 'cp1255',
        1256: 'cp1256',
        1257: 'cp1257',
        1258: 'cp1258',
        866: 'cp866',
        850: 'cp850',
        852: 'cp852',
    }
    return mapping.get(codepage, 'cp1251')


class OleFileMini:
    def __init__(self, data):
        if not data.startswith(OLE_MAGIC):
            raise RuntimeError('Файл не є OLE2 XLS контейнером.')
        self.data = data
        self.sector_size = 1 << int.from_bytes(data[30:32], 'little')
        self.mini_sector_size = 1 << int.from_bytes(data[32:34], 'little')
        self.dir_first_sector = int.from_bytes(data[48:52], 'little')
        self.mini_cutoff = int.from_bytes(data[56:60], 'little')
        self.mini_fat_first_sector = int.from_bytes(data[60:64], 'little')
        self.difat_first_sector = int.from_bytes(data[68:72], 'little')
        self.difat_sector_count = int.from_bytes(data[72:76], 'little')
        self.total_sectors = max(0, (len(data) - 512) // self.sector_size)
        self.fat = self._read_fat()
        self.dir_entries = self._read_directory()
        self.root = next((entry for entry in self.dir_entries if entry.get('type') == 5), None)
        self.mini_stream = self._read_regular_stream(self.root.get('start_sector', NOSTREAM), self.root.get('size', 0)) if self.root else b''
        self.mini_fat = self._read_mini_fat()

    def _sector(self, sid):
        offset = 512 + sid * self.sector_size
        return self.data[offset:offset + self.sector_size]

    def _chain(self, start_sid, fat=None):
        fat = self.fat if fat is None else fat
        chain = []
        seen = set()
        sid = start_sid
)PYX2");
    script += QString::fromUtf8(R"PYX3(
        while sid not in (ENDOFCHAIN, FREESECT, NOSTREAM) and 0 <= sid < len(fat) and sid not in seen:
            seen.add(sid)
            chain.append(sid)
            sid = fat[sid]
        return chain

    def _read_fat(self):
        difat = [int.from_bytes(self.data[76 + i * 4:80 + i * 4], 'little') for i in range(109)]
        next_sid = self.difat_first_sector
        for _ in range(self.difat_sector_count):
            if next_sid in (ENDOFCHAIN, FREESECT, NOSTREAM) or next_sid >= self.total_sectors:
                break
            sector = self._sector(next_sid)
            entries_per_sector = self.sector_size // 4 - 1
            for idx in range(entries_per_sector):
                difat.append(int.from_bytes(sector[idx * 4:(idx + 1) * 4], 'little'))
            next_sid = int.from_bytes(sector[self.sector_size - 4:self.sector_size], 'little')

        fat = []
        entries_per_sector = self.sector_size // 4
        for sid in difat:
            if sid in (FREESECT, ENDOFCHAIN, NOSTREAM) or sid >= self.total_sectors:
                continue
            sector = self._sector(sid)
            for idx in range(entries_per_sector):
                fat.append(int.from_bytes(sector[idx * 4:(idx + 1) * 4], 'little'))
        return fat

    def _read_regular_stream(self, start_sid, size):
        if start_sid in (ENDOFCHAIN, FREESECT, NOSTREAM) or size <= 0:
            return b''
        chunks = []
        for sid in self._chain(start_sid):
            chunks.append(self._sector(sid))
        return b''.join(chunks)[:int(size)]

    def _read_mini_fat(self):
        if self.mini_fat_first_sector in (ENDOFCHAIN, FREESECT, NOSTREAM):
            return []
        chunks = []
        for sid in self._chain(self.mini_fat_first_sector):
            chunks.append(self._sector(sid))
        data = b''.join(chunks)
        return [int.from_bytes(data[idx:idx + 4], 'little') for idx in range(0, len(data), 4) if idx + 4 <= len(data)]

    def _read_directory(self):
        raw = self._read_regular_stream(self.dir_first_sector, max(self.sector_size * 16, self.sector_size))
        entries = []
        for pos in range(0, len(raw), 128):
            chunk = raw[pos:pos + 128]
            if len(chunk) < 128:
                continue
            name_len = int.from_bytes(chunk[64:66], 'little')
            if name_len >= 2:
                name = chunk[:max(0, name_len - 2)].decode('utf-16le', errors='ignore')
            else:
                name = ''
            entries.append({
                'name': name,
                'type': chunk[66],
                'start_sector': int.from_bytes(chunk[116:120], 'little'),
                'size': int.from_bytes(chunk[120:128], 'little'),
            })
        return entries

    def open_stream(self, names):
        lowered = {name.lower() for name in names}
        entry = next((item for item in self.dir_entries if item.get('type') == 2 and item.get('name', '').lower() in lowered), None)
        if entry is None:
            raise RuntimeError('У XLS файлі не знайдено потік Workbook/Book.')

        size = int(entry.get('size', 0))
        start_sid = entry.get('start_sector', NOSTREAM)
        if size >= self.mini_cutoff or not self.mini_fat:
            return self._read_regular_stream(start_sid, size)

        chunks = []
        sid = start_sid
        seen = set()
        while sid not in (ENDOFCHAIN, FREESECT, NOSTREAM) and 0 <= sid < len(self.mini_fat) and sid not in seen and sum(len(c) for c in chunks) < size:
            seen.add(sid)
            offset = sid * self.mini_sector_size
            chunks.append(self.mini_stream[offset:offset + self.mini_sector_size])
            sid = self.mini_fat[sid]
        return b''.join(chunks)[:size]


def parse_short_unicode(data, offset, char_count, flags, codepage):
    is_unicode = bool(flags & 0x01)
    byte_len = char_count * (2 if is_unicode else 1)
    raw = data[offset:offset + byte_len]
    encoding = 'utf-16le' if is_unicode else codepage_to_encoding(codepage)
    return raw.decode(encoding, errors='ignore'), offset + byte_len


def parse_xl_unicode(data, offset, codepage):
    char_count = int.from_bytes(data[offset:offset + 2], 'little')
    offset += 2
    flags = data[offset]
    offset += 1
    rich_runs = int.from_bytes(data[offset:offset + 2], 'little') if (flags & 0x08) else 0
    if flags & 0x08:
        offset += 2
    ext_size = int.from_bytes(data[offset:offset + 4], 'little') if (flags & 0x04) else 0
    if flags & 0x04:
        offset += 4
    text, offset = parse_short_unicode(data, offset, char_count, flags, codepage)
    offset += rich_runs * 4
    offset += ext_size
    return text, offset


def parse_sst(blob, codepage):
    if len(blob) < 8:
        return []
    unique_count = int.from_bytes(blob[4:8], 'little')
    offset = 8
    values = []
    for _ in range(unique_count):
        if offset + 3 > len(blob):
            break
        try:
            text, offset = parse_xl_unicode(blob, offset, codepage)
        except Exception:
            break
        values.append(text)
    return values


def decode_rk(rk_value):
    divide_by_100 = bool(rk_value & 0x01)
    is_integer = bool(rk_value & 0x02)
    if is_integer:
        signed = int.from_bytes((rk_value & 0xFFFFFFFC).to_bytes(4, 'little'), 'little', signed=True)
        value = signed >> 2
    else:
        raw64 = (rk_value & 0xFFFFFFFC) << 32
        import struct
        value = struct.unpack('<d', raw64.to_bytes(8, 'little'))[0]
    if divide_by_100:
        value /= 100.0
    return value


def normalize_number(value):
    try:
        numeric = float(value)
    except Exception:
        return str(value)
    if abs(numeric - round(numeric)) < 1e-9:
        return str(int(round(numeric)))
    return ('%.15g' % numeric)


def parse_label(payload, codepage):
    if len(payload) < 9:
        return ''
    char_count = int.from_bytes(payload[6:8], 'little')
    flags = payload[8]
)PYX3");
    script += QString::fromUtf8(R"PYX4(
    text, _ = parse_short_unicode(payload, 9, char_count, flags, codepage)
    return text


def parse_boundsheet(payload, codepage):
    if len(payload) < 8:
        return None
    offset = int.from_bytes(payload[0:4], 'little')
    name_len = payload[6]
    flags = payload[7]
    name, _ = parse_short_unicode(payload, 8, name_len, flags, codepage)
    return (offset, name)


def read_xls_rows(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    ole = OleFileMini(data)
    workbook = ole.open_stream(['Workbook', 'Book'])

    codepage = 1251
    shared_strings = []
    sheets = []
    offset = 0
    pending_sst = None
    while offset + 4 <= len(workbook):
        record_id = int.from_bytes(workbook[offset:offset + 2], 'little')
        size = int.from_bytes(workbook[offset + 2:offset + 4], 'little')
        payload = workbook[offset + 4:offset + 4 + size]
        offset += 4 + size

        if record_id == 0x003C and pending_sst is not None:
            pending_sst += payload
            continue

        if pending_sst is not None:
            shared_strings = parse_sst(pending_sst, codepage)
            pending_sst = None

        if record_id == 0x0042 and len(payload) >= 2:
            codepage = int.from_bytes(payload[0:2], 'little')
        elif record_id == 0x0085:
            item = parse_boundsheet(payload, codepage)
            if item is not None:
                sheets.append(item)
        elif record_id == 0x00FC:
            pending_sst = payload
        elif record_id == 0x000A:
            break

    if pending_sst is not None:
        shared_strings = parse_sst(pending_sst, codepage)

    if not sheets:
        raise RuntimeError('У XLS файлі не знайдено жодного аркуша.')

    first_sheet_offset = int(sheets[0][0])
    cells = {}
    offset = first_sheet_offset
    while offset + 4 <= len(workbook):
        record_id = int.from_bytes(workbook[offset:offset + 2], 'little')
        size = int.from_bytes(workbook[offset + 2:offset + 4], 'little')
        payload = workbook[offset + 4:offset + 4 + size]
        offset += 4 + size

        if record_id == 0x000A:
            break
        if record_id == 0x0203 and len(payload) >= 14:
            import struct
            row = int.from_bytes(payload[0:2], 'little')
            col = int.from_bytes(payload[2:4], 'little')
            value = struct.unpack('<d', payload[6:14])[0]
            cells.setdefault(row, {})[col] = normalize_number(value)
        elif record_id == 0x027E and len(payload) >= 10:
            row = int.from_bytes(payload[0:2], 'little')
            col = int.from_bytes(payload[2:4], 'little')
            rk_value = int.from_bytes(payload[6:10], 'little')
            cells.setdefault(row, {})[col] = normalize_number(decode_rk(rk_value))
        elif record_id == 0x00BD and len(payload) >= 6:
            row = int.from_bytes(payload[0:2], 'little')
            first_col = int.from_bytes(payload[2:4], 'little')
            last_col = int.from_bytes(payload[-2:], 'little')
            for idx in range(max(0, last_col - first_col + 1)):
                base = 4 + idx * 6
                if base + 6 > len(payload) - 2:
                    break
                rk_value = int.from_bytes(payload[base + 2:base + 6], 'little')
                cells.setdefault(row, {})[first_col + idx] = normalize_number(decode_rk(rk_value))
        elif record_id == 0x00FD and len(payload) >= 10:
            row = int.from_bytes(payload[0:2], 'little')
            col = int.from_bytes(payload[2:4], 'little')
            sst_index = int.from_bytes(payload[6:10], 'little')
            value = shared_strings[sst_index] if 0 <= sst_index < len(shared_strings) else ''
            cells.setdefault(row, {})[col] = value
        elif record_id == 0x0204 and len(payload) >= 9:
            row = int.from_bytes(payload[0:2], 'little')
            col = int.from_bytes(payload[2:4], 'little')
            cells.setdefault(row, {})[col] = parse_label(payload, codepage)
        elif record_id == 0x0006 and len(payload) >= 14 and payload[12:14] != b'\xff\xff':
            import struct
            row = int.from_bytes(payload[0:2], 'little')
            col = int.from_bytes(payload[2:4], 'little')
            value = struct.unpack('<d', payload[6:14])[0]
            cells.setdefault(row, {})[col] = normalize_number(value)

    rows = []
    for row_index in sorted(cells.keys()):
        row_map = cells[row_index]
        width = max(row_map.keys()) + 1 if row_map else 0
        row = [str(row_map.get(col, '')) for col in range(width)]
        while row and not row[-1]:
            row.pop()
        if any(cell.strip() for cell in row):
            rows.append(row)
    return rows


def read_rows(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    if data.startswith(ZIP_MAGIC):
        return read_xlsx_rows(path)
    if data.startswith(OLE_MAGIC):
        rows = read_xls_rows(path)
        if rows:
            return rows
        raise RuntimeError('Не вдалося прочитати рядки з binary XLS файлу.')

    text = try_decode_text(data)
    lowered = text.lower()
    if '<table' in lowered or '<html' in lowered or '<body' in lowered:
        rows = parse_html_rows(text)
        if rows:
            return rows
    if ('urn:schemas-microsoft-com:office:spreadsheet' in lowered or
            ('<worksheet' in lowered and '<table' in lowered and '<row' in lowered)):
        rows = parse_xmlss_rows(text)
        if rows:
            return rows
    rows = parse_csv_rows(text)
    if rows:
        return rows
    raise RuntimeError('Не вдалося прочитати табличні рядки з файлу імпорту.')


if __name__ == '__main__':
    rows = read_rows(sys.argv[1])
    sys.stdout.write(json.dumps({'rows': rows, 'highlightedColumns': []}, ensure_ascii=False))
)PYX4");
    return script;
}

static ExcelReadResult readXlsxRowsViaPython(const QString& filePath, QString* errorText)
{
    ExcelReadResult result;
#if defined(Q_OS_WIN)
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("spreadsheet_python_fallback"), errorText)) {
        return result;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList launchers = candidatePythonExecutablesForSpreadsheetImport(appDir);
    if (launchers.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено Python launcher для резервного читання табличного файлу імпорту.");
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для резервного табличного імпорту.");
        return result;
    }

    const QString scriptPath = tempDir.filePath(QStringLiteral("import_excel_parts.py"));
    QSaveFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасовий Python-скрипт для табличного імпорту.");
        return result;
    }
    const QByteArray scriptBytes = excelImportPythonScript().toUtf8();
    if (scriptFile.write(scriptBytes) != scriptBytes.size() || !scriptFile.commit()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося записати Python-скрипт для табличного імпорту.");
        return result;
    }

    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    QString lastError;
    for (const QString& launcher : launchers) {
        QStringList launcherArgs;
        const QString lowerLauncher = launcher.toLower();
        if (lowerLauncher.endsWith(QStringLiteral("/py.exe")) ||
            lowerLauncher.endsWith(QStringLiteral("\\py.exe")) ||
            lowerLauncher == QStringLiteral("py") ||
            lowerLauncher.endsWith(QStringLiteral("/py")) ||
            lowerLauncher.endsWith(QStringLiteral("\\py"))) {
            launcherArgs << QStringLiteral("-3");
        }
        launcherArgs << scriptPath << filePath;

        appendInvoiceImportLog(QStringLiteral("python spreadsheet begin | launcher=%1 | suffix=%2 | args=%3 | file=%4")
                                   .arg(launcher)
                                   .arg(suffix)
                                   .arg(launcherArgs.join(QLatin1Char(' ')))
                                   .arg(filePath));

        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        process.setProcessEnvironment(env);
        process.setProgram(launcher);
        process.setArguments(launcherArgs);
        const IncrementalImportProcessResult processResult =
            runImportProcessIncrementally(process, 10000, 90000, QStringLiteral("spreadsheet_python_fallback"));
        if (!processResult.started) {
            lastError = processResult.errorString.trimmed();
            appendInvoiceImportLog(QStringLiteral("python spreadsheet failedToStart | launcher=%1 | err=%2")
                                       .arg(launcher, lastError));
            continue;
        }
        if (processResult.timedOut) {
            lastError = QString::fromUtf8("Python не завершив резервний табличний імпорт вчасно.");
            appendInvoiceImportLog(QStringLiteral("python spreadsheet timeout | launcher=%1 | file=%2")
                                       .arg(launcher, filePath));
            continue;
        }

        const QByteArray stdOut = processResult.stdOut;
        const QByteArray stdErr = processResult.stdErr;
        appendInvoiceImportLog(QStringLiteral("python spreadsheet finished | launcher=%1 | exitStatus=%2 | exitCode=%3 | stdout=%4 | stderr=%5")
                                   .arg(launcher)
                                   .arg(int(processResult.exitStatus))
                                   .arg(processResult.exitCode)
                                   .arg(importProcessLogPreview(stdOut))
                                   .arg(importProcessLogPreview(stdErr)));
        if (processResult.exitStatus != QProcess::NormalExit || processResult.exitCode != 0) {
            const QString details = QString::fromUtf8(stdErr).trimmed();
            lastError = details.isEmpty()
                ? QString::fromUtf8("Python повернув помилку під час резервного читання табличного файлу.")
                : details;
            continue;
        }
        if (stdOut.trimmed().isEmpty()) {
            lastError = QString::fromUtf8("Python завершився без JSON-відповіді під час табличного імпорту.");
            continue;
        }

        auto tryParseJsonPayload = [](const QByteArray& bytes, QJsonParseError* parseError) -> QJsonDocument {
            QJsonDocument doc = QJsonDocument::fromJson(bytes, parseError);
            if (parseError->error == QJsonParseError::NoError && doc.isObject()) return doc;

            const QList<QStringDecoder::Encoding> encodings = {
                QStringDecoder::System,
                QStringDecoder::Utf16,
                QStringDecoder::Utf16LE,
                QStringDecoder::Utf16BE
            };
            for (const QStringDecoder::Encoding encoding : encodings) {
                QStringDecoder decoder(encoding);
                const QString converted = decoder.decode(bytes);
                if (converted.isEmpty()) continue;
                QJsonParseError retryError;
                doc = QJsonDocument::fromJson(converted.toUtf8(), &retryError);
                if (retryError.error == QJsonParseError::NoError && doc.isObject()) {
                    if (parseError) *parseError = retryError;
                    return doc;
                }
            }
            return QJsonDocument();
        };

        QJsonParseError jsonError;
        const QJsonDocument doc = tryParseJsonPayload(stdOut, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
            lastError = QString::fromUtf8("Не вдалося розібрати відповідь Python при резервному читанні табличного файлу.");
            continue;
        }

        const QJsonObject obj = doc.object();
        const QJsonArray jsonRows = obj.value(QStringLiteral("rows")).toArray();
        ExcelReadResult candidate;
        for (const QJsonValue& rowValue : jsonRows) {
            if (!rowValue.isArray()) continue;
            QStringList row;
            const QJsonArray cells = rowValue.toArray();
            row.reserve(cells.size());
            for (const QJsonValue& cell : cells) row << cell.toVariant().toString();
            candidate.rows.append(row);
        }

        const QJsonArray highlightedColumns = obj.value(QStringLiteral("highlightedColumns")).toArray();
        for (const QJsonValue& columnValue : highlightedColumns) {
            const int column = columnValue.toInt(-1);
            if (column >= 0) candidate.highlightedColumns.insert(column);
        }

        const InvoiceImportMeta meta = detectInvoiceImportMeta(candidate.rows, candidate.highlightedColumns);
        candidate.documentNumber = meta.documentNumber;
        candidate.documentDate = meta.documentDate;
        if (!candidate.rows.isEmpty()) {
            return candidate;
        }
        lastError = QString::fromUtf8("Python не знайшов жодного табличного рядка у файлі імпорту.");
    }

    if (errorText) {
        *errorText = lastError.isEmpty()
            ? QString::fromUtf8("Не вдалося виконати резервний Python-імпорт табличного файлу.")
            : lastError;
    }
#else
    Q_UNUSED(filePath);
    if (errorText) *errorText = QString::fromUtf8("Резервний табличний імпорт через Python підтримується у Windows-збірці застосунку.");
#endif
    return result;
}

static bool fileStartsWithMagic(const QString& filePath, const QByteArray& magic)
{
    if (magic.isEmpty()) return false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray prefix = file.read(magic.size());
    return prefix.size() == magic.size() && prefix == magic;
}

static bool shouldPreferZipSpreadsheetReader(const QString& filePath, const QString& suffix)
{
    if (suffix == QStringLiteral("xlsx") ||
        suffix == QStringLiteral("xlsm") ||
        suffix == QStringLiteral("xltx") ||
        suffix == QStringLiteral("xltm") ||
        suffix == QStringLiteral("csv")  ||
        suffix == QStringLiteral("txt")  ||
        suffix == QStringLiteral("xml")  ||
        suffix == QStringLiteral("html") ||
        suffix == QStringLiteral("htm")) {
        return true;
    }
    return fileStartsWithMagic(filePath, QByteArray::fromHex("504B0304"));
}

static bool shouldPreferPythonSpreadsheetReader(const QString& filePath, const QString& suffix)
{
    if (suffix == QStringLiteral("xls")) return true;
    return shouldPreferZipSpreadsheetReader(filePath, suffix);
}

ExcelReadResult ParserBridge::readSpreadsheetInvoiceRowsInternal(const QString& filePath, QString* errorText)
{
    ExcelReadResult result;
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();

    const bool looksLikeLegacyXls = (suffix == QStringLiteral("xls")) || fileStartsWithMagic(filePath, kOleMagic);
    if (looksLikeLegacyXls) {
        QString nativeError;
        const ExcelReadResult nativeResult = readLegacyXlsRowsDirect(filePath, &nativeError);
        if (!nativeResult.rows.isEmpty()) {
            appendInvoiceImportLog(QStringLiteral("native xls reader success | file=%1 | rows=%2").arg(filePath).arg(nativeResult.rows.size()));
            return nativeResult;
        }
        appendInvoiceImportLog(QStringLiteral("native xls reader failed | error=%1 | file=%2").arg(nativeError.left(800), filePath));

        QString pythonError;
        const ExcelReadResult pythonResult = readXlsxRowsViaPython(filePath, &pythonError);
        if (!pythonResult.rows.isEmpty()) {
            appendInvoiceImportLog(QStringLiteral("python xls fallback success | file=%1 | rows=%2").arg(filePath).arg(pythonResult.rows.size()));
            return pythonResult;
        }

        const QString combinedError = QString::fromUtf8("Не вдалося імпортувати binary .xls без Microsoft Excel. Native reader: %1 | Python fallback: %2")
                                          .arg(nativeError, pythonError);
        appendInvoiceImportLog(QStringLiteral("xls import failed without Excel | error=%1 | file=%2").arg(combinedError.left(800), filePath));
        if (errorText) *errorText = combinedError;
        return result;
    }

    const bool shouldTryPythonReader = shouldPreferZipSpreadsheetReader(filePath, suffix);
    if (shouldTryPythonReader) {
        QString pythonError;
        const ExcelReadResult pythonResult = readXlsxRowsViaPython(filePath, &pythonError);
        if (!pythonResult.rows.isEmpty()) {
            return pythonResult;
        }
        appendInvoiceImportLog(QStringLiteral("python spreadsheet fallback failed | error=%1").arg(pythonError.left(800)));
    }
#if defined(Q_OS_WIN)
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("excel_powershell_fallback"), errorText)) {
        return result;
    }

    const QString powerShell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (powerShell.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено powershell.exe для читання Excel-файлу.");
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для імпорту Excel.");
        return result;
    }

    const QString scriptPath = tempDir.filePath(QStringLiteral("import_excel_parts.ps1"));
    QSaveFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасовий PowerShell-скрипт.");
        return result;
    }
    QByteArray scriptBytes = excelImportPowerShellScript().toUtf8();
    scriptBytes.prepend(QByteArray::fromHex("EFBBBF"));
    if (scriptFile.write(scriptBytes) != scriptBytes.size() || !scriptFile.commit()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося записати PowerShell-скрипт для імпорту Excel.");
        return result;
    }

    QProcess process;
    process.setProgram(powerShell);
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), scriptPath,
        filePath
    });
    const IncrementalImportProcessResult processResult =
        runImportProcessIncrementally(process, 10000, 60000, QStringLiteral("excel_powershell_fallback"));
    if (!processResult.started) {
        if (errorText) *errorText = processResult.errorString.trimmed().isEmpty()
            ? QString::fromUtf8("PowerShell не зміг запустити імпорт Excel.")
            : processResult.errorString;
        return result;
    }
    if (processResult.timedOut) {
        if (errorText) *errorText = QString::fromUtf8("PowerShell не завершив імпорт Excel вчасно.");
        return result;
    }

    const QByteArray stdOut = processResult.stdOut;
    const QByteArray stdErr = processResult.stdErr;
    appendInvoiceImportLog(QStringLiteral("powershell finished | exitStatus=%1 | exitCode=%2 | stdout=%3 | stderr=%4")
                               .arg(int(processResult.exitStatus))
                               .arg(processResult.exitCode)
                               .arg(importProcessLogPreview(stdOut))
                               .arg(importProcessLogPreview(stdErr)));
    if (processResult.exitStatus != QProcess::NormalExit || processResult.exitCode != 0) {
        if (errorText) {
            const QString details = QString::fromUtf8(stdErr).trimmed();
            *errorText = details.isEmpty()
                ? QString::fromUtf8("PowerShell повернув помилку під час читання Excel-файлу.")
                : details;
        }
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(stdOut, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося розібрати відповідь PowerShell при читанні Excel.");
        }
        return result;
    }

    const QJsonObject obj = doc.object();
    const QJsonArray jsonRows = obj.value(QStringLiteral("rows")).toArray();
    for (const QJsonValue& rowValue : jsonRows) {
        if (!rowValue.isArray()) continue;
        QStringList row;
        const QJsonArray cells = rowValue.toArray();
        row.reserve(cells.size());
        for (const QJsonValue& cell : cells) row << cell.toVariant().toString();
        result.rows.append(row);
    }

    const QJsonArray highlightedColumns = obj.value(QStringLiteral("highlightedColumns")).toArray();
    for (const QJsonValue& columnValue : highlightedColumns) {
        const int column = columnValue.toInt(-1);
        if (column >= 0) result.highlightedColumns.insert(column);
    }

    const InvoiceImportMeta meta = detectInvoiceImportMeta(result.rows, result.highlightedColumns);
    result.documentNumber = meta.documentNumber;
    result.documentDate = meta.documentDate;
#else
    Q_UNUSED(filePath);
    if (errorText) *errorText = QString::fromUtf8("Імпорт Excel підтримується у Windows-збірці застосунку.");
#endif
    return result;
}

ExcelReadResult readInvoiceRows(const QString& filePath, QString* errorText)
{
    return readInvoiceRows(filePath, errorText, {});
}

ExcelReadResult readInvoiceRows(const QString& filePath,
                                QString* errorText,
                                const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    if (importCancellationRequested(cancelToken)) {
        return makeCancelledImportResult(errorText, QStringLiteral("before_detect_type"));
    }
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (importCancellationRequested(cancelToken)) {
        return makeCancelledImportResult(errorText, QStringLiteral("before_parser_dispatch"));
    }
    ExcelReadResult result = (suffix == QStringLiteral("pdf"))
        ? GenericPdfParser::readRows(filePath, errorText)
        : GenericXlsParser::readRows(filePath, errorText);
    if (importCancellationRequested(cancelToken)) {
        return makeCancelledImportResult(errorText, QStringLiteral("after_parser_return"));
    }
    return result;
}

} // namespace InvoiceImport
