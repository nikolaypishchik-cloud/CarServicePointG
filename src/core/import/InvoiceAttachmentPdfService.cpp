#include "core/util/DateFormats.h"
#include "core/import/InvoiceAttachmentPdfService.h"

#include "runtime/AppRuntime.h"
#include "core/import/InvoiceImportEngine.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "core/io/SafeFileOps.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QList>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QStringList>
#include <QTextOption>
#include <QThread>
#include <QtGlobal>
#include <QVector>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QLockFile>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace InvoiceImport {

static bool invoiceAttachmentOnGuiThread()
{
    return QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}

static QString invoicePdfCacheDir(QString* errorText = nullptr)
{
    const QString dir = QDir(AppRuntime::sessionDir()).filePath(QStringLiteral("invoice_pdf_cache"));
    if (!QDir().mkpath(dir)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити кеш PDF накладних:\n%1")
                .arg(QDir::toNativeSeparators(dir));
        }
        return QString();
    }
    return dir;
}

static QString normalizedInvoiceDateForUi(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) return value;
    const QString normalized = PartImportTextSupport::normalizeImportedDateDisplay(value).trimmed();
    if (!normalized.isEmpty() && normalized != value) return normalized;
    QDate d = DateFormats::parseDate(value);
    return d.isValid() ? d.toString(QStringLiteral("dd.MM.yy")) : value;
}

static QString normalizedImportHeader(QString text)
{
    text = text.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return text;
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
static bool isExcelImportFooterRow(const QStringList& row)
{
    QString joined;
    for (const QString& cell : row) {
        if (!joined.isEmpty()) joined += QLatin1Char(' ');
        joined += cell.trimmed();
    }

    const QString normalized = normalizedImportHeader(joined);
    if (normalized.isEmpty()) return false;

    return normalized.contains(QStringLiteral("всьогодосплати")) ||
           normalized.contains(QStringLiteral("всегодосплате")) ||
           normalized.contains(QStringLiteral("відпустив")) ||
           normalized.contains(QStringLiteral("отпустил")) ||
           normalized.contains(QStringLiteral("отримав")) ||
           normalized.contains(QStringLiteral("получил")) ||
           normalized.contains(QStringLiteral("своїмпідписом")) ||
           normalized.contains(QStringLiteral("своейподписью")) ||
           normalized.contains(QStringLiteral("замовникпідтверджує")) ||
           normalized.contains(QStringLiteral("заказчикподтверждает"));
}

static QString normalizedInvoiceDisplayNumber(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};

    auto scoreInvoiceNumberCandidate = [](QString value) {
        value = cleanInvoiceMetaNumber(value).trimmed();
        if (!looksLikeInvoiceMetaNumberCandidate(value)) return -1;

        const QString compact = value.simplified();
        const QString upper = compact.toUpper();
        int score = 0;
        if (upper.startsWith(QStringLiteral("A-")) ||
            upper.startsWith(QStringLiteral("А-")) ||
            upper.startsWith(QStringLiteral("AN-")) ||
            upper.startsWith(QStringLiteral("SI"))) {
            score += 100;
        }
        if (upper.contains(QLatin1Char('-'))) score += 20;
        if (upper.contains(QRegularExpression(QStringLiteral(R"(\d{4,})")))) score += 10;
        if (!compact.contains(QLatin1Char(' '))) score += 5;
        score += qMin(compact.size(), 24);
        return score;
    };

    QString candidate = extractInvoiceMetaNumberFromText(raw);
    int bestScore = scoreInvoiceNumberCandidate(candidate);

    const QList<QRegularExpression> fallbackPatterns = {
        QRegularExpression(QStringLiteral(R"(([AА][NН]?-[0-9]{3,}[A-ZА-ЯІЇЄҐ0-9/-]*))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((SI[0-9A-ZА-ЯІЇЄҐ-]{3,}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(([A-ZА-ЯІЇЄҐ]{1,5}-[A-ZА-ЯІЇЄҐ0-9]{3,}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(([A-ZА-ЯІЇЄҐ0-9][A-ZА-ЯІЇЄҐ0-9/\_.\-]{3,}))"), QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression& rx : fallbackPatterns) {
        QRegularExpressionMatchIterator it = rx.globalMatch(raw);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString current = cleanInvoiceMetaNumber(match.captured(1));
            const int currentScore = scoreInvoiceNumberCandidate(current);
            if (currentScore > bestScore) {
                bestScore = currentScore;
                candidate = current;
            }
        }
    }

    if (bestScore < 0) {
        const QString cleaned = cleanInvoiceMetaNumber(raw).simplified();
        const QString folded = cleaned.toCaseFolded();
        if (!cleaned.contains(QLatin1Char(' ')) &&
            !folded.contains(QString::fromUtf8("видаткова")) &&
            !folded.contains(QString::fromUtf8("накладна")) &&
            !folded.contains(QString::fromUtf8("від")) &&
            looksLikeInvoiceMetaNumberCandidate(cleaned)) {
            candidate = cleaned;
            bestScore = scoreInvoiceNumberCandidate(candidate);
        }
    }

    if (bestScore < 0) return {};
    return cleanInvoiceMetaNumber(candidate).trimmed().toUpper();
}

static QString normalizedInvoiceDisplayDate(const QString& rawDate, const QString& rawMetaText = QString())
{
    QString iso = normalizeInvoiceMetaDateToIso(rawDate);
    if (iso.isEmpty() && !rawMetaText.trimmed().isEmpty()) {
        iso = extractInvoiceMetaDateFromText(rawMetaText);
    }

    return normalizedInvoiceDateForUi(iso.isEmpty() ? rawDate : iso);
}

