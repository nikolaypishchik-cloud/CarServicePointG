// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

    script += QString::fromUtf8(R"EXCELPS5(
function Read-CsvRows([string]$FilePath) {
    Add-Type -AssemblyName Microsoft.VisualBasic
    $firstLine = [System.IO.File]::ReadLines($FilePath) | Select-Object -First 1
    if ($null -eq $firstLine) {
        return [ordered]@{ rows = @(); highlightedColumns = @() }
    }

    $delimiter = ';'
    $semicolonCount = (Get-SafeCount ($firstLine.ToCharArray() | Where-Object { $_ -eq ';' }))
    $commaCount = (Get-SafeCount ($firstLine.ToCharArray() | Where-Object { $_ -eq ',' }))
    $tabCount = (Get-SafeCount ($firstLine.ToCharArray() | Where-Object { $_ -eq "`t" }))
    if ($tabCount -ge $semicolonCount -and $tabCount -ge $commaCount) {
        $delimiter = "`t"
    } elseif ($commaCount -gt $semicolonCount) {
        $delimiter = ','
    }

    $parser = New-Object Microsoft.VisualBasic.FileIO.TextFieldParser($FilePath)
    $parser.TextFieldType = [Microsoft.VisualBasic.FileIO.FieldType]::Delimited
    $parser.SetDelimiters(@($delimiter))
    $parser.HasFieldsEnclosedInQuotes = $true
    $resultRows = New-Object System.Collections.ArrayList
    try {
        while (-not $parser.EndOfData) {
            $fields = $parser.ReadFields()
            if ($null -eq $fields) { continue }
            $hasData = $false
            foreach ($field in $fields) {
                if (-not [string]::IsNullOrWhiteSpace([string]$field)) { $hasData = $true; break }
            }
            if ($hasData) { [void]$resultRows.Add($fields) }
        }
    } finally {
        $parser.Close()
    }

    return [ordered]@{
        rows = $resultRows
        highlightedColumns = @()
    }
}

$extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
$mode = Get-DetectedExcelMode $Path $extension
$result = switch ($mode) {
    'xlsx' { Read-XlsxRows $Path }
    'xls'  { Read-LegacyExcelRows $Path }
    'csv'  { Read-CsvRows $Path }
    default { throw "Непідтримуваний формат файлу: $extension" }
}

$result | ConvertTo-Json -Depth 8 -Compress
)EXCELPS5");
    return script;
}

static QString valueAtOrEmpty(const QStringList& row, int index)
{
    return (index >= 0 && index < row.size()) ? row.at(index).trimmed() : QString();
}


static QByteArray readEntireFileBinary(const QString& filePath, QString* errorText = nullptr)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося відкрити файл імпорту: %1").arg(file.errorString());
        return QByteArray();
    }
    return file.readAll();
}

static quint16 readLe16(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 2 > data.size()) return 0;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

static quint32 readLe32(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 4 > data.size()) return 0;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

static QString decodeBytesWithCodePage(const QByteArray& raw, int codePage)
{
    if (raw.isEmpty()) return QString();
#if defined(Q_OS_WIN)
    if (codePage <= 0) codePage = 1251;
    const int required = MultiByteToWideChar(UINT(codePage), 0, raw.constData(), int(raw.size()), nullptr, 0);
    if (required > 0) {
        std::wstring wide;
        wide.resize(size_t(required));
        const int written = MultiByteToWideChar(UINT(codePage), 0, raw.constData(), int(raw.size()), wide.data(), required);
        if (written > 0) {
            return QString::fromWCharArray(wide.c_str(), written);
        }
    }
#endif
    const QString utf8 = QString::fromUtf8(raw.constData(), raw.size());
    if (!utf8.isEmpty() || raw.isEmpty()) return utf8;
    return QString::fromLatin1(raw.constData(), raw.size());
}

static int xlsCodepageToWindowsCodePage(int codepage)
{
    switch (codepage) {
    case 65001: return 65001;
    case 1250: return 1250;
    case 1251: return 1251;
    case 1252: return 1252;
    case 1253: return 1253;
    case 1254: return 1254;
    case 1255: return 1255;
    case 1256: return 1256;
    case 1257: return 1257;
    case 1258: return 1258;
    case 866:  return 866;
    case 850:  return 850;
    case 852:  return 852;
    case 1200: return 1251;
    default:   return 1251;
    }
}

static bool xlsBuiltinDateNumFmt(int numFmtId)
{
    return (numFmtId >= 14 && numFmtId <= 22) || (numFmtId >= 45 && numFmtId <= 47);
}

static bool xlsLooksLikeDateFormatCode(const QString& code)
{
    const QString lowered = code.trimmed().toLower();
    if (lowered.isEmpty()) return false;
    return (lowered.contains(QStringLiteral("dd")) && lowered.contains(QStringLiteral("mm")) && lowered.contains(QStringLiteral("yy"))) ||
           (lowered.contains(QStringLiteral("yyyy")) && lowered.contains(QStringLiteral("mm")));
}

static QString normalizeExcelNumeric(double value)
{
    if (std::isfinite(value) && std::fabs(value - std::round(value)) < 1e-9) {
        return QString::number(qint64(std::llround(value)));
    }
    return QString::number(value, 'g', 15);
}

static QString excelSerialNumberToDisplayDate(double value)
{
    if (!std::isfinite(value) || value <= 0.0) return normalizeExcelNumeric(value);
    const QDate baseDate(1899, 12, 30);
    const int wholeDays = int(std::floor(value));
    const QDate date = baseDate.addDays(wholeDays);
    if (!date.isValid()) return normalizeExcelNumeric(value);
    return date.toString(QStringLiteral("dd.MM.yyyy"));
}

static QString xlsNormalizeCellNumber(double value, bool asDate)
{
    return asDate ? excelSerialNumberToDisplayDate(value) : normalizeExcelNumeric(value);
}

struct XlsShortUnicodeResult {
    QString text;
    int nextOffset = 0;
};

static XlsShortUnicodeResult parseXlsShortUnicode(const QByteArray& data,
                                                  int offset,
                                                  int charCount,
                                                  quint8 flags,
                                                  int codepage)
{
    XlsShortUnicodeResult result;
    const bool isUnicode = (flags & 0x01) != 0;
    const int byteLen = charCount * (isUnicode ? 2 : 1);
    if (offset < 0 || byteLen < 0 || offset + byteLen > data.size()) {
        result.nextOffset = qMax(offset, 0);
        return result;
    }
    const QByteArray raw = data.mid(offset, byteLen);
    result.text = isUnicode
        ? QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), raw.size() / 2)
        : decodeBytesWithCodePage(raw, xlsCodepageToWindowsCodePage(codepage));
    result.nextOffset = offset + byteLen;
    return result;
}

static XlsShortUnicodeResult parseXlsUnicodeString(const QByteArray& data, int offset, int codepage)
{
    XlsShortUnicodeResult result;
    if (offset + 3 > data.size()) return result;
    const int charCount = int(readLe16(data, offset));
    offset += 2;
    const quint8 flags = quint8(data.at(offset));
    offset += 1;
    int richRuns = 0;
    int extSize = 0;
    if (flags & 0x08) {
        if (offset + 2 > data.size()) return result;
        richRuns = int(readLe16(data, offset));
        offset += 2;
    }
    if (flags & 0x04) {
        if (offset + 4 > data.size()) return result;
        extSize = int(readLe32(data, offset));
        offset += 4;
    }
    result = parseXlsShortUnicode(data, offset, charCount, flags, codepage);
    offset = result.nextOffset;
    offset += richRuns * 4;
    offset += extSize;
    result.nextOffset = qMin(offset, data.size());
    return result;
}

