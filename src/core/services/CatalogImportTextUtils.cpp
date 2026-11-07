#include "core/services/CatalogImportTextUtils.h"

#include <QRegularExpression>
#include <QSet>

namespace CatalogImportTextUtils {

QString foldedCatalogValue(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value;
}

QString normalizedCatalogLookupKey(QString value)
{
    value = foldedCatalogValue(value);
    value.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return value;
}

bool isCatalogPlaceholderCode(QString value)
{
    value = foldedCatalogValue(value);
    if (value.isEmpty()) return true;
    const QString compact = normalizedCatalogLookupKey(value);
    static const QSet<QString> placeholders = {
        QString(),
        QStringLiteral("none"),
        QStringLiteral("null"),
        QStringLiteral("na"),
        QStringLiteral("n/a"),
        QStringLiteral("noname"),
        QString::fromUtf8("нет"),
        QString::fromUtf8("безномера"),
        QString::fromUtf8("безномер"),
        QString::fromUtf8("без№")
    };
    if (placeholders.contains(value) || placeholders.contains(compact)) return true;
    return compact.isEmpty();
}

QString normalizedCatalogPartCode(QString value)
{
    if (isCatalogPlaceholderCode(value)) return QString();
    value = value.trimmed().toUpper();
    value.remove(QRegularExpression(QStringLiteral(R"([^A-ZА-ЯЁЇІЄҐ0-9]+)"), QRegularExpression::CaseInsensitiveOption));
    return value;
}

QString normalizedCatalogStoreKey(QString value)
{
    value = foldedCatalogValue(value);
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(""));
    return value;
}

QString normalizedCatalogBrandKey(QString value)
{
    value = foldedCatalogValue(value);
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(""));
    return value;
}

bool isCatalogMeaningfulNameOnly(const QString& normalizedName,
                                 const QString& brandName,
                                 const QString& vehicleBrand,
                                 const QString& vehicleModel,
                                 const QString& supplier)
{
    if (normalizedName.size() < 4) return false;
    if (!normalizedCatalogBrandKey(brandName).isEmpty()) return true;
    if (!normalizedCatalogLookupKey(vehicleBrand).isEmpty()) return true;
    if (!normalizedCatalogLookupKey(vehicleModel).isEmpty()) return true;
    if (!normalizedCatalogStoreKey(supplier).isEmpty()) return true;
    return normalizedName.size() >= 7;
}

} // namespace CatalogImportTextUtils