static QString cleanInvoiceCompanyLabel(QString text)
{
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text = text.simplified().trimmed();
    text.remove(QRegularExpression(QStringLiteral(R"(^\s*(?:постачальник|магазин|supplier|store)\s*[:\-]?\s*)"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral(R"(\s{2,})")));
    text = text.trimmed();
    if (text.size() > 80) text = text.left(80).trimmed();
    text.remove(QRegularExpression(QStringLiteral(R"([,;:\-\s]+$)")));
    return text.trimmed();
}

static QString normalizeInvoiceCompanyName(QString text)
{
    const QString display = cleanInvoiceCompanyLabel(text);
    const QString folded = display.toCaseFolded();
    if (folded.isEmpty()) return QString();
    if (folded.contains(QString::fromUtf8("автотехнікс")) ||
        folded.contains(QString::fromUtf8("автотехникс")) ||
        folded.contains(QStringLiteral("autotechnics")) ||
        folded.contains(QStringLiteral("autotehnix")) ||
        folded.contains(QStringLiteral("autotechnix"))) {
        return QString::fromUtf8("Автотехнікс");
    }
    if (folded.contains(QString::fromUtf8("автонова")) ||
        folded.contains(QStringLiteral("avtonova"))) {
        return QString::fromUtf8("Автонова-D");
    }
    if (folded.contains(QString::fromUtf8("еліт")) ||
        folded.contains(QString::fromUtf8("елит")) ||
        folded.contains(QStringLiteral("elit"))) {
        return QString::fromUtf8("ЕЛІТ-Україна");
    }
    const bool hasUnik = folded.contains(QString::fromUtf8("юнік")) ||
                         folded.contains(QString::fromUtf8("юник")) ||
                         folded.contains(QString::fromUtf8("юнікс")) ||
                         folded.contains(QString::fromUtf8("юникс")) ||
                         folded.contains(QStringLiteral("unik")) ||
                         folded.contains(QStringLiteral("uniks")) ||
                         folded.contains(QStringLiteral("unix"));
    if (hasUnik && (folded.contains(QString::fromUtf8("трейд")) || folded.contains(QStringLiteral("trade")))) {
        return display.contains(QString::fromUtf8("ППФ"), Qt::CaseInsensitive) ? display : QString::fromUtf8("Юнік-трейд");
    }
    if (folded.contains(QString::fromUtf8("ямато")) || folded.contains(QStringLiteral("yamato"))) {
        return QString::fromUtf8("Ямато");
    }
    return QString();
}

static QString extractInvoiceCompanyNameFromRows(const QList<QStringList>& rows)
{
    const int scanRows = qMin(rows.size(), 24);
    for (int rowIndex = 0; rowIndex < scanRows; ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        for (int col = 0; col < row.size(); ++col) {
            const QString cell = row.at(col).simplified().trimmed();
            if (cell.isEmpty()) continue;
            const QString folded = cell.toCaseFolded();
            const bool labeled = folded.contains(QString::fromUtf8("постачальник")) ||
                                 folded.contains(QString::fromUtf8("магазин")) ||
                                 folded.contains(QStringLiteral("supplier")) ||
                                 folded.contains(QStringLiteral("store"));
            if (!labeled) continue;

            QString candidate = cleanInvoiceCompanyLabel(cell);
            if (candidate.compare(cell, Qt::CaseInsensitive) == 0 || candidate.isEmpty()) {
                for (int next = col + 1; next < row.size(); ++next) {
                    candidate = cleanInvoiceCompanyLabel(row.at(next));
                    if (!candidate.isEmpty()) break;
                }
            }
            if (candidate.isEmpty()) {
                candidate = cleanInvoiceCompanyLabel(row.join(QLatin1Char(' ')));
            }
            if (!candidate.isEmpty()) {
                if (const QString normalized = normalizeInvoiceCompanyName(candidate); !normalized.isEmpty()) return normalized;
                return candidate;
            }
        }
    }

    for (int rowIndex = 0; rowIndex < scanRows; ++rowIndex) {
        const QString joined = rows.at(rowIndex).join(QLatin1Char(' '));
        if (const QString fromRow = normalizeInvoiceCompanyName(joined); !fromRow.isEmpty()) {
            return fromRow;
        }
    }

    return QString();
}

static QString detectInvoiceCompanyName(QString documentNumber,
                                       const QString& sourcePath = QString(),
                                       const QList<QStringList>& rows = {},
                                       const QString& supplierName = QString())
{
    if (const QString fromRows = extractInvoiceCompanyNameFromRows(rows); !fromRows.isEmpty()) {
        return fromRows;
    }
    if (const QString fromSupplier = normalizeInvoiceCompanyName(supplierName); !fromSupplier.isEmpty()) {
        return fromSupplier;
    }

    documentNumber = documentNumber.trimmed().toUpper();
    documentNumber.remove(QRegularExpression(QStringLiteral(R"([^A-ZА-ЯІЇЄҐ0-9-]+)")));

    if (documentNumber.startsWith(QStringLiteral("SI"))) {
        return QString::fromUtf8("Автотехнікс");
    }
    if (documentNumber.startsWith(QStringLiteral("A-")) ||
        documentNumber.startsWith(QStringLiteral("А-")) ||
        documentNumber.startsWith(QStringLiteral("AN-"))) {
        return QString::fromUtf8("Автонова-D");
    }

    if (const QString fromPath = normalizeInvoiceCompanyName(QFileInfo(sourcePath).fileName()); !fromPath.isEmpty()) {
        return fromPath;
    }

    return QString::fromUtf8("Постачальник");
}

static QString cachedInvoicePdfPath(const QString& sourcePath,
                                    const QString& documentNumber,
                                    const QString& documentDate,
                                    const QString& companyName = QString(),
                                    const QString& supplierName = QString(),
                                    QString* errorText = nullptr)
{
    const QFileInfo fi(sourcePath);
    QFile source(sourcePath);
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    if (!source.open(QIODevice::ReadOnly) || !sourceHash.addData(&source)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати джерело для перевірки кешу PDF.");
        return {};
    }
    const QString key = QStringLiteral("invoice_pdf_cache_v3|%1|%2|%3|%4|%5|%6|%7|%8")
                            .arg(fi.absoluteFilePath(),
                                 documentNumber.trimmed(),
                                 documentDate.trimmed(),
                                 QString::fromLatin1(sourceHash.result().toHex()),
                                 supplierName.trimmed(),
                                 QString::number(fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0),
                                 QString::number(fi.exists() ? fi.size() : 0), companyName);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    QString safeBase = fi.completeBaseName().trimmed();
    if (safeBase.isEmpty()) safeBase = QStringLiteral("invoice");
    safeBase.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}_-]+)")), QStringLiteral("_"));
    const QString cacheDir = invoicePdfCacheDir(errorText);
    if (cacheDir.trimmed().isEmpty()) return QString();
    return QDir(cacheDir).filePath(QStringLiteral("%1_%2.pdf").arg(safeBase, hash));
}