static QString parseXlsLabel(const QByteArray& payload, int codepage)
{
    if (payload.size() < 9) return QString();
    const int charCount = int(readLe16(payload, 6));
    const quint8 flags = quint8(payload.at(8));
    return parseXlsShortUnicode(payload, 9, charCount, flags, codepage).text;
}

struct XlsBoundSheet {
    int offset = -1;
    QString name;
};

static XlsBoundSheet parseXlsBoundSheet(const QByteArray& payload, int codepage)
{
    XlsBoundSheet sheet;
    if (payload.size() < 8) return sheet;
    sheet.offset = int(readLe32(payload, 0));
    const int nameLen = uchar(payload.at(6));
    const quint8 flags = quint8(payload.at(7));
    sheet.name = parseXlsShortUnicode(payload, 8, nameLen, flags, codepage).text;
    return sheet;
}

static QStringList parseXlsSharedStrings(const QByteArray& blob, int codepage)
{
    QStringList values;
    if (blob.size() < 8) return values;
    const int uniqueCount = int(readLe32(blob, 4));
    int offset = 8;
    for (int i = 0; i < uniqueCount && offset + 3 <= blob.size(); ++i) {
        const XlsShortUnicodeResult parsed = parseXlsUnicodeString(blob, offset, codepage);
        if (parsed.nextOffset <= offset) break;
        values.append(parsed.text);
        offset = parsed.nextOffset;
    }
    return values;
}

static double decodeXlsRk(quint32 rkValue)
{
    const bool divideBy100 = (rkValue & 0x01u) != 0;
    const bool isInteger = (rkValue & 0x02u) != 0;
    double value = 0.0;
    if (isInteger) {
        const qint32 signedValue = qint32(rkValue & 0xFFFFFFFCu);
        value = double(signedValue >> 2);
    } else {
        union {
            quint64 u64;
            double d;
        } conv{};
        conv.u64 = quint64(rkValue & 0xFFFFFFFCu) << 32;
        value = conv.d;
    }
    if (divideBy100) value /= 100.0;
    return value;
}

namespace {
constexpr quint32 XlsFreeSect = 0xFFFFFFFFu;
constexpr quint32 XlsEndOfChain = 0xFFFFFFFEu;
constexpr quint32 XlsNoStream = 0xFFFFFFFFu;
const QByteArray kOleMagic = QByteArray::fromHex("D0CF11E0A1B11AE1");

struct OleDirectoryEntry {
    QString name;
    quint8 type = 0;
    quint32 startSector = XlsNoStream;
    quint64 size = 0;
};

class OleCompoundReader {
public:
    explicit OleCompoundReader(const QByteArray& data) : m_data(data) {}

    bool open(QString* errorText)
    {
        if (!m_data.startsWith(kOleMagic)) {
            if (errorText) *errorText = QString::fromUtf8("Файл не є binary XLS/OLE контейнером.");
            return false;
        }
        if (m_data.size() < 512) {
            if (errorText) *errorText = QString::fromUtf8("Binary XLS файл пошкоджений: замалий OLE заголовок.");
            return false;
        }
        m_sectorSize = 1u << readLe16(m_data, 30);
        m_miniSectorSize = 1u << readLe16(m_data, 32);
        m_dirFirstSector = readLe32(m_data, 48);
        m_miniCutoff = readLe32(m_data, 56);
        m_miniFatFirstSector = readLe32(m_data, 60);
        m_difatFirstSector = readLe32(m_data, 68);
        m_difatSectorCount = readLe32(m_data, 72);
        if (m_sectorSize == 0 || m_sectorSize > (1u << 20)) {
            if (errorText) *errorText = QString::fromUtf8("Неприпустимий розмір OLE sector у XLS файлі.");
            return false;
        }
        m_totalSectors = int((m_data.size() - 512) / qMax(1u, m_sectorSize));
        if (!readFat(errorText)) return false;
        if (!readDirectory(errorText)) return false;
        if (!readMiniFat(errorText)) return false;
        return true;
    }

    QByteArray openWorkbookStream(QString* errorText) const
    {
        const auto it = std::find_if(m_dirEntries.cbegin(), m_dirEntries.cend(), [](const OleDirectoryEntry& entry) {
            const QString lowered = entry.name.trimmed().toLower();
            return entry.type == 2 && (lowered == QStringLiteral("workbook") || lowered == QStringLiteral("book"));
        });
        if (it == m_dirEntries.cend()) {
            if (errorText) *errorText = QString::fromUtf8("У XLS файлі не знайдено потік Workbook/Book.");
            return QByteArray();
        }
        return openStream(*it, errorText);
    }

private:
    QByteArray sector(quint32 sid) const
    {
        if (sid >= quint32(m_totalSectors)) return QByteArray();
        const qsizetype offset = 512 + qsizetype(sid) * qsizetype(m_sectorSize);
        if (offset < 0 || offset >= m_data.size()) return QByteArray();
        return m_data.mid(offset, int(m_sectorSize));
    }

    QVector<quint32> buildChain(quint32 startSid, const QVector<quint32>& fat) const
    {
        QVector<quint32> chain;
        QSet<quint32> seen;
        quint32 sid = startSid;
        while (sid != XlsEndOfChain && sid != XlsFreeSect && sid != XlsNoStream && sid < quint32(fat.size()) && !seen.contains(sid)) {
            seen.insert(sid);
            chain.append(sid);
            sid = fat.at(int(sid));
        }
        return chain;
    }

    QByteArray readRegularStream(quint32 startSid, quint64 size) const
    {
        if (startSid == XlsEndOfChain || startSid == XlsFreeSect || startSid == XlsNoStream || size == 0) return QByteArray();
        QByteArray out;
        const QVector<quint32> chain = buildChain(startSid, m_fat);
        for (quint32 sid : chain) out.append(sector(sid));
        if (size < quint64(out.size())) out.truncate(int(size));
        return out;
    }

    bool readFat(QString* errorText)
    {
        QVector<quint32> difat;
        difat.reserve(109 + int(m_difatSectorCount) * int(m_sectorSize / 4));
        for (int i = 0; i < 109; ++i) difat.append(readLe32(m_data, 76 + i * 4));
        quint32 nextSid = m_difatFirstSector;
        const int entriesPerDifatSector = int(m_sectorSize / 4) - 1;
        for (quint32 i = 0; i < m_difatSectorCount; ++i) {
            if (nextSid == XlsEndOfChain || nextSid == XlsFreeSect || nextSid == XlsNoStream || nextSid >= quint32(m_totalSectors)) break;
            const QByteArray sec = sector(nextSid);
            for (int idx = 0; idx < entriesPerDifatSector; ++idx) {
                difat.append(readLe32(sec, idx * 4));
            }
            nextSid = readLe32(sec, int(m_sectorSize) - 4);
        }

        const int entriesPerSector = int(m_sectorSize / 4);
        for (quint32 sid : difat) {
            if (sid == XlsFreeSect || sid == XlsEndOfChain || sid == XlsNoStream || sid >= quint32(m_totalSectors)) continue;
            const QByteArray sec = sector(sid);
            for (int idx = 0; idx < entriesPerSector; ++idx) {
                m_fat.append(readLe32(sec, idx * 4));
            }
        }
        if (m_fat.isEmpty()) {
            if (errorText) *errorText = QString::fromUtf8("У XLS файлі не вдалося прочитати FAT таблицю.");
            return false;
        }
        return true;
    }

