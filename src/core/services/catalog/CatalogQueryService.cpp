#include "core/services/catalog/CatalogQueryService.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/util/VehicleNameUtils.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

namespace {
bool execSelect(QSqlQuery& q, const QString& sql, const QList<QVariant>& args = {})
{
    q.clear();
    if (!q.prepare(sql)) return false;
    for (int i = 0; i < args.size(); ++i) q.bindValue(i, args[i]);
    return q.exec();
}

QString normalizeSpaces(QString value)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    value = value.trimmed().toCaseFolded();
    value.replace(whitespace, QStringLiteral(" "));
    return value;
}

QString normalizeBrandFilter(QString value)
{
    value = normalizeSpaces(value);
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('.'));
    return value;
}

QString normalizePartSearch(QString value)
{
    static const QRegularExpression punctuation(QStringLiteral(R"([^\p{L}\p{N}])"));
    value = normalizeSpaces(value);
    value.remove(punctuation);
    return value;
}

QString optionKeyForValue(const QString& value, bool brandValue)
{
    return brandValue ? normalizeBrandFilter(value) : normalizeSpaces(value);
}

QString preferredOptionDisplay(const QString& current, const QString& candidate, bool brandValue)
{
    if (current.trimmed().isEmpty()) return candidate.trimmed().simplified();
    if (candidate.trimmed().isEmpty()) return current.trimmed().simplified();
    const QString currentDisplay = current.trimmed().simplified();
    const QString candidateDisplay = candidate.trimmed().simplified();
    if (!brandValue) return currentDisplay.size() >= candidateDisplay.size() ? currentDisplay : candidateDisplay;
    const bool currentAllUpper = currentDisplay == currentDisplay.toUpper();
    const bool candidateAllUpper = candidateDisplay == candidateDisplay.toUpper();
    if (candidateAllUpper != currentAllUpper) return candidateAllUpper ? candidateDisplay : currentDisplay;
    return currentDisplay.size() >= candidateDisplay.size() ? currentDisplay : candidateDisplay;
}

void collectYears(QSet<int>& target, const CatalogRowData& row)
{
    int from = row.yearFrom;
    int to = row.yearTo;
    if (from <= 0 && to <= 0) return;
    if (from <= 0) from = to;
    if (to <= 0) to = from;
    if (from > to) std::swap(from, to);
    if (to - from <= 40) {
        for (int year = from; year <= to; ++year) target.insert(year);
    } else {
        target.insert(from);
        target.insert(to);
    }
}

QStringList selectDistinctValues(const QString& sql, const QList<QVariant>& args = {})
{
    QStringList values;
    QSqlQuery q(Database::db());
    if (!execSelect(q, sql, args)) return values;
    while (q.next()) {
        const QString value = q.value(0).toString().trimmed();
        if (!value.isEmpty()) values << value;
    }
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) {
        return a.toLower() < b.toLower();
    });
    return values;
}