static QString invoiceRowsIdentity(const QList<QStringList>& rows, const QString& company)
{
    QJsonArray values;
    for (const auto& row : rows) {
        QJsonArray cells;
        for (const auto& cell : row) cells.append(cell);
        values.append(cells);
    }
    return company + QLatin1Char('|') + QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(values).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

static QByteArray invoicePdfDigest(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

static bool verifiedInvoicePdfCache(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile receipt(path + QStringLiteral(".sha256"));
    if (!receipt.open(QIODevice::ReadOnly)) return false;
    const QByteArray expected = receipt.readAll().trimmed();
    return expected.size() == 64 && expected == invoicePdfDigest(path);
}

static bool publishInvoicePdf(const QString& outputPath, const std::function<bool(const QString&)>& render, QString* errorText)
{
    QLockFile lock(outputPath + QStringLiteral(".lock"));
    if (!lock.tryLock(10000)) {
        if (errorText) *errorText = QString::fromUtf8("PDF зараз формується іншою операцією.");
        return false;
    }
    if (verifiedInvoicePdfCache(outputPath)) return true;
    QTemporaryFile staging(QFileInfo(outputPath).dir().filePath(QStringLiteral("invoice_XXXXXX.pdf")));
    if (!staging.open()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасовий PDF.");
        return false;
    }
    const QString stagingPath = staging.fileName();
    staging.close();
    if (!render(stagingPath) || !validateInvoicePdf(stagingPath, errorText)) return false;
    QFile source(stagingPath);
    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося підготувати збереження PDF.");
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() || output.write(block) != block.size()) {
            if (errorText) *errorText = QString::fromUtf8("Не вдалося повністю записати PDF.");
            return false;
        }
    }
    if (source.error() != QFileDevice::NoError || !output.commit()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося завершити збереження PDF.");
        return false;
    }
    const QByteArray digest = invoicePdfDigest(outputPath);
    QSaveFile receipt(outputPath + QStringLiteral(".sha256"));
    receipt.setDirectWriteFallback(false);
    if (digest.size() != 64 || !receipt.open(QIODevice::WriteOnly) || receipt.write(digest) != digest.size() || !receipt.commit()) {
        if (errorText) *errorText = QString::fromUtf8("PDF збережено, але перевірку кешу не завершено. Повторіть операцію.");
        return false;
    }
    return true;
}


static int invoiceMmToPx(QPdfWriter* pdf, qreal mm)
{
    return qRound((mm / 25.4) * pdf->resolution());
}

static QString invoiceGroupThousands(const QString& digits)
{
    QString out;
    out.reserve(digits.size() + digits.size() / 3);
    const int n = digits.size();
    for (int i = 0; i < n; ++i) {
        out.append(digits.at(i));
        const int left = n - i - 1;
        if (left > 0 && (left % 3) == 0) out.append(QLatin1Char(' '));
    }
    return out;
}

static double invoiceParseLooseNumber(QString raw, bool* ok = nullptr)
{
    raw = raw.trimmed();
    raw.remove(QChar(0x00A0));
    raw.remove(QLatin1Char(' '));
    raw.replace(QRegularExpression(QStringLiteral(R"([^\d,\.\-]+)")), QString());
    raw.replace(QLatin1Char(','), QLatin1Char('.'));
    const double value = raw.toDouble(ok);
    return value;
}

static QString invoiceFixedNumber(double value, int decimals, bool trimTrailingZeros = false)
{
    if (!qIsFinite(value)) value = 0.0;
    const bool neg = value < 0.0;
    QString raw = QString::number(qAbs(value), 'f', qMax(0, decimals));
    const int dot = raw.indexOf(QLatin1Char('.'));
    QString intPart = (dot >= 0) ? raw.left(dot) : raw;
    QString fracPart = (dot >= 0) ? raw.mid(dot + 1) : QString();
    if (trimTrailingZeros) {
        while (!fracPart.isEmpty() && fracPart.endsWith(QLatin1Char('0'))) fracPart.chop(1);
    }
    QString out = invoiceGroupThousands(intPart);
    if (!fracPart.isEmpty()) out += QLatin1Char(',') + fracPart;
    if (neg) out.prepend(QLatin1Char('-'));
    return out;
}

