#include "core/util/VehicleNameUtils.h"
#include "core/util/VehicleNameRules.h"

#include <QRegularExpression>
#include <QVector>
#include <QPair>
#include <QStringList>
#include <algorithm>

namespace {

using Rule = VehicleNameRules::Rule;

QString buildRulePattern(const Rule& rule)
{
    const QString alias = QRegularExpression::escape(rule.first);
    const QString canonicalLower = rule.second.toLower();
    const QString aliasLower = rule.first.toLower();
    QString tail = QStringLiteral(R"((?![\p{L}\p{N}]))");

    const QChar delimiter = canonicalLower.contains(QChar('-')) ? QChar('-')
                           : canonicalLower.contains(QChar(' ')) ? QChar(' ')
                           : QChar();
    if (!delimiter.isNull()) {
        const QStringList parts = canonicalLower.split(delimiter, Qt::SkipEmptyParts);
        if (parts.size() >= 2 && aliasLower == parts.first()) {
            QStringList prefixes;
            QString prefix;
            for (const QChar& ch : parts.at(1)) {
                prefix += ch;
                prefixes << QRegularExpression::escape(prefix);
            }
            const QString suffixGuard = prefixes.join(QStringLiteral("|"));
            tail = QStringLiteral(R"((?![\p{L}\p{N}])(?!(?:[-\s]+(?:%1))(?:$|[^\p{L}\p{N}])))").arg(suffixGuard);
        }
    }

    return QStringLiteral(R"((?i)(?<![\p{L}\p{N}])%1%2)").arg(alias, tail);
}

void applyRules(QString& text, QVector<Rule> rules)
{
    std::sort(rules.begin(), rules.end(), [](const Rule& a, const Rule& b) {
        return a.first.size() > b.first.size();
    });

    QStringList placeholders;
    QStringList values;
    placeholders.reserve(rules.size());
    values.reserve(rules.size());

    int index = 0;
    for (const auto& rule : rules) {
        const QString placeholder = QStringLiteral("\uE000%1\uE001").arg(index++);
        const QRegularExpression re(buildRulePattern(rule));
        text.replace(re, placeholder);
        placeholders.push_back(placeholder);
        values.push_back(rule.second);
    }

    for (int i = 0; i < placeholders.size(); ++i) {
        text.replace(placeholders.at(i), values.at(i));
    }
}


QString dedupeCanonicalBrands(QString text)
{
    static const QStringList brands = VehicleNameRules::canonicalBrands();
    for (const QString& brand : brands) {
        const QString escapedBrand = QRegularExpression::escape(brand);
        const QString exactRepeatPattern = QStringLiteral(R"((?i)(?<![\p{L}\p{N}])%1(?:[-\s]+%1)+(?![\p{L}\p{N}]))").arg(escapedBrand);
        text.replace(QRegularExpression(exactRepeatPattern), brand);

        const QStringList parts = brand.split(QRegularExpression(QStringLiteral(R"([-\s]+)")), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            const QString tailToken = QRegularExpression::escape(parts.last());
            const QString tailRepeatPattern = QStringLiteral(R"((?i)(?<![\p{L}\p{N}])%1(?:[-\s]+%2)+(?![\p{L}\p{N}]))").arg(escapedBrand, tailToken);
            text.replace(QRegularExpression(tailRepeatPattern), brand);
        }
    }
    return text;
}

QString normalizeNumericSpecs(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"((?<!\d)(\d)\s*[,\.]\s*(\d)(?!\d))")), QStringLiteral("\\1.\\2"));
    text.replace(QRegularExpression(QStringLiteral(R"((?<![\p{L}\p{N}.,])(\d)\s+([Tt][Dd][Ii]|[Tt][Ff][Ss][Ii]|[Tt][Ss][Ii]|[Ff][Ss][Ii]|[Cc][Dd][Ii]|[Gg][Dd][Ii]|[Mm][Pp][Ii]|[Hh][Dd][Ii]|[dD][Cc][Ii])(?!\p{L}))")), QStringLiteral("\\1.0 \\2"));
    return text;
}