QStringList curatedModelsForBrandKey(const QString& localKey)
{
        if (localKey == QStringLiteral("audi")) return {
            QStringLiteral("80"), QStringLiteral("100"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"),
            QStringLiteral("A4"), QStringLiteral("A5"), QStringLiteral("A6"), QStringLiteral("A7"), QStringLiteral("A8"),
            QStringLiteral("Allroad"), QStringLiteral("Q2"), QStringLiteral("Q3"), QStringLiteral("Q5"), QStringLiteral("Q7"),
            QStringLiteral("Q8"), QStringLiteral("TT")
        };
        if (localKey == QStringLiteral("bmw")) return {
            QStringLiteral("1 Series"), QStringLiteral("2 Series"), QStringLiteral("3 Series"), QStringLiteral("4 Series"),
            QStringLiteral("5 Series"), QStringLiteral("6 Series"), QStringLiteral("7 Series"), QStringLiteral("X1"),
            QStringLiteral("X3"), QStringLiteral("X5"), QStringLiteral("X6"), QStringLiteral("X7")
        };
        if (localKey == QStringLiteral("byd")) return {
            QStringLiteral("Atto 3"), QStringLiteral("Dolphin"), QStringLiteral("Han"), QStringLiteral("Qin"),
            QStringLiteral("Seal"), QStringLiteral("Song"), QStringLiteral("Tang"), QStringLiteral("Yuan")
        };
        if (localKey == QStringLiteral("chery")) return {
            QStringLiteral("Amulet"), QStringLiteral("Arrizo 5"), QStringLiteral("Arrizo 8"), QStringLiteral("Eastar"),
            QStringLiteral("Elara"), QStringLiteral("QQ"), QStringLiteral("Tiggo 2"), QStringLiteral("Tiggo 4"),
            QStringLiteral("Tiggo 7"), QStringLiteral("Tiggo 8")
        };
        if (localKey == QStringLiteral("chevrolet")) return {
            QStringLiteral("Aveo"), QStringLiteral("Captiva"), QStringLiteral("Cruze"), QStringLiteral("Epica"),
            QStringLiteral("Lacetti"), QStringLiteral("Malibu"), QStringLiteral("Niva"), QStringLiteral("Orlando"),
            QStringLiteral("Spark"), QStringLiteral("Tacuma")
        };
        if (localKey == QStringLiteral("chrysler")) return {
            QStringLiteral("200"), QStringLiteral("300C"), QStringLiteral("Pacifica"), QStringLiteral("PT Cruiser"),
            QStringLiteral("Sebring"), QStringLiteral("Town & Country"), QStringLiteral("Voyager")
        };
        if (localKey == QStringLiteral("citroen")) return {
            QStringLiteral("Berlingo"), QStringLiteral("C-Elysee"), QStringLiteral("C1"), QStringLiteral("C3"),
            QStringLiteral("C4"), QStringLiteral("C4 Picasso"), QStringLiteral("C5"), QStringLiteral("C5 Aircross"),
            QStringLiteral("Jumper"), QStringLiteral("Jumpy")
        };
        if (localKey == QStringLiteral("dacia")) return {
            QStringLiteral("Dokker"), QStringLiteral("Duster"), QStringLiteral("Lodgy"), QStringLiteral("Logan"),
            QStringLiteral("Sandero"), QStringLiteral("Solenza"), QStringLiteral("Spring")
        };
        if (localKey == QStringLiteral("daewoo")) return {
            QStringLiteral("Espero"), QStringLiteral("Gentra"), QStringLiteral("Lanos"), QStringLiteral("Leganza"),
            QStringLiteral("Matiz"), QStringLiteral("Nexia"), QStringLiteral("Nubira"), QStringLiteral("Sens")
        };
        if (localKey == QStringLiteral("dodge")) return {
            QStringLiteral("Avenger"), QStringLiteral("Caliber"), QStringLiteral("Challenger"), QStringLiteral("Charger"),
            QStringLiteral("Durango"), QStringLiteral("Journey"), QStringLiteral("Nitro"), QStringLiteral("RAM")
        };
        if (localKey == QStringLiteral("fiat")) return {
            QStringLiteral("500"), QStringLiteral("Albea"), QStringLiteral("Bravo"), QStringLiteral("Doblo"),
            QStringLiteral("Ducato"), QStringLiteral("Fiorino"), QStringLiteral("Linea"), QStringLiteral("Panda"),
            QStringLiteral("Punto"), QStringLiteral("Scudo")
        };
        if (localKey == QStringLiteral("ford")) return {
            QStringLiteral("B-Max"), QStringLiteral("C-Max"), QStringLiteral("Escort"), QStringLiteral("Fiesta"),
            QStringLiteral("Focus"), QStringLiteral("Fusion"), QStringLiteral("Galaxy"), QStringLiteral("Kuga"),
            QStringLiteral("Mondeo"), QStringLiteral("Ranger"), QStringLiteral("S-Max"), QStringLiteral("Transit")
        };
        if (localKey == QStringLiteral("geely")) return {
            QStringLiteral("Atlas"), QStringLiteral("CK"), QStringLiteral("Coolray"), QStringLiteral("Emgrand"),
            QStringLiteral("GC6"), QStringLiteral("MK"), QStringLiteral("Monjaro"), QStringLiteral("Tugella")
        };
        if (localKey == QStringLiteral("haval")) return {
            QStringLiteral("Dargo"), QStringLiteral("F7"), QStringLiteral("F7x"), QStringLiteral("H2"),
            QStringLiteral("H5"), QStringLiteral("H6"), QStringLiteral("Jolion"), QStringLiteral("M6")
        };
        if (localKey == QStringLiteral("honda")) return {
            QStringLiteral("Accord"), QStringLiteral("Civic"), QStringLiteral("CR-V"), QStringLiteral("HR-V"),
            QStringLiteral("Jazz"), QStringLiteral("Legend"), QStringLiteral("Pilot"), QStringLiteral("Ridgeline")
        };
        if (localKey == QStringLiteral("hyundai")) return {
            QStringLiteral("Accent"), QStringLiteral("Elantra"), QStringLiteral("Getz"), QStringLiteral("i30"),
            QStringLiteral("ix35"), QStringLiteral("Kona"), QStringLiteral("Santa Fe"), QStringLiteral("Sonata"),
            QStringLiteral("Tucson"), QStringLiteral("Veloster")
        };
        if (localKey == QStringLiteral("infiniti")) return {
            QStringLiteral("EX"), QStringLiteral("FX"), QStringLiteral("G25"), QStringLiteral("G35"),
            QStringLiteral("M35"), QStringLiteral("Q30"), QStringLiteral("Q50"), QStringLiteral("Q60"),
            QStringLiteral("Q70"), QStringLiteral("QX50"), QStringLiteral("QX60"), QStringLiteral("QX70")
        };
        if (localKey == QStringLiteral("jaguar")) return {
            QStringLiteral("E-Pace"), QStringLiteral("F-Pace"), QStringLiteral("F-Type"), QStringLiteral("S-Type"),
            QStringLiteral("XE"), QStringLiteral("XF"), QStringLiteral("XJ"), QStringLiteral("XK")
        };
        if (localKey == QStringLiteral("jeep")) return {
            QStringLiteral("Cherokee"), QStringLiteral("Commander"), QStringLiteral("Compass"), QStringLiteral("Grand Cherokee"),
            QStringLiteral("Liberty"), QStringLiteral("Patriot"), QStringLiteral("Renegade"), QStringLiteral("Wrangler")
        };
        if (localKey == QStringLiteral("kia")) return {
            QStringLiteral("Carens"), QStringLiteral("Ceed"), QStringLiteral("Cerato"), QStringLiteral("Magentis"),
            QStringLiteral("Mohave"), QStringLiteral("Optima"), QStringLiteral("Picanto"), QStringLiteral("Rio"),
            QStringLiteral("Sorento"), QStringLiteral("Soul"), QStringLiteral("Sportage"), QStringLiteral("Stonic")
        };
        if (localKey == QStringLiteral("landrover") || localKey == QStringLiteral("landrover")) return {
            QStringLiteral("Defender"), QStringLiteral("Discovery"), QStringLiteral("Discovery Sport"),
            QStringLiteral("Freelander"), QStringLiteral("Range Rover"), QStringLiteral("Range Rover Evoque"),
            QStringLiteral("Range Rover Sport"), QStringLiteral("Range Rover Velar")
        };
        if (localKey == QStringLiteral("lexus")) return {
            QStringLiteral("CT"), QStringLiteral("ES"), QStringLiteral("GS"), QStringLiteral("GX"),
            QStringLiteral("IS"), QStringLiteral("LS"), QStringLiteral("LX"), QStringLiteral("NX"),
            QStringLiteral("RX"), QStringLiteral("UX")
        };
        if (localKey == QStringLiteral("mazda")) return {
            QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("5"), QStringLiteral("6"),
            QStringLiteral("CX-3"), QStringLiteral("CX-5"), QStringLiteral("CX-7"), QStringLiteral("CX-9"),
            QStringLiteral("MPV"), QStringLiteral("MX-5")
        };
        if (localKey == QStringLiteral("mg")) return {
            QStringLiteral("MG 3"), QStringLiteral("MG 4"), QStringLiteral("MG 5"), QStringLiteral("MG 6"),
            QStringLiteral("MG HS"), QStringLiteral("MG ZS"), QStringLiteral("RX5")
        };
        if (localKey == QStringLiteral("mercedesbenz") || localKey == QStringLiteral("mercedes") || localKey == QStringLiteral("mb")) return {
            QStringLiteral("A-Class"), QStringLiteral("B-Class"), QStringLiteral("C-Class"), QStringLiteral("CLA"),
            QStringLiteral("CLK"), QStringLiteral("CLS"), QStringLiteral("E-Class"), QStringLiteral("G-Class"),
            QStringLiteral("GLA"), QStringLiteral("GLC"), QStringLiteral("GLE"), QStringLiteral("GLS"),
            QStringLiteral("ML"), QStringLiteral("S-Class"), QStringLiteral("Sprinter"), QStringLiteral("Vito")
        };
        if (localKey == QStringLiteral("mitsubishi")) return {
            QStringLiteral("ASX"), QStringLiteral("Colt"), QStringLiteral("Eclipse Cross"), QStringLiteral("Galant"),
            QStringLiteral("Grandis"), QStringLiteral("L200"), QStringLiteral("Lancer"), QStringLiteral("Outlander"),
            QStringLiteral("Pajero"), QStringLiteral("Space Star")
        };
        if (localKey == QStringLiteral("nissan")) return {
            QStringLiteral("Almera"), QStringLiteral("Juke"), QStringLiteral("Leaf"), QStringLiteral("Micra"),
            QStringLiteral("Murano"), QStringLiteral("Navara"), QStringLiteral("Note"), QStringLiteral("Pathfinder"),
            QStringLiteral("Primera"), QStringLiteral("Qashqai"), QStringLiteral("Rogue"), QStringLiteral("Tiida"),
            QStringLiteral("X-Trail")
        };
        if (localKey == QStringLiteral("opel")) return {
            QStringLiteral("Astra"), QStringLiteral("Combo"), QStringLiteral("Corsa"), QStringLiteral("Frontera"),
            QStringLiteral("Insignia"), QStringLiteral("Meriva"), QStringLiteral("Mokka"), QStringLiteral("Omega"),
            QStringLiteral("Signum"), QStringLiteral("Vectra"), QStringLiteral("Vivaro"), QStringLiteral("Zafira")
        };
        if (localKey == QStringLiteral("peugeot")) return {
            QStringLiteral("107"), QStringLiteral("206"), QStringLiteral("207"), QStringLiteral("208"),
            QStringLiteral("301"), QStringLiteral("307"), QStringLiteral("308"), QStringLiteral("407"),
            QStringLiteral("508"), QStringLiteral("2008"), QStringLiteral("3008"), QStringLiteral("4008"),
            QStringLiteral("5008"), QStringLiteral("Boxer"), QStringLiteral("Expert"), QStringLiteral("Partner")
        };
        if (localKey == QStringLiteral("porsche")) return {
            QStringLiteral("718 Boxster"), QStringLiteral("718 Cayman"), QStringLiteral("911"), QStringLiteral("Cayenne"),
            QStringLiteral("Macan"), QStringLiteral("Panamera"), QStringLiteral("Taycan")
        };
        if (localKey == QStringLiteral("renault")) return {
            QStringLiteral("Captur"), QStringLiteral("Clio"), QStringLiteral("Duster"), QStringLiteral("Fluence"),
            QStringLiteral("Kangoo"), QStringLiteral("Koleos"), QStringLiteral("Laguna"), QStringLiteral("Logan"),
            QStringLiteral("Master"), QStringLiteral("Megane"), QStringLiteral("Sandero"), QStringLiteral("Scenic"),
            QStringLiteral("Trafic")
        };
        if (localKey == QStringLiteral("skoda")) return {
            QStringLiteral("Fabia"), QStringLiteral("Favorit"), QStringLiteral("Felicia"), QStringLiteral("Kamiq"),
            QStringLiteral("Karoq"), QStringLiteral("Kodiaq"), QStringLiteral("Octavia"), QStringLiteral("Rapid"),
            QStringLiteral("Roomster"), QStringLiteral("Superb"), QStringLiteral("Yeti")
        };
        if (localKey == QStringLiteral("ssangyong")) return {
            QStringLiteral("Actyon"), QStringLiteral("Korando"), QStringLiteral("Kyron"), QStringLiteral("Musso"),
            QStringLiteral("Rexton"), QStringLiteral("Tivoli"), QStringLiteral("Torres")
        };
        if (localKey == QStringLiteral("subaru")) return {
            QStringLiteral("Forester"), QStringLiteral("Impreza"), QStringLiteral("Legacy"), QStringLiteral("Levorg"),
            QStringLiteral("Outback"), QStringLiteral("Tribeca"), QStringLiteral("WRX"), QStringLiteral("XV")
        };
        if (localKey == QStringLiteral("suzuki")) return {
            QStringLiteral("Baleno"), QStringLiteral("Grand Vitara"), QStringLiteral("Ignis"), QStringLiteral("Jimny"),
            QStringLiteral("Liana"), QStringLiteral("S-Cross"), QStringLiteral("Swift"), QStringLiteral("SX4"),
            QStringLiteral("Vitara")
        };
        if (localKey == QStringLiteral("tesla")) return {
            QStringLiteral("Model 3"), QStringLiteral("Model S"), QStringLiteral("Model X"), QStringLiteral("Model Y"),
            QStringLiteral("Roadster"), QStringLiteral("Cybertruck")
        };
        if (localKey == QStringLiteral("toyota")) return {
            QStringLiteral("Auris"), QStringLiteral("Avensis"), QStringLiteral("Camry"), QStringLiteral("Corolla"),
            QStringLiteral("Highlander"), QStringLiteral("Land Cruiser"), QStringLiteral("Prius"), QStringLiteral("RAV4"),
            QStringLiteral("Verso"), QStringLiteral("Yaris")
        };
        if (localKey == QStringLiteral("volkswagen") || localKey == QStringLiteral("vw")) return {
            QStringLiteral("Amarok"), QStringLiteral("Bora"), QStringLiteral("Caddy"), QStringLiteral("Crafter"),
            QStringLiteral("Golf"), QStringLiteral("Jetta"), QStringLiteral("Multivan"), QStringLiteral("Passat"),
            QStringLiteral("Polo"), QStringLiteral("Sharan"), QStringLiteral("T-Cross"), QStringLiteral("Tiguan"),
            QStringLiteral("Touareg"), QStringLiteral("Touran"), QStringLiteral("Transporter")
        };
        if (localKey == QStringLiteral("volvo")) return {
            QStringLiteral("S40"), QStringLiteral("S60"), QStringLiteral("S80"), QStringLiteral("S90"),
            QStringLiteral("V40"), QStringLiteral("V50"), QStringLiteral("V60"), QStringLiteral("V70"),
            QStringLiteral("XC60"), QStringLiteral("XC70"), QStringLiteral("XC90")
        };
        return {};
}
}