static QString invoiceFormatMoneyText(const QString& raw, int decimals = 2, bool trimTrailingZeros = false)
{
    bool ok = false;
    const double value = invoiceParseLooseNumber(raw, &ok);
    return ok ? invoiceFixedNumber(value, decimals, trimTrailingZeros) : raw.trimmed();
}

static QString invoiceFormatQtyText(const QString& raw)
{
    bool ok = false;
    const double value = invoiceParseLooseNumber(raw, &ok);
    if (!ok) return raw.trimmed();
    const qint64 rounded = qRound64(value);
    if (qAbs(value - double(rounded)) < 1e-9) return QString::number(rounded);
    return invoiceFixedNumber(value, 2, true);
}

static QString invoiceNormalizeUnitText(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) return QString::fromUtf8("шт");
    value.replace(QChar(0x00A0), QLatin1Char(' '));
    value = value.simplified();
    return value;
}

static QString invoiceNormalizeLabel(QString text)
{
    return normalizedImportHeader(text);
}

static bool isInvoiceMetaLabel(const QString& normalized)
{
    return normalized.startsWith(QStringLiteral("замовник")) ||
           normalized.startsWith(QStringLiteral("заказчик")) ||
           normalized == QStringLiteral("код") ||
           normalized.startsWith(QStringLiteral("тел")) ||
           normalized.startsWith(QStringLiteral("телефон")) ||
           normalized.startsWith(QStringLiteral("замовлення")) ||
           normalized.startsWith(QStringLiteral("заказ")) ||
           normalized.startsWith(QStringLiteral("маршрут")) ||
           normalized.startsWith(QStringLiteral("борг")) ||
           normalized.startsWith(QStringLiteral("eur")) ||
           normalized.startsWith(QStringLiteral("usd"));
}

static QString invoiceCellValue(const QStringList& row, int index)
{
    return (index >= 0 && index < row.size()) ? row.at(index).trimmed() : QString();
}

static QString invoiceValueToRight(const QStringList& row, int startCol)
{
    QStringList parts;
    for (int col = startCol + 1; col < row.size(); ++col) {
        const QString cell = row.at(col).trimmed();
        if (cell.isEmpty()) {
            if (!parts.isEmpty()) break;
            continue;
        }
        if (isInvoiceMetaLabel(invoiceNormalizeLabel(cell))) break;
        parts << cell;
    }
    return parts.join(QLatin1Char(' ')).simplified();
}

