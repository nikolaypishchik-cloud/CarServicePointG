#include "ui/tabs/catalog/CatalogPage.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "core/services/ReportsPdfSupport.h"
#include "core/services/catalog/CatalogMutationService.h"
#include "core/services/catalog/CatalogQueryService.h"
#include "ui/tabs/catalog/CatalogController.h"
#include "core/services/settings/UiStateSettingsService.h"

#include "ui/common/widgets/GlossyButton.h"
#include "ui/common/AdaptiveUi.h"

#include <algorithm>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QLineEdit>
#include <QMessageBox>
#include "app/AppContext.h"
#include "app/App.h"
#include "core/backup/BackupCommitPublisher.h"
#include <QPushButton>
#include <QScrollBar>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QUuid>

namespace {
QString norm(const QString& s) { return s.trimmed(); }

QString foldedFilterValue(QString value)
{
    value = value.trimmed().toCaseFolded();
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value;
}

QString normalizedCatalogBrandFilterValue(QString value)
{
    value = foldedFilterValue(value);
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('.'));
    return value;
}

QString normalizedCatalogPartSearchValue(QString value)
{
    value = foldedFilterValue(value);
    value.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}])")));
    return value;
}

QString displayYearsRange(int yearFrom, int yearTo)
{
    if (yearFrom <= 0 && yearTo <= 0) return QString();
    if (yearFrom > 0 && yearTo > 0 && yearFrom == yearTo) return QString::number(yearFrom);
    if (yearFrom <= 0) return QString::fromUtf8("до ") + QString::number(yearTo);
    if (yearTo <= 0) return QString::number(yearFrom) + QString::fromUtf8("+");
    return QString::number(yearFrom) + QStringLiteral("-") + QString::number(yearTo);
}

QString whiteEditStyle()
{
    return QString::fromUtf8(
        "QLineEdit {"
        " background:#ffffff; color:#0f141a; border:1px solid rgba(202,210,222,0.96);"
        " border-radius:11px; padding:6px 12px; selection-background-color:#d9e7ff; selection-color:#0f141a;"
        "}"
        "QLineEdit:focus { border:1px solid rgba(122,164,214,1.0); }"
        "QLineEdit:disabled { background:#e8edf3; color:#677381; }"
    );
}

QString whiteComboStyle()
{
    return QString::fromUtf8(
        "QComboBox {"
        " background:#ffffff; color:#0f141a; border:1px solid rgba(202,210,222,0.96);"
        " border-radius:11px; padding:6px 42px 6px 12px; min-height:34px;"
        "}"
        "QComboBox:focus { border:1px solid rgba(122,164,214,1.0); }"
        "QComboBox:disabled { background:#e8edf3; color:#677381; }"
        "QComboBox::drop-down {"
        " width:34px;"
        " border:none;"
        " border-left:1px solid rgba(202,210,222,0.96);"
        " border-top-right-radius:11px;"
        " border-bottom-right-radius:11px;"
        " background:rgba(246,248,251,0.98);"
        " subcontrol-origin:padding;"
        " subcontrol-position:top right;"
        "}"
        "QComboBox::down-arrow { image:url(:/assets/combo_arrow_black.png); width:12px; height:8px; }"
        "QComboBox QAbstractItemView {"
        " background:#ffffff; color:#0f141a; border:1px solid rgba(202,210,222,0.96);"
        " selection-background-color:#d9e7ff; selection-color:#0f141a; outline:none;"
        "}"
        "QComboBox QLineEdit { background:#ffffff; color:#0f141a; border:none; padding:0px; }"
    );
}

QLineEdit* makeEdit(QWidget* parent, const QString& ph = {})
{
    auto* e = new QLineEdit(parent);
    if (!ph.isEmpty()) e->setPlaceholderText(ph);
    e->setMinimumHeight(34);
    e->setStyleSheet(whiteEditStyle());
    return e;
}