bool CatalogQueryService::ensureSchema(QString* errorText)
{
    return Database::ensureCatalogImportSchema(errorText);
}

QString CatalogQueryService::foldedFilterValue(QString value)
{
    return normalizeSpaces(std::move(value));
}

QString CatalogQueryService::normalizedBrandFilterValue(QString value)
{
    return normalizeBrandFilter(std::move(value));
}

QString CatalogQueryService::normalizedPartSearchValue(QString value)
{
    return normalizePartSearch(std::move(value));
}

QStringList CatalogQueryService::activeGroupOptions()
{
    return selectDistinctValues(QStringLiteral("SELECT DISTINCT group_name FROM parts_catalog WHERE COALESCE(is_active,1)=1 ORDER BY group_name COLLATE NOCASE"));
}

QStringList CatalogQueryService::modelOptionsForBrand(const QString& brand)
{
    const QString key = normalizeBrandFilter(brand);
    if (key.isEmpty() || key == QStringLiteral("усі")) {
        QStringList all;
        const QStringList keys = {
            QStringLiteral("audi"), QStringLiteral("bmw"), QStringLiteral("byd"), QStringLiteral("chery"), QStringLiteral("chevrolet"),
            QStringLiteral("chrysler"), QStringLiteral("citroen"), QStringLiteral("dacia"), QStringLiteral("daewoo"), QStringLiteral("dodge"),
            QStringLiteral("fiat"), QStringLiteral("ford"), QStringLiteral("geely"), QStringLiteral("haval"), QStringLiteral("honda"),
            QStringLiteral("hyundai"), QStringLiteral("infiniti"), QStringLiteral("jaguar"), QStringLiteral("jeep"), QStringLiteral("kia"),
            QStringLiteral("landrover"), QStringLiteral("lexus"), QStringLiteral("mazda"), QStringLiteral("mg"),
            QStringLiteral("mercedesbenz"), QStringLiteral("mitsubishi"), QStringLiteral("nissan"), QStringLiteral("opel"),
            QStringLiteral("peugeot"), QStringLiteral("porsche"), QStringLiteral("renault"), QStringLiteral("skoda"),
            QStringLiteral("ssangyong"), QStringLiteral("subaru"), QStringLiteral("suzuki"), QStringLiteral("tesla"),
            QStringLiteral("toyota"), QStringLiteral("volkswagen"), QStringLiteral("volvo")
        };
        for (const QString& entry : keys) all.append(curatedModelsForBrandKey(entry));
        all.append(selectDistinctValues(QStringLiteral("SELECT DISTINCT vehicle_model FROM parts_catalog WHERE COALESCE(is_active,1)=1 ORDER BY vehicle_model COLLATE NOCASE")));
        all.removeDuplicates();
        std::sort(all.begin(), all.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
        return VehicleNameUtils::uniqueModelOptions(all);
    }

    QStringList values = curatedModelsForBrandKey(key);
    values.append(selectDistinctValues(
        QStringLiteral("SELECT DISTINCT vehicle_model FROM parts_catalog WHERE COALESCE(is_active,1)=1 AND TRIM(COALESCE(vehicle_brand,'')) = ? ORDER BY vehicle_model COLLATE NOCASE"),
        {brand.trimmed()}));
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    return VehicleNameUtils::uniqueModelOptions(values);
}

QString CatalogQueryService::displayYearsRange(int yearFrom, int yearTo)
{
    if (yearFrom <= 0 && yearTo <= 0) return QString();
    if (yearFrom > 0 && yearTo > 0 && yearFrom == yearTo) return QString::number(yearFrom);
    if (yearFrom <= 0) return QString::fromUtf8("до ") + QString::number(yearTo);
    if (yearTo <= 0) return QString::number(yearFrom) + QString::fromUtf8("+");
    return QString::number(yearFrom) + QStringLiteral("-") + QString::number(yearTo);
}

void CatalogQueryService::ensureDerived(CatalogRowData& row)
{
    row.effectiveBrand = row.brand.trimmed().simplified();
    row.effectiveModel = VehicleNameUtils::uniqueModelOptions({row.model}).value(0);
    row.brandRecognized = !row.effectiveBrand.isEmpty();
    row.brandKey = normalizeBrandFilter(row.effectiveBrand);
    row.modelKey = normalizeSpaces(row.effectiveModel);
    row.fuelKey = normalizeSpaces(row.fuel);
    row.groupKey = normalizeSpaces(row.group);
    row.oemSearchKey = normalizePartSearch(row.oem);
    row.effectiveYearFrom = row.yearFrom;
    row.effectiveYearTo = row.yearTo;
    if (row.effectiveYearFrom <= 0) row.effectiveYearFrom = row.effectiveYearTo;
    if (row.effectiveYearTo <= 0) row.effectiveYearTo = row.effectiveYearFrom;
    if (row.effectiveYearFrom > row.effectiveYearTo) std::swap(row.effectiveYearFrom, row.effectiveYearTo);

    QStringList haystackParts = {
        normalizeSpaces(row.effectiveBrand),
        normalizeSpaces(row.effectiveModel),
        normalizeSpaces(row.fuel),
        normalizeSpaces(row.group),
        normalizeSpaces(row.oem),
        normalizeSpaces(row.name),
        normalizeSpaces(row.brandName),
        normalizeSpaces(row.supplier),
        normalizeSpaces(row.note),
        normalizeSpaces(displayYearsRange(row.yearFrom, row.yearTo))
    };
    haystackParts.erase(std::remove_if(haystackParts.begin(), haystackParts.end(), [](const QString& s) { return s.isEmpty(); }), haystackParts.end());
    row.searchHaystack = haystackParts.join(QLatin1Char(' '));
}

QVector<CatalogRowData> CatalogQueryService::loadAllRows(QString* errorText)
{
    QVector<CatalogRowData> rows;
    QSqlQuery q(Database::db());
    const QString sql =
        QStringLiteral(
            "SELECT id, vehicle_brand, vehicle_model, year_from, year_to, fuel_type, group_name, "
            "COALESCE(NULLIF(part_number,''), NULLIF(oem_number,''), '') AS catalog_oem, "
            "name, brand, supplier, price, note "
            "FROM parts_catalog WHERE COALESCE(is_active,1)=1 "
            "ORDER BY vehicle_brand COLLATE NOCASE, vehicle_model COLLATE NOCASE, group_name COLLATE NOCASE, name COLLATE NOCASE");
    // Startup prepares the schema. Only repair a missing/old schema on failure;
    // ordinary reads must not run migrations and their backup hooks again.
    if (!execSelect(q, sql) && (!ensureSchema(errorText) || !execSelect(q, sql))) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати каталог.\n") + q.lastError().text();
        return rows;
    }

    while (q.next()) {
        CatalogRowData r;
        r.id = q.value(0).toInt();
        r.brand = q.value(1).toString();
        r.model = q.value(2).toString();
        r.yearFrom = q.value(3).toInt();
        r.yearTo = q.value(4).toInt();
        r.fuel = q.value(5).toString();
        r.group = q.value(6).toString();
        r.oem = q.value(7).toString();
        r.name = q.value(8).toString();
        r.brandName = q.value(9).toString();
        r.supplier = StoreNameNormalizer::canonicalDisplayName(q.value(10).toString());
        r.price = q.value(11).toDouble();
        r.note = q.value(12).toString();
        ensureDerived(r);
        rows.push_back(r);
    }
    return rows;
}