static QString invoiceExtractLabeledValue(const QList<QStringList>& rows,
                                          int maxRows,
                                          std::initializer_list<const char*> labels)
{
    for (int rowIndex = 0; rowIndex < qMin(maxRows, rows.size()); ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        for (int col = 0; col < row.size(); ++col) {
            const QString cell = row.at(col).trimmed();
            if (cell.isEmpty()) continue;
            const QString normalized = invoiceNormalizeLabel(cell);
            bool match = false;
            for (const char* label : labels) {
                const QString expected = QString::fromUtf8(label);
                if (normalized == expected || normalized.startsWith(expected)) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;

            const int colonPos = cell.indexOf(QLatin1Char(':'));
            if (colonPos >= 0) {
                const QString inlineValue = cell.mid(colonPos + 1).trimmed();
                if (!inlineValue.isEmpty()) return inlineValue;
            }

            const QString rightValue = invoiceValueToRight(row, col);
            if (!rightValue.isEmpty()) return rightValue;

            if (rowIndex + 1 < rows.size()) {
                const QString nextLine = invoiceCellValue(rows.at(rowIndex + 1), col);
                if (!nextLine.isEmpty() && !isInvoiceMetaLabel(invoiceNormalizeLabel(nextLine))) return nextLine;
                const QString nextRight = invoiceValueToRight(rows.at(rowIndex + 1), col - 1);
                if (!nextRight.isEmpty()) return nextRight;
            }
        }
    }
    return QString();
}

static QString invoiceExtractByRegex(const QList<QStringList>& rows,
                                     int maxRows,
                                     const QRegularExpression& rx,
                                     int captureIndex = 1)
{
    for (int rowIndex = 0; rowIndex < qMin(maxRows, rows.size()); ++rowIndex) {
        const QString joined = joinInvoiceMetaRow(rows.at(rowIndex));
        if (joined.isEmpty()) continue;
        const QRegularExpressionMatch match = rx.match(joined);
        if (match.hasMatch()) return match.captured(captureIndex).trimmed();
    }
    return QString();
}

struct InvoiceTemplateColumnMap {
    int oem = -1;
    int name = -1;
    int brand = -1;
    int qty = -1;
    int unit = -1;
    int price = -1;
    int amount = -1;
    int euro = -1;

    bool hasCoreColumns() const { return oem >= 0 || name >= 0; }
};

static int scoreInvoiceTemplateHeaderCell(const QString& normalized, int* logicalTypeOut = nullptr)
{
    enum LogicalType {
        TypeNone = 0,
        TypeOem,
        TypeName,
        TypeBrand,
        TypeQty,
        TypeUnit,
        TypePrice,
        TypeAmount,
        TypeEuro
    };

    int type = TypeNone;
    int score = 0;

    if (normalizedHeaderEqualsAny(normalized, {"oemномер", "oem", "каталожнийномер", "активнкодтовару", "кодтовару", "артикул"}) ||
        normalizedHeaderContainsAny(normalized, {"каталожн", "кодтовару", "oem"})) {
        type = TypeOem;
        score = 8;
    } else if (normalizedHeaderEqualsAny(normalized, {"найменування", "наименование", "товар", "запчастина", "назва"}) ||
               normalizedHeaderContainsAny(normalized, {"найменув", "наименов", "запчаст"})) {
        type = TypeName;
        score = 8;
    } else if (normalizedHeaderEqualsAny(normalized, {"бренд", "brand", "виробник", "manufacturer"}) ||
               normalizedHeaderContainsAny(normalized, {"бренд", "brand", "вироб"})) {
        type = TypeBrand;
        score = 7;
    } else if (normalizedHeaderEqualsAny(normalized, {"кількість", "кiлькiсть", "количество", "кілть", "qty", "quantity"}) ||
               normalizedHeaderContainsAny(normalized, {"кіл", "кiл", "кол", "qty", "quantity"})) {
        type = TypeQty;
        score = 7;
    } else if (normalizedHeaderEqualsAny(normalized, {"од", "ед", "одиницявиміру", "одиницявимiру", "едизм", "unit"}) ||
               normalizedHeaderContainsAny(normalized, {"одиниц", "unit"})) {
        type = TypeUnit;
        score = 6;
    } else if (normalizedHeaderEqualsAny(normalized, {"ціна", "цiна", "цена", "price", "ціназпдв", "ценаспдв"}) ||
               normalizedHeaderContainsAny(normalized, {"ціна", "цiна", "цена", "price"})) {
        type = TypePrice;
        score = 7;
    } else if (normalized.contains(QStringLiteral("euro")) || normalized.contains(QStringLiteral("євро"))) {
        type = TypeEuro;
        score = 8;
    } else if (normalizedHeaderEqualsAny(normalized, {"сума", "сумма", "вартість", "стоимость", "amount", "total"}) ||
               normalizedHeaderContainsAny(normalized, {"сума", "сумм", "варт", "amount", "total"})) {
        type = TypeAmount;
        score = 7;
    }

    if (logicalTypeOut) *logicalTypeOut = type;
    return score;
}

static int scoreInvoiceTemplateHeaderRow(const QStringList& row, InvoiceTemplateColumnMap* outMap = nullptr)
{
    InvoiceTemplateColumnMap map;
    int score = 0;
    for (int col = 0; col < row.size(); ++col) {
        const QString normalized = invoiceNormalizeLabel(row.at(col));
        if (normalized.isEmpty()) continue;
        int type = 0;
        const int cellScore = scoreInvoiceTemplateHeaderCell(normalized, &type);
        if (cellScore <= 0) continue;
        bool assigned = false;
        switch (type) {
        case 1: if (map.oem    < 0) { map.oem = col; assigned = true; } break;
        case 2: if (map.name   < 0) { map.name = col; assigned = true; } break;
        case 3: if (map.brand  < 0) { map.brand = col; assigned = true; } break;
        case 4: if (map.qty    < 0) { map.qty = col; assigned = true; } break;
        case 5: if (map.unit   < 0) { map.unit = col; assigned = true; } break;
        case 6: if (map.price  < 0) { map.price = col; assigned = true; } break;
        case 7: if (map.amount < 0) { map.amount = col; assigned = true; } break;
        case 8: if (map.euro   < 0) { map.euro = col; assigned = true; } break;
        default: break;
        }
        if (assigned) score += cellScore;
    }
    if (outMap) *outMap = map;
    return score;
}

static InvoiceTemplateColumnMap detectInvoiceTemplateColumns(const QList<QStringList>& rows, int* headerRowIndex)
{
    InvoiceTemplateColumnMap bestMap;
    int bestScore = -1;
    int bestRow = -1;
    const int probeRows = qMin(rows.size(), 24);
    for (int rowIndex = 0; rowIndex < probeRows; ++rowIndex) {
        InvoiceTemplateColumnMap current;
        const int score = scoreInvoiceTemplateHeaderRow(rows.at(rowIndex), &current);
        if (current.hasCoreColumns() && score > bestScore) {
            bestScore = score;
            bestMap = current;
            bestRow = rowIndex;
        }
    }
    if (headerRowIndex) *headerRowIndex = bestRow;
    return bestMap;
}

struct InvoiceTemplateItem {
    QString oem;
    QString name;
    QString brand;
    QString qty;
    QString unit;
    QString price;
    QString amount;
    QString euro;
};

struct InvoiceTemplateData {
    QString documentNumber;
    QString documentDate;
    QString customerName;
    QString customerCode;
    QString customerPhone;
    QString orderReference;
    QString totalDebt;
    QString eurRate;
    QString usdRate;
    QString totalPayable;
    QString totalEuro;
    QList<InvoiceTemplateItem> items;
};

static bool invoiceCanonicalTemplateHasCurrencyColumn(const InvoiceTemplateData& data)
{
    for (const InvoiceTemplateItem& item : data.items) {
        if (!item.euro.trimmed().isEmpty()) return true;
    }
    return false;
}

static QStringList invoiceCanonicalTemplateHeaders(bool includeCurrencyColumn)
{
    // Canonical visual standard for Excel-originated supplier invoices.
    // The layout intentionally mirrors the supplied Avtonova PDF sample: every
    // spreadsheet supplier is rendered into the same columns before attachment.
    // If the source invoice has no currency values, the currency column is not shown.
    QStringList headers{
        QString::fromUtf8("№"),
        QString::fromUtf8("OEM номер"),
        QString::fromUtf8("Найменування"),
        QString::fromUtf8("Бренд"),
        QString::fromUtf8("Кільк\n-ть"),
        QString::fromUtf8("Од"),
        QString::fromUtf8("Ціна"),
        QString::fromUtf8("Сума")
    };
    if (includeCurrencyColumn) headers << QString::fromUtf8("Сума\nEURO");
    return headers;
}

static QStringList invoiceCanonicalTemplateHeaders()
{
    return invoiceCanonicalTemplateHeaders(true);
}

static QStringList invoiceCanonicalTemplateRow(int index, const InvoiceTemplateItem& item, bool includeCurrencyColumn)
{
    QStringList row{
        QString::number(index + 1),
        item.oem,
        item.name,
        item.brand,
        item.qty,
        item.unit,
        item.price,
        item.amount
    };
    if (includeCurrencyColumn) row << item.euro;
    return row;
}

static QStringList invoiceCanonicalTemplateRow(int index, const InvoiceTemplateItem& item)
{
    return invoiceCanonicalTemplateRow(index, item, true);
}

static QList<QStringList> buildCanonicalInvoiceRows(const InvoiceTemplateData& data, bool includeCurrencyColumn)
{
    QList<QStringList> canonicalRows;
    canonicalRows.reserve(data.items.size() + 1);
    canonicalRows.append(invoiceCanonicalTemplateHeaders(includeCurrencyColumn));
    for (int i = 0; i < data.items.size(); ++i) {
        canonicalRows.append(invoiceCanonicalTemplateRow(i, data.items.at(i), includeCurrencyColumn));
    }
    return canonicalRows;
}

static QList<QStringList> buildCanonicalInvoiceRows(const InvoiceTemplateData& data)
{
    return buildCanonicalInvoiceRows(data, invoiceCanonicalTemplateHasCurrencyColumn(data));
}

static InvoiceTemplateData buildInvoiceTemplateData(const QString& documentNumber,
                                                    const QString& documentDate,
                                                    const QList<QStringList>& rows)
{
    InvoiceTemplateData data;
    data.documentNumber = normalizedInvoiceDisplayNumber(documentNumber);
    data.documentDate = normalizedInvoiceDisplayDate(documentDate, documentNumber);

    if (data.documentNumber.isEmpty() || data.documentDate.isEmpty()) {
        const InvoiceImportMeta meta = detectInvoiceImportMeta(rows, {});
        if (data.documentNumber.isEmpty()) data.documentNumber = normalizedInvoiceDisplayNumber(meta.documentNumber);
        if (data.documentDate.isEmpty()) data.documentDate = normalizedInvoiceDisplayDate(meta.documentDate, meta.documentNumber);
    }

    int headerRow = -1;
    const InvoiceTemplateColumnMap map = detectInvoiceTemplateColumns(rows, &headerRow);
    const int topRows = (headerRow >= 0) ? qMin(headerRow + 2, rows.size()) : qMin(18, rows.size());

    data.customerName = invoiceExtractLabeledValue(rows, topRows, {"замовник", "заказчик", "клиент"});
    data.customerCode = invoiceExtractLabeledValue(rows, topRows, {"код"});
    data.customerPhone = invoiceExtractLabeledValue(rows, topRows, {"тел", "телефон"});
    data.orderReference = invoiceExtractLabeledValue(rows, topRows, {"замовлення", "заказ", "маршрут"});

    const QString debtRaw = invoiceExtractByRegex(rows, topRows,
        QRegularExpression(QStringLiteral(R"((?:борг\w*\s*(?:всього)?)[^\d-]*([\d\s]+(?:[.,]\d+)?))"), QRegularExpression::CaseInsensitiveOption));
    if (!debtRaw.isEmpty()) {
        data.totalDebt = invoiceFormatMoneyText(debtRaw, 0, true);
    }

    const QString eurRaw = invoiceExtractByRegex(rows, topRows,
        QRegularExpression(QStringLiteral(R"(EUR\s*[:;]?\s*([\d\s]+(?:[.,]\d+)?))"), QRegularExpression::CaseInsensitiveOption));
    const QString usdRaw = invoiceExtractByRegex(rows, topRows,
        QRegularExpression(QStringLiteral(R"(USD\s*[:;]?\s*([\d\s]+(?:[.,]\d+)?))"), QRegularExpression::CaseInsensitiveOption));
    if (!eurRaw.isEmpty()) data.eurRate = invoiceFormatMoneyText(eurRaw, 2, true);
    if (!usdRaw.isEmpty()) data.usdRate = invoiceFormatMoneyText(usdRaw, 2, true);

    const QString totalPayableRaw = invoiceExtractByRegex(rows, rows.size(),
        QRegularExpression(QStringLiteral(R"((?:всього\s*до\s*сплати|всего\s*к\s*оплате)[^\d-]*([\d\s]+(?:[.,]\d+)?))"), QRegularExpression::CaseInsensitiveOption));
    if (!totalPayableRaw.isEmpty()) data.totalPayable = invoiceFormatMoneyText(totalPayableRaw, 2, false);

    double amountSum = 0.0;
    double euroSum = 0.0;
    bool amountSumOk = false;
    bool euroSumOk = false;

    if (headerRow >= 0 && map.hasCoreColumns()) {
        for (int rowIndex = headerRow + 1; rowIndex < rows.size(); ++rowIndex) {
            const QStringList& row = rows.at(rowIndex);
            if (isExcelImportFooterRow(row)) continue;
            const int headerLikeScore = scoreInvoiceTemplateHeaderRow(row, nullptr);
            if (headerLikeScore >= 14) continue;

            InvoiceTemplateItem item;
            item.oem = invoiceCellValue(row, map.oem);
            item.name = invoiceCellValue(row, map.name);
            item.brand = invoiceCellValue(row, map.brand);
            item.qty = invoiceFormatQtyText(invoiceCellValue(row, map.qty));
            item.unit = invoiceNormalizeUnitText(invoiceCellValue(row, map.unit));
            item.price = invoiceFormatMoneyText(invoiceCellValue(row, map.price), 2, false);
            item.amount = invoiceFormatMoneyText(invoiceCellValue(row, map.amount), 2, false);
            item.euro = invoiceFormatMoneyText(invoiceCellValue(row, map.euro), 2, true);

            const bool allEmpty = item.oem.isEmpty() && item.name.isEmpty() && item.brand.isEmpty() &&
                                  item.qty.isEmpty() && item.price.isEmpty() && item.amount.isEmpty() && item.euro.isEmpty();
            if (allEmpty) continue;

            if (item.unit.isEmpty()) item.unit = QString::fromUtf8("шт");
            if (item.qty.isEmpty()) item.qty = QStringLiteral("1");

            if (item.amount.trimmed().isEmpty()) {
                bool qtyOk = false;
                bool priceOk = false;
                const double qty = invoiceParseLooseNumber(item.qty, &qtyOk);
                const double price = invoiceParseLooseNumber(item.price, &priceOk);
                if (qtyOk && priceOk) item.amount = invoiceFixedNumber(qMax(0.0, qty) * qMax(0.0, price), 2, false);
            }

            if (item.name.isEmpty() && item.oem.isEmpty()) continue;
            data.items.append(item);

            bool amountOk = false;
            const double amountValue = invoiceParseLooseNumber(item.amount, &amountOk);
            if (amountOk) {
                amountSum += amountValue;
                amountSumOk = true;
            }

            bool euroOk = false;
            const double euroValue = invoiceParseLooseNumber(item.euro, &euroOk);
            if (euroOk) {
                euroSum += euroValue;
                euroSumOk = true;
            }
        }
    }

    if (data.totalPayable.isEmpty() && amountSumOk) data.totalPayable = invoiceFixedNumber(amountSum, 2, false);
    if (euroSumOk) data.totalEuro = invoiceFixedNumber(euroSum, 2, true);

    if (data.customerCode.isEmpty()) {
        const QString codeRaw = invoiceExtractByRegex(rows, topRows,
            QRegularExpression(QStringLiteral(R"((?:^|\s)код\s*[:\-]?\s*([\d\s]+))"), QRegularExpression::CaseInsensitiveOption));
        if (!codeRaw.isEmpty()) data.customerCode = invoiceFormatMoneyText(codeRaw, 0, true);
    }

    if (data.customerPhone.isEmpty()) {
        const QString phoneRaw = invoiceExtractByRegex(rows, topRows,
            QRegularExpression(QStringLiteral(R"((?:тел(?:ефон)?)[^\d]*([+\d][\d\s\-()]{7,}))"), QRegularExpression::CaseInsensitiveOption));
        if (!phoneRaw.isEmpty()) data.customerPhone = phoneRaw.simplified();
    }

    return data;
}

#include "core/import/internal/InvoiceAttachmentPdfRendering.ipp"

static bool sourceIsSpreadsheetInvoiceFile(const QString& suffix)
{
    const QString ext = suffix.trimmed().toLower();
    return ext == QStringLiteral("xlsx") || ext == QStringLiteral("xls") || ext == QStringLiteral("xlsm") ||
           ext == QStringLiteral("xltx") || ext == QStringLiteral("xltm") || ext == QStringLiteral("csv") ||
           ext == QStringLiteral("txt");
}

QString ensureInvoicePdfAttachmentFromRows(const QString& sourcePath,
                                           const ExcelReadResult& data,
                                           const QString& documentNumber,
                                           const QString& documentDate,
                                           const QString& supplierName,
                                           QString* errorText)
{
    if (errorText) errorText->clear();
    const QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл накладної не знайдено: %1").arg(sourcePath);
        return QString();
    }
    if (data.rows.size() <= 1) {
        if (errorText) *errorText = QString::fromUtf8("Немає достатньо рядків для створення PDF накладної.");
        return QString();
    }
    if (invoiceAttachmentOnGuiThread()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Підготовку PDF накладної відкладено: конвертація з Excel має виконуватися у фоновому потоці.");
        }
        return QString();
    }

    QString localError;
    QString pdfNumber = normalizedInvoiceDisplayNumber(documentNumber);
    QString pdfDate = normalizedInvoiceDisplayDate(documentDate, documentNumber);
    if (pdfNumber.isEmpty()) pdfNumber = normalizedInvoiceDisplayNumber(data.documentNumber);
    if (pdfDate.isEmpty()) pdfDate = normalizedInvoiceDisplayDate(data.documentDate, data.documentNumber);

    const QString companyName = detectInvoiceCompanyName(pdfNumber, fi.absoluteFilePath(), data.rows, supplierName);
    const QString outputPath = cachedInvoicePdfPath(fi.absoluteFilePath(), pdfNumber, pdfDate,
        invoiceRowsIdentity(data.rows, companyName), supplierName, &localError);
    if (outputPath.trimmed().isEmpty()) {
        if (errorText) *errorText = localError.trimmed().isEmpty()
            ? QString::fromUtf8("Не вдалося підготувати директорію для PDF накладної.")
            : localError;
        return QString();
    }
    if (verifiedInvoicePdfCache(outputPath)) {
        return outputPath;
    }

    QString renderError;
    const bool ok = renderInvoiceTablePdf(outputPath,
                                          QString::fromUtf8("Накладна"),
                                          fi.absoluteFilePath(),
                                          pdfNumber,
                                          pdfDate,
                                          data.rows,
                                          supplierName,
                                          &renderError);
    if (!ok) {
        if (errorText) *errorText = renderError.trimmed().isEmpty()
            ? QString::fromUtf8("Не вдалося створити PDF накладної у єдиному форматі.")
            : renderError;
        return QString();
    }
    return outputPath;
}

