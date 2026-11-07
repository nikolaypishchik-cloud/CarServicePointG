#include "core/util/PlateUtils.h"

#include <QRegularExpression>
#include <QHash>

namespace {
QChar mapLatinToCyrillicDisplay(const QChar& ch)
{
    static const QHash<QChar, QChar> map = {
        { QChar('A'), QChar(u'А') },
        { QChar('B'), QChar(u'В') },
        { QChar('C'), QChar(u'С') },
        { QChar('E'), QChar(u'Е') },
        { QChar('H'), QChar(u'Н') },
        { QChar('I'), QChar(u'І') },
        { QChar('K'), QChar(u'К') },
        { QChar('M'), QChar(u'М') },
        { QChar('O'), QChar(u'О') },
        { QChar('P'), QChar(u'Р') },
        { QChar('T'), QChar(u'Т') },
        { QChar('X'), QChar(u'Х') },
    };

    auto it = map.find(ch);
    return (it == map.end()) ? ch : it.value();
}

QChar mapCyrillicLookalike(const QChar& ch)
{
    // Map Cyrillic letters that visually match Latin.
    static const QHash<QChar, QChar> map = {
        { QChar(u'А'), QChar('A') },
        { QChar(u'В'), QChar('B') },
        { QChar(u'Е'), QChar('E') },
        { QChar(u'І'), QChar('I') },
        { QChar(u'К'), QChar('K') },
        { QChar(u'М'), QChar('M') },
        { QChar(u'Н'), QChar('H') },
        { QChar(u'О'), QChar('O') },
        { QChar(u'Р'), QChar('P') },
        { QChar(u'С'), QChar('C') },
        { QChar(u'Т'), QChar('T') },
        { QChar(u'Х'), QChar('X') },
        // Lowercase variants
        { QChar(u'а'), QChar('A') },
        { QChar(u'в'), QChar('B') },
        { QChar(u'е'), QChar('E') },
        { QChar(u'і'), QChar('I') },
        { QChar(u'к'), QChar('K') },
        { QChar(u'м'), QChar('M') },
        { QChar(u'н'), QChar('H') },
        { QChar(u'о'), QChar('O') },
        { QChar(u'р'), QChar('P') },
        { QChar(u'с'), QChar('C') },
        { QChar(u'т'), QChar('T') },
        { QChar(u'х'), QChar('X') },
    };

    auto it = map.find(ch);
    return (it == map.end()) ? ch : it.value();
}
}

QString PlateUtils::normalizePlate(const QString& input)
{
    QString s = input.trimmed().toUpper();
    if (s.isEmpty()) return QString();

    // remove separators
    static const QRegularExpression sep(QStringLiteral("[\\s\\-_. ,]+"));
    s.remove(sep);

    QString out;
    out.reserve(s.size());
    for (const QChar& ch : s) {
        out.append(mapCyrillicLookalike(ch));
    }
    return out;
}

QString PlateUtils::formatPlateDisplay(const QString& plate)
{
    const QString s = normalizePlate(plate);
    if (s.isEmpty()) return QString();

    // UA plates: 2 letters + 4/5 digits + 2 letters
    static const QRegularExpression re(QStringLiteral("^([A-Z]{2})(\\d{4,5})([A-Z]{2})$"));
    QRegularExpressionMatch m = re.match(s);
    if (m.hasMatch()) {
        QString a = m.captured(1);
        QString b = m.captured(3);
        for (QChar& ch : a) ch = mapLatinToCyrillicDisplay(ch);
        for (QChar& ch : b) ch = mapLatinToCyrillicDisplay(ch);
        return QStringLiteral("%1 %2 %3").arg(a, m.captured(2), b);
    }
    return s;
}


bool PlateUtils::isLikelyPlateInput(const QString& input)
{
    const QString s = normalizePlate(input);
    if (s.isEmpty()) return false;

    static const QRegularExpression re(QStringLiteral(R"(^[A-Z]{0,2}\d{0,4}[A-Z]{0,2}$)"));
    if (!re.match(s).hasMatch()) return false;

    int letters = 0;
    int digits = 0;
    for (const QChar& ch : s) {
        if (ch.isLetter()) ++letters;
        else if (ch.isDigit()) ++digits;
    }
    return letters >= 2 && digits >= 1;
}

QString PlateUtils::formatPlateProgressive(const QString& input)
{
    const QString norm = normalizePlate(input);
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

    for (QChar& ch : a1) ch = mapLatinToCyrillicDisplay(ch);
    for (QChar& ch : a2) ch = mapLatinToCyrillicDisplay(ch);

    QString out;
    if (!a1.isEmpty()) out += a1;
    if (!nums.isEmpty()) {
        if (!out.isEmpty()) out += QLatin1Char(' ');
        out += nums;
    }
    if (!a2.isEmpty()) {
        if (!out.isEmpty()) out += QLatin1Char(' ');
        out += a2;
    }
    return out;
}
