#include "core/import/InvoiceRecognitionProfiles.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace InvoiceImport {
namespace {
QString path() {
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath("invoice_templates");
    return dir.filePath("invoice_templates/confirmed_layouts.json");
}
QJsonObject readRoot() {
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
using Column = int InvoiceColumnMappingPreview::*;
const Column columns[] = {&InvoiceColumnMappingPreview::dateColumn, &InvoiceColumnMappingPreview::articleColumn,
    &InvoiceColumnMappingPreview::partNameColumn, &InvoiceColumnMappingPreview::brandColumn,
    &InvoiceColumnMappingPreview::quantityColumn, &InvoiceColumnMappingPreview::priceWithVatColumn,
    &InvoiceColumnMappingPreview::totalWithVatColumn, &InvoiceColumnMappingPreview::supplierColumn};
InvoiceRecognitionProfile decode(const QString& key, const QJsonObject& obj) {
    InvoiceRecognitionProfile profile;
    profile.key = key;
    profile.updatedAt = obj["updatedAt"].toString();
    auto& m = profile.mapping;
    m.templateKey = key;
    m.headerRow = obj["headerRow"].toInt(-1);
    m.supplierDisplayName = obj["supplier"].toString();
    m.splitCombinedDescription = obj["splitDescription"].toBool(true);
    m.priceWithVatConfirmed = obj["confirmed"].toBool(false);
    const auto savedColumns = obj["columns"].toArray();
    if (savedColumns.size() != 8) { m.priceWithVatConfirmed = false; return profile; }
    for (int i = 0; i < 8; ++i) m.*columns[i] = savedColumns.at(i).toInt(-1);
    for (const auto& row : obj["sampleRows"].toArray()) {
        QStringList cells;
        for (const auto& cell : row.toArray()) cells.append(cell.toString());
        profile.sampleRows.append(cells);
    }
    return profile;
}
bool writeRoot(const QJsonObject& root, QString* error) {
    QSaveFile file(path());
    const auto data = QJsonDocument(root).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QString::fromUtf8("Не вдалося зберегти шаблон накладної: ") + file.errorString();
        return false;
    }
    return true;
}
}