QComboBox* makeCombo(QWidget* parent, bool editable = false, const QStringList& items = {})
{
    auto* cb = new QComboBox(parent);
    cb->setEditable(editable);
    cb->setMinimumHeight(34);
    cb->setMaxVisibleItems(20);
    cb->setInsertPolicy(QComboBox::NoInsert);
    cb->setStyleSheet(whiteComboStyle());
    if (!items.isEmpty()) cb->addItems(items);
    if (editable && cb->lineEdit()) {
        cb->lineEdit()->setStyleSheet(QStringLiteral("background:#ffffff; color:#0f141a; border:none;"));
        if (auto* comp = cb->completer()) {
            comp->setCaseSensitivity(Qt::CaseInsensitive);
            comp->setFilterMode(Qt::MatchContains);
            comp->setCompletionMode(QCompleter::PopupCompletion);
        }
    }
    return cb;
}

QComboBox* makeYearCombo(QWidget* parent, const QString& anyText)
{
    auto* cb = makeCombo(parent, false);
    cb->addItem(anyText, 0);
    const int currentYear = QDate::currentDate().year();
    for (int year = 1970; year <= currentYear; ++year) cb->addItem(QString::number(year), year);
    return cb;
}

int comboYearValue(const QComboBox* cb)
{
    return cb ? cb->currentData().toInt() : 0;
}

void setComboYearValue(QComboBox* cb, int year)
{
    if (!cb) return;
    int idx = cb->findData(year);
    cb->setCurrentIndex(idx >= 0 ? idx : 0);
}

QStringList catalogBrandOptions()
{
    QStringList values = {
        QStringLiteral("Acura"), QStringLiteral("Alfa Romeo"), QStringLiteral("Aston Martin"), QStringLiteral("Audi"),
        QStringLiteral("BAIC"), QStringLiteral("Bentley"), QStringLiteral("BMW"), QStringLiteral("Brilliance"),
        QStringLiteral("Bugatti"), QStringLiteral("Buick"), QStringLiteral("BYD"), QStringLiteral("Cadillac"),
        QStringLiteral("Changan"), QStringLiteral("Chery"), QStringLiteral("Chevrolet"), QStringLiteral("Chrysler"),
        QStringLiteral("Citroen"), QStringLiteral("Cupra"), QStringLiteral("Dacia"), QStringLiteral("Daewoo"),
        QStringLiteral("Daihatsu"), QStringLiteral("Dodge"), QStringLiteral("Dongfeng"), QStringLiteral("DS"),
        QStringLiteral("FAW"), QStringLiteral("Ferrari"), QStringLiteral("Fiat"), QStringLiteral("Ford"),
        QStringLiteral("Geely"), QStringLiteral("Genesis"), QStringLiteral("GMC"), QStringLiteral("Great Wall"),
        QStringLiteral("Haval"), QStringLiteral("Honda"), QStringLiteral("Hyundai"), QStringLiteral("Infiniti"),
        QStringLiteral("Isuzu"), QStringLiteral("Iveco"), QStringLiteral("Jaguar"), QStringLiteral("Jeep"),
        QStringLiteral("Jetour"), QStringLiteral("Kia"), QStringLiteral("Lada"), QStringLiteral("Lamborghini"),
        QStringLiteral("Lancia"), QStringLiteral("Land Rover"), QStringLiteral("Lexus"), QStringLiteral("Li Auto"),
        QStringLiteral("Lincoln"), QStringLiteral("Lotus"), QStringLiteral("Maserati"), QStringLiteral("Mazda"),
        QStringLiteral("McLaren"), QStringLiteral("Mercedes-Benz"), QStringLiteral("MG"), QStringLiteral("Mini"),
        QStringLiteral("Mitsubishi"), QStringLiteral("Nissan"), QStringLiteral("Opel"), QStringLiteral("Peugeot"),
        QStringLiteral("Polestar"), QStringLiteral("Porsche"), QStringLiteral("RAM"), QStringLiteral("Renault"),
        QStringLiteral("Rolls-Royce"), QStringLiteral("Rover"), QStringLiteral("Saab"), QStringLiteral("Seat"),
        QStringLiteral("Skoda"), QStringLiteral("Smart"), QStringLiteral("SsangYong"), QStringLiteral("Subaru"),
        QStringLiteral("Suzuki"), QStringLiteral("Tesla"), QStringLiteral("Toyota"), QStringLiteral("Volkswagen"),
        QStringLiteral("Volvo"), QStringLiteral("Voyah"), QStringLiteral("ZAZ")
    };
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) {
        return a.toLower() < b.toLower();
    });
    return values;
}