    bool readDirectory(QString* errorText)
    {
        const QByteArray raw = readRegularStream(m_dirFirstSector, quint64(m_sectorSize) * 16ull);
        if (raw.isEmpty()) {
            if (errorText) *errorText = QString::fromUtf8("У XLS файлі не вдалося прочитати OLE directory.");
            return false;
        }
        for (int pos = 0; pos + 128 <= raw.size(); pos += 128) {
            const QByteArray chunk = raw.mid(pos, 128);
            const int nameLen = int(readLe16(chunk, 64));
            QString name;
            if (nameLen >= 2) {
                const QByteArray rawName = chunk.left(qMax(0, nameLen - 2));
                name = QString::fromUtf16(reinterpret_cast<const char16_t*>(rawName.constData()), rawName.size() / 2);
            }
            OleDirectoryEntry entry;
            entry.name = name;
            entry.type = quint8(chunk.at(66));
            entry.startSector = readLe32(chunk, 116);
            entry.size = quint64(readLe32(chunk, 120));
            m_dirEntries.append(entry);
        }
        const auto rootIt = std::find_if(m_dirEntries.cbegin(), m_dirEntries.cend(), [](const OleDirectoryEntry& entry) {
            return entry.type == 5;
        });
        if (rootIt != m_dirEntries.cend()) {
            m_miniStream = readRegularStream(rootIt->startSector, rootIt->size);
        }
        return true;
    }

    bool readMiniFat(QString* errorText)
    {
        Q_UNUSED(errorText);
        if (m_miniFatFirstSector == XlsEndOfChain || m_miniFatFirstSector == XlsFreeSect || m_miniFatFirstSector == XlsNoStream) {
            return true;
        }
        QByteArray raw;
        const QVector<quint32> chain = buildChain(m_miniFatFirstSector, m_fat);
        for (quint32 sid : chain) raw.append(sector(sid));
        for (int idx = 0; idx + 4 <= raw.size(); idx += 4) m_miniFat.append(readLe32(raw, idx));
        return true;
    }

    QByteArray openStream(const OleDirectoryEntry& entry, QString* errorText) const
    {
        if (entry.size >= m_miniCutoff || m_miniFat.isEmpty()) {
            return readRegularStream(entry.startSector, entry.size);
        }
        QByteArray out;
        QSet<quint32> seen;
        quint32 sid = entry.startSector;
        while (sid != XlsEndOfChain && sid != XlsFreeSect && sid != XlsNoStream && sid < quint32(m_miniFat.size()) && !seen.contains(sid) && quint64(out.size()) < entry.size) {
            seen.insert(sid);
            const qsizetype offset = qsizetype(sid) * qsizetype(m_miniSectorSize);
            if (offset < 0 || offset >= m_miniStream.size()) break;
            out.append(m_miniStream.mid(offset, int(m_miniSectorSize)));
            sid = m_miniFat.at(int(sid));
        }
        if (entry.size < quint64(out.size())) out.truncate(int(entry.size));
        if (out.isEmpty() && errorText) *errorText = QString::fromUtf8("Не вдалося відкрити workbook stream у XLS файлі.");
        return out;
    }

private:
    QByteArray m_data;
    quint32 m_sectorSize = 0;
    quint32 m_miniSectorSize = 0;
    quint32 m_dirFirstSector = XlsNoStream;
    quint32 m_miniCutoff = 4096;
    quint32 m_miniFatFirstSector = XlsNoStream;
    quint32 m_difatFirstSector = XlsEndOfChain;
    quint32 m_difatSectorCount = 0;
    int m_totalSectors = 0;
    QVector<quint32> m_fat;
    QVector<quint32> m_miniFat;
    QVector<OleDirectoryEntry> m_dirEntries;
    QByteArray m_miniStream;
};
} // namespace

