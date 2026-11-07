// Extracted from src/ui/tabs/orders/WorkPartsDialogSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

    const QString folded = foldedWorkPartsPhrase(value);
    if (folded.isEmpty()) return value.trimmed();

    static const QList<QPair<QRegularExpression, QString>> exactRules = {
        { QRegularExpression(QStringLiteral(R"(^(?:(?:масло|олива)\s+)?(\d{1,2})w(\d{2})$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Олива моторна \\1W\\2") },
        { QRegularExpression(QStringLiteral(R"(^(?:масло|олива)\s+моторн(?:е|ое|а)?\s+(\d{1,2})w(\d{2})$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Олива моторна \\1W\\2") },
        { QRegularExpression(QStringLiteral(R"(^мастило\s+(\d{1,2})w(\d{2})$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Олива моторна \\1W\\2") },
        { QRegularExpression(QStringLiteral(R"(^(?:фильтр|фільтр)\s+(?:масл|олив)(?:а|яный|яний|ний|ного)?$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр оливний") },
        { QRegularExpression(QStringLiteral(R"(^(?:масл(?:яный|яний)|олив(?:ний|ного))\s+(?:фильтр|фільтр)$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр оливний") },
        { QRegularExpression(QStringLiteral(R"(^(?:фильтр|фільтр)\s+(?:воздуха|воздушный|воздушний|повітряний|повітря)(?:\s+(?:двс|двз|двигателя|двигуна))?$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр повітряний ДВЗ") },
        { QRegularExpression(QStringLiteral(R"(^(?:воздушный|воздушний|повітряний)\s+(?:фильтр|фільтр)(?:\s+(?:двс|двз|двигателя|двигуна))?$)"), QRegularExpression::CaseInsensitiveOption),
          QString::fromUtf8("Фільтр повітряний ДВЗ") },
    };

    for (const auto& rule : exactRules) {
        auto match = rule.first.match(folded);
        if (!match.hasMatch()) continue;
        QString out = rule.second;
        for (int i = 1; i <= match.lastCapturedIndex(); ++i) out.replace(QStringLiteral("\\%1").arg(i), match.captured(i).toUpper());
        return out;
    }

    return value.trimmed();
}

static QString canonicalServiceOilTitle()
{
    return QString::fromUtf8("Заміна оливи ДВЗ (проведення ТО-1)");
}

static QString translateRussianPartWordsToUkrainian(QString value)
{
    QString text = normalizeWorkPartsTypingMistakes(canonicalizeOilGradesInText(value.simplified().trimmed()));
    if (text.isEmpty()) return text;

    static const QList<QPair<QString, QString>> tokenRules = {
        {QString::fromUtf8("патрубок"), QString::fromUtf8("патрубок")},
        {QString::fromUtf8("системы"), QString::fromUtf8("системи")},
        {QString::fromUtf8("охлаждения"), QString::fromUtf8("охолодження")},
        {QString::fromUtf8("резиновый"), QString::fromUtf8("гумовий")},
        {QString::fromUtf8("резиновая"), QString::fromUtf8("гумова")},
        {QString::fromUtf8("резиновые"), QString::fromUtf8("гумові")},
        {QString::fromUtf8("резиновое"), QString::fromUtf8("гумове")},
        {QString::fromUtf8("с"), QString::fromUtf8("з")},
        {QString::fromUtf8("фитингами"), QString::fromUtf8("фітингами")},
        {QString::fromUtf8("подшипник"), QString::fromUtf8("підшипник")},
        {QString::fromUtf8("ступицы"), QString::fromUtf8("ступиці")},
        {QString::fromUtf8("фильтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("фильтра"), QString::fromUtf8("фільтра")},
        {QString::fromUtf8("салона"), QString::fromUtf8("салону")},
        {QString::fromUtf8("воздушный"), QString::fromUtf8("повітряний")},
        {QString::fromUtf8("воздуха"), QString::fromUtf8("повітря")},
        {QString::fromUtf8("масла"), QString::fromUtf8("оливи")},
        {QString::fromUtf8("масло"), QString::fromUtf8("олива")},
        {QString::fromUtf8("масляного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("масляный"), QString::fromUtf8("оливний")},
        {QString::fromUtf8("маслянного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("маслянный"), QString::fromUtf8("оливний")},
        {QString::fromUtf8("топливный"), QString::fromUtf8("паливний")},
        {QString::fromUtf8("передний"), QString::fromUtf8("передній")},
        {QString::fromUtf8("задний"), QString::fromUtf8("задній")},
        {QString::fromUtf8("правый"), QString::fromUtf8("правий")},
        {QString::fromUtf8("левый"), QString::fromUtf8("лівий")},
        {QString::fromUtf8("ремкомплект"), QString::fromUtf8("ремкомплект")},
        {QString::fromUtf8("тормозной"), QString::fromUtf8("гальмівний")},
        {QString::fromUtf8("тормозные"), QString::fromUtf8("гальмівні")},
        {QString::fromUtf8("колодки"), QString::fromUtf8("колодки")},
        {QString::fromUtf8("диск"), QString::fromUtf8("диск")},
        {QString::fromUtf8("диски"), QString::fromUtf8("диски")},
        {QString::fromUtf8("ролик"), QString::fromUtf8("ролик")},
        {QString::fromUtf8("натяжителя"), QString::fromUtf8("натягувача")},
        {QString::fromUtf8("генератора"), QString::fromUtf8("генератора")},
        {QString::fromUtf8("помпа"), QString::fromUtf8("помпа")},
        {QString::fromUtf8("свеча"), QString::fromUtf8("свічка")},
        {QString::fromUtf8("свечи"), QString::fromUtf8("свічки")},
        {QString::fromUtf8("прокладка"), QString::fromUtf8("прокладка")},
        {QString::fromUtf8("шланг"), QString::fromUtf8("шланг")},
        {QString::fromUtf8("комплект"), QString::fromUtf8("комплект")},
        {QString::fromUtf8("и"), QString::fromUtf8("і")}
    };

    for (const auto& rule : tokenRules) {
        QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                                  .arg(QRegularExpression::escape(rule.first)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        text.replace(rx, rule.second);
    }
    return text;
}

QString normalizePartSentence(const QString& value)
{
    return PartImportTextSupport::normalizePartName(value);
}

static QString applyProfessionalServicePhraseDictionary(QString text)
{
    if (text.trimmed().isEmpty()) return text;

    static const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
    static const QList<QPair<QRegularExpression, QString>> phraseRules = {
        { QRegularExpression(QString::fromUtf8(R"((?:развал|розвал)\s*[- ]?\s*(?:схождение|сходження))"), options),
          QString::fromUtf8("Розвал-сходження") },
        { QRegularExpression(QString::fromUtf8(R"(сход\s*[- ]?\s*развал)"), options),
          QString::fromUtf8("Розвал-сходження") },
        { QRegularExpression(QString::fromUtf8(R"((?:техническое|тех\.?|плановое)\s+обслуживание|плановое\s+то)"), options),
          QString::fromUtf8("Планове технічне обслуговування") },
        { QRegularExpression(QString::fromUtf8(R"(компьютерн(?:ая|ой)\s+диагностик[аи])"), options),
          QString::fromUtf8("Комп'ютерна діагностика") },
        { QRegularExpression(QString::fromUtf8(R"(диагностик[аи]\s+(?:ходовой|подвески|ходової|підвіски))"), options),
          QString::fromUtf8("Діагностика ходової частини") },
        { QRegularExpression(QString::fromUtf8(R"(ремонт\s+(?:ходовой|подвески|ходової|підвіски))"), options),
          QString::fromUtf8("Ремонт ходової частини") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+масл[ао]\s+(?:двигателя|в\s+двигателе|двс|двз|двигуна))"), options),
          QString::fromUtf8("Заміна оливи ДВЗ") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+маслян(?:ого|ый)\s+фильтр[а]?)"), options),
          QString::fromUtf8("Заміна оливного фільтра") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+воздушн(?:ого|ый)\s+фильтр[а]?)"), options),
          QString::fromUtf8("Заміна повітряного фільтра ДВЗ") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+салонн(?:ого|ый)\s+фильтр[а]?)"), options),
          QString::fromUtf8("Заміна фільтра салону") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+топливн(?:ого|ый)\s+фильтр[а]?)"), options),
          QString::fromUtf8("Заміна паливного фільтра") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+тормозн(?:ых|ые)\s+колод(?:ок|ки))"), options),
          QString::fromUtf8("Заміна гальмівних колодок") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+тормозн(?:ых|ые)\s+диск(?:ов|и|и))"), options),
          QString::fromUtf8("Заміна гальмівних дисків") },
        { QRegularExpression(QString::fromUtf8(R"(прокачк[ауы]\s+тормозн(?:ой|ую)\s+систем[ыу])"), options),
          QString::fromUtf8("Прокачування гальмівної системи") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+тормозн(?:ой|ую)\s+жидкост[иь])"), options),
          QString::fromUtf8("Заміна гальмівної рідини") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+антифриз[а]?)"), options),
          QString::fromUtf8("Заміна антифризу") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+охлаждающ(?:ей|ую)\s+жидкост[иь])"), options),
          QString::fromUtf8("Заміна охолоджувальної рідини") },
        { QRegularExpression(QString::fromUtf8(R"(заправк[ауы]\s+кондиционер[а]?)"), options),
          QString::fromUtf8("Заправка кондиціонера") },
        { QRegularExpression(QString::fromUtf8(R"(диагностик[аи]\s+кондиционер[а]?)"), options),
          QString::fromUtf8("Діагностика кондиціонера") },
        { QRegularExpression(QString::fromUtf8(R"(ремонт\s+кондиционер[а]?)"), options),
          QString::fromUtf8("Ремонт кондиціонера") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+ремн[яь]\s+грм)"), options),
          QString::fromUtf8("Заміна ременя ГРМ") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+цеп[ииь]\s+грм)"), options),
          QString::fromUtf8("Заміна ланцюга ГРМ") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+ремн[яь]\s+генератор[а]?)"), options),
          QString::fromUtf8("Заміна ременя генератора") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+свеч(?:ей|и)\s+зажигания)"), options),
          QString::fromUtf8("Заміна свічок запалювання") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+свеч(?:ей|и)\s+накала)"), options),
          QString::fromUtf8("Заміна свічок розжарювання") },
        { QRegularExpression(QString::fromUtf8(R"(ремонт\s+тормозн(?:ой|ої)\s+систем[ыи])"), options),
          QString::fromUtf8("Ремонт гальмівної системи") },
        { QRegularExpression(QString::fromUtf8(R"(ремонт\s+рулев(?:ой|ої)\s+рейк[и])"), options),
          QString::fromUtf8("Ремонт кермової рейки") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+рулев(?:ой|ої)\s+рейк[и])"), options),
          QString::fromUtf8("Заміна кермової рейки") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+сцепления)"), options),
          QString::fromUtf8("Заміна зчеплення") },
        { QRegularExpression(QString::fromUtf8(R"(чистк[ауы]\s+дроссельн(?:ой|ую)\s+заслонк[иу])"), options),
          QString::fromUtf8("Очищення дросельної заслінки") },
        { QRegularExpression(QString::fromUtf8(R"(адаптац(?:ия|ії)\s+дроссельн(?:ой|ої)\s+заслонк[и])"), options),
          QString::fromUtf8("Адаптація дросельної заслінки") },
        { QRegularExpression(QString::fromUtf8(R"(чистк[ауы]\s+клапан[а]?\s+egr)"), options),
          QString::fromUtf8("Очищення клапана EGR") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+подшипник[а]?\s+ступиц[ыи])"), options),
          QString::fromUtf8("Заміна підшипника маточини") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+шаров(?:ой|ую)\s+опор[уы])"), options),
          QString::fromUtf8("Заміна кульової опори") },
        { QRegularExpression(QString::fromUtf8(R"(замен[аы]\s+стойк[иу]\s+стабилизатор[а]?)"), options),
          QString::fromUtf8("Заміна стійки стабілізатора") },
        { QRegularExpression(QString::fromUtf8(R"(капитальн(?:ый|ий)\s+ремонт\s+двигател[я])"), options),
          QString::fromUtf8("Капітальний ремонт двигуна") },
        { QRegularExpression(QString::fromUtf8(R"(ремонт\s+двигател[я])"), options),
          QString::fromUtf8("Ремонт двигуна") },
        { QRegularExpression(QString::fromUtf8(R"(диагностик[аи]\s+двигател[я])"), options),
          QString::fromUtf8("Діагностика двигуна") },
        { QRegularExpression(QString::fromUtf8(R"(диагностик[аи]\s+электрооборудования)"), options),
          QString::fromUtf8("Діагностика електрообладнання") },
        { QRegularExpression(QString::fromUtf8(R"(проверк[аи]\s+уровн[я]\s+масл[ао])"), options),
          QString::fromUtf8("Перевірка рівня оливи") },
        { QRegularExpression(QString::fromUtf8(R"(сброс\s+сервисн(?:ого|ый)\s+интервал[а]?)"), options),
          QString::fromUtf8("Скидання сервісного інтервалу") },
        { QRegularExpression(QString::fromUtf8(R"(калибровк[аи]\s+датчик[а]?\s+угл[а]\s+рул[я])"), options),
          QString::fromUtf8("Калібрування датчика кута керма") }
    };

    for (const auto& rule : phraseRules) text.replace(rule.first, rule.second);
    return text;
}