QStringList standardFuelOptions()
{
    QStringList values = {
        QStringLiteral("Бензин"), QStringLiteral("Дизель"), QStringLiteral("Газ"), QStringLiteral("Газ / бензин"),
        QStringLiteral("Гібрид"), QStringLiteral("Плагін-гібрид"), QStringLiteral("Електро"),
        QStringLiteral("LPG"), QStringLiteral("CNG")
    };
    values.removeDuplicates();
    return values;
}

QStringList standardGroupOptions()
{
    QStringList values = {
        QStringLiteral("Гальмівна система"), QStringLiteral("Підвіска"), QStringLiteral("Рульове керування"),
        QStringLiteral("Двигун"), QStringLiteral("ГРМ"), QStringLiteral("Система охолодження"),
        QStringLiteral("Система змащення"), QStringLiteral("Фільтри"), QStringLiteral("Паливна система"),
        QStringLiteral("Запалювання"), QStringLiteral("Впуск / випуск"), QStringLiteral("Турбіна"),
        QStringLiteral("Трансмісія"), QStringLiteral("АКПП / МКПП"), QStringLiteral("Зчеплення"),
        QStringLiteral("Привід / ШРУС"), QStringLiteral("Електрика"), QStringLiteral("Стартер / генератор"),
        QStringLiteral("Освітлення"), QStringLiteral("Кузов"), QStringLiteral("Салон"),
        QStringLiteral("Кондиціонер / клімат"), QStringLiteral("Склоочисники"), QStringLiteral("Вихлопна система")
    };
    values.removeDuplicates();
    return values;
}

QString normalizedCatalogBrandKey(QString brand)
{
    brand = brand.trimmed().toLower();
    brand.remove(QLatin1Char(' '));
    brand.remove(QLatin1Char('-'));
    brand.remove(QLatin1Char('.'));
    return brand;
}

struct BrandAliasEntry
{
    QString canonical;
    QStringList aliases;
};

struct ResolvedCatalogVehicle
{
    QString brand;
    QString model;
    bool brandRecognized = false;
};

