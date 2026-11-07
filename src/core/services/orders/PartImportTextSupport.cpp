#include "core/util/DateFormats.h"
#include "PartImportTextSupport.h"

#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/util/FormatUtils.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>

namespace {

QString foldAutoText(QString s)
{
    s = s.trimmed().simplified().toLower();
    s.replace(QChar(0x00A0), QLatin1Char(' '));
    s.replace(QChar(0x202F), QLatin1Char(' '));
    s.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)"),
                                 QRegularExpression::UseUnicodePropertiesOption),
              QStringLiteral(" "));
    return s.simplified();
}

void replaceWholeWord(QString& text, const QString& from, const QString& to)
{
    const QRegularExpression rx(
        QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
            .arg(QRegularExpression::escape(from)),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    text.replace(rx, to);
}

QString applyOilGradeCanonicalization(QString text)
{
    static const QRegularExpression oilRx(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*([WwВв])\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    text.replace(oilRx, QStringLiteral("\\1W\\3"));
    return text;
}

QString normalizeAutoTypingMistakes(QString text)
{
    static const QList<QPair<QString, QString>> typoRules = {
        {QString::fromUtf8("фильтер"), QString::fromUtf8("фильтр")},
        {QString::fromUtf8("филтер"), QString::fromUtf8("фильтр")},
        {QString::fromUtf8("філтер"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("філтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("топлевный"), QString::fromUtf8("топливный")},
        {QString::fromUtf8("топлевного"), QString::fromUtf8("топливного")},
        {QString::fromUtf8("маслянный"), QString::fromUtf8("масляный")},
        {QString::fromUtf8("маслянного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("воздушний"), QString::fromUtf8("воздушный")},
        {QString::fromUtf8("подшибник"), QString::fromUtf8("подшипник")}
    };
    for (const auto& rule : typoRules) {
        replaceWholeWord(text, rule.first, rule.second);
    }
    return text.simplified();
}

bool isCyrillicLetter(QChar ch)
{
    if (!ch.isLetter()) return false;
    const char32_t u = ch.unicode();
    return (u >= 0x0400 && u <= 0x052F) ||
           (u >= 0x2DE0 && u <= 0x2DFF) ||
           (u >= 0xA640 && u <= 0xA69F) ||
           (u >= 0x1C80 && u <= 0x1C8F);
}

bool isLatinLetter(QChar ch)
{
    if (!ch.isLetter()) return false;
    const char32_t u = ch.unicode();
    return (u >= 0x0041 && u <= 0x005A) ||
           (u >= 0x0061 && u <= 0x007A) ||
           (u >= 0x00C0 && u <= 0x024F);
}

bool tokenContainsLetter(const QString& token)
{
    for (const QChar ch : token) {
        if (ch.isLetter()) return true;
    }
    return false;
}

const QList<QPair<QString, QString>>& autoAcronymReplacements()
{
    static const QList<QPair<QString, QString>> acronyms = {
        {QString::fromUtf8("сто"), QString::fromUtf8("СТО")},
        {QString::fromUtf8("двз"), QString::fromUtf8("ДВЗ")},
        {QString::fromUtf8("двс"), QString::fromUtf8("ДВЗ")},
        {QString::fromUtf8("грм"), QString::fromUtf8("ГРМ")},
        {QString::fromUtf8("гбц"), QString::fromUtf8("ГБЦ")},
        {QString::fromUtf8("кпп"), QString::fromUtf8("КПП")},
        {QString::fromUtf8("акпп"), QString::fromUtf8("АКПП")},
        {QString::fromUtf8("мкпп"), QString::fromUtf8("МКПП")},
        {QString::fromUtf8("ркпп"), QString::fromUtf8("РКПП")},
        {QString::fromUtf8("гур"), QString::fromUtf8("ГУР")},
        {QString::fromUtf8("егур"), QString::fromUtf8("ЕГУР")},
        {QString::fromUtf8("еур"), QString::fromUtf8("ЕУР")},
        {QString::fromUtf8("шрус"), QString::fromUtf8("ШРУС")},
        {QString::fromUtf8("тнвд"), QString::fromUtf8("ТНВД")},
        {QString::fromUtf8("тннд"), QString::fromUtf8("ТННД")},
        {QString::fromUtf8("пнвт"), QString::fromUtf8("ПНВТ")},
        {QString::fromUtf8("пннд"), QString::fromUtf8("ПННД")},
        {QString::fromUtf8("ебу"), QString::fromUtf8("ЕБУ")},
        {QString::fromUtf8("дпкв"), QString::fromUtf8("ДПКВ")},
        {QString::fromUtf8("дпрв"), QString::fromUtf8("ДПРВ")},
        {QString::fromUtf8("дмрв"), QString::fromUtf8("ДМРВ")},
        {QString::fromUtf8("дпдз"), QString::fromUtf8("ДПДЗ")},
        {QString::fromUtf8("рхх"), QString::fromUtf8("РХХ")},
        {QString::fromUtf8("гбо"), QString::fromUtf8("ГБО")},
        {QStringLiteral("abs"), QStringLiteral("ABS")},
        {QStringLiteral("ebd"), QStringLiteral("EBD")},
        {QStringLiteral("eba"), QStringLiteral("EBA")},
        {QStringLiteral("esp"), QStringLiteral("ESP")},
        {QStringLiteral("esc"), QStringLiteral("ESC")},
        {QStringLiteral("asr"), QStringLiteral("ASR")},
        {QStringLiteral("tcs"), QStringLiteral("TCS")},
        {QStringLiteral("egr"), QStringLiteral("EGR")},
        {QStringLiteral("srs"), QStringLiteral("SRS")},
        {QStringLiteral("tpms"), QStringLiteral("TPMS")},
        {QStringLiteral("adas"), QStringLiteral("ADAS")},
        {QStringLiteral("vin"), QStringLiteral("VIN")},
        {QStringLiteral("oem"), QStringLiteral("OEM")},
        {QStringLiteral("ecu"), QStringLiteral("ECU")},
        {QStringLiteral("ecm"), QStringLiteral("ECM")},
        {QStringLiteral("tcm"), QStringLiteral("TCM")},
        {QStringLiteral("bcm"), QStringLiteral("BCM")},
        {QStringLiteral("pcm"), QStringLiteral("PCM")},
        {QStringLiteral("maf"), QStringLiteral("MAF")},
        {QStringLiteral("tps"), QStringLiteral("TPS")},
        {QStringLiteral("dpf"), QStringLiteral("DPF")},
        {QStringLiteral("fap"), QStringLiteral("FAP")},
        {QStringLiteral("evap"), QStringLiteral("EVAP")},
        {QStringLiteral("cvt"), QStringLiteral("CVT")},
        {QStringLiteral("dsg"), QStringLiteral("DSG")},
        {QStringLiteral("atf"), QStringLiteral("ATF")},
        {QStringLiteral("mtf"), QStringLiteral("MTF")},
        {QStringLiteral("awd"), QStringLiteral("AWD")},
        {QStringLiteral("fwd"), QStringLiteral("FWD")},
        {QStringLiteral("rwd"), QStringLiteral("RWD")},
        {QStringLiteral("4wd"), QStringLiteral("4WD")},
        {QStringLiteral("4x4"), QStringLiteral("4x4")},
        {QStringLiteral("obd"), QStringLiteral("OBD")},
        {QStringLiteral("obd2"), QStringLiteral("OBD2")},
        {QStringLiteral("obdii"), QStringLiteral("OBD-II")}
    };
    return acronyms;
}

QString canonicalAutoAcronym(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return {};
    for (const auto& pair : autoAcronymReplacements()) {
        if (trimmed.compare(pair.first, Qt::CaseInsensitive) == 0) return pair.second;
    }
    return {};
}

QString uppercaseFirstLetter(QString token)
{
    for (int i = 0; i < token.size(); ++i) {
        if (token[i].isLetter()) {
            token[i] = token[i].toUpper();
            break;
        }
    }
    return token;
}

QString normalizeSentenceCaseToken(QString token, bool capitalizeFirstMeaningful)
{
    if (token.isEmpty() || !tokenContainsLetter(token)) return token;

    if (const QString acronym = canonicalAutoAcronym(token); !acronym.isEmpty()) return acronym;

    bool hasCyrillic = false;
    bool hasLatin = false;
    bool hasDigit = false;
    bool allLettersUpper = true;
    int letterCount = 0;
    for (const QChar ch : token) {
        if (ch.isDigit()) hasDigit = true;
        if (!ch.isLetter()) continue;
        ++letterCount;
        if (isCyrillicLetter(ch)) hasCyrillic = true;
        if (isLatinLetter(ch)) hasLatin = true;
        if (!ch.isUpper()) allLettersUpper = false;
    }

    if (letterCount > 1 && allLettersUpper && hasLatin && !hasCyrillic) return token;

    QString normalized = token.toLower();
    if (hasLatin && !hasCyrillic) {
        return uppercaseFirstLetter(normalized);
    }
    if (hasCyrillic) {
        if (capitalizeFirstMeaningful) return uppercaseFirstLetter(normalized);
        return normalized;
    }
    if (hasDigit) return uppercaseFirstLetter(normalized);
    return capitalizeFirstMeaningful ? uppercaseFirstLetter(normalized) : normalized;
}

QString sentenceCaseAutoWords(QString s)
{
    s = s.trimmed().simplified();
    if (s.isEmpty()) return s;

    QString out;
    out.reserve(s.size());
    QString token;
    bool capitalizeFirstMeaningful = true;

    auto flushToken = [&]() {
        if (token.isEmpty()) return;
        out.append(normalizeSentenceCaseToken(token, capitalizeFirstMeaningful));
        if (tokenContainsLetter(token)) capitalizeFirstMeaningful = false;
        token.clear();
    };

    auto isTokenChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('\'') || ch == QChar(0x2019) || ch == QChar(0x02BC);
    };

    for (const QChar ch : s) {
        if (isTokenChar(ch)) {
            token.append(ch);
        } else {
            flushToken();
            out.append(ch);
        }
    }
    flushToken();
    return out;
}

QString titleCaseAutoWords(QString s)
{
    s = s.trimmed().simplified();
    if (s.isEmpty()) return s;

    QString out;
    out.reserve(s.size());
    bool newWord = true;
    for (const QChar ch : s) {
        if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('/') || ch == QLatin1Char('(')) {
            out.append(ch);
            newWord = true;
            continue;
        }
        if (newWord) {
            out.append(ch.toUpper());
            newWord = false;
        } else {
            out.append(ch.toLower());
        }
    }
    return out;
}

QString uppercaseAutoAcronyms(QString text)
{
    for (const auto& pair : autoAcronymReplacements()) {
        replaceWholeWord(text, pair.first, pair.second);
    }
    return text;
}

QString translateRuUaAutoTerms(QString text)
{
    text = normalizeAutoTypingMistakes(text.simplified().trimmed());
    if (text.isEmpty()) return text;

    static const QList<QPair<QRegularExpression, QString>> phraseRules = {
        { QRegularExpression(QString::fromUtf8(R"((?:датчик\s+)?положения\s+колен(?:чатого\s+)?вала|\bдпкв\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Датчик положення колінчастого вала") },
        { QRegularExpression(QString::fromUtf8(R"((?:датчик\s+)?положения\s+распред(?:елительного)?\s+вала|\bдпрв\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Датчик положення розподільного вала") },
        { QRegularExpression(QString::fromUtf8(R"(датчик\s+массового\s+расхода\s+воздуха|\bдмрв\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Датчик масової витрати повітря") },
        { QRegularExpression(QString::fromUtf8(R"(датчик\s+положения\s+дроссельной\s+заслонки|\bдпдз\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Датчик положення дросельної заслінки") },
        { QRegularExpression(QString::fromUtf8(R"(кислородный\s+датчик|датчик\s+кислорода|лямбда\s*зонд)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Лямбда-зонд") },
        { QRegularExpression(QString::fromUtf8(R"(шаровая\s+опора|опора\s+шаровая)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Кульова опора") },
        { QRegularExpression(QString::fromUtf8(R"(стойка\s+стабилизатора)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Стійка стабілізатора") },
        { QRegularExpression(QString::fromUtf8(R"(втулка\s+стабилизатора)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Втулка стабілізатора") },
        { QRegularExpression(QString::fromUtf8(R"(подшипник\s+ступиц[ыи])"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Підшипник ступиці") },
        { QRegularExpression(QString::fromUtf8(R"(рулевая\s+рейка|рейка\s+рулевая)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Кермова рейка") },
        { QRegularExpression(QString::fromUtf8(R"(рулевой\s+наконечник|наконечник\s+рулевой\s+тяги)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Наконечник кермової тяги") },
        { QRegularExpression(QString::fromUtf8(R"(рулевая\s+тяга|тяга\s+рулевая)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Кермова тяга") },
        { QRegularExpression(QString::fromUtf8(R"(приводной\s+вал|вал\s+приводной|полуось)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Привідний вал") },
        { QRegularExpression(QString::fromUtf8(R"(пыльник\s+шрус[а]?|пыльник\s+привод[а]?)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Пильник ШРУС") },
        { QRegularExpression(QString::fromUtf8(R"(ремень\s+грм|ремня\s+грм)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Ремінь ГРМ") },
        { QRegularExpression(QString::fromUtf8(R"(цепь\s+грм|цепи\s+грм)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Ланцюг ГРМ") },
        { QRegularExpression(QString::fromUtf8(R"(выжимной\s+подшипник|подшипник\s+выжимной)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Вижимний підшипник") },
        { QRegularExpression(QString::fromUtf8(R"(главный\s+цилиндр\s+сцепления)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Головний циліндр зчеплення") },
        { QRegularExpression(QString::fromUtf8(R"(рабочий\s+цилиндр\s+сцепления)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Робочий циліндр зчеплення") },
        { QRegularExpression(QString::fromUtf8(R"(тормозной\s+шланг|шланг\s+тормозной)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Гальмівний шланг") },
        { QRegularExpression(QString::fromUtf8(R"(тормозная\s+жидкость|жидкость\s+тормозная)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Гальмівна рідина") },
        { QRegularExpression(QString::fromUtf8(R"(масло\s+моторное|моторное\s+масло)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Олива моторна") },
        { QRegularExpression(QString::fromUtf8(R"(масло\s+трансмиссионное|трансмиссионное\s+масло)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Олива трансмісійна") },
        { QRegularExpression(QString::fromUtf8(R"(масло\s+гидравлическое|гидравлическое\s+масло)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Олива гідравлічна") },
        { QRegularExpression(QString::fromUtf8(R"(свеч[аи]\s+зажигания|свечи\s+зажигания)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Свічка запалювання") },
        { QRegularExpression(QString::fromUtf8(R"(свеч[аи]\s+накала|свечи\s+накала)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Свічка розжарювання") },
        { QRegularExpression(QString::fromUtf8(R"(топливный\s+насос|насос\s+топливный|бензонасос)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Паливний насос") },
        { QRegularExpression(QString::fromUtf8(R"(насос\s+высокого\s+давления|тнвд)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Паливний насос високого тиску") },
        { QRegularExpression(QString::fromUtf8(R"(насос\s+низкого\s+давления|тннд)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Паливний насос низького тиску") },
        { QRegularExpression(QString::fromUtf8(R"(водяной\s+насос|насос\s+водяной|помпа)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Водяний насос") },
        { QRegularExpression(QString::fromUtf8(R"(сайлентблок\s+заднего\s+рычага)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Сайлентблок заднього важеля") },
        { QRegularExpression(QString::fromUtf8(R"(сайлентблок\s+переднего\s+рычага)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Сайлентблок переднього важеля") },
        { QRegularExpression(QString::fromUtf8(R"(подушка\s+двигателя|опора\s+двигателя)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Опора двигуна") },
        { QRegularExpression(QString::fromUtf8(R"(выпускной\s+коллектор|коллектор\s+выпускной)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Випускний колектор") },
        { QRegularExpression(QString::fromUtf8(R"(впускной\s+коллектор|коллектор\s+впускной)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Впускний колектор") },
        { QRegularExpression(QString::fromUtf8(R"(сажевый\s+фильтр)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Сажовий фільтр") },
        { QRegularExpression(QString::fromUtf8(R"(развал\s*[- ]?\s*схождение)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Розвал-сходження") },
        { QRegularExpression(QString::fromUtf8(R"(техническое\s+обслуживание|тех\.?\s*обслуживание|\bто\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Технічне обслуговування") }
    };
    for (const auto& rule : phraseRules) text.replace(rule.first, rule.second);

    const QList<QPair<QString, QString>> tokenRules = {
        {QString::fromUtf8("аварийный"), QString::fromUtf8("аварійний")},
        {QString::fromUtf8("аварийная"), QString::fromUtf8("аварійна")},
        {QString::fromUtf8("аккумулятор"), QString::fromUtf8("акумулятор")},
        {QString::fromUtf8("амортизатор"), QString::fromUtf8("амортизатор")},
        {QString::fromUtf8("амортизатора"), QString::fromUtf8("амортизатора")},
        {QString::fromUtf8("амортизаторов"), QString::fromUtf8("амортизаторів")},
        {QString::fromUtf8("антифриз"), QString::fromUtf8("антифриз")},
        {QString::fromUtf8("бачок"), QString::fromUtf8("бачок")},
        {QString::fromUtf8("блок"), QString::fromUtf8("блок")},
        {QString::fromUtf8("верхний"), QString::fromUtf8("верхній")},
        {QString::fromUtf8("верхняя"), QString::fromUtf8("верхня")},
        {QString::fromUtf8("верхнего"), QString::fromUtf8("верхнього")},
        {QString::fromUtf8("верхние"), QString::fromUtf8("верхні")},
        {QString::fromUtf8("вентилятор"), QString::fromUtf8("вентилятор")},
        {QString::fromUtf8("внутренний"), QString::fromUtf8("внутрішній")},
        {QString::fromUtf8("внутренняя"), QString::fromUtf8("внутрішня")},
        {QString::fromUtf8("внутреннего"), QString::fromUtf8("внутрішнього")},
        {QString::fromUtf8("водяной"), QString::fromUtf8("водяний")},
        {QString::fromUtf8("воздух"), QString::fromUtf8("повітря")},
        {QString::fromUtf8("воздуха"), QString::fromUtf8("повітря")},
        {QString::fromUtf8("воздушний"), QString::fromUtf8("повітряний")},
        {QString::fromUtf8("воздушного"), QString::fromUtf8("повітряного")},
        {QString::fromUtf8("воздушные"), QString::fromUtf8("повітряні")},
        {QString::fromUtf8("воздушных"), QString::fromUtf8("повітряних")},
        {QString::fromUtf8("воздушный"), QString::fromUtf8("повітряний")},
        {QString::fromUtf8("впускной"), QString::fromUtf8("впускний")},
        {QString::fromUtf8("впускного"), QString::fromUtf8("впускного")},
        {QString::fromUtf8("втулка"), QString::fromUtf8("втулка")},
        {QString::fromUtf8("втулки"), QString::fromUtf8("втулки")},
        {QString::fromUtf8("выжимной"), QString::fromUtf8("вижимний")},
        {QString::fromUtf8("выпускной"), QString::fromUtf8("випускний")},
        {QString::fromUtf8("выпускного"), QString::fromUtf8("випускного")},
        {QString::fromUtf8("генератор"), QString::fromUtf8("генератор")},
        {QString::fromUtf8("генератора"), QString::fromUtf8("генератора")},
        {QString::fromUtf8("герметик"), QString::fromUtf8("герметик")},
        {QString::fromUtf8("гидравлический"), QString::fromUtf8("гідравлічний")},
        {QString::fromUtf8("гидравлическое"), QString::fromUtf8("гідравлічне")},
        {QString::fromUtf8("глушитель"), QString::fromUtf8("глушник")},
        {QString::fromUtf8("главный"), QString::fromUtf8("головний")},
        {QString::fromUtf8("главная"), QString::fromUtf8("головна")},
        {QString::fromUtf8("датчик"), QString::fromUtf8("датчик")},
        {QString::fromUtf8("датчика"), QString::fromUtf8("датчика")},
        {QString::fromUtf8("давления"), QString::fromUtf8("тиску")},
        {QString::fromUtf8("двери"), QString::fromUtf8("дверей")},
        {QString::fromUtf8("двигатель"), QString::fromUtf8("двигун")},
        {QString::fromUtf8("двигателя"), QString::fromUtf8("двигуна")},
        {QString::fromUtf8("диск"), QString::fromUtf8("диск")},
        {QString::fromUtf8("диски"), QString::fromUtf8("диски")},
        {QString::fromUtf8("дроссельной"), QString::fromUtf8("дросельної")},
        {QString::fromUtf8("жидкости"), QString::fromUtf8("рідини")},
        {QString::fromUtf8("жидкость"), QString::fromUtf8("рідина")},
        {QString::fromUtf8("задний"), QString::fromUtf8("задній")},
        {QString::fromUtf8("задняя"), QString::fromUtf8("задня")},
        {QString::fromUtf8("заднего"), QString::fromUtf8("заднього")},
        {QString::fromUtf8("задние"), QString::fromUtf8("задні")},
        {QString::fromUtf8("задних"), QString::fromUtf8("задніх")},
        {QString::fromUtf8("зажигания"), QString::fromUtf8("запалювання")},
        {QString::fromUtf8("замена"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("заслонки"), QString::fromUtf8("заслінки")},
        {QString::fromUtf8("комплект"), QString::fromUtf8("комплект")},
        {QString::fromUtf8("кондиционера"), QString::fromUtf8("кондиціонера")},
        {QString::fromUtf8("кондиционер"), QString::fromUtf8("кондиціонер")},
        {QString::fromUtf8("коленвала"), QString::fromUtf8("колінвала")},
        {QString::fromUtf8("коленчатого"), QString::fromUtf8("колінчастого")},
        {QString::fromUtf8("коллектор"), QString::fromUtf8("колектор")},
        {QString::fromUtf8("коллектора"), QString::fromUtf8("колектора")},
        {QString::fromUtf8("колодка"), QString::fromUtf8("колодка")},
        {QString::fromUtf8("колодки"), QString::fromUtf8("колодки")},
        {QString::fromUtf8("кольцо"), QString::fromUtf8("кільце")},
        {QString::fromUtf8("компрессор"), QString::fromUtf8("компресор")},
        {QString::fromUtf8("конденсор"), QString::fromUtf8("конденсор")},
        {QString::fromUtf8("корпус"), QString::fromUtf8("корпус")},
        {QString::fromUtf8("крепление"), QString::fromUtf8("кріплення")},
        {QString::fromUtf8("крышка"), QString::fromUtf8("кришка")},
        {QString::fromUtf8("левый"), QString::fromUtf8("лівий")},
        {QString::fromUtf8("левая"), QString::fromUtf8("ліва")},
        {QString::fromUtf8("левого"), QString::fromUtf8("лівого")},
        {QString::fromUtf8("масляного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("маслянного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("масляный"), QString::fromUtf8("оливний")},
        {QString::fromUtf8("маслянный"), QString::fromUtf8("оливний")},
        {QString::fromUtf8("масла"), QString::fromUtf8("оливи")},
        {QString::fromUtf8("масло"), QString::fromUtf8("олива")},
        {QString::fromUtf8("моторное"), QString::fromUtf8("моторне")},
        {QString::fromUtf8("наружный"), QString::fromUtf8("зовнішній")},
        {QString::fromUtf8("наружная"), QString::fromUtf8("зовнішня")},
        {QString::fromUtf8("насос"), QString::fromUtf8("насос")},
        {QString::fromUtf8("насоса"), QString::fromUtf8("насоса")},
        {QString::fromUtf8("нижний"), QString::fromUtf8("нижній")},
        {QString::fromUtf8("нижняя"), QString::fromUtf8("нижня")},
        {QString::fromUtf8("нижнего"), QString::fromUtf8("нижнього")},
        {QString::fromUtf8("обводной"), QString::fromUtf8("обвідний")},
        {QString::fromUtf8("обгонная"), QString::fromUtf8("обгінна")},
        {QString::fromUtf8("обратная"), QString::fromUtf8("зворотна")},
        {QString::fromUtf8("охлаждения"), QString::fromUtf8("охолодження")},
        {QString::fromUtf8("передний"), QString::fromUtf8("передній")},
        {QString::fromUtf8("передняя"), QString::fromUtf8("передня")},
        {QString::fromUtf8("переднего"), QString::fromUtf8("переднього")},
        {QString::fromUtf8("передние"), QString::fromUtf8("передні")},
        {QString::fromUtf8("передних"), QString::fromUtf8("передніх")},
        {QString::fromUtf8("подвески"), QString::fromUtf8("підвіски")},
        {QString::fromUtf8("подшипник"), QString::fromUtf8("підшипник")},
        {QString::fromUtf8("подшипника"), QString::fromUtf8("підшипника")},
        {QString::fromUtf8("положение"), QString::fromUtf8("положення")},
        {QString::fromUtf8("положения"), QString::fromUtf8("положення")},
        {QString::fromUtf8("полуось"), QString::fromUtf8("піввісь")},
        {QString::fromUtf8("помпа"), QString::fromUtf8("помпа")},
        {QString::fromUtf8("поршень"), QString::fromUtf8("поршень")},
        {QString::fromUtf8("правая"), QString::fromUtf8("права")},
        {QString::fromUtf8("правого"), QString::fromUtf8("правого")},
        {QString::fromUtf8("правый"), QString::fromUtf8("правий")},
        {QString::fromUtf8("привод"), QString::fromUtf8("привід")},
        {QString::fromUtf8("приводной"), QString::fromUtf8("привідний")},
        {QString::fromUtf8("прокладка"), QString::fromUtf8("прокладка")},
        {QString::fromUtf8("прокладки"), QString::fromUtf8("прокладки")},
        {QString::fromUtf8("пружина"), QString::fromUtf8("пружина")},
        {QString::fromUtf8("пружины"), QString::fromUtf8("пружини")},
        {QString::fromUtf8("пыльник"), QString::fromUtf8("пильник")},
        {QString::fromUtf8("радиатор"), QString::fromUtf8("радіатор")},
        {QString::fromUtf8("распредвала"), QString::fromUtf8("розподільного вала")},
        {QString::fromUtf8("ремень"), QString::fromUtf8("ремінь")},
        {QString::fromUtf8("ремня"), QString::fromUtf8("ременя")},
        {QString::fromUtf8("ремкомплект"), QString::fromUtf8("ремкомплект")},
        {QString::fromUtf8("рычаг"), QString::fromUtf8("важіль")},
        {QString::fromUtf8("рычага"), QString::fromUtf8("важеля")},
        {QString::fromUtf8("салона"), QString::fromUtf8("салону")},
        {QString::fromUtf8("салонного"), QString::fromUtf8("салонного")},
        {QString::fromUtf8("салонный"), QString::fromUtf8("салонний")},
        {QString::fromUtf8("сальник"), QString::fromUtf8("сальник")},
        {QString::fromUtf8("свеча"), QString::fromUtf8("свічка")},
        {QString::fromUtf8("свечи"), QString::fromUtf8("свічки")},
        {QString::fromUtf8("свечей"), QString::fromUtf8("свічок")},
        {QString::fromUtf8("система"), QString::fromUtf8("система")},
        {QString::fromUtf8("системы"), QString::fromUtf8("системи")},
        {QString::fromUtf8("сцепления"), QString::fromUtf8("зчеплення")},
        {QString::fromUtf8("стабилизатора"), QString::fromUtf8("стабілізатора")},
        {QString::fromUtf8("стекло"), QString::fromUtf8("скло")},
        {QString::fromUtf8("стойка"), QString::fromUtf8("стійка")},
        {QString::fromUtf8("стойки"), QString::fromUtf8("стійки")},
        {QString::fromUtf8("ступица"), QString::fromUtf8("ступиця")},
        {QString::fromUtf8("ступицы"), QString::fromUtf8("ступиці")},
        {QString::fromUtf8("суппорт"), QString::fromUtf8("супорт")},
        {QString::fromUtf8("топлевного"), QString::fromUtf8("паливного")},
        {QString::fromUtf8("топлевный"), QString::fromUtf8("паливний")},
        {QString::fromUtf8("топливная"), QString::fromUtf8("паливна")},
        {QString::fromUtf8("топливного"), QString::fromUtf8("паливного")},
        {QString::fromUtf8("топливный"), QString::fromUtf8("паливний")},
        {QString::fromUtf8("тормозная"), QString::fromUtf8("гальмівна")},
        {QString::fromUtf8("тормозного"), QString::fromUtf8("гальмівного")},
        {QString::fromUtf8("тормозной"), QString::fromUtf8("гальмівний")},
        {QString::fromUtf8("тормозних"), QString::fromUtf8("гальмівних")},
        {QString::fromUtf8("тормозные"), QString::fromUtf8("гальмівні")},
        {QString::fromUtf8("тормозных"), QString::fromUtf8("гальмівних")},
        {QString::fromUtf8("трансмиссионное"), QString::fromUtf8("трансмісійне")},
        {QString::fromUtf8("трубка"), QString::fromUtf8("трубка")},
        {QString::fromUtf8("турбина"), QString::fromUtf8("турбіна")},
        {QString::fromUtf8("уплотнитель"), QString::fromUtf8("ущільнювач")},
        {QString::fromUtf8("фильтер"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("фильтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("фильтра"), QString::fromUtf8("фільтра")},
        {QString::fromUtf8("фильтры"), QString::fromUtf8("фільтри")},
        {QString::fromUtf8("філтер"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("філтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("форсунка"), QString::fromUtf8("форсунка")},
        {QString::fromUtf8("форсунки"), QString::fromUtf8("форсунки")},
        {QString::fromUtf8("цилиндр"), QString::fromUtf8("циліндр")},
        {QString::fromUtf8("цепи"), QString::fromUtf8("ланцюга")},
        {QString::fromUtf8("цепь"), QString::fromUtf8("ланцюг")},
        {QString::fromUtf8("шаровая"), QString::fromUtf8("кульова")},
        {QString::fromUtf8("шкив"), QString::fromUtf8("шків")},
        {QString::fromUtf8("шланг"), QString::fromUtf8("шланг")},
        {QString::fromUtf8("штекер"), QString::fromUtf8("штекер")},
        {QString::fromUtf8("электронный"), QString::fromUtf8("електронний")},
        {QString::fromUtf8("электрический"), QString::fromUtf8("електричний")},
        {QString::fromUtf8("и"), QString::fromUtf8("і")}
    };
    for (const auto& pair : tokenRules) {
        replaceWholeWord(text, pair.first, pair.second);
    }
    return text;
}

QString canonicalizeKnownPartName(QString value)
{
    value = normalizeAutoTypingMistakes(applyOilGradeCanonicalization(value));
    value = translateRuUaAutoTerms(value);
    value = sentenceCaseAutoWords(value);
    value = uppercaseAutoAcronyms(value);

    const QString folded = foldAutoText(value);
    if (folded.isEmpty()) return value.trimmed();

    static const QList<QPair<QRegularExpression, QString>> exactRules = {
        { QRegularExpression(QStringLiteral(R"(^(?:масло|олива|мастило)(?:\s+моторн(?:е|а))?\s+(\d{1,2})w(\d{2})$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Олива моторна \\1W\\2") },
        { QRegularExpression(QStringLiteral(R"(^(?:фільтр\s+(?:масляний|масляного|оливний|оливного)|(?:масляний|масляного|оливний|оливного)\s+фільтр)$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр оливний") },
        { QRegularExpression(QStringLiteral(R"(^(?:фільтр\s+салонний|фільтр\s+салону|салонний\s+фільтр|салону\s+фільтр)$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр салону") },
        { QRegularExpression(QStringLiteral(R"(^(?:фільтр\s+паливний|фільтр\s+паливного|паливний\s+фільтр|паливного\s+фільтр)$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр паливний") },
        { QRegularExpression(QStringLiteral(R"(^(?:фільтр\s+повітряний|фільтр\s+повітряного|фільтр\s+повітря|повітряний\s+фільтр|повітряного\s+фільтр)(?:\s+(?:двз|двигуна))?$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр повітряний ДВЗ") },
        { QRegularExpression(QStringLiteral(R"(^фільтр\s+повітря\s+салону$|^фільтр\s+салону\s+повітря$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр салону") },
        { QRegularExpression(QStringLiteral(R"(^прокладка\s+випускного\s+колектора$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Прокладка випускного колектора") },
        { QRegularExpression(QStringLiteral(R"(^прокладка\s+впускного\s+колектора$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Прокладка впускного колектора") }
    };

    for (const auto& rule : exactRules) {
        const auto match = rule.first.match(folded);
        if (!match.hasMatch()) continue;
        QString out = rule.second;
        for (int i = 1; i <= match.lastCapturedIndex(); ++i) {
            out.replace(QStringLiteral("\\%1").arg(i), match.captured(i).toUpper());
        }
        return out;
    }

    static const QList<QPair<QRegularExpression, QString>> phraseRules = {
        { QRegularExpression(QStringLiteral(R"(\bфільтр\s+повітр\w*\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Фільтр повітряний") },
        { QRegularExpression(QStringLiteral(R"(\bфільтр\s+салон\w*\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Фільтр салону") },
        { QRegularExpression(QStringLiteral(R"(\bфільтр\s+палив\w*\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Фільтр паливний") },
        { QRegularExpression(QStringLiteral(R"(\bфільтр\s+(?:масля|олив)\w*\b)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Фільтр оливний") },
        { QRegularExpression(QStringLiteral(R"(^колодк\w*\s+гальмівн\w*\s+передн\w*$)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Колодки гальмівні передні") },
        { QRegularExpression(QStringLiteral(R"(^колодк\w*\s+гальмівн\w*\s+задн\w*$)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Колодки гальмівні задні") },
        { QRegularExpression(QStringLiteral(R"(^пружина\s+амортизатор\w*\s+задн\w*$)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Пружина амортизатора задня") },
        { QRegularExpression(QStringLiteral(R"(^пружина\s+амортизатор\w*\s+передн\w*$)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption),
          QString::fromUtf8("Пружина амортизатора передня") }
    };
    for (const auto& rule : phraseRules) {
        value.replace(rule.first, rule.second);
    }

    value = value.simplified();
    value = uppercaseAutoAcronyms(sentenceCaseAutoWords(value));
    return value.trimmed();
}

} // namespace

namespace PartImportTextSupport {

QString currentDisplayDate()
{
    return QDate::currentDate().toString(QStringLiteral("dd.MM.yy"));
}

QDate parseDisplayDateWithCentury(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};

    QDate date = DateFormats::parseDate(raw);
    if (date.isValid()) return date;

    const QStringList fourDigitFormats = {
        QStringLiteral("dd.MM.yyyy"), QStringLiteral("d.M.yyyy"),
        QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/M/d"),
        QStringLiteral("dd/MM/yyyy"), QStringLiteral("d/M/yyyy"),
        QStringLiteral("MM/dd/yyyy"), QStringLiteral("M/d/yyyy")
    };
    for (const QString& format : fourDigitFormats) {
        date = QDate::fromString(raw, format);
        if (date.isValid()) return date;
    }

    const QChar sep = raw.contains(QLatin1Char('.')) ? QLatin1Char('.') : (raw.contains(QLatin1Char('/')) ? QLatin1Char('/') : QChar());
    if (!sep.isNull()) {
        const QStringList parts = raw.split(sep);
        if (parts.size() == 3) {
            bool okDay = false;
            bool okMonth = false;
            bool okYear = false;
            const int day = parts.at(0).toInt(&okDay);
            const int month = parts.at(1).toInt(&okMonth);
            int year = parts.at(2).toInt(&okYear);
            if (okDay && okMonth && okYear) {
                if (parts.at(2).size() <= 2) year += (year >= 70 ? 1900 : 2000);
                const QDate custom(year, month, day);
                if (custom.isValid()) return custom;
            }
        }
    }

    const QDateTime dtIso = DateFormats::parseDateTime(raw);
    if (dtIso.isValid()) return dtIso.date();
    return {};
}

double parseLooseNumber(const QString& raw, bool* ok)
{
    QString s = raw.trimmed();
    if (ok) *ok = false;
    if (s.isEmpty()) return 0.0;

    s.remove(QChar(0x00A0));
    s.remove(QChar(0x202F));
    s.remove(QLatin1Char(' '));

    const int lastComma = s.lastIndexOf(QLatin1Char(','));
    const int lastDot = s.lastIndexOf(QLatin1Char('.'));
    if (lastComma >= 0 && lastDot >= 0) {
        if (lastComma > lastDot) {
            s.remove(QLatin1Char('.'));
            s.replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            s.remove(QLatin1Char(','));
        }
    } else if (lastComma >= 0) {
        s.replace(QLatin1Char(','), QLatin1Char('.'));
    }

    const double value = s.toDouble(ok);
    return value;
}

QString titleCaseWords(QString s)
{
    return titleCaseAutoWords(s);
}

QString translateTechnicalRuToUa(QString s)
{
    s = normalizeAutoTypingMistakes(applyOilGradeCanonicalization(s.simplified().trimmed()));
    if (s.isEmpty()) return s;
    s = translateRuUaAutoTerms(s);
    s = uppercaseAutoAcronyms(sentenceCaseAutoWords(s));
    return s.trimmed();
}

QString normalizePartName(QString s)
{
    return canonicalizeKnownPartName(s);
}

QString normalizeRecommendations(const QString& text)
{
    static const QList<QPair<QString, QString>> rules = {
        {QString::fromUtf8("техническое обслуживание"), QString::fromUtf8("технічне обслуговування")},
        {QString::fromUtf8("технического обслуживания"), QString::fromUtf8("технічного обслуговування")},
        {QString::fromUtf8("тормозных колодок"), QString::fromUtf8("гальмівних колодок")},
        {QString::fromUtf8("тормозные колодки"), QString::fromUtf8("гальмівні колодки")},
        {QString::fromUtf8("тормозную систему"), QString::fromUtf8("гальмівну систему")},
        {QString::fromUtf8("тормозной жидкости"), QString::fromUtf8("гальмівної рідини")},
        {QString::fromUtf8("моторного масла"), QString::fromUtf8("моторної оливи")},
        {QString::fromUtf8("воздушного фильтра"), QString::fromUtf8("повітряного фільтра")},
        {QString::fromUtf8("масляного фильтра"), QString::fromUtf8("оливного фільтра")},
        {QString::fromUtf8("при необходимости"), QString::fromUtf8("за потреби")},
        {QString::fromUtf8("в ближайшее время"), QString::fromUtf8("найближчим часом")},
        {QString::fromUtf8("рекомендуется"), QString::fromUtf8("рекомендовано")},
        {QString::fromUtf8("рекомендуем"), QString::fromUtf8("рекомендуємо")},
        {QString::fromUtf8("необходимо"), QString::fromUtf8("потрібно")},
        {QString::fromUtf8("выполнить"), QString::fromUtf8("виконати")},
        {QString::fromUtf8("сделать"), QString::fromUtf8("зробити")},
          {QString::fromUtf8("делать"), QString::fromUtf8("робити")},
          {QString::fromUtf8("делатьь"), QString::fromUtf8("робити")},
          {QString::fromUtf8("перевіріти"), QString::fromUtf8("перевірити")},
        {QString::fromUtf8("проверить"), QString::fromUtf8("перевірити")},
        {QString::fromUtf8("провести"), QString::fromUtf8("провести")},
        {QString::fromUtf8("заменить"), QString::fromUtf8("замінити")},
        {QString::fromUtf8("замену"), QString::fromUtf8("заміну")},
        {QString::fromUtf8("замена"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("уровень масла"), QString::fromUtf8("рівень оливи")},
        {QString::fromUtf8("давление"), QString::fromUtf8("тиск")},
        {QString::fromUtf8("двигателя"), QString::fromUtf8("двигуна")},
        {QString::fromUtf8("охлаждения"), QString::fromUtf8("охолодження")},
        {QString::fromUtf8("жидкости"), QString::fromUtf8("рідини")},
        {QString::fromUtf8("пробега"), QString::fromUtf8("пробігу")},
        {QString::fromUtf8("обслуживание"), QString::fromUtf8("обслуговування")},
        {QString::fromUtf8("техничне"), QString::fromUtf8("технічне")},
        {QString::fromUtf8("обслуговуваня"), QString::fromUtf8("обслуговування")},
        {QString::fromUtf8("обслуговуванння"), QString::fromUtf8("обслуговування")},
        {QString::fromUtf8("гальмивних"), QString::fromUtf8("гальмівних")},
        {QString::fromUtf8("перевирити"), QString::fromUtf8("перевірити")},
        {QString::fromUtf8("заміниити"), QString::fromUtf8("замінити")}
    };
    QStringList paragraphs;
    for (QString paragraph : text.split(QLatin1Char('\n'))) {
        paragraph = paragraph.simplified();
        for (const auto& rule : rules) {
            const QRegularExpression expression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(rule.first)),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
            paragraph.replace(expression, rule.second);
        }
        paragraph = translateRuUaAutoTerms(paragraph);
        paragraph.replace(QRegularExpression(QString::fromUtf8(R"((\d)\s*(км|тис\.)(?=\b|\s|[.,;:]|$))"), QRegularExpression::UseUnicodePropertiesOption), QStringLiteral("\\1 \\2"));
        if (!paragraph.isEmpty()) paragraph[0] = paragraph[0].toUpper();
          // A paragraph may contain several sentences. Do not split decimal
          // quantities, OEMs, or common abbreviated units at their dots.
          const QRegularExpression sentenceStart(QStringLiteral("[.!?]+[\\s]+[\"«(]*([\\p{L}])"));
          auto sentences = sentenceStart.globalMatch(paragraph);
          while (sentences.hasNext()) {
              const auto match = sentences.next();
              const QString prefix = paragraph.left(match.capturedStart()).toLower();
              if (paragraph.at(match.capturedStart()) == QLatin1Char('.')
                  && QRegularExpression(QString::fromUtf8("(?:тис|шт|грн|ім|вул)$")).match(prefix).hasMatch()) continue;
              const int position = match.capturedStart(1);
              paragraph[position] = paragraph[position].toUpper();
          }
        paragraphs << paragraph;
    }
    return paragraphs.join(QLatin1Char('\n'));
}

QString normalizeImportedDateDisplay(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return QString();

    bool numericOk = false;
    const double numericValue = parseLooseNumber(raw, &numericOk);
    if (numericOk && numericValue > 20000.0 && numericValue < 90000.0) {
        const QDate excelBase(1899, 12, 30);
        const QDate converted = excelBase.addDays(qRound64(numericValue));
        if (converted.isValid()) return converted.toString(QStringLiteral("dd.MM.yy"));
    }

    const QDate parsed = parseDisplayDateWithCentury(raw);
    if (parsed.isValid()) return parsed.toString(QStringLiteral("dd.MM.yy"));
    return raw;
}

QStringList knownStoreNames()
{
    QStringList names = PartMarkupPolicyService::knownStoreNames();
    for (QString& name : names) {
        name = titleCaseWords(name);
    }
    names.removeDuplicates();
    return names;
}

} // namespace PartImportTextSupport