static ExcelReadResult readLegacyXlsRowsDirect(const QString& filePath, QString* errorText)
{
    ExcelReadResult result;
    QString ioError;
    const QByteArray rawFile = readEntireFileBinary(filePath, &ioError);
    if (rawFile.isEmpty()) {
        if (errorText) *errorText = ioError.isEmpty() ? QString::fromUtf8("Не вдалося прочитати binary XLS файл.") : ioError;
        return result;
    }
    if (!rawFile.startsWith(kOleMagic)) {
        if (errorText) *errorText = QString::fromUtf8("Файл не є підтримуваним binary XLS контейнером.");
        return result;
    }

    QString oleError;
    OleCompoundReader ole(rawFile);
    if (!ole.open(&oleError)) {
        if (errorText) *errorText = oleError;
        return result;
    }
    const QByteArray workbook = ole.openWorkbookStream(&oleError);
    if (workbook.isEmpty()) {
        if (errorText) *errorText = oleError.isEmpty() ? QString::fromUtf8("У XLS файлі не знайдено workbook stream.") : oleError;
        return result;
    }

    int codepage = 1251;
    QStringList sharedStrings;
    QList<XlsBoundSheet> sheets;
    QByteArray pendingSst;
    QHash<int, QString> customNumFormats;
    QSet<int> dateXfIndexes;

    int offset = 0;
    int xfCounter = 0;
    while (offset + 4 <= workbook.size()) {
        const quint16 recordId = readLe16(workbook, offset);
        const int size = int(readLe16(workbook, offset + 2));
        const QByteArray payload = workbook.mid(offset + 4, size);
        offset += 4 + size;

        if (recordId == 0x003C && !pendingSst.isEmpty()) {
            pendingSst += payload;
            continue;
        }

        if (!pendingSst.isEmpty()) {
            sharedStrings = parseXlsSharedStrings(pendingSst, codepage);
            pendingSst.clear();
        }

        if (recordId == 0x0042 && payload.size() >= 2) {
            codepage = int(readLe16(payload, 0));
        } else if (recordId == 0x0085) {
            const XlsBoundSheet sheet = parseXlsBoundSheet(payload, codepage);
            if (sheet.offset >= 0) sheets.append(sheet);
        } else if (recordId == 0x00FC) {
            pendingSst = payload;
        } else if (recordId == 0x041E && payload.size() >= 5) {
            const int fmtId = int(readLe16(payload, 0));
            const int charCount = int(readLe16(payload, 2));
            const quint8 flags = quint8(payload.at(4));
            customNumFormats.insert(fmtId, parseXlsShortUnicode(payload, 5, charCount, flags, codepage).text);
        } else if (recordId == 0x00E0 && payload.size() >= 4) {
            const int numFmtId = int(readLe16(payload, 2));
            if (xlsBuiltinDateNumFmt(numFmtId) || xlsLooksLikeDateFormatCode(customNumFormats.value(numFmtId))) {
                dateXfIndexes.insert(xfCounter);
            }
            ++xfCounter;
        } else if (recordId == 0x000A) {
            break;
        }
    }

    if (!pendingSst.isEmpty()) sharedStrings = parseXlsSharedStrings(pendingSst, codepage);
    if (sheets.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("У XLS файлі не знайдено жодного аркуша.");
        return result;
    }

    const int firstSheetOffset = sheets.first().offset;
    QMap<int, QMap<int, QString>> cells;
    offset = firstSheetOffset;
    while (offset + 4 <= workbook.size()) {
        const quint16 recordId = readLe16(workbook, offset);
        const int size = int(readLe16(workbook, offset + 2));
        const QByteArray payload = workbook.mid(offset + 4, size);
        offset += 4 + size;

        if (recordId == 0x000A) break;

        if (recordId == 0x0203 && payload.size() >= 14) {
            union { quint64 u64; double d; } conv{};
            conv.u64 = quint64(readLe32(payload, 6)) | (quint64(readLe32(payload, 10)) << 32);
            const int row = int(readLe16(payload, 0));
            const int col = int(readLe16(payload, 2));
            const int xf = int(readLe16(payload, 4));
            cells[row][col] = xlsNormalizeCellNumber(conv.d, dateXfIndexes.contains(xf));
        } else if (recordId == 0x027E && payload.size() >= 10) {
            const int row = int(readLe16(payload, 0));
            const int col = int(readLe16(payload, 2));
            const int xf = int(readLe16(payload, 4));
            cells[row][col] = xlsNormalizeCellNumber(decodeXlsRk(readLe32(payload, 6)), dateXfIndexes.contains(xf));
        } else if (recordId == 0x00BD && payload.size() >= 6) {
            const int row = int(readLe16(payload, 0));
            const int firstCol = int(readLe16(payload, 2));
            const int lastCol = int(readLe16(payload, payload.size() - 2));
            for (int idx = 0; idx <= qMax(0, lastCol - firstCol); ++idx) {
                const int base = 4 + idx * 6;
                if (base + 6 > payload.size() - 2) break;
                const int xf = int(readLe16(payload, base));
                const quint32 rk = readLe32(payload, base + 2);
                cells[row][firstCol + idx] = xlsNormalizeCellNumber(decodeXlsRk(rk), dateXfIndexes.contains(xf));
            }
        } else if (recordId == 0x00FD && payload.size() >= 10) {
            const int row = int(readLe16(payload, 0));
            const int col = int(readLe16(payload, 2));
            const int sstIndex = int(readLe32(payload, 6));
            cells[row][col] = (sstIndex >= 0 && sstIndex < sharedStrings.size()) ? sharedStrings.at(sstIndex) : QString();
        } else if (recordId == 0x0204 && payload.size() >= 9) {
            const int row = int(readLe16(payload, 0));
            const int col = int(readLe16(payload, 2));
            cells[row][col] = parseXlsLabel(payload, codepage);
        } else if (recordId == 0x0006 && payload.size() >= 14 && payload.mid(12, 2) != QByteArray::fromHex("FFFF")) {
            union { quint64 u64; double d; } conv{};
            conv.u64 = quint64(readLe32(payload, 6)) | (quint64(readLe32(payload, 10)) << 32);
            const int row = int(readLe16(payload, 0));
            const int col = int(readLe16(payload, 2));
            const int xf = int(readLe16(payload, 4));
            cells[row][col] = xlsNormalizeCellNumber(conv.d, dateXfIndexes.contains(xf));
        }
    }

    for (auto rowIt = cells.cbegin(); rowIt != cells.cend(); ++rowIt) {
        const QMap<int, QString>& rowMap = rowIt.value();
        if (rowMap.isEmpty()) continue;
        QStringList row;
        const int width = rowMap.lastKey() + 1;
        row.reserve(width);
        for (int col = 0; col < width; ++col) row << rowMap.value(col);
        while (!row.isEmpty() && row.last().trimmed().isEmpty()) row.removeLast();
        bool hasData = false;
        for (const QString& cell : row) {
            if (!cell.trimmed().isEmpty()) { hasData = true; break; }
        }
        if (hasData) {
            logInvoiceImportParserStage(QStringLiteral("RAW_ROW"), result.rows.size(), row);
            result.rows.append(row);
        }
    }

    if (result.rows.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати жодного рядка з binary XLS файлу без Excel.");
        return ExcelReadResult();
    }

    const InvoiceImportMeta meta = detectInvoiceImportMeta(result.rows, result.highlightedColumns);
    result.documentNumber = meta.documentNumber;
    result.documentDate = meta.documentDate;
    return result;
}

static QString joinInvoiceMetaRow(const QStringList& row)
{
    QStringList parts;
    parts.reserve(row.size());
    for (const QString& cell : row) {
        const QString trimmed = cell.trimmed();
        if (!trimmed.isEmpty()) parts << trimmed;
    }
    return parts.join(QLatin1Char(' '));
}

static QDate tryParseInvoiceMetaDate(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};
    raw.replace(QChar(0x00A0), QLatin1Char(' '));
    raw.replace(QChar(0x2013), QLatin1Char('-'));
    raw.replace(QChar(0x2014), QLatin1Char('-'));
    raw.replace('/', '.');
    raw.replace('-', '.');
    raw = raw.simplified();

    for (const QString& fmt : {QStringLiteral("dd.MM.yyyy"), QStringLiteral("d.M.yyyy"), QStringLiteral("dd.MM.yy"), QStringLiteral("d.M.yy"),
                               QStringLiteral("yyyy.MM.dd"), QStringLiteral("yyyy.M.d")}) {
        QDate d = QDate::fromString(raw, fmt);
        if (d.isValid()) {
            if (d.year() >= 1900 && d.year() < 2000) d = d.addYears(100);
            return d;
        }
    }

    const QDate iso = DateFormats::parseDate(raw);
    if (iso.isValid()) return iso;
    return {};
}

static QString normalizeInvoiceMetaDateToIso(const QString& raw)
{
    const QDate d = tryParseInvoiceMetaDate(raw);
    return d.isValid() ? d.toString(QStringLiteral("yyyy-MM-dd")) : QString();
}

static bool looksLikeInvoiceMetaNumberCandidate(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty() || raw.size() > 48) return false;
    if (!normalizeInvoiceMetaDateToIso(raw).isEmpty()) return false;
    if (raw.contains(QRegularExpression(QStringLiteral(R"([=+*])")))) return false;
    if (!raw.contains(QRegularExpression(QStringLiteral(R"(\d)")))) return false;

    const QString compact = raw.simplified();
    if (compact.contains(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}/\\_\-.№ ])")))) return false;

    const QString withoutSpaces = compact;
    if (withoutSpaces.contains(QRegularExpression(QStringLiteral(R"(^\d{1,2}[.,]\d+$)")))) return false;
    if (withoutSpaces.contains(QRegularExpression(QStringLiteral(R"(^\d+[.,]\d{2}$)")))) return false;
    return true;
}

static QString cleanInvoiceMetaNumber(QString raw)
{
    raw = raw.trimmed();
    raw.remove(QRegularExpression(QStringLiteral(R"(^[№#:\-\s]+)")));
    raw.remove(QRegularExpression(QStringLiteral(R"([,;:.\s]+$)")));
    return raw.trimmed();
}