QStringList brandAliasTokens(QString alias)
{
    alias = alias.trimmed();
    alias.replace(QRegularExpression(QStringLiteral(R"([\-\./_]+)")), QStringLiteral(" "));
    return alias.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

const QVector<BrandAliasEntry>& catalogBrandAliases()
{
    static const QVector<BrandAliasEntry> kAliases = [] {
        QVector<BrandAliasEntry> out;
        const QStringList canonicalBrands = catalogBrandOptions();
        out.reserve(canonicalBrands.size());
        for (const QString& canonical : canonicalBrands) {
            BrandAliasEntry entry;
            entry.canonical = canonical;
            entry.aliases << canonical;
            const QString key = normalizedCatalogBrandKey(canonical);
            if (key == QStringLiteral("mercedesbenz")) entry.aliases << QStringLiteral("Mercedes") << QStringLiteral("MB") << QStringLiteral("Mercedes Benz");
            if (key == QStringLiteral("landrover")) entry.aliases << QStringLiteral("LandRover") << QStringLiteral("Range Rover");
            if (key == QStringLiteral("volkswagen")) entry.aliases << QStringLiteral("VW") << QStringLiteral("Passat") << QStringLiteral("Golf") << QStringLiteral("Tiguan");
            if (key == QStringLiteral("skoda")) entry.aliases << QStringLiteral("Škoda");
            if (key == QStringLiteral("peugeot")) entry.aliases << QStringLiteral("Peugot");
            if (key == QStringLiteral("mercedesbenz")) entry.aliases << QStringLiteral("Viano") << QStringLiteral("Vito") << QStringLiteral("Sprinter");
            out.push_back(entry);
        }
        return out;
    }();
    return kAliases;
}

QString detectCanonicalCatalogBrand(const QString& text, QString* remainderOut = nullptr)
{
    const QString source = text.trimmed().simplified();
    if (source.isEmpty()) {
        if (remainderOut) remainderOut->clear();
        return QString();
    }

    QString bestCanonical;
    QString bestRemainder;
    int bestScore = -1;
    for (const BrandAliasEntry& entry : catalogBrandAliases()) {
        QStringList aliases = entry.aliases;
        std::sort(aliases.begin(), aliases.end(), [](const QString& a, const QString& b) {
            return normalizedCatalogBrandKey(a).size() > normalizedCatalogBrandKey(b).size();
        });
        for (const QString& alias : aliases) {
            const QString aliasKey = normalizedCatalogBrandKey(alias);
            if (aliasKey.isEmpty()) continue;

            const QStringList tokens = brandAliasTokens(alias);
            QString pattern = QStringLiteral("^\\s*");
            bool first = true;
            for (const QString& token : tokens) {
                if (!first) pattern += QStringLiteral("[\\s\\-\\./_]*");
                pattern += QRegularExpression::escape(token);
                first = false;
            }
            pattern += QStringLiteral("(?:\\b|[\\s\\-\\./_]+)?");
            const QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = rx.match(source);
            if (!m.hasMatch()) continue;

            const int score = aliasKey.size();
            if (score < bestScore) continue;
            QString remainder = source.mid(m.capturedLength()).trimmed();
            remainder.remove(QRegularExpression(QStringLiteral(R"(^[\s\-\./_]+|[\s\-\./_]+$)")));
            bestCanonical = entry.canonical;
            bestRemainder = remainder;
            bestScore = score;
        }
    }

    if (remainderOut) *remainderOut = bestRemainder;
    return bestCanonical;
}

ResolvedCatalogVehicle resolveCatalogVehicleTexts(const QString& rawBrand, const QString& rawModel)
{
    ResolvedCatalogVehicle out;
    const QString safeBrand = rawBrand.trimmed().simplified();
    const QString safeModel = rawModel.trimmed().simplified();

    QString brandRemainder;
    const QString brandCanonical = detectCanonicalCatalogBrand(safeBrand, &brandRemainder);
    QString modelRemainder;
    const QString modelCanonical = detectCanonicalCatalogBrand(safeModel, &modelRemainder);

    if (!brandCanonical.isEmpty()) {
        out.brand = brandCanonical;
        out.brandRecognized = true;
        if (!brandRemainder.isEmpty()) {
            out.model = brandRemainder;
        } else {
            out.model = safeModel;
        }
        if (!modelCanonical.isEmpty()) {
            const QString brandKey = normalizedCatalogBrandKey(brandCanonical);
            const QString modelBrandKey = normalizedCatalogBrandKey(modelCanonical);
            if (modelBrandKey == brandKey && !modelRemainder.isEmpty()) {
                out.model = modelRemainder;
            } else if (modelBrandKey != brandKey) {
                out.model.clear();
            }
        }
    } else if (!modelCanonical.isEmpty()) {
        out.brand = modelCanonical;
        out.brandRecognized = true;
        out.model = modelRemainder;
    } else {
        out.brand = safeBrand;
        out.model = safeModel;
        out.brandRecognized = false;
    }

    out.brand = out.brand.trimmed().simplified();
    out.model = out.model.trimmed().simplified();
    if (!out.model.isEmpty()) {
        QString nestedRemainder;
        const QString nestedCanonical = detectCanonicalCatalogBrand(out.model, &nestedRemainder);
        if (!nestedCanonical.isEmpty()) {
            const QString nestedKey = normalizedCatalogBrandKey(nestedCanonical);
            const QString brandKey = normalizedCatalogBrandKey(out.brand);
            if (nestedKey == brandKey && !nestedRemainder.isEmpty()) {
                out.model = nestedRemainder;
            } else if (nestedKey != brandKey) {
                out.model.clear();
            }
        }
    }
    if (foldedFilterValue(out.model) == foldedFilterValue(out.brand)) out.model.clear();
    return out;
}

QString optionKeyForValue(const QString& value, bool brandValue)
{
    return brandValue ? normalizedCatalogBrandFilterValue(value) : foldedFilterValue(value);
}

QString preferredOptionDisplay(const QString& current, const QString& candidate, bool brandValue)
{
    Q_UNUSED(brandValue)
    const QString a = current.trimmed();
    const QString b = candidate.trimmed();
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    const bool aAllUpper = (a == a.toUpper());
    const bool bAllUpper = (b == b.toUpper());
    if (aAllUpper && !bAllUpper) return b;
    if (!aAllUpper && bAllUpper) return a;
    return a.size() <= b.size() ? a : b;
}

int findComboIndexByCatalogKey(QComboBox* cb, const QString& value, bool brandValue)
{
    if (!cb) return -1;
    const QString wantedKey = optionKeyForValue(value, brandValue);
    if (wantedKey.isEmpty()) return -1;
    for (int i = 0; i < cb->count(); ++i) {
        if (optionKeyForValue(cb->itemText(i), brandValue) == wantedKey) return i;
    }
    return -1;
}

void appendUniqueSorted(QComboBox* cb, const QStringList& values)
{
    for (const QString& raw : values) {
        const QString v = raw.trimmed();
        if (v.isEmpty()) continue;
        if (cb->findText(v, Qt::MatchFixedString) < 0) cb->addItem(v);
    }
}

QString cellText(const QTableWidget* table, int row, int col)
{
    const auto* item = table->item(row, col);
    return item ? item->text() : QString();
}

}

