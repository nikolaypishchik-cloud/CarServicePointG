// Extracted from src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

    QString sqlText;
    QString bindSnapshot;
    QString nativeErrorCode;
    qlonglong lastInsertId = 0;
};

struct CatalogPreflightDecision
{
    bool ok = false;
    QString reasonCode;
    QString reasonText;
    QString candidateClass;
};


using CatalogImportTextUtils::foldedCatalogValue;
using CatalogImportTextUtils::normalizedCatalogLookupKey;
using CatalogImportTextUtils::isCatalogPlaceholderCode;
using CatalogImportTextUtils::normalizedCatalogPartCode;
using CatalogImportTextUtils::normalizedCatalogStoreKey;
using CatalogImportTextUtils::normalizedCatalogBrandKey;
using CatalogImportTextUtils::isCatalogMeaningfulNameOnly;


static QString normalizedLooseIsoDate(QString value)
{
    value = nonNullText(value).trimmed();
    if (value.isEmpty()) return QStringLiteral("");
    const QString raw10 = value.left(10);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("dd/MM/yyyy")
    };
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(raw10, fmt);
        if (d.isValid()) return d.toString(QStringLiteral("yyyy-MM-dd"));
    }
    const QDate iso = DateFormats::parseDate(raw10);
    if (iso.isValid()) return iso.toString(QStringLiteral("yyyy-MM-dd"));
    return QStringLiteral("");
}

static QString bestCatalogSeenDate(const QString& orderedAt, const QString& fallbackA, const QString& fallbackB)
{
    const QString a = normalizedLooseIsoDate(orderedAt);
    if (!a.isEmpty()) return a;
    const QString b = normalizedLooseIsoDate(fallbackA);
    if (!b.isEmpty()) return b;
    return nonNullText(normalizedLooseIsoDate(fallbackB));
}

static bool looksTechnicalCatalogGroupKey(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) return false;
    if (value.contains(QLatin1Char(' '))) return false;
    static const QRegularExpression techRe(QStringLiteral(R"(^[A-Z0-9_\-]{3,}$)"));
    return techRe.match(value).hasMatch();
}

static QString mergeUniqueCatalogText(const QString& left, const QString& right)
{
    const QString a = nonNullText(left).trimmed();
    const QString b = nonNullText(right).trimmed();
    if (a.isEmpty()) return b.isEmpty() ? QStringLiteral("") : b;
    if (b.isEmpty()) return a;
    if (foldedCatalogValue(a).contains(foldedCatalogValue(b))) return a;
    if (foldedCatalogValue(b).contains(foldedCatalogValue(a))) return b;
    return a + QStringLiteral("\n") + b;
}

static QString inferCatalogGroupFromText(const QString& name, const QString& note)
{
    const QString hay = foldedCatalogValue(nonNullText(name) + QStringLiteral(" ") + nonNullText(note));
    if (hay.isEmpty()) return QStringLiteral("");
    auto hasAny = [&](std::initializer_list<const char*> terms) {
        for (const char* term : terms) {
            if (hay.contains(QString::fromUtf8(term))) return true;
        }
        return false;
    };
    if (hasAny({"фільтр", "filter", "oil filter", "air filter", "cabin filter", "паливн"})) return QString::fromUtf8("Фільтри");
    if (hasAny({"гальм", "brake", "колод", "диск гальм", "супорт"})) return QString::fromUtf8("Гальмівна система");
    if (hasAny({"аморт", "стійк", "сайлент", "пружин", "подвес", "suspension"})) return QString::fromUtf8("Підвіска");
    if (hasAny({"ремінь грм", "ролик грм", "timing", "грм"})) return QString::fromUtf8("ГРМ");
    if (hasAny({"охолодж", "радіатор", "термостат", "помпа", "coolant"})) return QString::fromUtf8("Система охолодження");
    if (hasAny({"свіч", "coil", "катушка", "запал"})) return QString::fromUtf8("Запалювання");
    if (hasAny({"руль", "рейк", "тяг", "наконечник"})) return QString::fromUtf8("Рульове керування");
    if (hasAny({"зчеплен", "clutch"})) return QString::fromUtf8("Зчеплення");
    if (hasAny({"акпп", "мкпп", "коробк", "gearbox", "transmission"})) return QString::fromUtf8("Трансмісія");
    if (hasAny({"генератор", "стартер", "датчик", "лампа", "электр", "електр"})) return QString::fromUtf8("Електрика");
    if (hasAny({"кондиціон", "клімат", "ac ", "compressor"})) return QString::fromUtf8("Кондиціонер / клімат");
    if (hasAny({"вихлоп", "глушник", "каталіз", "exhaust"})) return QString::fromUtf8("Вихлопна система");
    if (hasAny({"двиг", "engine", "прокладка гбц", "сальник коленвала", "масляний насос"})) return QString::fromUtf8("Двигун");
    return QStringLiteral("");
}