QString normalizeSpacing(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(R"(\s+([,;:/\)]))")), QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral(R"(\(\s+)")), QStringLiteral("("));
    text.replace(QRegularExpression(QStringLiteral(R"(\s+\))")), QStringLiteral(")"));
    text.replace(QRegularExpression(QStringLiteral(R"((?i)(?<![\p{L}\p{N}])MERCEDES-BENZ(?:[-\s]+BENZ)+(?![\p{L}\p{N}]))")), QStringLiteral("MERCEDES-BENZ"));
    return text.trimmed();
}

QString dedupeAdjacentRepeatedWords(QString text)
{
    // Vehicle APIs sometimes return duplicated brand/model fragments such as
    // "Volkswagen Golf Golf" or "Volkswagen Volkswagen Golf".  Collapse only
    // adjacent alphabetic tokens so technical numbers like "2.0" are untouched.
    static const QRegularExpression repeatedWordRe(
        QStringLiteral(R"((?i)(?<![\p{L}\p{N}])([\p{L}][\p{L}\p{Mn}'’\-]{1,})(?:\s+\1)+(?![\p{L}\p{N}]))"),
        QRegularExpression::UseUnicodePropertiesOption);

    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 8) {
        changed = false;
        const auto match = repeatedWordRe.match(text);
        if (match.hasMatch()) {
            text.replace(match.capturedStart(0), match.capturedLength(0), match.captured(1));
            changed = true;
        }
    }
    return text;
}

} // namespace

namespace VehicleNameUtils {

QStringList uniqueModelOptions(const QStringList& values)
{
    QStringList result;
    for (QString value : values) {
        value = value.simplified();
        // Only collapse complete repeated labels; preserve distinct trim/engine suffixes.
        const QStringList words = value.split(' ', Qt::SkipEmptyParts);
        for (int width = 1; width <= words.size() / 2; ++width) {
            if (words.size() % width) continue;
            const QString unit = words.mid(0, width).join(' ');
            bool repeated = true;
            for (int offset = width; offset < words.size(); offset += width)
                repeated &= unit.compare(words.mid(offset, width).join(' '), Qt::CaseInsensitive) == 0;
            if (repeated) { value = unit; break; }
        }
        if (!value.isEmpty() && !result.contains(value, Qt::CaseInsensitive)) result << value;
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

static QString normalizeVehicleFreeTextImpl(const QString& input, bool finalPass)
{
    QString text = input;
    if (text.trimmed().isEmpty()) return finalPass ? text.trimmed() : text;

    const bool hadTrailingSpace = !finalPass && input.endsWith(QChar(' '));

    text = VehicleNameRules::normalizeModelHints(text);
    text = normalizeNumericSpecs(text);
    applyRules(text, VehicleNameRules::makeRules());
    applyRules(text, VehicleNameRules::modelRules());
    text = VehicleNameRules::normalizeBrandSpecificModels(text);
    applyRules(text, VehicleNameRules::techRules());

    text.replace(QRegularExpression(QStringLiteral(R"((?<![\p{L}\p{N}])audi(?![\p{L}\p{N}]))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("AUDI"));
    text.replace(QRegularExpression(QStringLiteral(R"((?<![\p{L}\p{N}])bmw(?![\p{L}\p{N}]))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("BMW"));
    text = dedupeCanonicalBrands(text);
    text = dedupeAdjacentRepeatedWords(text);
    // Normalization must never infer drivetrain/equipment from a model or engine.
    text = normalizeSpacing(text);
    if (hadTrailingSpace && !text.endsWith(QChar(' '))) {
        text += QChar(' ');
    }
    return text;
}

QString normalizeVehicleFreeText(const QString& input)
{
    return normalizeVehicleFreeTextImpl(input, true);
}

QString normalizeVehicleModel(const QString& brand, const QString& model)
{
    const QString make = normalizeVehicleFreeText(brand).trimmed();
    if (make.isEmpty() || model.trimmed().isEmpty()) return normalizeVehicleFreeText(model);
    const QString vehicle = normalizeVehicleFreeText(make + QLatin1Char(' ') + model);
    const QString prefix = make + QLatin1Char(' ');
    return vehicle.startsWith(prefix, Qt::CaseInsensitive)
        ? vehicle.mid(prefix.size()).trimmed() : normalizeVehicleFreeText(model);
}

QString normalizeVehicleFreeTextProgressive(const QString& input)
{
    return normalizeVehicleFreeTextImpl(input, false);
}

} // namespace VehicleNameUtils