QString InvoiceRecognitionProfiles::keyFor(const QList<QStringList>& rows, int headerRow, const QString& documentNumber) {
    if (headerRow < 0 || headerRow >= rows.size()) return {};
    QString supplier;
    for (const auto& row : rows) {
        const QString line = row.join(' ').simplified();
        const auto match = QRegularExpression(QString::fromUtf8(R"(^(?:Постачальник|Продавець|Supplier)\s*:?\s+(.+)$)"),
            QRegularExpression::CaseInsensitiveOption).match(line);
        if (match.hasMatch()) { supplier = match.captured(1).toCaseFolded(); break; }
    }
    if (supplier.isEmpty()) {
        for (int col = 0; col < rows[headerRow].size(); ++col) {
            const QString h = rows[headerRow][col].toCaseFolded();
            if (!h.contains(QString::fromUtf8("магазин")) && !h.contains(QString::fromUtf8("постачальник"))) continue;
            for (int r = headerRow + 1; r < rows.size(); ++r) {
                supplier = rows[r].value(col).simplified().toCaseFolded();
                if (!supplier.isEmpty()) break;
            }
        }
    }
    QString series = documentNumber.toUpper();
    series.replace(QRegularExpression("[0-9]+"), "#");
    if (!series.contains(QRegularExpression("[\\p{L}]"))) series.clear();
    if (supplier.isEmpty() && series.isEmpty()) return {};
    QStringList headers;
    for (const auto& cell : rows[headerRow]) headers.append(cell.simplified().toCaseFolded());
    const QByteArray identity = (supplier + '\n' + series + '\n' + headers.join('\t')).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QList<InvoiceRecognitionProfile> InvoiceRecognitionProfiles::list() {
    QList<InvoiceRecognitionProfile> result;
    const auto root = readRoot();
    for (auto it = root.begin(); it != root.end(); ++it) result.append(decode(it.key(), it.value().toObject()));
    return result;
}
bool InvoiceRecognitionProfiles::load(const QString& key, InvoiceColumnMappingPreview* mapping) {
    const auto root = readRoot();
    if (key.isEmpty() || !root.contains(key)) return false;
    auto saved = decode(key, root[key].toObject()).mapping;
    if (!saved.priceWithVatConfirmed || saved.supplierDisplayName.isEmpty()) return false;
    saved.templateApplied = true;
    if (mapping) *mapping = saved;
    return true;
}
bool InvoiceRecognitionProfiles::save(const QList<QStringList>& rows, const InvoiceColumnMappingPreview& mapping, QString* error) {
    if (mapping.templateKey.isEmpty()) return true;
    if (mapping.supplierDisplayName.trimmed().isEmpty() || !mapping.priceWithVatConfirmed) {
        if (error) *error = QString::fromUtf8("Вкажіть магазин і підтвердьте колонки накладної.");
        return false;
    }
    QLockFile lock(path() + ".lock");
    if (!lock.tryLock(1000)) {
        if (error) *error = QString::fromUtf8("Шаблони зараз змінюються. Повторіть збереження.");
        return false;
    }
    auto root = readRoot();
    QJsonObject obj;
    obj["supplier"] = mapping.supplierDisplayName.trimmed();
    obj["headerRow"] = mapping.headerRow;
    obj["confirmed"] = true;
    obj["splitDescription"] = mapping.splitCombinedDescription;
    obj["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonArray indices, sample;
    for (Column c : columns) indices.append(mapping.*c);
    for (const auto& row : rows.mid(0, qMax(12, mapping.headerRow + 11))) sample.append(QJsonArray::fromStringList(row));
    obj["columns"] = indices;
    obj["sampleRows"] = sample;
    root[mapping.templateKey] = obj;
    return writeRoot(root, error);
}
bool InvoiceRecognitionProfiles::remove(const QString& key, QString* error) {
    QLockFile lock(path() + ".lock");
    if (!lock.tryLock(1000)) {
        if (error) *error = QString::fromUtf8("Шаблони зараз змінюються. Повторіть дію.");
        return false;
    }
    auto root = readRoot();
    root.remove(key);
    return writeRoot(root, error);
}
bool InvoiceRecognitionProfiles::validateRows(const QList<QStringList>& rows, const InvoiceColumnMappingPreview& m,
                                              const ParseLooseNumberFn& parseNumber, QString* error) {
    QSet<int> used;
    for (Column c : columns) {
        const int index = m.*c;
        if (index < 0) continue;
        if (used.contains(index)) {
            if (error) *error = QString::fromUtf8("Одну колонку призначено кільком полям.");
            return false;
        }
        used.insert(index);
    }
    if (m.partNameColumn < 0 || m.quantityColumn < 0 || m.priceWithVatColumn < 0) {
        if (error) *error = QString::fromUtf8("Виберіть колонки назви запчастини, кількості та ціни з ПДВ.");
        return false;
    }
    int count = 0;
    for (int i = qMax(0, m.headerRow + 1); i < rows.size(); ++i) {
        const auto& r = rows[i];
        const QString name = r.value(m.partNameColumn).trimmed();
        if (name.isEmpty() || name.startsWith(QString::fromUtf8("Всього"), Qt::CaseInsensitive)
            || name.startsWith(QString::fromUtf8("Разом"), Qt::CaseInsensitive)) continue;
        const QString qty = r.value(m.quantityColumn).trimmed(), price = r.value(m.priceWithVatColumn).trimmed();
        if (qty.isEmpty() && price.isEmpty()) continue;
        bool qok, pok, aok = true;
        double q = parseNumber(qty, &qok), p = parseNumber(price, &pok);
        double amount = m.totalWithVatColumn >= 0 ? parseNumber(r.value(m.totalWithVatColumn), &aok) : q * p;
        if (!qok || !pok || !aok || !std::isfinite(q) || !std::isfinite(p) || !std::isfinite(amount)
            || q <= 0 || p < 0 || qAbs(q * p - amount) > qMax(0.02, q * 0.005)) {
            if (error) *error = QString::fromUtf8("Перевірте колонки та суму в рядку %1. Автоматичний імпорт зупинено.").arg(i + 1);
            return false;
        }
        ++count;
    }
    if (!count && error) *error = QString::fromUtf8("За цими колонками не знайдено товарних рядків.");
    return count > 0;
}

void InvoiceRecognitionProfiles::splitDescription(const QString& raw, ImportedPartDraft* draft) {
    if (!draft) return;
    const QString text = raw.simplified();
    // An explicit brand followed by a catalogue code; keep spaced codes intact.
    static const QRegularExpression known(QStringLiteral(
        R"(^(.+?)\s+(ELRING|ERLING|VAG|BOSCH|KNECHT|STARLINE|MANN(?:-FILTER)?|FEBI(?: BILSTEIN)?|LEMFORDER|SACHS|TRW|SKF|INA|FAG|MAHLE|NGK|DENSO|GATES|DAYCO|CONTITECH|MEYLE|ATE|BREMBO|VALEO|DELPHI|SNR|SWAG|TOPRAN|VICTOR REINZ|REINZ)\s+([A-Z0-9][A-Z0-9 ./_-]*\d[A-Z0-9 ./_-]*|\d[A-Z0-9 ./_-]*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = known.match(text);
    if (!match.hasMatch()) {
        static const QRegularExpression unknown(QStringLiteral(R"(^(.+[\p{Cyrillic}].*)\s+([A-Z][A-Z&-]{1,23})\s+([A-Z0-9./_-]*\d[A-Z0-9./_-]*)$)"));
        match = unknown.match(text);
    }
    if (!match.hasMatch()) return;
    const QString brand = match.captured(2).toUpper(), code = match.captured(3).trimmed().toUpper();
    if ((!draft->brandName.isEmpty() && draft->brandName.compare(brand, Qt::CaseInsensitive) != 0)
        || (!draft->oemNumber.isEmpty() && draft->oemNumber.compare(code, Qt::CaseInsensitive) != 0)) return;
    draft->name = match.captured(1).trimmed();
    if (draft->brandName.isEmpty()) draft->brandName = brand;
    if (draft->oemNumber.isEmpty()) draft->oemNumber = code;
}
}
