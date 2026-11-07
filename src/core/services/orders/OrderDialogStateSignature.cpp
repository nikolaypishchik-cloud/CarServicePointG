#include "core/services/orders/OrderDialogStateSignature.h"

#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"

#include <QRegularExpression>
#include <QStringList>

namespace {
static QString toPlateDisplayLetters(QString s)
{
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

static QString comparablePlateText(const QString& raw)
{
    const QString normalized = PlateUtils::normalizePlate(raw);
    const QString display = PlateUtils::formatPlateDisplay(normalized);
    return display.isEmpty() ? formatPlateProgressive(raw) : display;
}

static QString comparableVinText(const QString& raw)
{
    return raw.trimmed().toUpper();
}

static QString capitalizeSentence(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return s;
    s[0] = s.at(0).toUpper();
    return s;
}

static QString comparableCarText(const QString& raw)
{
    return VehicleNameUtils::normalizeVehicleFreeText(raw);
}

static QString comparableColorText(const QString& raw)
{
    return capitalizeSentence(raw);
}

static QString comparableClientText(const QString& raw)
{
    return capitalizeSentence(raw);
}
}

QString OrderDialogStateSignature::build(const OrderDialogStateSnapshot& snapshot)
{
    return QStringList{
        QString::number(snapshot.visibleNumber),
        snapshot.openedDate.toString(Qt::ISODate),
        snapshot.openedTime.toString(QStringLiteral("HH:mm:ss")),
        snapshot.status.trimmed(),
        comparablePlateText(snapshot.plate),
        comparableVinText(snapshot.vin),
        comparableCarText(snapshot.car),
        comparableColorText(snapshot.color),
        QString::number(snapshot.carYear),
        QString::number(snapshot.odometer),
        snapshot.engineCode.trimmed(),
        snapshot.engineVolume.trimmed(),
        snapshot.fuelType.trimmed(),
        comparableClientText(snapshot.client),
        snapshot.phone.trimmed(),
        snapshot.masterId.trimmed(),
        QString::number(snapshot.discount, 'f', 2), snapshot.discountMode
    }.join(QStringLiteral("\x1F"));
}
