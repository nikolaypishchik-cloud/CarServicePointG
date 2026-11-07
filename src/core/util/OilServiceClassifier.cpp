#include "core/util/OilServiceClassifier.h"

#include <QRegularExpression>
#include <QStringList>

namespace OilServiceClassifier {
namespace {

static bool containsAny(const QString& text, const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (!needle.isEmpty() && text.contains(needle)) return true;
    }
    return false;
}

static bool containsWholeWord(const QString& text, const QString& word)
{
    if (word.isEmpty()) return false;
    const QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                                    .arg(QRegularExpression::escape(word)),
                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return rx.match(text).hasMatch();
}

static bool containsAnyWholeWord(const QString& text, const QStringList& words)
{
    for (const QString& word : words) {
        if (containsWholeWord(text, word)) return true;
    }
    return false;
}

static bool hasViscosityGrade(const QString& text)
{
    static const QRegularExpression rx(QStringLiteral(R"((?<!\d)(\d{1,2})\s*[wв]\s*[-/]?\s*(\d{2})(?!\d))"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return rx.match(text).hasMatch();
}

static bool hasNonEngineSystemMarkers(const QString& text)
{
    static const QStringList markers = {
        QString::fromUtf8("гур"),
        QString::fromUtf8("гидроусил"),
        QString::fromUtf8("акпп"),
        QString::fromUtf8("мкпп"),
        QString::fromUtf8("кпп"),
        QString::fromUtf8("коробк"),
        QString::fromUtf8("трансмис"),
        QString::fromUtf8("редукт"),
        QString::fromUtf8("мост"),
        QString::fromUtf8("раздат"),
        QString::fromUtf8("дифф"),
        QString::fromUtf8("диф"),
        QString::fromUtf8("вариатор"),
        QString::fromUtf8("халдекс"),
        QStringLiteral("haldex"),
        QStringLiteral("dsg"),
        QStringLiteral("cvt"),
        QStringLiteral("atf"),
        QStringLiteral("psf"),
        QStringLiteral("gear oil"),
        QStringLiteral("gearbox"),
        QStringLiteral("transmission"),
        QStringLiteral("power steering")
    };
    return containsAny(text, markers);
}

static bool hasOilWords(const QString& text)
{
    static const QStringList words = {
        QString::fromUtf8("масло"),
        QString::fromUtf8("масл"),
        QString::fromUtf8("мастило"),
        QString::fromUtf8("мастил"),
        QString::fromUtf8("олива"),
        QString::fromUtf8("олив"),
        QStringLiteral("oil")
    };
    return containsAny(text, words);
}

static bool hasStandaloneOilProductWord(const QString& text)
{
    static const QStringList words = {
        QString::fromUtf8("масло"),
        QString::fromUtf8("мастило"),
        QString::fromUtf8("олива"),
        QStringLiteral("oil")
    };
    return containsAnyWholeWord(text, words);
}

static bool hasEngineOilProductMarkers(const QString& text)
{
    return containsAny(text, {
        QString::fromUtf8("мотор"),
        QString::fromUtf8("двиг"),
        QString::fromUtf8("двз"),
        QString::fromUtf8("дизел"),
        QStringLiteral("engine"),
        QStringLiteral("motor")
    });
}

static bool hasMechanicalPartMarkers(const QString& text)
{
    static const QStringList markers = {
        QString::fromUtf8("болт"),
        QString::fromUtf8("гвинт"),
        QString::fromUtf8("гайк"),
        QString::fromUtf8("шайб"),
        QString::fromUtf8("ланцюг"),
        QString::fromUtf8("цеп"),
        QString::fromUtf8("зіроч"),
        QString::fromUtf8("шестер"),
        QString::fromUtf8("натяж"),
        QString::fromUtf8("успоко"),
        QString::fromUtf8("головк"),
        QString::fromUtf8("циліндр"),
        QString::fromUtf8("цилиндр"),
        QString::fromUtf8("насос"),
        QString::fromUtf8("помпа"),
        QString::fromUtf8("радіатор"),
        QString::fromUtf8("радиатор"),
        QString::fromUtf8("патруб"),
        QString::fromUtf8("датчик"),
        QString::fromUtf8("ремін"),
        QString::fromUtf8("ремен"),
        QString::fromUtf8("ролик")
    };
    return containsAny(text, markers);
}

static bool hasEngineMarkers(const QString& text)
{
    static const QStringList markers = {
        QString::fromUtf8("двиг"),
        QString::fromUtf8("мотор"),
        QString::fromUtf8("масляне то"),
        QString::fromUtf8("масляное то"),
        QString::fromUtf8("оливне то"),
        QString::fromUtf8("оливное то"),
        QStringLiteral("engine oil"),
        QStringLiteral("motor oil")
    };
    return containsAny(text, markers) || hasViscosityGrade(text);
}

static bool isFilterLike(const QString& text)
{
    return containsAny(text, {
        QString::fromUtf8("фильтр"), QString::fromUtf8("фільтр"), QStringLiteral("filter")
    });
}

static bool isSealLike(const QString& text)
{
    return containsAny(text, {
        QString::fromUtf8("сальник"), QString::fromUtf8("проклад"), QString::fromUtf8("кільц"), QString::fromUtf8("кольц"), QStringLiteral("seal")
    });
}

} // namespace

QString normalizeOilTextForMatch(const QString& text)
{
    QString s = text.toCaseFolded().simplified();
    s.replace(QChar(0x0456), QChar(0x0438));
    s.replace(QChar(0x0457), QChar(0x0438));
    s.replace(QChar(0x0454), QChar(0x0435));
    s.replace(QChar(0x044d), QChar(0x0435));
    s.replace(QChar(0x0491), QChar(0x0433));
    return s;
}

OilWorkKind classifyOilWork(const QString& serviceTitle)
{
    const QString s = normalizeOilTextForMatch(serviceTitle);
    if (s.isEmpty()) return OilWorkKind::None;

    const bool hasOil = hasOilWords(s) || hasViscosityGrade(s);
    const bool hasTo = containsWholeWord(s, QString::fromUtf8("то"));

    if (!hasOil && !hasTo) return OilWorkKind::None;

    if (hasNonEngineSystemMarkers(s)) {
        if (containsAny(s, {QString::fromUtf8("гур"), QString::fromUtf8("гидроусил"), QStringLiteral("power steering"), QStringLiteral("psf")})) {
            return OilWorkKind::PowerSteeringFluidService;
        }
        if (containsAny(s, {QString::fromUtf8("акпп"), QStringLiteral("atf")})) {
            return OilWorkKind::AutomaticTransmissionFluidService;
        }
        if (containsAny(s, {QString::fromUtf8("мкпп"), QString::fromUtf8("кпп"), QString::fromUtf8("коробк"), QStringLiteral("gearbox")})) {
            return OilWorkKind::ManualTransmissionOilService;
        }
        if (containsAny(s, {QString::fromUtf8("раздат"), QStringLiteral("haldex")})) {
            return OilWorkKind::TransferCaseOilService;
        }
        if (containsAny(s, {QString::fromUtf8("редукт"), QString::fromUtf8("мост"), QString::fromUtf8("дифф"), QString::fromUtf8("диф")})) {
            return OilWorkKind::DifferentialOilService;
        }
        return OilWorkKind::OtherNonEngineOilService;
    }

    if (hasEngineMarkers(s)) return OilWorkKind::EngineOilService;

    if (hasOil && (containsAny(s, {QString::fromUtf8("замен"), QString::fromUtf8("замі"), QString::fromUtf8("масляне то"), QString::fromUtf8("масляное то"), QString::fromUtf8("оливне то"), QString::fromUtf8("оливное то")})
                   || s == QString::fromUtf8("замена масла")
                   || s == QString::fromUtf8("заміна масла")
                   || s == QString::fromUtf8("заміна оливи"))) {
        return OilWorkKind::EngineOilService;
    }

    if (hasTo && containsAny(s, {QString::fromUtf8("маслян"), QString::fromUtf8("масляне"), QString::fromUtf8("масляное"), QString::fromUtf8("оливн"), QString::fromUtf8("оливне"), QString::fromUtf8("оливное")})) {
        return OilWorkKind::EngineOilService;
    }

    return OilWorkKind::None;
}

bool isEngineOilServiceTitle(const QString& serviceTitle)
{
    return classifyOilWork(serviceTitle) == OilWorkKind::EngineOilService;
}

bool isNonEngineOilServiceTitle(const QString& serviceTitle)
{
    const OilWorkKind kind = classifyOilWork(serviceTitle);
    return kind != OilWorkKind::None && kind != OilWorkKind::EngineOilService;
}

bool isMotorOilProductName(const QString& partName)
{
    const QString s = normalizeOilTextForMatch(partName);
    if (s.isEmpty()) return false;
    if (isSealLike(s) || isFilterLike(s)) return false;
    if (hasNonEngineSystemMarkers(s)) return false;
    if (containsAny(s, {QString::fromUtf8("тормозн"), QString::fromUtf8("охолодж"), QString::fromUtf8("антифриз"), QStringLiteral("coolant"), QStringLiteral("brake fluid")})) return false;

    if (hasViscosityGrade(s)) return true;

    const bool hasStandaloneOilWord = hasStandaloneOilProductWord(s);
    const bool explicitMotorOil = containsAny(s, {QStringLiteral("engine oil"), QStringLiteral("motor oil")})
        || (hasStandaloneOilWord && hasEngineOilProductMarkers(s));
    if (explicitMotorOil) return true;

    if (!hasStandaloneOilWord) return false;
    if (hasMechanicalPartMarkers(s)) return false;

    return true;
}

bool isEngineOilMaintenancePartName(const QString& partName)
{
    const QString s = normalizeOilTextForMatch(partName);
    if (s.isEmpty()) return false;
    if (hasNonEngineSystemMarkers(s)) return false;
    if (isMotorOilProductName(partName)) return true;
    if (isFilterLike(s) && containsAny(s, {
            QString::fromUtf8("маслян"), QString::fromUtf8("масл"), QString::fromUtf8("оливн"), QString::fromUtf8("олив"), QString::fromUtf8("air"), QString::fromUtf8("повітр"), QString::fromUtf8("воздуш"), QString::fromUtf8("воздух"), QString::fromUtf8("салон"), QStringLiteral("cabin")
        })) {
        return true;
    }
    return false;
}

bool matchesOilWorkKindPart(OilWorkKind kind, const QString& partName)
{
    const QString s = normalizeOilTextForMatch(partName);
    if (s.isEmpty()) return false;

    switch (kind) {
    case OilWorkKind::EngineOilService:
        return isEngineOilMaintenancePartName(partName);
    case OilWorkKind::PowerSteeringFluidService:
        return containsAny(s, {QString::fromUtf8("гур"), QString::fromUtf8("гидроусил"), QStringLiteral("power steering"), QStringLiteral("psf"), QStringLiteral("steering fluid")});
    case OilWorkKind::AutomaticTransmissionFluidService:
        return containsAny(s, {QString::fromUtf8("акпп"), QStringLiteral("atf"), QStringLiteral("automatic transmission"), QStringLiteral("dsg"), QStringLiteral("cvt")});
    case OilWorkKind::ManualTransmissionOilService:
        return containsAny(s, {QString::fromUtf8("мкпп"), QString::fromUtf8("кпп"), QString::fromUtf8("коробк"), QStringLiteral("gear oil"), QStringLiteral("gearbox"), QString::fromUtf8("трансмис")});
    case OilWorkKind::TransferCaseOilService:
        return containsAny(s, {QString::fromUtf8("раздат"), QStringLiteral("haldex"), QString::fromUtf8("халдекс")});
    case OilWorkKind::DifferentialOilService:
        return containsAny(s, {QString::fromUtf8("редукт"), QString::fromUtf8("мост"), QString::fromUtf8("дифф"), QString::fromUtf8("диф")});
    case OilWorkKind::OtherNonEngineOilService:
        return hasOilWords(s) && !isMotorOilProductName(partName);
    case OilWorkKind::None:
    default:
        return false;
    }
}

} // namespace OilServiceClassifier
