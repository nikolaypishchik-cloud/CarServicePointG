// Extracted from src/ui/common/dialogs/OrderDialogModulePrelude.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

namespace {
class ScopedOrderSaveBackupDisableForAccept final
{
public:
    ScopedOrderSaveBackupDisableForAccept()
    {
        if (!qApp) return;
        m_prev = qApp->property("csDisableOrderSaveBackup");
        qApp->setProperty("csDisableOrderSaveBackup", true);
    }

    ~ScopedOrderSaveBackupDisableForAccept()
    {
        if (!qApp) return;
        qApp->setProperty("csDisableOrderSaveBackup", m_prev);
    }

private:
    QVariant m_prev;
};

static QString toPlateDisplayLetters(QString s) {
    for (QChar& ch : s) {
        switch (ch.unicode()) {
            case 'A': ch = QChar(u'А'); break;
            case 'B': ch = QChar(u'В'); break;
            case 'C': ch = QChar(u'С'); break;
            case 'E': ch = QChar(u'Е'); break;
            case 'H': ch = QChar(u'Н'); break;
            case 'I': ch = QChar(u'І'); break;
            case 'K': ch = QChar(u'К'); break;
            case 'M': ch = QChar(u'М'); break;
            case 'O': ch = QChar(u'О'); break;
            case 'P': ch = QChar(u'Р'); break;
            case 'T': ch = QChar(u'Т'); break;
            case 'X': ch = QChar(u'Х'); break;
            default: break;
        }
    }
    return s;
}

static QString capitalizeSentence(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return s;
    // Uppercase first letter only (rest unchanged)
    const QChar first = s.at(0).toUpper();
    s[0] = first;
    return s;
}

static QString titleCaseWords(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return s;
    const auto parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (QString p : parts) {
        if (p.contains('.')) {
            out << p.toUpper();
        } else if (!p.isEmpty()) {
            p = p.toLower();
            p[0] = p.at(0).toUpper();
            out << p;
        }
    }
    return out.join(' ');
}

static QString nonNullDialogText(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

static bool ensureOrderDialogDbReady(QWidget* parent, QString* errorText = nullptr)
{
    QString dbError;
    if (!AppRuntimeService::ensureConnectionAlive(&dbError)) {
        if (errorText) *errorText = dbError;
        if (parent) {
            QMessageBox::critical(parent, QString::fromUtf8("Помилка"),
                                  QString::fromUtf8("Не вдалося підготувати БД для відкриття наряду.\n%1").arg(dbError));
        }
        return false;
    }
    // Avoid full schema/routing rebuilds in the interactive order-detail hot
    // path. Startup already completed the required schema preparation.
    return true;
}

static QString formatPlateProgressive(const QString& input)
{
    const QString norm = PlateUtils::normalizePlate(input);
    if (norm.isEmpty()) return QString();

    QString a1, nums, a2;
    for (const QChar& ch : norm) {
        if (ch.isLetter()) {
            if (a1.size() < 2 && nums.isEmpty() && a2.isEmpty()) a1 += ch;
            else if (!a1.isEmpty() && nums.size() >= 1 && a2.size() < 2) a2 += ch;
        } else if (ch.isDigit()) {
            if (a2.isEmpty() && nums.size() < 4) nums += ch;
        }
    }

    a1 = toPlateDisplayLetters(a1);
    a2 = toPlateDisplayLetters(a2);

    QString out;
    if (!a1.isEmpty()) out += a1;
    if (!nums.isEmpty()) {
        if (!out.isEmpty()) out += ' ';
        out += nums;
    }
    if (!a2.isEmpty()) {
        if (!out.isEmpty()) out += ' ';
        out += a2;
    }
    return out;
}

static QString safeTrimmed(const QVariant& v)
{
    return v.toString().trimmed();
}

static QString fieldValue(const QSqlRecord& rec, const QString& name)
{
    const int idx = rec.indexOf(name);
    return idx >= 0 ? safeTrimmed(rec.value(idx)) : QString();
}

static QString firstNonEmptyField(const QSqlRecord& rec, const std::initializer_list<const char*>& names)
{
    for (const char* name : names) {
        const QString value = fieldValue(rec, QString::fromLatin1(name));
        if (!value.isEmpty()) return value;
    }
    return QString();
}

static QString normalizeLegacyClientName(const QSqlRecord& rec)
{
    QString client = firstNonEmptyField(rec, {"client", "owner_name"});
    if (client == QStringLiteral("-") || client == QStringLiteral("—")) client.clear();
    return client;
}

static QString normalizeLegacyCarDisplay(const QSqlRecord& rec)
{
    QString brand = firstNonEmptyField(rec, {"car_brand"});
    QString model = firstNonEmptyField(rec, {"car_model"});
    QString display = firstNonEmptyField(rec, {"car"});

    if (display == QStringLiteral("-") || display == QStringLiteral("—")) display.clear();
    if (brand == QStringLiteral("-") || brand == QStringLiteral("—")) brand.clear();
    if (model == QStringLiteral("-") || model == QStringLiteral("—")) model.clear();

    if (!brand.isEmpty() || !model.isEmpty()) {
        QString joined = (brand + QStringLiteral(" ") + model).trimmed();
        joined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        joined = VehicleNameUtils::normalizeVehicleFreeText(joined);
        if (!joined.isEmpty()) return titleCaseWords(joined);
    }

    display.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return titleCaseWords(VehicleNameUtils::normalizeVehicleFreeText(display));
}

static QString normalizeLegacyPlateDisplay(const QSqlRecord& rec)
{
    const QString raw = firstNonEmptyField(rec, {"plate_norm", "plate"});
    const QString normalized = PlateUtils::normalizePlate(raw);
    const QString display = PlateUtils::formatPlateDisplay(normalized);
    return display.isEmpty() ? formatPlateProgressive(raw) : display;
}

static QString normalizeLegacyMasterId(const QSqlRecord& rec)
{
    return firstNonEmptyField(rec, {"master_id"});
}

static QString normalizeLegacyMasterName(const QSqlRecord& rec)
{
    return firstNonEmptyField(rec, {"master_name_closed"});
}

static QString normalizeSnapshotClientName(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    QString client = snapshot.client.trimmed();
    if (client.isEmpty()) client = snapshot.ownerName.trimmed();
    if (client == QStringLiteral("-") || client == QStringLiteral("—")) client.clear();
    return client;
}

static QString normalizeSnapshotCarDisplay(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    QString brand = snapshot.carBrand.trimmed();
    QString model = snapshot.carModel.trimmed();
    QString display = snapshot.car.trimmed();

    if (display == QStringLiteral("-") || display == QStringLiteral("—")) display.clear();
    if (brand == QStringLiteral("-") || brand == QStringLiteral("—")) brand.clear();
    if (model == QStringLiteral("-") || model == QStringLiteral("—")) model.clear();

    if (!brand.isEmpty() || !model.isEmpty()) {
        QString joined = (brand + QStringLiteral(" ") + model).trimmed();
        joined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        joined = VehicleNameUtils::normalizeVehicleFreeText(joined);
        if (!joined.isEmpty()) return titleCaseWords(joined);
    }

    display.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return titleCaseWords(VehicleNameUtils::normalizeVehicleFreeText(display));
}

static QString normalizeSnapshotPlateDisplay(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    const QString raw = !snapshot.plateNorm.trimmed().isEmpty() ? snapshot.plateNorm : snapshot.plate;