QString ensureInvoicePdfAttachment(const QString& sourcePath,
                                  const QString& documentNumber,
                                  const QString& documentDate,
                                  const QString& supplierName,
                                  QString* errorText)
{
    if (errorText) errorText->clear();
    const QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл накладної не знайдено: %1").arg(sourcePath);
        return QString();
    }

    const QString ext = fi.suffix().trimmed().toLower();

    // Conversion and cache hashing belong to ImportJob or another worker.
    // Never substitute an Excel original for a failed PDF preparation.
    if (invoiceAttachmentOnGuiThread()) {
        if (ext == QStringLiteral("pdf")) {
            if (errorText) errorText->clear();
            return fi.absoluteFilePath();
        }

        if (errorText) {
            *errorText = QString::fromUtf8("Підготовку PDF накладної відкладено: блокуюча конвертація у UI-потоці заборонена.");
        }
        return QString();
    }

    QString localError;
    const QString title = QString::fromUtf8("Накладна");
    bool ok = false;
    QString outputPath;

    auto tryRenderStructuredInvoicePdf = [&](const ExcelReadResult& data) -> bool {
        if (data.rows.size() <= 1) return false;

        QString pdfNumber = normalizedInvoiceDisplayNumber(documentNumber);
        QString pdfDate = normalizedInvoiceDisplayDate(documentDate, documentNumber);
        if (pdfNumber.isEmpty()) pdfNumber = normalizedInvoiceDisplayNumber(data.documentNumber);
        if (pdfDate.isEmpty()) pdfDate = normalizedInvoiceDisplayDate(data.documentDate, data.documentNumber);

        const QString companyName = detectInvoiceCompanyName(pdfNumber, fi.absoluteFilePath(), data.rows, supplierName);
        outputPath = cachedInvoicePdfPath(fi.absoluteFilePath(), pdfNumber, pdfDate,
            invoiceRowsIdentity(data.rows, companyName), supplierName, &localError);
        if (outputPath.trimmed().isEmpty()) return false;
        if (verifiedInvoicePdfCache(outputPath)) {
            ok = true;
            return true;
        }

        QString renderError;
        ok = renderInvoiceTablePdf(outputPath, title, fi.absoluteFilePath(), pdfNumber, pdfDate, data.rows, supplierName, &renderError);
        if (!ok) {
            if (localError.trimmed().isEmpty()) localError = renderError;
            return false;
        }
        localError.clear();
        return true;
    };

    if (ext == QStringLiteral("pdf")) {
        const ExcelReadResult data = readInvoiceRows(fi.absoluteFilePath(), &localError);
        if (tryRenderStructuredInvoicePdf(data)) {
            return outputPath;
        }
        if (errorText) errorText->clear();
        return fi.absoluteFilePath();
    }

    if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") ||
        ext == QStringLiteral("bmp") || ext == QStringLiteral("webp") || ext == QStringLiteral("gif")) {
        outputPath = cachedInvoicePdfPath(fi.absoluteFilePath(), normalizedInvoiceDisplayNumber(documentNumber), normalizedInvoiceDisplayDate(documentDate, documentNumber), QString(), supplierName, &localError);
        if (outputPath.trimmed().isEmpty()) {
            if (errorText) *errorText = localError.trimmed().isEmpty()
                ? QString::fromUtf8("Не вдалося підготувати директорію для PDF накладної.")
                : localError;
            return QString();
        }
        if (verifiedInvoicePdfCache(outputPath)) {
            return outputPath;
        }
        ok = renderImageInvoicePdf(outputPath, title, fi.absoluteFilePath(), documentNumber, documentDate, &localError);
    } else if (sourceIsSpreadsheetInvoiceFile(ext)) {
        const ExcelReadResult data = readInvoiceRows(fi.absoluteFilePath(), &localError);
        // Spreadsheet originals are always normalized to the canonical invoice
        // PDF standard instead of being attached with supplier-specific columns.
        outputPath = ensureInvoicePdfAttachmentFromRows(fi.absoluteFilePath(), data, documentNumber, documentDate, supplierName, &localError);
        ok = !outputPath.trimmed().isEmpty();
    } else {
        QList<QStringList> rows;
        rows << (QStringList() << QString::fromUtf8("Файл") << fi.fileName())
             << (QStringList() << QString::fromUtf8("Шлях") << fi.absoluteFilePath());
        const QString normalizedNumber = normalizedInvoiceDisplayNumber(documentNumber);
        const QString normalizedDate = normalizedInvoiceDisplayDate(documentDate, documentNumber);
        const QString companyName = detectInvoiceCompanyName(normalizedNumber, fi.absoluteFilePath(), rows, supplierName);
        outputPath = cachedInvoicePdfPath(fi.absoluteFilePath(), normalizedNumber, normalizedDate, companyName, supplierName, &localError);
        if (outputPath.trimmed().isEmpty()) {
            if (errorText) *errorText = localError.trimmed().isEmpty()
                ? QString::fromUtf8("Не вдалося підготувати директорію для PDF накладної.")
                : localError;
            return QString();
        }
        if (verifiedInvoicePdfCache(outputPath)) {
            return outputPath;
        }
        ok = renderInvoiceTablePdf(outputPath, title, fi.absoluteFilePath(), normalizedNumber, normalizedDate, rows, supplierName, &localError);
    }

    if (!ok) {
        if (errorText) *errorText = localError.trimmed().isEmpty()
            ? QString::fromUtf8("Не вдалося підготувати PDF накладної.")
            : localError;
        return QString();
    }
    return outputPath;
}

} // namespace InvoiceImport
