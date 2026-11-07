#include "core/util/StoreNameNormalizer.h"

#include <QRegularExpression>

namespace {

QString foldStoreName(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QChar(0x00A0), QLatin1Char(' '));
    value = value.simplified();
    value.replace(QChar(0x0456), QChar(0x0438)); // і -> и
    value.replace(QChar(0x0457), QChar(0x0438)); // ї -> и
    value.replace(QChar(0x0454), QChar(0x0435)); // є -> е
    value.replace(QChar(0x044d), QChar(0x0435)); // э -> е
    value.replace(QChar(0x0491), QChar(0x0433)); // ґ -> г
    return value;
}

bool isNoStore(const QString& folded)
{
    return folded.isEmpty()
        || folded == QString::fromUtf8("без магазину")
        || folded == QString::fromUtf8("(без магазину)")
        || folded == QString::fromUtf8("без магазина")
        || folded == QString::fromUtf8("(без магазина)");
}

} // namespace

namespace StoreNameNormalizer {

QString canonKey(const QString& raw)
{
    const QString folded = foldStoreName(raw);
    if (isNoStore(folded)) return QStringLiteral("nomagazin");

    // Keep store policy keys identical across UI, import, reports, PDF and batch recalculation.
    // Invoice/import sources may provide Cyrillic names (Автонова-Д, Еліт/Элит) or Latin aliases
    // (Avtonova-D/Autonova-D, ELIT).  All of them must collapse to the same canonical keys.
    if (folded.contains(QString::fromUtf8("автонова"))
        || folded.contains(QStringLiteral("avtonova"))
        || folded.contains(QStringLiteral("autonova"))) {
        return QStringLiteral("avtonova_d");
    }
    if (folded.contains(QString::fromUtf8("елит"))
        || folded.contains(QString::fromUtf8("еліт"))
        || folded.contains(QStringLiteral("elit"))
        || folded.contains(QStringLiteral("elite"))) {
        return QStringLiteral("elit");
    }
    if ((folded.contains(QString::fromUtf8("юник"))
         || folded.contains(QStringLiteral("unik"))
         || folded.contains(QStringLiteral("unix"))
         || folded.contains(QStringLiteral("uniks"))
         || folded.contains(QStringLiteral("unique")))
        && (folded.contains(QString::fromUtf8("трейд")) || folded.contains(QStringLiteral("trade")))) {
        return QStringLiteral("unik_trade");
    }

    QString out;
    out.reserve(folded.size());
    for (const QChar& ch : folded) {
        if (ch.isLetterOrNumber()) out.append(ch);
    }
    return out;
}

QString canonicalDisplayName(const QString& raw)
{
    const QString folded = foldStoreName(raw);
    if (isNoStore(folded)) return QString();

    const QString key = canonKey(raw);
    if (key == QStringLiteral("avtonova_d")) return QString::fromUtf8("Автонова-D");
    if (key == QStringLiteral("elit")) return QString::fromUtf8("Еліт");
    if (key == QStringLiteral("unik_trade")) return QString::fromUtf8("Юнік-трейд");

    return raw.simplified().trimmed();
}

QString canonicalDisplayNameOrDefault(const QString& raw, const QString& defaultValue)
{
    const QString name = canonicalDisplayName(raw);
    return name.isEmpty() ? defaultValue : name;
}

} // namespace StoreNameNormalizer