static QString extractInvoiceMetaNumberFromText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return {};

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral(R"((?:видаткова\s+накладна|накладна|документ)\s*(?:№|N|No|Номер)?\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄє0-9][A-Za-zА-Яа-яІіЇїЄє0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:номер\s+накладн(?:ої|ой)?|номер\s+документа|номер|документ\s*№)\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄє0-9][A-Za-zА-Яа-яІіЇїЄє0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:^|\s)№\s*([A-Za-zА-Яа-яІіЇїЄє0-9][A-Za-zА-Яа-яІіЇїЄє0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression& rx : patterns) {
        const QRegularExpressionMatch match = rx.match(trimmed);
        if (match.hasMatch()) {
            const QString candidate = cleanInvoiceMetaNumber(match.captured(1));
            if (looksLikeInvoiceMetaNumberCandidate(candidate)) return candidate;
        }
    }
    return {};
}

static QString extractInvoiceMetaDateFromText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return {};

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral(R"((?:дата\s+накладн(?:ої|ой)?|дата\s+документа|дата|від)\s*[:\-]?\s*((?:\d{1,2}[./-]\d{1,2}[./-]\d{2,4})|(?:\d{4}[./-]\d{1,2}[./-]\d{1,2})))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:^|\s)((?:\d{1,2}[./-]\d{1,2}[./-]\d{2,4})|(?:\d{4}[./-]\d{1,2}[./-]\d{1,2}))(?:\s|$))"))
    };

    for (const QRegularExpression& rx : patterns) {
        const QRegularExpressionMatch match = rx.match(trimmed);
        if (match.hasMatch()) {
            const QString normalized = normalizeInvoiceMetaDateToIso(match.captured(1));
            if (!normalized.isEmpty()) return normalized;
        }
    }
    return {};
}

InvoiceImportMeta detectInvoiceImportMeta(const QList<QStringList>& rows, const QSet<int>& highlightedColumns)
{
    InvoiceImportMeta meta;
    if (rows.isEmpty()) return meta;

    const int scanRows = qMin(rows.size(), 30);
    for (int rowIndex = 0; rowIndex < scanRows; ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        for (int col = 0; col < row.size(); ++col) {
            const QString cell = valueAtOrEmpty(row, col);
            if (cell.isEmpty()) continue;
            const QString normalized = normalizedImportHeader(cell);

            if (meta.documentNumber.isEmpty()) {
                if (cell.trimmed() == QString::fromUtf8("№") || normalized == QStringLiteral("номер") ||
                    normalized.contains(QStringLiteral("номернаклад")) || normalized.contains(QStringLiteral("номердокумент")) ||
                    normalized == QStringLiteral("документ")) {
                    for (int step = 1; step <= 3; ++step) {
                        const QString candidate = cleanInvoiceMetaNumber(valueAtOrEmpty(row, col + step));
                        if (looksLikeInvoiceMetaNumberCandidate(candidate)) {
                            meta.documentNumber = candidate;
                            break;
                        }
                    }
                }
                if (meta.documentNumber.isEmpty()) {
                    const QString directCandidate = cleanInvoiceMetaNumber(cell);
                    if (looksLikeInvoiceMetaNumberCandidate(directCandidate)) {
                        meta.documentNumber = directCandidate;
                    }
                }
                if (meta.documentNumber.isEmpty()) {
                    const QString candidate = extractInvoiceMetaNumberFromText(cell);
                    if (!candidate.isEmpty()) meta.documentNumber = candidate;
                }
            }

            if (meta.documentDate.isEmpty()) {
                if (normalized.startsWith(QStringLiteral("дата")) || normalized == QStringLiteral("від") || normalized.contains(QStringLiteral("датадокумент"))) {
                    for (int step = 1; step <= 3; ++step) {
                        const QString candidate = normalizeInvoiceMetaDateToIso(valueAtOrEmpty(row, col + step));
                        if (!candidate.isEmpty()) {
                            meta.documentDate = candidate;
                            break;
                        }
                    }
                }
                if (meta.documentDate.isEmpty()) {
                    const QString candidate = extractInvoiceMetaDateFromText(cell);
                    if (!candidate.isEmpty()) meta.documentDate = candidate;
                }
            }

            if (!meta.documentNumber.isEmpty() && !meta.documentDate.isEmpty()) return meta;
        }

        const QString joined = joinInvoiceMetaRow(row);
        if (meta.documentNumber.isEmpty()) meta.documentNumber = extractInvoiceMetaNumberFromText(joined);
        if (meta.documentDate.isEmpty()) meta.documentDate = extractInvoiceMetaDateFromText(joined);
        if (!meta.documentNumber.isEmpty() && !meta.documentDate.isEmpty()) return meta;
    }

    if (!highlightedColumns.isEmpty()) {
        const QList<int> cols = highlightedColumns.values();
        for (int col : cols) {
            for (int rowIndex = 0; rowIndex < scanRows; ++rowIndex) {
                const QString cell = valueAtOrEmpty(rows.at(rowIndex), col);
                if (cell.isEmpty()) continue;
                if (meta.documentNumber.isEmpty()) {
                    const QString candidate = cleanInvoiceMetaNumber(cell);
                    if (looksLikeInvoiceMetaNumberCandidate(candidate)) meta.documentNumber = candidate;
                }
                if (meta.documentDate.isEmpty()) {
                    const QString candidate = normalizeInvoiceMetaDateToIso(cell);
                    if (!candidate.isEmpty()) meta.documentDate = candidate;
                }
                if (!meta.documentNumber.isEmpty() && !meta.documentDate.isEmpty()) return meta;
            }
        }
    }

    return meta;
}

static bool isLikelyDateHeader(const QString& normalized)
{
    return normalized.startsWith(QStringLiteral("дата")) || normalized.contains(QStringLiteral("date")) ||
           normalized.contains(QStringLiteral("замовл")) || normalized.contains(QStringLiteral("заказ"));
}

static bool normalizedHeaderEqualsAny(const QString& normalized, std::initializer_list<const char*> values)
{
    for (const char* value : values) {
        if (normalized == QString::fromUtf8(value)) return true;
    }
    return false;
}

static bool normalizedHeaderContainsAny(const QString& normalized, std::initializer_list<const char*> values)
{
    for (const char* value : values) {
        if (normalized.contains(QString::fromUtf8(value))) return true;
    }
    return false;
}

// UniversalInvoiceColumnDetector: hybrid header scoring for different supplier invoices.
// Mandatory rule: imported part purchase price must be the VAT-included price.
// Negative VAT dictionary: "Ціна без ПДВ", "Цена без НДС", "Price excl. VAT", "Без ПДВ".
static bool invoiceHeaderHasVatExcludedMarker(const QString& normalized)
{
    return normalized.contains(QStringLiteral("безпдв")) ||
           normalized.contains(QStringLiteral("безндс")) ||
           normalized.contains(QStringLiteral("exclvat")) ||
           normalized.contains(QStringLiteral("excludingvat")) ||
           normalized.contains(QStringLiteral("withoutvat")) ||
           normalized.contains(QStringLiteral("vatnotincluded"));
}