static bool splitCatalogBrandModelFallback(const QString& rawCar, QString* brandOut, QString* modelOut)
{
    const QString cleaned = rawCar.trimmed().simplified();
    if (cleaned.isEmpty()) return false;
    const QStringList parts = cleaned.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    if (brandOut) *brandOut = parts.first();
    if (modelOut) *modelOut = parts.size() > 1 ? parts.mid(1).join(QStringLiteral(" ")) : QString();
    return true;
}

static void sanitizeCatalogBrandModel(const QString& rawCar, QString* brandInOut, QString* modelInOut)
{
    QString brand = brandInOut ? brandInOut->trimmed().simplified() : QString();
    QString model = modelInOut ? modelInOut->trimmed().simplified() : QString();

    if ((brand.isEmpty() || model.isEmpty()) && !rawCar.trimmed().isEmpty()) {
        QString fallbackBrand;
        QString fallbackModel;
        if (splitCatalogBrandModelFallback(rawCar, &fallbackBrand, &fallbackModel)) {
            if (brand.isEmpty()) brand = fallbackBrand.trimmed().simplified();
            if (model.isEmpty()) model = fallbackModel.trimmed().simplified();
        }
    }

    if (brandInOut) *brandInOut = brand;
    if (modelInOut) *modelInOut = model;
}


static QString preferredCatalogText(const QString& current, const QString& candidate)
{
    const QString safeCurrent = nonNullText(current).trimmed();
    const QString safeCandidate = nonNullText(candidate).trimmed();
    if (safeCurrent.isEmpty()) return safeCandidate;
    if (safeCandidate.isEmpty()) return safeCurrent;
    return safeCandidate.size() > safeCurrent.size() ? safeCandidate : safeCurrent;
}

static QString businessGroupFromLegacyFields(const QString& currentGroup, const QString& legacyKind, const QString& legacyGroupKey, const QString& itemName, const QString& note)
{
    const QString safeCurrentGroup = nonNullText(currentGroup).trimmed();
    if (!safeCurrentGroup.isEmpty()) return safeCurrentGroup;
    const QString kind = nonNullText(legacyKind).trimmed();
    if (!kind.isEmpty() && !looksTechnicalCatalogGroupKey(kind)) return kind;
    const QString groupKey = nonNullText(legacyGroupKey).trimmed();
    if (!groupKey.isEmpty() && !looksTechnicalCatalogGroupKey(groupKey)) return groupKey;
    return nonNullText(inferCatalogGroupFromText(itemName, note)).trimmed();
}

static QString catalogImportPriceBucket(double price)
{
    if (price <= 0.0) return QStringLiteral("0");
    return QString::number(qRound64(price * 100.0));
}

static QString buildCatalogMatchKey(const CatalogSourceWorkRow& row, QString* dedupeModeOut)
{
    if (!row.normalizedOemNumber.isEmpty()) {
        if (dedupeModeOut) *dedupeModeOut = QStringLiteral("normalized_oem_number");
        return QStringLiteral("oem:") + row.normalizedOemNumber;
    }
    if (!row.normalizedPartNumber.isEmpty()) {
        if (dedupeModeOut) *dedupeModeOut = QStringLiteral("normalized_part_number");
        return QStringLiteral("part:") + row.normalizedPartNumber;
    }
    if (!row.normalizedName.isEmpty() && !normalizedCatalogLookupKey(row.brandName).isEmpty()) {
        if (dedupeModeOut) *dedupeModeOut = QStringLiteral("brand_plus_normalized_name");
        return QStringLiteral("brand_name:") + normalizedCatalogLookupKey(row.brandName) + QStringLiteral("|") + row.normalizedName;
    }
    if (!row.normalizedName.isEmpty()) {
        if (dedupeModeOut) *dedupeModeOut = QStringLiteral("normalized_name_vehicle_store_price");
        return QStringLiteral("name_vehicle_store:")
            + row.normalizedName
            + QStringLiteral("|") + normalizedCatalogLookupKey(row.vehicleBrand)
            + QStringLiteral("|") + normalizedCatalogLookupKey(row.vehicleModel)
            + QStringLiteral("|") + row.normalizedStore
            + QStringLiteral("|") + catalogImportPriceBucket(row.price);
    }
    if (dedupeModeOut) *dedupeModeOut = QStringLiteral("unresolved");
    return QString();
}

static QString buildCatalogSourceSignature(const CatalogSourceWorkRow& row)
{
    const QString raw = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
        .arg(row.sourceDbLayer,
             QString::number(row.sourcePartId),
             row.orderUid,
             QString::number(row.orderNumber),
             row.normalizedPartNumber,