CatalogFilterOptions CatalogQueryService::buildFilterOptions(const QVector<CatalogRowData>& allRows,
                                                             const CatalogFilterState& state)
{
    CatalogFilterOptions out;
    const QString allText = QString::fromUtf8("Усі");
    const QString brandKey = (state.brand.isEmpty() || state.brand == allText) ? QString() : normalizeBrandFilter(state.brand);
    const QString modelKey = (state.model.isEmpty() || state.model == allText) ? QString() : normalizeSpaces(state.model);
    const QString fuelKey = (state.fuel.isEmpty() || state.fuel == allText) ? QString() : normalizeSpaces(state.fuel);
    const QString groupKey = (state.group.isEmpty() || state.group == allText) ? QString() : normalizeSpaces(state.group);
    const QString searchKey = normalizeSpaces(state.search);
    const QString partSearchKey = normalizePartSearch(state.search);

    auto yearMatch = [&](const CatalogRowData& row, bool applyFrom, bool applyTo) {
        int from = state.yearFrom;
        int to = state.yearTo;
        if (!applyFrom) from = 0;
        if (!applyTo) to = 0;
        if (from > 0 && to > 0 && from > to) std::swap(from, to);
        if (from <= 0 && to <= 0) return true;
        int rowFrom = row.yearFrom;
        int rowTo = row.yearTo;
        if (rowFrom <= 0 && rowTo <= 0) return false;
        if (rowFrom <= 0) rowFrom = rowTo;
        if (rowTo <= 0) rowTo = rowFrom;
        if (rowFrom > rowTo) std::swap(rowFrom, rowTo);
        if (from > 0 && rowTo < from) return false;
        if (to > 0 && rowFrom > to) return false;
        return true;
    };

    auto searchMatch = [&](const CatalogRowData& row) {
        if (searchKey.isEmpty()) return true;
        if (row.searchHaystack.contains(searchKey)) return true;
        return !partSearchKey.isEmpty() && row.oemSearchKey.contains(partSearchKey);
    };

    auto matches = [&](const CatalogRowData& row, bool applyBrand, bool applyModel, bool applyFuel, bool applyGroup, bool applyYearFrom, bool applyYearTo, bool applySearch) {
        if (applyBrand && !brandKey.isEmpty()) {
            if (!row.brandRecognized || row.brandKey != brandKey) return false;
        }
        if (applyModel && !modelKey.isEmpty() && row.modelKey != modelKey) return false;
        if (applyFuel && !fuelKey.isEmpty() && row.fuelKey != fuelKey) return false;
        if (applyGroup && !groupKey.isEmpty() && row.groupKey != groupKey) return false;
        if (!yearMatch(row, applyYearFrom, applyYearTo)) return false;
        if (applySearch && !searchMatch(row)) return false;
        return true;
    };

    QMap<QString, QString> brandValues;
    QMap<QString, QString> modelValues;
    QMap<QString, QString> fuelValues;
    QMap<QString, QString> groupValues;
    QSet<int> yearsFrom;
    QSet<int> yearsTo;

    auto insertOption = [](QMap<QString, QString>& target, const QString& value, bool brandValue) {
        const QString display = value.trimmed().simplified();
        const QString key = optionKeyForValue(display, brandValue);
        if (display.isEmpty() || key.isEmpty()) return;
        target.insert(key, preferredOptionDisplay(target.value(key), display, brandValue));
    };

    for (const CatalogRowData& row : allRows) {
        if (matches(row, false, true, true, true, true, true, true) && row.brandRecognized && !row.effectiveBrand.isEmpty()) {
            insertOption(brandValues, row.effectiveBrand, true);
        }
        if (matches(row, true, false, true, true, true, true, true)) {
            const bool brandFits = brandKey.isEmpty() || (!row.brandKey.isEmpty() && row.brandKey == brandKey);
            if (brandFits && !row.effectiveModel.isEmpty()) insertOption(modelValues, row.effectiveModel, false);
        }
        if (matches(row, true, true, false, true, true, true, true) && !row.fuel.trimmed().isEmpty()) {
            insertOption(fuelValues, row.fuel.trimmed(), false);
        }
        if (matches(row, true, true, true, false, true, true, true) && !row.group.trimmed().isEmpty()) {
            insertOption(groupValues, row.group.trimmed(), false);
        }
        if (matches(row, true, true, true, true, false, true, true)) collectYears(yearsFrom, row);
        if (matches(row, true, true, true, true, true, false, true)) collectYears(yearsTo, row);
    }

    out.brands = brandValues.values();
    out.models = modelValues.values();
    out.fuels = fuelValues.values();
    out.groups = groupValues.values();
    std::sort(out.brands.begin(), out.brands.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(out.models.begin(), out.models.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(out.fuels.begin(), out.fuels.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(out.groups.begin(), out.groups.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    out.yearsFrom = yearsFrom.values();
    out.yearsTo = yearsTo.values();
    std::sort(out.yearsFrom.begin(), out.yearsFrom.end());
    std::sort(out.yearsTo.begin(), out.yearsTo.end());
    return out;
}

QVector<CatalogRowData> CatalogQueryService::filterRows(const QVector<CatalogRowData>& allRows,
                                                        const CatalogFilterState& state)
{
    QVector<CatalogRowData> rows;
    const QString allText = QString::fromUtf8("Усі");
    QString brandKey = (state.brand.isEmpty() || state.brand == allText) ? QString() : normalizeBrandFilter(state.brand);
    QString modelKey = (state.model.isEmpty() || state.model == allText) ? QString() : normalizeSpaces(state.model);
    QString fuelKey = (state.fuel.isEmpty() || state.fuel == allText) ? QString() : normalizeSpaces(state.fuel);
    QString groupKey = (state.group.isEmpty() || state.group == allText) ? QString() : normalizeSpaces(state.group);
    QString searchKey = normalizeSpaces(state.search);
    QString partSearchKey = normalizePartSearch(state.search);
    int currentYearFrom = state.yearFrom;
    int currentYearTo = state.yearTo;
    if (currentYearFrom > 0 && currentYearTo > 0 && currentYearFrom > currentYearTo) std::swap(currentYearFrom, currentYearTo);

    auto rowMatches = [&](const CatalogRowData& row) {
        if (!brandKey.isEmpty() && (!row.brandRecognized || row.brandKey != brandKey)) return false;
        if (!modelKey.isEmpty() && row.modelKey != modelKey) return false;
        if (!fuelKey.isEmpty() && row.fuelKey != fuelKey) return false;
        if (!groupKey.isEmpty() && row.groupKey != groupKey) return false;
        if (currentYearFrom > 0 || currentYearTo > 0) {
            if (row.effectiveYearFrom <= 0 && row.effectiveYearTo <= 0) return false;
            if (currentYearFrom > 0 && row.effectiveYearTo < currentYearFrom) return false;
            if (currentYearTo > 0 && row.effectiveYearFrom > currentYearTo) return false;
        }
        if (!searchKey.isEmpty() && !row.searchHaystack.contains(searchKey) && (partSearchKey.isEmpty() || !row.oemSearchKey.contains(partSearchKey))) {
            return false;
        }
        return true;
    };

    for (const CatalogRowData& rawRow : allRows) {
        if (!rowMatches(rawRow)) continue;
        CatalogRowData row = rawRow;
        if (!row.effectiveBrand.isEmpty()) row.brand = row.effectiveBrand;
        if (!row.effectiveModel.isEmpty()) row.model = row.effectiveModel;
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const CatalogRowData& a, const CatalogRowData& b) {
        const QStringList left = {normalizeSpaces(a.brand), normalizeSpaces(a.model), normalizeSpaces(a.group), normalizeSpaces(a.name), normalizeSpaces(a.oem)};
        const QStringList right = {normalizeSpaces(b.brand), normalizeSpaces(b.model), normalizeSpaces(b.group), normalizeSpaces(b.name), normalizeSpaces(b.oem)};
        return left < right;
    });

    return rows;
}