static bool invoiceHeaderHasVatIncludedMarker(const QString& normalized)
{
    if (invoiceHeaderHasVatExcludedMarker(normalized)) return false;
    return normalized.contains(QStringLiteral("зпдв")) ||
           normalized.contains(QStringLiteral("спдв")) ||
           normalized.contains(QStringLiteral("зндс")) ||
           normalized.contains(QStringLiteral("сндс")) ||
           normalized.contains(QStringLiteral("inclvat")) ||
           normalized.contains(QStringLiteral("includingvat")) ||
           normalized.contains(QStringLiteral("withvat")) ||
           normalized.contains(QStringLiteral("vatincluded")) ||
           normalized.contains(QStringLiteral("pricevat")) ||
           normalized.contains(QStringLiteral("vatprice"));
}

static bool invoiceHeaderLooksLikePrice(const QString& normalized)
{
    // UniversalInvoiceColumnDetector synonym dictionary for VAT-included price:
    // "Ціна з ПДВ", "Цена с НДС", "Ціна, грн з ПДВ", "Price VAT", "VAT Price", "Price incl. VAT", "Вартість з ПДВ".
    // "Вартість з ПДВ" / "Стоимость с НДС" is intentionally treated as a unit-price
    // synonym here; explicit total headers are still detected separately by "сума/total".
    const bool vatIncludedCostSynonym = invoiceHeaderHasVatIncludedMarker(normalized) &&
        normalizedHeaderContainsAny(normalized, {"вартість", "вартiсть", "стоимость", "cost", "value"});
    return vatIncludedCostSynonym ||
           normalizedHeaderEqualsAny(normalized, {"ціна", "цiна", "цена", "price", "unitprice", "ціназпдв", "цiназпдв", "ценаспдв", "ценасндс", "pricevat", "priceinclvat", "priceincludingvat", "вартістьзпдв", "вартiстьзпдв", "стоимостьсндс"}) ||
           normalizedHeaderContainsAny(normalized, {"ціна", "цiна", "цена", "price", "unitprice"});
}

static bool invoiceHeaderLooksLikeAmount(const QString& normalized)
{
    return normalizedHeaderEqualsAny(normalized, {"сума", "вартість", "стоимость", "amount", "total", "сумазпдв", "сумаспдв", "сумасндс"}) ||
           normalizedHeaderContainsAny(normalized, {"сум", "варт", "стоим", "amount", "total"});
}

static bool invoiceHeaderIsExplicitVatPrice(const QString& normalized)
{
    return invoiceHeaderLooksLikePrice(normalized) && invoiceHeaderHasVatIncludedMarker(normalized);
}

static bool invoiceHeaderIsExplicitVatAmount(const QString& normalized)
{
    return invoiceHeaderLooksLikeAmount(normalized) && invoiceHeaderHasVatIncludedMarker(normalized);
}

static int vatIncludedPriceHeaderScore(const QString& normalized)
{
    if (!invoiceHeaderLooksLikePrice(normalized)) return 0;
    if (invoiceHeaderHasVatExcludedMarker(normalized)) return 0;
    if (invoiceHeaderHasVatIncludedMarker(normalized)) return 12;
    return normalizedHeaderEqualsAny(normalized, {"ціна", "цiна", "цена", "price", "unitprice"}) ? 3 : 1;
}

static int vatIncludedAmountHeaderScore(const QString& normalized)
{
    if (!invoiceHeaderLooksLikeAmount(normalized)) return 0;
    if (invoiceHeaderHasVatExcludedMarker(normalized)) return 0;
    if (invoiceHeaderHasVatIncludedMarker(normalized)) return 9;
    return normalizedHeaderEqualsAny(normalized, {"сума", "вартість", "стоимость", "amount", "total"}) ? 6 : 4;
}

static bool isVatSafePriceColumnHeader(const QList<QStringList>& rows, int headerRow, int priceColumn)
{
    if (priceColumn < 0 || rows.isEmpty()) return false;
    if (headerRow < 0 || headerRow >= rows.size()) return false;
    const QString normalized = normalizedImportHeader(valueAtOrEmpty(rows.at(headerRow), priceColumn));
    if (normalized.isEmpty()) return false;
    return invoiceHeaderIsExplicitVatPrice(normalized);
}

static QString supplierInvoiceTemplateKey(QString supplierName);

static bool isVatSafePriceColumn(const QList<QStringList>& rows, int headerRow, const ExcelImportColumnMap& map)
{
    if (map.price < 0) return false;
    if (map.priceFromSupplierTemplate && map.hasVatConfirmedPrice()) return true;
    if (map.detectionSupplierKey == QStringLiteral("avtonova_d") && map.hasVatConfirmedPrice()) return true;
    return isVatSafePriceColumnHeader(rows, headerRow, map.price);
}

static void applySupplierExcelPdfVatRules(const QList<QStringList>& rows, int headerRow, ExcelImportColumnMap* map)
{
    if (!map || map->hasVatConfirmedPrice() || headerRow < 0 || headerRow >= rows.size() || map->price < 0) return;
    const QString key = supplierInvoiceTemplateKey(detectInvoiceSourceStore(rows));
    if (key != QStringLiteral("avtonova_d")) return;
    const QString priceHeader = normalizedImportHeader(valueAtOrEmpty(rows.at(headerRow), map->price));
    const QString amountHeader = normalizedImportHeader(valueAtOrEmpty(rows.at(headerRow), map->amount));
    const bool plainPrice = normalizedHeaderEqualsAny(priceHeader, {"ціна", "цiна", "цена", "price", "unitprice"});
    if (plainPrice && !invoiceHeaderHasVatExcludedMarker(priceHeader) &&
        map->amount >= 0 && invoiceHeaderLooksLikeAmount(amountHeader) && !invoiceHeaderHasVatExcludedMarker(amountHeader)) {
        map->priceVatExplicit = true;
        map->priceHeaderScore = qMax(map->priceHeaderScore, 9);
        map->detectionSupplierKey = key;
    }
}