static void styleMessageBoxReadableLocal(QMessageBox* box)
{
    applyStandardMessageBoxStyle(box);
}

static QPair<QString, QString> splitBrandModel(const QString& rawCar)
{
    const QString car = rawCar.trimmed().simplified();
    if (car.isEmpty()) return {QString(), QString()};
    const QStringList parts = car.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() <= 1) return {car, QString()};
    return {parts.first(), parts.mid(1).join(QStringLiteral(" "))};
}

struct CatalogImportSourceRow
{
    int sourcePartId = 0;
    QString orderUid;
    int orderNumber = 0;
    QString rawCar;
    QString vehicleBrand;
    QString vehicleModel;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuel;
    QString engineType;
    QString bodyType;
    QString oem;
    QString name;
    QString brandName;
    QString supplier;
    QString note;
    double price = 0.0;
};

struct CatalogImportAggregate
{
    QString rawCar;
    QString vehicleBrand;
    QString vehicleModel;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuel;
    QString engineType;
    QString bodyType;
    QString oem;
    QString name;
    QString brandName;
    QString supplier;
    QString groupName;
    double price = 0.0;
    QString sourceOrderUidFirst;
    QString sourceOrderUidLast;
    int firstOrderNumber = 0;
    int lastOrderNumber = 0;
    QSet<QString> suppliers;
    QSet<QString> orderNotes;
    QList<CatalogImportSourceRow> sources;
};

static QString foldedImportValue(QString value)
{
    return value.trimmed().simplified().toLower();
}

static QString normalizedOemValue(QString value)
{
    value = foldedImportValue(value);
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char('.'));
    value.remove(QLatin1Char('/'));
    return value;
}

static bool isLowQualityPartName(const QString& name)
{
    const QString n = foldedImportValue(name);
    return n.isEmpty() || n == QString::fromUtf8("запчастина") || n == QString::fromUtf8("деталь")
        || n == QString::fromUtf8("товар") || n == QString::fromUtf8("матеріал")
        || n == QStringLiteral("part") || n == QStringLiteral("parts");
}

static bool isMotorOilForCatalogImport(const QString& name, const QString& note)
{
    if (ReportsPdfSupport::isMotorOilName(name)) return true;
    const QString joined = foldedImportValue(name + QStringLiteral(" ") + note);
    if (joined.contains(QString::fromUtf8("моторне масло"))
        || joined.contains(QString::fromUtf8("моторна олива"))
        || joined.contains(QString::fromUtf8("engine oil"))
        || joined.contains(QString::fromUtf8("motor oil"))) {
        return true;
    }
    static const QRegularExpression viscosityRe(QStringLiteral("\\b(0w|5w|10w|15w|20w)[- ]?\\d{2}\\b"), QRegularExpression::CaseInsensitiveOption);
    if (viscosityRe.match(joined).hasMatch()
        && (joined.contains(QString::fromUtf8("масло")) || joined.contains(QString::fromUtf8("олива")) || joined.contains(QStringLiteral("oil")))) {
        return true;
    }
    return false;
}