static QString translateRussianServiceWordsToUkrainian(QString value)
{
    QString text = normalizeWorkPartsTypingMistakes(canonicalizeOilGradesInText(value.simplified().trimmed()));
    if (text.isEmpty()) return text;

    text = applyProfessionalServicePhraseDictionary(text);

    static const QList<QPair<QString, QString>> tokenRules = {
        {QString::fromUtf8("аварийный"), QString::fromUtf8("аварійний")},
        {QString::fromUtf8("аварийная"), QString::fromUtf8("аварійна")},
        {QString::fromUtf8("адаптация"), QString::fromUtf8("адаптація")},
        {QString::fromUtf8("аккумулятора"), QString::fromUtf8("акумулятора")},
        {QString::fromUtf8("аккумулятор"), QString::fromUtf8("акумулятор")},
        {QString::fromUtf8("амортизатора"), QString::fromUtf8("амортизатора")},
        {QString::fromUtf8("амортизаторов"), QString::fromUtf8("амортизаторів")},
        {QString::fromUtf8("амортизатор"), QString::fromUtf8("амортизатор")},
        {QString::fromUtf8("антифриза"), QString::fromUtf8("антифризу")},
        {QString::fromUtf8("балансировка"), QString::fromUtf8("балансування")},
        {QString::fromUtf8("болта"), QString::fromUtf8("болта")},
        {QString::fromUtf8("вакуумирование"), QString::fromUtf8("вакуумування")},
        {QString::fromUtf8("вентилятора"), QString::fromUtf8("вентилятора")},
        {QString::fromUtf8("верхнего"), QString::fromUtf8("верхнього")},
        {QString::fromUtf8("верхний"), QString::fromUtf8("верхній")},
        {QString::fromUtf8("внутреннего"), QString::fromUtf8("внутрішнього")},
        {QString::fromUtf8("внутренний"), QString::fromUtf8("внутрішній")},
        {QString::fromUtf8("водяного"), QString::fromUtf8("водяного")},
        {QString::fromUtf8("восстановление"), QString::fromUtf8("відновлення")},
        {QString::fromUtf8("впускного"), QString::fromUtf8("впускного")},
        {QString::fromUtf8("впускной"), QString::fromUtf8("впускний")},
        {QString::fromUtf8("втулки"), QString::fromUtf8("втулки")},
        {QString::fromUtf8("втулка"), QString::fromUtf8("втулка")},
        {QString::fromUtf8("выжимного"), QString::fromUtf8("вижимного")},
        {QString::fromUtf8("выпускного"), QString::fromUtf8("випускного")},
        {QString::fromUtf8("выпускной"), QString::fromUtf8("випускний")},
        {QString::fromUtf8("генератора"), QString::fromUtf8("генератора")},
        {QString::fromUtf8("гидравлической"), QString::fromUtf8("гідравлічної")},
        {QString::fromUtf8("гидравлический"), QString::fromUtf8("гідравлічний")},
        {QString::fromUtf8("глушителя"), QString::fromUtf8("глушника")},
        {QString::fromUtf8("датчика"), QString::fromUtf8("датчика")},
        {QString::fromUtf8("двигателя"), QString::fromUtf8("двигуна")},
        {QString::fromUtf8("двигатель"), QString::fromUtf8("двигун")},
        {QString::fromUtf8("диагностика"), QString::fromUtf8("діагностика")},
        {QString::fromUtf8("диагностики"), QString::fromUtf8("діагностики")},
        {QString::fromUtf8("дисков"), QString::fromUtf8("дисків")},
        {QString::fromUtf8("диск"), QString::fromUtf8("диск")},
        {QString::fromUtf8("дроссельной"), QString::fromUtf8("дросельної")},
        {QString::fromUtf8("жидкости"), QString::fromUtf8("рідини")},
        {QString::fromUtf8("жидкость"), QString::fromUtf8("рідина")},
        {QString::fromUtf8("заднего"), QString::fromUtf8("заднього")},
        {QString::fromUtf8("задний"), QString::fromUtf8("задній")},
        {QString::fromUtf8("задних"), QString::fromUtf8("задніх")},
        {QString::fromUtf8("зажигания"), QString::fromUtf8("запалювання")},
        {QString::fromUtf8("замена"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("замены"), QString::fromUtf8("заміни")},
        {QString::fromUtf8("заправка"), QString::fromUtf8("заправка")},
        {QString::fromUtf8("заслонки"), QString::fromUtf8("заслінки")},
        {QString::fromUtf8("измерение"), QString::fromUtf8("вимірювання")},
        {QString::fromUtf8("калибровка"), QString::fromUtf8("калібрування")},
        {QString::fromUtf8("клапана"), QString::fromUtf8("клапана")},
        {QString::fromUtf8("колеса"), QString::fromUtf8("колеса")},
        {QString::fromUtf8("колодок"), QString::fromUtf8("колодок")},
        {QString::fromUtf8("колодки"), QString::fromUtf8("колодки")},
        {QString::fromUtf8("компьютерная"), QString::fromUtf8("комп'ютерна")},
        {QString::fromUtf8("кондиционера"), QString::fromUtf8("кондиціонера")},
        {QString::fromUtf8("контроль"), QString::fromUtf8("контроль")},
        {QString::fromUtf8("крепления"), QString::fromUtf8("кріплення")},
        {QString::fromUtf8("кузовных"), QString::fromUtf8("кузовних")},
        {QString::fromUtf8("лакокрасочного"), QString::fromUtf8("лакофарбового")},
        {QString::fromUtf8("левого"), QString::fromUtf8("лівого")},
        {QString::fromUtf8("левый"), QString::fromUtf8("лівий")},
        {QString::fromUtf8("масла"), QString::fromUtf8("оливи")},
        {QString::fromUtf8("масляного"), QString::fromUtf8("оливного")},
        {QString::fromUtf8("масляный"), QString::fromUtf8("оливний")},
        {QString::fromUtf8("мойка"), QString::fromUtf8("мийка")},
        {QString::fromUtf8("мотора"), QString::fromUtf8("мотора")},
        {QString::fromUtf8("нижнего"), QString::fromUtf8("нижнього")},
        {QString::fromUtf8("нижний"), QString::fromUtf8("нижній")},
        {QString::fromUtf8("обслуживание"), QString::fromUtf8("обслуговування")},
        {QString::fromUtf8("опоры"), QString::fromUtf8("опори")},
        {QString::fromUtf8("охлаждающей"), QString::fromUtf8("охолоджувальної")},
        {QString::fromUtf8("охлаждения"), QString::fromUtf8("охолодження")},
        {QString::fromUtf8("переднего"), QString::fromUtf8("переднього")},
        {QString::fromUtf8("передний"), QString::fromUtf8("передній")},
        {QString::fromUtf8("передних"), QString::fromUtf8("передніх")},
        {QString::fromUtf8("плановое"), QString::fromUtf8("планове")},
        {QString::fromUtf8("подвески"), QString::fromUtf8("підвіски")},
        {QString::fromUtf8("подшипника"), QString::fromUtf8("підшипника")},
        {QString::fromUtf8("проверка"), QString::fromUtf8("перевірка")},
        {QString::fromUtf8("прокачка"), QString::fromUtf8("прокачування")},
        {QString::fromUtf8("промывка"), QString::fromUtf8("промивання")},
        {QString::fromUtf8("правого"), QString::fromUtf8("правого")},
        {QString::fromUtf8("правый"), QString::fromUtf8("правий")},
        {QString::fromUtf8("разборка"), QString::fromUtf8("розбирання")},
        {QString::fromUtf8("регулировка"), QString::fromUtf8("регулювання")},
        {QString::fromUtf8("рейки"), QString::fromUtf8("рейки")},
        {QString::fromUtf8("ремень"), QString::fromUtf8("ремінь")},
        {QString::fromUtf8("ремня"), QString::fromUtf8("ременя")},
        {QString::fromUtf8("ремонт"), QString::fromUtf8("ремонт")},
        {QString::fromUtf8("рулевого"), QString::fromUtf8("кермового")},
        {QString::fromUtf8("рулевой"), QString::fromUtf8("кермової")},
        {QString::fromUtf8("салона"), QString::fromUtf8("салону")},
        {QString::fromUtf8("салонного"), QString::fromUtf8("салонного")},
        {QString::fromUtf8("сервисного"), QString::fromUtf8("сервісного")},
        {QString::fromUtf8("системы"), QString::fromUtf8("системи")},
        {QString::fromUtf8("снятие"), QString::fromUtf8("зняття")},
        {QString::fromUtf8("смазка"), QString::fromUtf8("змащення")},
        {QString::fromUtf8("сошника"), QString::fromUtf8("сошника")},
        {QString::fromUtf8("сцепления"), QString::fromUtf8("зчеплення")},
        {QString::fromUtf8("стойки"), QString::fromUtf8("стійки")},
        {QString::fromUtf8("ступицы"), QString::fromUtf8("маточини")},
        {QString::fromUtf8("техническое"), QString::fromUtf8("технічне")},
        {QString::fromUtf8("топливного"), QString::fromUtf8("паливного")},
        {QString::fromUtf8("топливный"), QString::fromUtf8("паливний")},
        {QString::fromUtf8("тормозная"), QString::fromUtf8("гальмівна")},
        {QString::fromUtf8("тормозного"), QString::fromUtf8("гальмівного")},
        {QString::fromUtf8("тормозной"), QString::fromUtf8("гальмівної")},
        {QString::fromUtf8("тормозные"), QString::fromUtf8("гальмівні")},
        {QString::fromUtf8("тормозных"), QString::fromUtf8("гальмівних")},
        {QString::fromUtf8("установка"), QString::fromUtf8("встановлення")},
        {QString::fromUtf8("уровня"), QString::fromUtf8("рівня")},
        {QString::fromUtf8("фильтра"), QString::fromUtf8("фільтра")},
        {QString::fromUtf8("фильтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("ходовой"), QString::fromUtf8("ходової частини")},
        {QString::fromUtf8("чистка"), QString::fromUtf8("очищення")},
        {QString::fromUtf8("шаровой"), QString::fromUtf8("кульової")},
        {QString::fromUtf8("электрики"), QString::fromUtf8("електрики")},
        {QString::fromUtf8("электрооборудования"), QString::fromUtf8("електрообладнання")},
        {QString::fromUtf8("электронный"), QString::fromUtf8("електронний")},
        {QString::fromUtf8("и"), QString::fromUtf8("і")}
     };

    for (const auto& rule : tokenRules) {
        QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                                  .arg(QRegularExpression::escape(rule.first)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        text.replace(rx, rule.second);
    }

    text = applyProfessionalServicePhraseDictionary(text);
    return text;
}

static QString canonicalizeKnownServiceTitle(QString value)
{
    value = sentenceCaseWorkPartsText(translateRussianServiceWordsToUkrainian(value));
    const QString folded = foldedWorkPartsPhrase(value);
    if (folded.isEmpty()) return value.trimmed();

    if (folded == foldedWorkPartsPhrase(canonicalServiceOilTitle())) return canonicalServiceOilTitle();

    static const QList<QRegularExpression> oilServiceRules = {
        QRegularExpression(QStringLiteral(R"(^зам[иеі]на\s+(?:масл[ао]|олив[аи])$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(^зам[иеі]на\s+(?:масл[ао]|олив[аи])\s+(?:двс|двз)$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(^заміна\s+мастил[ао]$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(^oil\s+service$)"), QRegularExpression::CaseInsensitiveOption),
    };
    for (const auto& rx : oilServiceRules) {
        if (rx.match(folded).hasMatch()) return canonicalServiceOilTitle();
    }

    if (folded.startsWith(QString::fromUtf8("заміна масла двс")) ||
        folded.startsWith(QString::fromUtf8("замена масла двс")) ||
        folded.startsWith(QString::fromUtf8("заміна оливи двз")) ||
        folded.startsWith(QString::fromUtf8("заміна оливи двс")) ||
        (folded.startsWith(QString::fromUtf8("заміна масла")) && folded.contains(QString::fromUtf8("то 1"))) ||
        (folded.startsWith(QString::fromUtf8("заміна оливи")) && folded.contains(QString::fromUtf8("то 1")))) {
        return canonicalServiceOilTitle();
    }

    return sentenceCaseWorkPartsText(value.trimmed());
}

QString normalizeSentence(const QString& s)
{
    QString t = PartImportTextSupport::translateTechnicalRuToUa(normalizeWorkPartsTypingMistakes(s.simplified().trimmed()));
    t = sentenceCaseWorkPartsText(canonicalizeOilGradesInText(t));
    if (t.isEmpty()) return t;
    t = canonicalizeOilGradesInText(applyUppercaseAcronyms(t));
    return canonicalizeKnownPartName(t);
}

QString canonicalizeServiceTitle(const QString& title)
{
    QString normalized = canonicalizeKnownServiceTitle(normalizeSentence(title));
    if (normalized.isEmpty()) return normalized;
    if (isOilServiceTitle(normalized)) return canonicalServiceOilTitle();
    return normalized;
}

QString currentOrderDateDisplay()
{
    return QDate::currentDate().toString(QStringLiteral("dd.MM.yy"));
}

QString mainDialogTextColor()
{
    return ThemeManager::currentTheme() == QStringLiteral("light") ? QStringLiteral("#101214") : QStringLiteral("#f5f7fa");
}

static int calcMainPartsRowHeight(QTableWidget* table, int row);
void updateSingleMainPartsRowHeight(QTableWidget* table, int row);
static void updateAllMainPartsRowHeights(QTableWidget* table);

QString partManufacturerText(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return QString();
    if (auto* brandItem = table->item(row, PartColBrand)) {
        const QString brand = normalizeSentence(brandItem->text()).trimmed();
        if (!brand.isEmpty()) return brand;
    }
    if (auto* nameItem = table->item(row, PartColName)) {
        return normalizeSentence(nameItem->data(RolePartManufacturer).toString()).trimmed();
    }
    return QString();
}

void syncPartManufacturerRoleFromRow(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    if (auto* nameItem = table->item(row, PartColName)) {
        const QSignalBlocker blocker(table);
        nameItem->setData(RolePartManufacturer, partManufacturerText(table, row));
    }
}

QString displayDateFromStored(const QString& raw);
QString storedDateFromDisplay(const QString& raw);
static PartMarkupPolicyService::PolicySnapshot& cachedPricingPolicySnapshot()
{
    static PartMarkupPolicyService::PolicySnapshot snapshot = PartMarkupPolicyService::loadPolicySnapshot();
    static quint64 cachedRevision = FinanceSettingsService::markupSettingsRevision();
    const quint64 currentRevision = FinanceSettingsService::markupSettingsRevision();
    if (currentRevision != cachedRevision) {
        snapshot = PartMarkupPolicyService::loadPolicySnapshot();
        cachedRevision = currentRevision;
    }
    return snapshot;
}

static QString pricingGuardKey(QTableWidget* table, int row)
{
    return QStringLiteral("%1:%2").arg(reinterpret_cast<quintptr>(table)).arg(row);
}

static void applyPricingResultToPriceCell(QTableWidget* table,
                                          int row,
                                          QTableWidgetItem* priceItem,
                                          QTableWidgetItem* amountItem,
                                          const PartPricingResult& pricing)
{
    if (!table || !priceItem || !amountItem) return;
    const QSignalBlocker blocker(table);
    priceItem->setText(pricing.markupSource=="inventory" ? FormatUtils::money(pricing.clientPrice,2) : moneyText(pricing.clientPrice));
    priceItem->setData(RolePurchasePrice, pricing.purchasePrice);
    priceItem->setData(RoleMarkupPercent, pricing.manualMarkupOverride ? pricing.markupPercent : 0.0);
    priceItem->setData(RoleManualMarkupOverride, pricing.manualMarkupOverride);
    priceItem->setToolTip(QString::fromUtf8("%1: %2 %. Закупівля: %3 грн. Клієнту: %4 грн.")
        .arg(pricing.manualMarkupOverride ? QString::fromUtf8("Індивідуальна націнка") : QString::fromUtf8("Глобальна націнка"))
        .arg(pricing.markupPercent, 0, 'f', 1)
        .arg(pricing.purchasePrice, 0, 'f', 2)
        .arg(pricing.clientPrice, 0, 'f', 2));
    amountItem->setText(pricing.markupSource=="inventory" ? FormatUtils::money(pricing.clientAmount,2) : moneyText(pricing.clientAmount));
}

void refreshPartRowClientPricing(QTableWidget* table, int row, int changedColumn)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    static QSet<QString> pricingGuard;
    const QString guardKey = pricingGuardKey(table, row);
    if (pricingGuard.contains(guardKey)) return;
    pricingGuard.insert(guardKey);
    const auto guardCleanup = qScopeGuard([&]() { pricingGuard.remove(guardKey); });

    auto* priceItem = table->item(row, PartColPrice);
    auto* qtyItem = table->item(row, PartColQty);
    auto* nameItem = table->item(row, PartColName);
    auto* storeItem = table->item(row, PartColStore);
    auto* amountItem = table->item(row, PartColAmount);
    if (!priceItem || !qtyItem || !amountItem) return;

    bool okQty = false;
    const double qtyValue = parseLooseNumber(qtyItem->text().trimmed(), &okQty);
    const double safeQty = okQty && qtyValue > 0.0 ? qtyValue : 1.0;

    const QString partName = nameItem ? WorkshopInventoryService::canonicalMaterialName(normalizeSentence(nameItem->text())) : QString();
    QString store = storeItem ? normalizeSentence(storeItem->text()) : QString();
    // Capture the mode once when a new row acquires a name, not when it is saved.
    constexpr int sourceCapturedRole = Qt::UserRole + 420;
    if (nameItem && !partName.isEmpty() && nameItem->data(RoleId).toInt() <= 0
        && !nameItem->data(sourceCapturedRole).toBool() && !partRowHasImportedInvoiceMarker(table, row)) {
        const QSignalBlocker blocker(table);
        nameItem->setData(sourceCapturedRole, true);
        if (table->property("csCustomerPartsMode").toBool()) {
            store = PartMarkupPolicyService::customerPartsStore();
            if (!storeItem) { storeItem = new QTableWidgetItem(); table->setItem(row, PartColStore, storeItem); }
            storeItem->setText(store);
        }
    }
    if (PartMarkupPolicyService::isCustomerSupplied(store)) {
        const QSignalBlocker blocker(table);
        priceItem->setText(QString());
        amountItem->setText(QString());
        priceItem->setData(RolePurchasePrice, 0.0);
        priceItem->setData(RoleMarkupPercent, 0.0);
        priceItem->setData(RoleManualMarkupOverride, false);
        priceItem->setToolTip(QString::fromUtf8("Запчастина клієнта. Не входить у вартість наряду та витрати складу."));
        return;
    }
    const auto policy = store.isEmpty() || PartMarkupPolicyService::isInventoryStore(store)
        ? PartMarkupPolicyService::loadPolicySnapshot() : cachedPricingPolicySnapshot();
    if (store.isEmpty() && !partRowHasImportedInvoiceMarker(table, row)
        && policy.inventoryClientPrices.contains(WorkshopInventoryService::normalizedMaterialKey(partName))) {
        const QSignalBlocker blocker(table);
        store = QString::fromUtf8("База");
        if (!storeItem) { storeItem = new QTableWidgetItem(); table->setItem(row, PartColStore, storeItem); }
        storeItem->setText(store);
    }

    if (nameItem && PartMarkupPolicyService::isInventoryStore(store)
        && !partRowHasImportedInvoiceMarker(table, row) && nameItem->text() != partName) {
        const QSignalBlocker blocker(table);
        nameItem->setText(partName);
    }

    const QString rawPriceText = priceItem->text().trimmed();
    bool okTypedPrice = false;
    const double typedPrice = parseLooseNumber(rawPriceText, &okTypedPrice);
    const double storedPurchase = priceItem->data(RolePurchasePrice).toDouble();

    const bool hasExplicitPriceInput = !rawPriceText.isEmpty() && okTypedPrice;
    const double purchasePrice = (changedColumn == PartColPrice && hasExplicitPriceInput)
        ? qMax(0.0, typedPrice)
        : qMax(0.0, storedPurchase);

    if (purchasePrice <= 0.0 && rawPriceText.isEmpty() && !PartMarkupPolicyService::isInventoryStore(store)) {
        const QSignalBlocker blocker(table);
        priceItem->setText(QString());
        amountItem->setText(QString());
        priceItem->setData(RolePurchasePrice, 0.0);
        priceItem->setData(RoleMarkupPercent, 0.0);
        priceItem->setData(RoleManualMarkupOverride, false);
        priceItem->setToolTip(QString());
        return;
    }

    bool manualMarkupOk = false;
    const double manualMarkup = priceItem->data(RoleMarkupPercent).toDouble(&manualMarkupOk);
    const bool manualOverride = priceItem->data(RoleManualMarkupOverride).toBool() && manualMarkupOk && manualMarkup >= 0.0;

    PartPricingInput pricingInput;
    pricingInput.partName = partName;
    pricingInput.store = store;
    pricingInput.qty = safeQty;
    pricingInput.purchasePrice = purchasePrice;
    pricingInput.manualMarkupOverride = manualOverride;
    pricingInput.manualMarkupPercent = manualOverride ? qMax(0.0, manualMarkup) : 0.0;
    pricingInput.sourceType = partRowHasImportedInvoiceMarker(table, row) ? QStringLiteral("import") : QStringLiteral("manual_inline");

    const PartPricingResult pricing = PartPricingEngine::calculate(pricingInput, policy);
    applyPricingResultToPriceCell(table, row, priceItem, amountItem, pricing);
}

// importedPartTraceId is provided by WorkPartsInlineEditorController.h through the module prelude.


bool partRowHasImportedInvoiceMarker(QTableWidget* table, int row);

static QString partCrashCellText(QTableWidget* table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return QString();
    if (auto* item = table->item(row, col)) return item->text().trimmed();
    return QString();
}

static QString partCrashCellDataText(QTableWidget* table, int row, int col, int role)
{
    if (!table || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return QString();
    if (auto* item = table->item(row, col)) return item->data(role).toString().trimmed();
    return QString();
}

static int partCrashCellId(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return 0;
    for (int col : {PartColName, PartColOrderedAt, PartColOem, PartColQty, PartColPrice, PartColStore, PartColBrand, PartColInvoiceNumber, PartColAttachment}) {
        if (auto* item = table->item(row, col)) {
            const int id = item->data(RoleId).toInt();
            if (id > 0) return id;
        }
    }
    return 0;
}

void logPartCrashContext(const QString& category,
                                const QString& stage,
                                QTableWidget* table,
                                int row,
                                int col,
                                int orderNumber,
                                const QString& orderUid,
                                const QString& extra)
{
    const bool imported = partRowHasImportedInvoiceMarker(table, row);
    qCDebug(csWorkPartsTraceLog).noquote() << category
                      << QStringLiteral(" stage=") << stage
                      << QStringLiteral(" order=") << orderNumber
                      << QStringLiteral(" orderUid=") << clippedImportLogText(orderUid)
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" col=") << col
                      << QStringLiteral(" partId=") << partCrashCellId(table, row)
                      << QStringLiteral(" imported=") << imported
                      << QStringLiteral(" trace=") << importedPartTraceId(table, row)
                      << QStringLiteral(" invoiceNumber=") << clippedImportLogText(partCrashCellText(table, row, PartColInvoiceNumber))
                      << QStringLiteral(" invoiceDate=") << clippedImportLogText(partCrashCellDataText(table, row, PartColAttachment, RoleAttachmentDocumentDate))
                      << QStringLiteral(" orderedAt=") << clippedImportLogText(partCrashCellText(table, row, PartColOrderedAt))
                      << QStringLiteral(" deliveryDate=") << clippedImportLogText(partCrashCellDataText(table, row, PartColDelivered, RoleDeliveryDate))
                      << QStringLiteral(" store=") << clippedImportLogText(partCrashCellText(table, row, PartColStore))
                      << QStringLiteral(" brand=") << clippedImportLogText(partManufacturerText(table, row))
                      << QStringLiteral(" oem=") << clippedImportLogText(partCrashCellText(table, row, PartColOem))
                      << QStringLiteral(" name=") << clippedImportLogText(partCrashCellText(table, row, PartColName))
                      << QStringLiteral(" qty=") << clippedImportLogText(partCrashCellText(table, row, PartColQty))
                      << QStringLiteral(" price=") << clippedImportLogText(partCrashCellText(table, row, PartColPrice))
                      << QStringLiteral(" threadId=") << reinterpret_cast<quintptr>(QThread::currentThreadId())
                      << (extra.trimmed().isEmpty() ? QString() : QStringLiteral(" extra=%1").arg(clippedImportLogText(extra, 300)));
}

QString writePartCrashSnapshot(const QString& stage,
                                      QTableWidget* table,