static int scoreExcelImportHeaderCell(const QString& normalized, int* logicalTypeOut = nullptr)
{
    enum LogicalType {
        TypeNone = 0,
        TypeDate,
        TypeOem,
        TypeName,
        TypeQty,
        TypePrice,
        TypeAmount,
        TypeStore
    };

    int logicalType = TypeNone;
    int weight = 0;

    const int vatPriceScore = vatIncludedPriceHeaderScore(normalized);
    const int vatAmountScore = vatIncludedAmountHeaderScore(normalized);

    if (normalizedHeaderEqualsAny(normalized, {"каталожнийномер", "активнкодтовару", "кодтовару", "oemномер", "oem", "артикул", "partnumber", "sku"}) ||
        normalizedHeaderContainsAny(normalized, {"каталожн", "кодтовару", "коддетал", "номердетал", "partnumber", "артикул", "sku"})) {
        logicalType = TypeOem;
        weight = normalizedHeaderEqualsAny(normalized, {"каталожнийномер", "активнкодтовару", "кодтовару", "oemномер", "oem", "артикул", "partnumber"}) ? 6 : 4;
    } else if (normalizedHeaderEqualsAny(normalized, {"найменування", "наименование", "запчастина", "товар", "назва", "название", "partname", "опис", "описание", "description"}) ||
               normalizedHeaderContainsAny(normalized, {"найменув", "наименов", "запчаст", "partname", "опис", "descript"})) {
        logicalType = TypeName;
        weight = normalizedHeaderEqualsAny(normalized, {"найменування", "наименование", "запчастина", "товар", "назва", "название", "partname"}) ? 6 : 4;
    } else if (normalizedHeaderEqualsAny(normalized, {"кількість", "кiлькiсть", "количество", "кілть", "qty", "quantity"}) ||
               normalizedHeaderContainsAny(normalized, {"кіл", "кiл", "кол", "qty", "quantity"})) {
        logicalType = TypeQty;
        weight = normalizedHeaderEqualsAny(normalized, {"кількість", "кiлькiсть", "количество", "кілть", "qty", "quantity"}) ? 6 : 4;
    } else if (vatPriceScore > 0) {
        logicalType = TypePrice;
        weight = vatPriceScore;
    } else if (vatAmountScore > 0) {
        logicalType = TypeAmount;
        weight = vatAmountScore;
    } else if (normalizedHeaderEqualsAny(normalized, {"бренд", "brand", "manufacturer", "maker", "виробник"}) ||
               normalizedHeaderContainsAny(normalized, {"бренд", "brand", "manufact", "maker", "вироб"})) {
        logicalType = 8;
        weight = normalizedHeaderEqualsAny(normalized, {"бренд", "brand", "manufacturer", "maker", "виробник"}) ? 6 : 4;
    } else if (normalizedHeaderEqualsAny(normalized, {"магазин", "постачальник", "supplier", "store"}) ||
               normalizedHeaderContainsAny(normalized, {"магаз", "постач", "supplier", "store"})) {
        logicalType = TypeStore;
        weight = normalizedHeaderEqualsAny(normalized, {"магазин", "постачальник", "supplier", "store"}) ? 6 : 4;
    } else if (isLikelyDateHeader(normalized)) {
        logicalType = TypeDate;
        weight = normalized.startsWith(QStringLiteral("дата")) ? 6 : 4;
    }

    if (logicalTypeOut) *logicalTypeOut = logicalType;
    return weight;
}

static int scoreExcelImportHeaderRow(const QStringList& row, ExcelImportColumnMap* outMap = nullptr, const QSet<int>* allowedColumns = nullptr)
{
    ExcelImportColumnMap current;
    int score = 0;
    int selectedPriceScore = -1;
    int selectedAmountScore = -1;
    bool hasExplicitAmountHeader = false;
    int vartistColumn = -1;
    for (int col = 0; col < row.size(); ++col) {
        if (allowedColumns && !allowedColumns->contains(col)) continue;
        const QString normalized = normalizedImportHeader(row.at(col));
        if (normalized.isEmpty()) continue;

        if (normalized == QStringLiteral("сума") ||
            normalized == QStringLiteral("amount") ||
            normalized == QStringLiteral("total")) {
            hasExplicitAmountHeader = true;
        }
        if (normalized == QStringLiteral("вартість") ||
            normalized == QStringLiteral("стоимость")) {
            vartistColumn = col;
        }

        int logicalType = 0;
        const int cellScore = scoreExcelImportHeaderCell(normalized, &logicalType);
        if (cellScore <= 0) continue;

        bool assigned = false;
        switch (logicalType) {
        case 1: if (current.date   < 0) { current.date = col; assigned = true; } break;
        case 2: if (current.oem    < 0) { current.oem = col; assigned = true; } break;
        case 3: if (current.name   < 0) { current.name = col; assigned = true; } break;
        case 8: if (current.brand  < 0) { current.brand = col; assigned = true; } break;
        case 4: if (current.qty    < 0) { current.qty = col; assigned = true; } break;
        case 5:
            if (current.price < 0 || cellScore > selectedPriceScore) {
                if (current.price < 0) assigned = true;
                current.price = col;
                current.priceHeaderScore = cellScore;
                current.priceVatExplicit = invoiceHeaderIsExplicitVatPrice(normalized);
                current.priceVatExcludedCandidate = invoiceHeaderHasVatExcludedMarker(normalized);
                selectedPriceScore = cellScore;
            }
            break;
        case 6:
            if (current.amount < 0 || cellScore > selectedAmountScore) {
                if (current.amount < 0) assigned = true;
                current.amount = col;
                current.amountHeaderScore = cellScore;
                current.amountVatExplicit = invoiceHeaderIsExplicitVatAmount(normalized);
                selectedAmountScore = cellScore;
            }
            break;
        case 7: if (current.store  < 0) { current.store = col; assigned = true; } break;
        default: break;
        }

        if (assigned) {
            score += cellScore;
        }
    }
    if (hasExplicitAmountHeader && vartistColumn >= 0) {
        if (current.price < 0 && current.amount == vartistColumn) {
            const QString vartistHeader = normalizedImportHeader(row.value(vartistColumn));
            if (invoiceHeaderIsExplicitVatPrice(vartistHeader)) {
                current.price = vartistColumn;
                current.priceHeaderScore = vatIncludedPriceHeaderScore(vartistHeader);
                current.priceVatExplicit = true;
                current.priceVatExcludedCandidate = false;
            }
            current.amount = -1;
        }
        for (int col = 0; col < row.size(); ++col) {
            if (allowedColumns && !allowedColumns->contains(col)) continue;
            const QString normalized = normalizedImportHeader(row.at(col));
            if ((normalized == QStringLiteral("сума") ||
                 normalized == QStringLiteral("amount") ||
                 normalized == QStringLiteral("total")) &&
                col != current.price) {
                current.amount = col;
                break;
            }
        }
    }

    current.headerScore = score;
    if (outMap) *outMap = current;
    return score;
}

static void restrictExcelImportMapToColumns(ExcelImportColumnMap* map, const QSet<int>& allowedColumns)
{
    if (!map || allowedColumns.isEmpty()) return;
    for (int* idx : {&map->date, &map->oem, &map->name, &map->brand, &map->qty, &map->price, &map->amount, &map->store}) {
        if (*idx >= 0 && !allowedColumns.contains(*idx)) {
            *idx = -1;
        }
    }
}

static QString supplierInvoiceTemplateKey(QString supplierName)
{
    supplierName = supplierName.trimmed().toLower();
    supplierName.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    if (supplierName.contains(QStringLiteral("юнік")) ||
        supplierName.contains(QStringLiteral("юнікс")) ||
        supplierName.contains(QStringLiteral("uniks")) ||
        supplierName.contains(QStringLiteral("unik"))) {
        return QStringLiteral("unik_trade");
    }
    if (supplierName.contains(QStringLiteral("автонова")) || supplierName.contains(QStringLiteral("avtonova"))) {
        return QStringLiteral("avtonova_d");
    }
    if (supplierName.contains(QStringLiteral("еліт")) || supplierName.contains(QStringLiteral("елит")) || supplierName.contains(QStringLiteral("elit"))) {
        return QStringLiteral("elit");
    }
    return supplierName;
}

static QString supplierInvoiceHeaderSignature(const QList<QStringList>& rows)
{
    int bestScore = 0;
    QString signature;
    for (const auto& row : rows.mid(0, 16)) {
        ExcelImportColumnMap candidate;
        const int score = scoreExcelImportHeaderRow(row, &candidate, nullptr);
        if (!candidate.hasCoreColumns() || score <= bestScore) continue;
        bestScore = score;
        QStringList headers;
        for (const auto& cell : row) headers.append(normalizedImportHeader(cell));
        signature = headers.join(QLatin1Char('\t'));
    }
    return signature;
}