static QString inferCatalogGroupName(const QString& name)
{
    const QString n = foldedImportValue(name);
    if (n.contains(QString::fromUtf8("фільтр"))) return QString::fromUtf8("Фільтри");
    if (n.contains(QString::fromUtf8("колод")) || n.contains(QString::fromUtf8("диск гальм")) || n.contains(QString::fromUtf8("супорт"))) return QString::fromUtf8("Гальмівна система");
    if (n.contains(QString::fromUtf8("аморт")) || n.contains(QString::fromUtf8("стійк")) || n.contains(QString::fromUtf8("пружин")) || n.contains(QString::fromUtf8("сайлент"))) return QString::fromUtf8("Підвіска");
    if (n.contains(QString::fromUtf8("ремен")) || n.contains(QString::fromUtf8("ролик")) || n.contains(QString::fromUtf8("грм"))) return QString::fromUtf8("Привід / ГРМ");
    if (n.contains(QString::fromUtf8("свіч")) || n.contains(QString::fromUtf8("котуш")) || n.contains(QString::fromUtf8("форсун"))) return QString::fromUtf8("Двигун");
    if (n.contains(QString::fromUtf8("помпа")) || n.contains(QString::fromUtf8("радіатор")) || n.contains(QString::fromUtf8("антифриз"))) return QString::fromUtf8("Охолодження");
    return QString();
}

static QString catalogImportKey(const CatalogImportAggregate& row)
{
    const QString oem = normalizedOemValue(row.oem);
    if (!oem.isEmpty()) return QStringLiteral("oem|") + oem;
    return QStringLiteral("name|%1|%2")
        .arg(foldedImportValue(row.name), foldedImportValue(row.brandName));
}

static QStringList sortedValues(const QSet<QString>& values)
{
    QStringList out = values.values();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.toLower() < b.toLower();
    });
    return out;
}

static QString buildImportedCatalogNote(const CatalogImportAggregate& row)
{
    QStringList lines;
    lines << QString::fromUtf8("Імпортовано із нарядів");

    const QStringList suppliers = sortedValues(row.suppliers);
    if (!suppliers.isEmpty()) {
        lines << QString::fromUtf8("Магазини / постачальники: %1").arg(suppliers.join(QStringLiteral(", ")));
    }

    if (!row.sourceOrderUidFirst.isEmpty() || !row.sourceOrderUidLast.isEmpty()) {
        lines << QString::fromUtf8("Джерело: перший UID %1, останній UID %2")
                     .arg(row.sourceOrderUidFirst.isEmpty() ? QString::fromUtf8("—") : row.sourceOrderUidFirst,
                          row.sourceOrderUidLast.isEmpty() ? QString::fromUtf8("—") : row.sourceOrderUidLast);
    }

    if (row.firstOrderNumber > 0 || row.lastOrderNumber > 0) {
        lines << QString::fromUtf8("Наряди: перший №%1, останній №%2")
                     .arg(row.firstOrderNumber > 0 ? QString::number(row.firstOrderNumber) : QString::fromUtf8("—"),
                          row.lastOrderNumber > 0 ? QString::number(row.lastOrderNumber) : QString::fromUtf8("—"));
    }

    const QStringList notes = sortedValues(row.orderNotes);
    if (!notes.isEmpty()) {
        lines << QString::fromUtf8("Нотатки з нарядів: %1").arg(notes.join(QStringLiteral(" | ")));
    }

    return lines.join(QStringLiteral("\n"));
}

static QString mergeCatalogNotes(const QString& existing, const QString& imported)
{
    const QString left = existing.trimmed();
    const QString right = imported.trimmed();
    if (left.isEmpty()) return right;
    if (right.isEmpty()) return left;
    if (left.contains(right, Qt::CaseInsensitive)) return left;
    return left + QStringLiteral("\n") + right;
}

namespace {

void applyCatalogMetalButtonStyle(QPushButton* btn)
{
    if (!btn) return;
    if (auto* glossy = qobject_cast<GlossyButton*>(btn)) {
        glossy->setVisualStyle(GlossyButton::VisualStyle::CatalogMetal);
    }
}

} // namespace

CatalogPage::CatalogPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    // Heavy data loading is deferred to refresh()/prewarm so the first shell
    // construction stays cheap and does not double-load on first activation.
}

void CatalogPage::invalidateDataCache()
{
    m_allRows.clear();
    m_rows.clear();
    m_allRowsLoaded = false;
}

void CatalogPage::ensureRowDerived(RowData& row) const
{
    CatalogQueryService::ensureDerived(row);
}