static void applySupplierInvoiceTemplate(const QList<QStringList>& rows, ExcelImportColumnMap* map)
{
    if (!map || map->hasVatConfirmedPrice()) return;
    const QString supplierKey = supplierInvoiceTemplateKey(detectInvoiceSourceStore(rows));
    if (supplierKey.trimmed().isEmpty()) return;

    const QJsonObject root = SupplierInvoiceTemplateStore::read(SupplierInvoiceTemplateStore::defaultPath());
    const QJsonObject supplier = root.value(supplierKey).toObject();
    if (supplier.isEmpty()) return;
    const QString signature = supplierInvoiceHeaderSignature(rows);
    if (signature.isEmpty() || supplier.value(QStringLiteral("headerSignature")).toString() != signature) return;
    const QJsonObject columns = supplier.value(QStringLiteral("columnMapping")).toObject();
    const int price = columns.value(QStringLiteral("priceWithVat")).toInt(-1);
    const int qty = columns.value(QStringLiteral("quantity")).toInt(map->qty);
    const int name = columns.value(QStringLiteral("partName")).toInt(map->name);
    const int oem = columns.value(QStringLiteral("article")).toInt(map->oem);
    if (price < 0 || (name < 0 && oem < 0) || qty < 0) return;

    map->price = price;
    map->qty = qty;
    map->name = name;
    map->oem = oem;
    map->brand = columns.value(QStringLiteral("brand")).toInt(map->brand);
    map->amount = columns.value(QStringLiteral("totalWithVat")).toInt(map->amount);
    map->store = columns.value(QStringLiteral("supplierName")).toInt(map->store);
    map->priceVatExplicit = supplier.value(QStringLiteral("vatPriceConfirmed")).toBool(true);
    map->priceVatExcludedCandidate = false;
    map->priceFromSupplierTemplate = true;
    map->amountFromSupplierTemplate = map->amount >= 0;
    map->detectionSupplierKey = supplierKey;
}

static int evaluateInvoiceSupplierScore(const QList<QStringList>& rows, const ExcelImportColumnMap& map);

static void persistSupplierInvoiceTemplateIfUseful(const QList<QStringList>& rows,
                                                   const ExcelImportColumnMap& map,
                                                   int totalConfidence)
{
    if (!map.hasVatConfirmedPrice() || map.qty < 0 || map.name < 0 || totalConfidence < 85) return;
    const QString supplierName = detectInvoiceSourceStore(rows);
    const QString supplierKey = supplierInvoiceTemplateKey(supplierName);
    if (supplierKey.trimmed().isEmpty()) return;

    QString templateError;
    const bool saved = SupplierInvoiceTemplateStore::update(SupplierInvoiceTemplateStore::defaultPath(), [&](QJsonObject& root) {
    QJsonObject supplier = root.value(supplierKey).toObject();
    supplier.insert(QStringLiteral("supplierDisplayName"), supplierName.isEmpty() ? supplierKey : supplierName);
    supplier.insert(QStringLiteral("supplierKey"), supplierKey);
    supplier.insert(QStringLiteral("headerSignature"), supplierInvoiceHeaderSignature(rows));
    supplier.insert(QStringLiteral("vatPriceConfirmed"), true);
    supplier.insert(QStringLiteral("lastUsedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    supplier.insert(QStringLiteral("lastConfidence"), totalConfidence);
    supplier.insert(QStringLiteral("vatPriceColumn"), map.price);

    QJsonArray confidenceHistory = supplier.value(QStringLiteral("confidenceHistory")).toArray();
    QJsonObject confidenceEntry;
    confidenceEntry.insert(QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    confidenceEntry.insert(QStringLiteral("totalConfidence"), totalConfidence);
    confidenceEntry.insert(QStringLiteral("supplierScore"), evaluateInvoiceSupplierScore(rows, map));
    confidenceEntry.insert(QStringLiteral("vatPriceColumn"), map.price);
    confidenceHistory.append(confidenceEntry);
    while (confidenceHistory.size() > 20) confidenceHistory.removeAt(0);
    supplier.insert(QStringLiteral("confidenceHistory"), confidenceHistory);

    QJsonObject columns;
    columns.insert(QStringLiteral("partName"), map.name);
    columns.insert(QStringLiteral("article"), map.oem);
    columns.insert(QStringLiteral("brand"), map.brand);
    columns.insert(QStringLiteral("quantity"), map.qty);
    columns.insert(QStringLiteral("priceWithVat"), map.price);
    columns.insert(QStringLiteral("totalWithVat"), map.amount);
    columns.insert(QStringLiteral("supplierName"), map.store);
    supplier.insert(QStringLiteral("columnMapping"), columns);

    root.insert(supplierKey, supplier);
    }, &templateError);
    if (!saved) qWarning() << "Optional supplier invoice template not saved:" << templateError;
}

static ExcelImportColumnMap detectExcelImportColumns(const QList<QStringList>& rows, int* headerRowIndex, const QSet<int>& highlightedColumns = {})
{
    ExcelImportColumnMap bestMap;
    int bestScore = -1;
    int bestRow = -1;

    const int probeRows = qMin(rows.size(), 16);
    for (int rowIndex = 0; rowIndex < probeRows; ++rowIndex) {
        ExcelImportColumnMap current;
        int finalScore = scoreExcelImportHeaderRow(rows.at(rowIndex), &current, nullptr);

        if (!highlightedColumns.isEmpty()) {
            ExcelImportColumnMap preferredMap;
            const int preferredScore = scoreExcelImportHeaderRow(rows.at(rowIndex), &preferredMap, &highlightedColumns);
            if (preferredMap.hasCoreColumns() && preferredScore > finalScore) {
                current = preferredMap;
                finalScore = preferredScore;
            }
        }

        if (current.hasCoreColumns() && finalScore > bestScore) {
            bestScore = finalScore;
            bestMap = current;
            bestRow = rowIndex;
        }
    }

    if (bestScore < 6) {
        bestMap.date = rows.isEmpty() ? -1 : 0;
        bestMap.oem = rows.isEmpty() ? -1 : 1;
        bestMap.name = rows.isEmpty() ? -1 : 2;
        bestMap.brand = -1;
        bestMap.qty = rows.isEmpty() ? -1 : 3;
        bestMap.price = -1;
        bestMap.amount = -1;
        bestMap.store = rows.isEmpty() ? -1 : 6;
        bestMap.headerScore = 0;
        bestRow = -1;
    }

    applySupplierInvoiceTemplate(rows, &bestMap);
    applySupplierExcelPdfVatRules(rows, bestRow, &bestMap);

    if (headerRowIndex) *headerRowIndex = bestRow;
    return bestMap;
}

// Split-stage contract markers kept here for the static TZ gate; implementation lives in InvoiceImportParsedDraftStage.ipp:
// invoiceAmountMatchesVatPrice
// evaluateInvoiceColumnConfidence
// automaticAllowed
// buildInvoiceColumnMappingPreview
// parseImportedPartDraftsWithConfirmedVatMapping
// Ручний імпорт заблоковано: підтвердьте колонку 'Ціна з ПДВ'
// report.totalConfidence >= 85
// draft.name.isEmpty()) continue
// qtyText.trimmed().isEmpty() || draft.priceText.trimmed().isEmpty()
// persistSupplierInvoiceTemplateFromManualConfirmation
#include "core/import/internal/InvoiceImportParsedDraftStage.ipp"
