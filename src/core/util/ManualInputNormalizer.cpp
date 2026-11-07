#include "ManualInputNormalizer.h"
#include "VehicleNameUtils.h"
#include "VehicleNameRules.h"
#include "PlateUtils.h"
#include "core/services/orders/PartImportTextSupport.h"
#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <spellcheck.h>
#include <wrl/client.h>
#endif

namespace {
using Role=ManualInputNormalizer::Role;
const QMap<QString,QString>& words() {
    static const QMap<QString,QString> rules=[] {
        QMap<QString,QString> result;
        // Reviewed exact replacements; approximate matches are suggestions only.
        const QString data=QString::fromUtf8(
            "проверка=перевірка|проверить=перевірити|проверили=перевірили|проверено=перевірено|проверьте=перевірте|"
            "заменить=замінити|заменили=замінили|заменено=замінено|замена=заміна|замены=заміни|замену=заміну|"
            "проведеня=проведення|ремонтировать=ремонтувати|отрегулировать=відрегулювати|регулировка=регулювання|очистить=очистити|"
            "очистка=очищення|промыть=промити|промывка=промивання|смазать=змастити|смазка=мастило|"
            "установить=встановити|установлено=встановлено|установка=встановлення|снять=зняти|снятие=зняття|"
            "неисправность=несправність|неисправен=несправний|неисправна=несправна|исправен=справний|"
            "повреждение=пошкодження|поврежден=пошкоджений|повреждена=пошкоджена|износ=зношення|"
            "изношен=зношений|изношены=зношені|подтекает=підтікає|течет=тече|утечка=витік|"
            "стук=стукіт|шумит=шумить|скрипит=скрипить|вибрация=вібрація|перегрев=перегрів|"
            "запчасть=запчастина|запчасти=запчастини|запчастей=запчастин|деталь=деталь|"
            "заказано=замовлено|заказать=замовити|заказ=замовлення|поставка=поставка|получено=отримано|"
            "возврат=повернення|вернуть=повернути|отказ=відмова|отказался=відмовився|причина=причина|"
            "клиент=клієнт|клиента=клієнта|оплачено=сплачено|оплатить=сплатити|задолженность=заборгованість|"
            "наличные=готівка|перевод=переказ|стоимость=вартість|количество=кількість|сумма=сума|"
            "сегодня=сьогодні|завтра=завтра|вчера=вчора|позвонить=зателефонувати|согласовано=погоджено|"
            "согласовать=погодити|ожидание=очікування|ожидается=очікується|готово=готово|"
            "нужно=потрібно|необходимо=потрібно|рекомендовано=рекомендовано|рекомендуется=рекомендовано|"
            "дополнительно=додатково|обязательно=обов’язково|повторно=повторно|отсутствует=відсутній|"
            "отсутствуют=відсутні|невозможно=неможливо|возможно=можливо|безопасность=безпека|"
            "двигатель=двигун|двигателя=двигуна|двигателю=двигуну|двигателем=двигуном|"
            "масло=олива|масла=оливи|маслу=оливі|маслом=оливою|моторное=моторна|"
            "моторного=моторної|масляный=оливний|масляного=оливного|масляная=оливна|"
            "фильтр=фільтр|фильтра=фільтра|фильтры=фільтри|фильтров=фільтрів|"
            "воздушный=повітряний|воздушного=повітряного|топливный=паливний|топливного=паливного|"
            "тормозной=гальмівний|тормозные=гальмівні|тормозных=гальмівних|тормоза=гальма|"
            "подшипник=підшипник|подшипника=підшипника|подшипники=підшипники|подшибник=підшипник|"
            "ремень=ремінь|ремня=ременя|цепь=ланцюг|цепи=ланцюга|натяжитель=натягувач|"
            "сцепление=зчеплення|сцепления=зчеплення|охлаждение=охолодження|охлаждения=охолодження|"
            "жидкость=рідина|жидкости=рідини|прокладка=прокладка|прокладки=прокладки|"
            "передний=передній|передние=передні|передних=передніх|задний=задній|задние=задні|"
            "левый=лівий|левая=ліва|правый=правий|правая=права|верхний=верхній|нижний=нижній|"
            "рулевой=кермовий|рулевая=кермова|пыльник=пильник|полуось=піввісь|"
            "колесо=колесо|колеса=колеса|колесный=колісний|пружина=пружина|амортизатор=амортизатор|"
            "свеча=свічка|свечи=свічки|зажигание=запалювання|зажигания=запалювання|"
            "датчик=датчик|давление=тиск|давления=тиску|температура=температура|температуры=температури|"
            "аккумулятор=акумулятор|стартер=стартер|генератор=генератор|проводка=проводка|"
            "ошибка=помилка|ошибки=помилки|ошибок=помилок|система=система|системы=системи|"
            "диагностика=діагностика|диагностики=діагностики|диагностировать=діагностувати|"
            "фільтер=фільтр|філтр=фільтр|філтер=фільтр|фильтер=фільтр|филтер=фільтр|"
            "топлевный=паливний|маслянный=оливний|маслянного=оливного|воздушний=повітряний|"
            "перевирити=перевірити|перевіріти=перевірити|заміниити=замінити|замінаа=заміна|"
            "обслуговуваня=обслуговування|обслуговуванння=обслуговування|техничне=технічне|"
            "гальмивний=гальмівний|гальмивних=гальмівних|підшипнник=підшипник|акумулятр=акумулятор|"
            "рекомендаціі=рекомендації|діагностіка=діагностика|несправнісь=несправність|"
            "стабілізатр=стабілізатор|амортизатр=амортизатор|антифриз=антифриз|термостат=термостат|"
            "черный=чорний|чёрный=чорний|белый=білий|серый=сірий|красный=червоний|синий=синій|"
            "зеленый=зелений|зелёный=зелений|желтый=жовтий|жёлтый=жовтий|коричневый=коричневий|"
            "серебристый=сріблястий|дополнительный=додатковий|дополнительные=додаткові|"
            "обслуживание=обслуговування|обслуживания=обслуговування|осмотр=огляд|проверять=перевіряти|"
            "неисправности=несправності|повреждения=пошкодження|устранить=усунути|устранено=усунуто|"
            "запустить=запустити|работает=працює|работают=працюють|запускается=запускається|"
            "заводится=заводиться|заглох=заглух|заглохла=заглухла|заменять=замінювати|"
            "исправить=виправити|исправлено=виправлено|сохранить=зберегти|удалить=видалити");
        for(const auto& pair:data.split('|')){const int eq=pair.indexOf('=');result.insert(pair.left(eq),pair.mid(eq+1));}
        return result;
    }();return rules;
}
QString sameCase(const QString& original,QString value) {
    if(original.isEmpty() || value.isEmpty())return value;
    if(original==original.toUpper())return value.toUpper();
    if(original[0].isUpper())value[0]=value[0].toUpper();return value;
}
QString layout(QString word,bool toLatin,bool russian=false) {
    const QString latin="qwertyuiop[]asdfghjkl;'zxcvbnm,./";
    const QString cyr=russian?QString::fromUtf8("йцукенгшщзхъфывапролджэячсмитьбю."):QString::fromUtf8("йцукенгшщзхїфівапролджєячсмитьбю.");
    const auto& from=toLatin?cyr:latin;const auto& to=toLatin?latin:cyr;
    for(auto& ch:word){const int i=from.indexOf(ch.toLower());if(i>=0)ch=ch.isUpper()?to[i].toUpper():to[i];}return word;
}
const QMap<QString,QString>& vehicles() {
    static const auto rules=[] {QMap<QString,QString> r;for(const auto& p:VehicleNameRules::makeRules()+VehicleNameRules::modelRules()+VehicleNameRules::techRules())r[p.first.toLower()]=p.second;return r;}();return rules;
}
QString keyboardCorrection(const QString& token,Role role) {
    if(words().contains(token.toLower()) || (role==Role::Vehicle && vehicles().contains(token.toLower())))return token;
    const QStringList candidates{layout(token,false),layout(token,false,true),layout(token,true),layout(token,true,true)};
    QSet<QString> accepted;
    for(const auto& candidate:candidates) {
        const auto key=candidate.toLower();
        if(role==Role::Vehicle) {if(vehicles().contains(key))accepted.insert(vehicles().value(key));}
        else if(words().contains(key))accepted.insert(sameCase(candidate,words().value(key)));
    }
    return accepted.size()==1?*accepted.begin():token;
}
QString oil(QString text) {
    static const QRegularExpression rx(QString::fromUtf8(R"((?<![\p{L}\p{N}])(\d{1,2})\s*[wWвВцЦ]\s*[-/]?\s*(\d{1,3})(?![\p{L}\p{N}]))"));
    auto it=rx.globalMatch(text);QList<QRegularExpressionMatch> matches;while(it.hasNext())matches.prepend(it.next());
    for(const auto& m:matches){const int cold=m.captured(1).toInt(),hot=m.captured(2).toInt();
        const bool engine=QSet<int>{0,5,10,15,20,25}.contains(cold) && QSet<int>{8,12,16,20,30,40,50,60}.contains(hot);
        const bool gear=QSet<int>{70,75,80,85}.contains(cold) && QSet<int>{80,85,90,110,140,190,250}.contains(hot);
        if(engine || gear)text.replace(m.capturedStart(),m.capturedLength(),QString::number(cold)+"W"+QString::number(hot));}
    return text;
}
int distance(const QString& a,const QString& b) {
    if(a.size()>40 || b.size()>40 || qAbs(a.size()-b.size())>2)return 99;
    int d[42][42]{};for(int i=0;i<=a.size();++i)d[i][0]=i;for(int j=0;j<=b.size();++j)d[0][j]=j;
    for(int i=1;i<=a.size();++i)for(int j=1;j<=b.size();++j){d[i][j]=qMin(qMin(d[i-1][j]+1,d[i][j-1]+1),d[i-1][j-1]+(a[i-1]!=b[j-1]));if(i>1 && j>1 && a[i-1]==b[j-2] && a[i-2]==b[j-1])d[i][j]=qMin(d[i][j],d[i-2][j-2]+1);}
    return d[a.size()][b.size()];
}
}
namespace ManualInputNormalizer {
QString roleKey(Role r){switch(r){case Role::Planning:return "planning";case Role::Technical:return "technical";case Role::Vehicle:return "vehicle";case Role::Plate:return "plate";case Role::Date:return "date";case Role::Search:return "search";case Role::Text:return "text";default:return "protected";}}
Role roleForHint(const QString& hint) {
    const auto h=hint.toLower();
    auto has=[&](const QString& pattern){return QRegularExpression(pattern,QRegularExpression::CaseInsensitiveOption).match(h).hasMatch();};
    if(has("пошук|search|filtertext"))return Role::Search;
    if(has("secret|credential|api.?key|endpoint|param|header|ssid|login|логін|виробник|manufacturer|employee|receiver|owner|company|компані|об.?єм|volume|маркування|бренд|сайт|website"))return Role::Protected;
    if(has("номер.*авто|держномер|державн.*номер|реєстраційн.*номер|номерн.*знак|plate"))return Role::Plate;
    if(has("примітк|коментар|рекомендац|опис|причина|скарг|несправн|note|comment|description|reason|fault"))return Role::Text;
    if(has("vin|о[еэ]м|oem|артикул|article|part.?number|password|парол|pin|пін|токен|token|email|e-mail|телефон|phone|iban|url|шлях|path|файл|file|uid|ключ|код|серійн|парті[яї]|документ|накладн|номер|number|пробіг|mileage|кількість|quantity|ціна|price|сума|amount|вартість|cost|знижк|discount|відсот|percent|рахунок|bank|клієнт|client|піб|прізвище|ім’я|ім'я|name.*(customer|master)|майстер|постачальник|supplier|магазин|store|бренд.*виробник|підпис|signature|адрес|address|рік|year|одиниц|unit"))return Role::Protected;
    if(has("марка|модель|vehicle|car(model|make|name)?$|brand|model|^\\s*авто\\s*$"))return Role::Vehicle;
    if(has("дата|date|сплатити до|придатний до"))return Role::Date;
    if(has("матеріал|material|запчастин|part.?name|назва|найменуван|робот|service|категор|вузол|node|рідина|двигун|кпп"))return Role::Technical;
    return Role::Text;
}
Result normalize(const QString& input,Role role,const QMap<QString,QString>& custom) {
    Result result{input,{}};
    if(role==Role::Protected || role==Role::Search || input.isEmpty() || input.size()>16000)return result;
    if(role==Role::Planning) {
        // Planning stores an identity suffix alongside editable prose. Never
        // translate the staff identity or interpret it as a vehicle/model.
        const auto identity=QRegularExpression(QString::fromUtf8(R"(\s*\|\s*Майстер ID:.*$)"),QRegularExpression::CaseInsensitiveOption).match(input);
        QString value=identity.hasMatch()?input.left(identity.capturedStart()):input;
        const QString trailing=QRegularExpression("\\s*$").match(value).captured();
        // Resolve vehicle aliases before prose rules, in either order of entry.
        // Protect identifiers and acronyms from phrase expansion (ТО, ТНВД)
        // and from the recommendation dictionary's sentence casing.
        value=VehicleNameUtils::normalizeVehicleFreeText(value);
        const QSet<QString> acronyms={"ТО","СТО","ГУР","ЕГУР","ДВЗ","ДВС","ГРМ","КПП","АКПП","МКПП",
            "РКПП","ТНВД","ТННД","ЕГР","АБС","ЕБУ","ЕБК","ГБО","ШРУС","ДМРВ","ДПДЗ",
            "ABS","ESP","EGR","DPF","VIN","OEM","OBD","ECU","DSG","CVT"};
        const QRegularExpression tokens(QString::fromUtf8(R"([\p{L}][\p{L}\p{N}'’\-]*)"));
        QStringList markers, originals;
        auto words=tokens.globalMatch(value);QList<QRegularExpressionMatch> protectedWords;
        while(words.hasNext())protectedWords.prepend(words.next());
        for(const auto& word:protectedWords){
            const auto token=word.captured();
            const bool acronym=acronyms.contains(token.toUpper());
            const bool uppercase=token.size()>=2 && token==token.toUpper();
            if(!acronym && !uppercase && !QRegularExpression("[A-Za-z]").match(token).hasMatch())continue;
            // A letter placeholder retains sentence position, unlike a numeric
            // placeholder which would capitalize the prose following a vehicle.
            QString marker=QString(QChar(0xe200))+QChar(ushort(0x4e00+markers.size()))+QChar(0xe201);
            while(value.contains(marker))marker+=QChar(0xe202);
            markers.append(marker);originals.append(acronym?token.toUpper():token);
            value.replace(word.capturedStart(),word.capturedLength(),marker);
        }
        value=normalize(value,Role::Text,custom).text;
        for(int i=0;i<markers.size();++i)value.replace(markers[i],originals[i]);
        QRegularExpression plates(QString::fromUtf8(R"((?<![\p{L}\p{N}])[A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[ -]?\d{4}[ -]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2}(?![\p{L}\p{N}]))"));
        auto it=plates.globalMatch(value);QList<QRegularExpressionMatch> matches;while(it.hasNext())matches.prepend(it.next());
        for(const auto& m:matches)value.replace(m.capturedStart(),m.capturedLength(),normalize(m.captured(),Role::Plate).text);
        result.text=value+trailing+(identity.hasMatch()?identity.captured():QString());return result;
    }
    if(role==Role::Plate){const auto p=PlateUtils::normalizePlate(input);static const QRegularExpression rx("^[ABCEHIKMOPTX]{2}[0-9]{4}[ABCEHIKMOPTX]{2}$");if(rx.match(p).hasMatch())result.text=PlateUtils::formatPlateDisplay(p);else result.warning=QString::fromUtf8("Очікується номер АА 1122 ВА. Інший формат залишено без змін.");return result;}
    if(role==Role::Date){QString t=input.trimmed();QDate d;QRegularExpression rx("^(\\d{1,2})[./-](\\d{1,2})[./-](\\d{4}|\\d{2})$");auto m=rx.match(t);
        if(m.hasMatch()){int y=m.captured(3).toInt();if(m.captured(3).size()==2)y+=2000;d=QDate(y,m.captured(2).toInt(),m.captured(1).toInt());}
        else {m=QRegularExpression("^(\\d{4})-(\\d{2})-(\\d{2})$").match(t);if(m.hasMatch())d=QDate(m.captured(1).toInt(),m.captured(2).toInt(),m.captured(3).toInt());else {m=QRegularExpression("^(\\d{2})(\\d{2})(\\d{2}|\\d{4})$").match(t);if(m.hasMatch())d=QDate(m.captured(3).toInt()+(m.captured(3).size()==2?2000:0),m.captured(2).toInt(),m.captured(1).toInt());}}
        if(d.isValid() && d.year()>=2000 && d.year()<=2099)result.text=d.toString("dd.MM.yy");else result.warning=QString::fromUtf8("Перевірте дату. Очікується ДД.ММ.ГГ; неможливі й неоднозначні дати не виправляються автоматично.");return result;}
    QString text=input.normalized(QString::NormalizationForm_C);text.replace(QChar(0x00a0),' ');text=oil(text);
    // Mask identities/specifications even inside prose before lexical replacements.
    QStringList protectedTokens;
    static const QRegularExpression protectedRx(QString::fromUtf8(R"((?:https?://|www\.)\S+|[\w.+-]+@[\w.-]+\.[\p{L}]+|[A-Za-z]:[\\/][^\s]+|(?<![\p{L}\p{N}])(?=[\p{L}\p{N}._/+-]*\d)[\p{L}\p{N}._/+-]+)"),QRegularExpression::UseUnicodePropertiesOption);
    if(role!=Role::Vehicle){auto it=protectedRx.globalMatch(text);QList<QRegularExpressionMatch> matches;while(it.hasNext())matches.prepend(it.next());for(const auto& m:matches){const auto marker=QString(QChar(0xe100))+QString::number(protectedTokens.size())+QChar(0xe101);protectedTokens.append(m.captured());text.replace(m.capturedStart(),m.capturedLength(),marker);}}
    if(role!=Role::Vehicle) {
        QStringList lines;for(const auto& line:text.split('\n')){
            const auto leading=QRegularExpression("^\\s*").match(line).captured(),trailing=QRegularExpression("\\s*$").match(line).captured();
            lines.append(line.trimmed().isEmpty()?line:leading+PartImportTextSupport::normalizeRecommendations(line.trimmed())+trailing);
        }text=lines.join('\n');
    }
    static const QRegularExpression tokenRx(QString::fromUtf8("[\\p{L}][\\p{L}’']*"),QRegularExpression::UseUnicodePropertiesOption);
    auto it=tokenRx.globalMatch(text);QList<QRegularExpressionMatch> matches;while(it.hasNext())matches.prepend(it.next());
    for(const auto& m:matches){QString token=keyboardCorrection(m.captured(),role);if(role!=Role::Vehicle && words().contains(token.toLower()))token=sameCase(token,words().value(token.toLower()));text.replace(m.capturedStart(),m.capturedLength(),token);}
    if(role==Role::Vehicle)text=VehicleNameUtils::normalizeVehicleFreeText(text);
    else {
        QStringList lines;
        for(const auto& line:text.split('\n')){
            const auto leading=QRegularExpression("^\\s*").match(line).captured();const auto trailing=QRegularExpression("\\s*$").match(line).captured();
            // Existing automotive phrases/abbreviations cover parts, work and recommendations.
            const auto corrected=role==Role::Technical?PartImportTextSupport::normalizePartName(line.trimmed()):PartImportTextSupport::normalizeRecommendations(line.trimmed());
            lines.append(line.trimmed().isEmpty()?line:leading+corrected+trailing);
        }text=lines.join('\n');
    }
    // Match canonical values as well as aliases in one pass. Existing expansions
    // stay intact, replacements are literal, and rules cannot cascade in this pass.
    QMap<QString,QString> replacements;
    for(auto i=custom.begin();i!=custom.end();++i)
        if(!i.key().trimmed().isEmpty() && !i.value().isEmpty())replacements[i.key().toCaseFolded()]=i.value();
    for(const auto& value:custom)replacements[value.toCaseFolded()]=value;
    auto keys=replacements.keys();std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return a.size()>b.size();});
    QStringList alternatives;for(const auto& key:keys)if(!key.isEmpty())alternatives.append(QRegularExpression::escape(key));
    if(!alternatives.isEmpty()) {
        QRegularExpression rx("(?<![\\p{L}\\p{N}])(?:"+alternatives.join('|')+")(?![\\p{L}\\p{N}])",QRegularExpression::CaseInsensitiveOption);
        auto it=rx.globalMatch(text);QList<QRegularExpressionMatch> found;while(it.hasNext())found.prepend(it.next());
        for(const auto& m:found)text.replace(m.capturedStart(),m.capturedLength(),replacements.value(m.captured().toCaseFolded()));
    }
    for(int i=0;i<protectedTokens.size();++i)text.replace(QString(QChar(0xe100))+QString::number(i)+QChar(0xe101),protectedTokens[i]);
    result.text=oil(text);return result;
}
QStringList suggestions(const QString& word,Role role) {
    if(role==Role::Protected || role==Role::Date || role==Role::Plate || word.size()<3 || word.size()>40 || word.contains(QRegularExpression("[0-9@:/]")))return {};
    const auto key=word.toLower();QMap<QString,QString> dictionary=role==Role::Vehicle?vehicles():words();
    if(role!=Role::Vehicle)for(const auto& value:words())dictionary.insert(value.toLower(),value);
    QList<QPair<int,QString>> scored;for(auto it=dictionary.begin();it!=dictionary.end();++it){int n=distance(key,it.key());if(n<= (key.size()>=7?2:1) && it.value().compare(word,Qt::CaseInsensitive)!=0)scored.append({n,sameCase(word,it.value())});}
    std::sort(scored.begin(),scored.end(),[](auto a,auto b){return a.first!=b.first?a.first<b.first:a.second<b.second;});QStringList result;
    const auto keyboard=keyboardCorrection(word,role);if(keyboard!=word)result.append(keyboard);
    for(const auto& item:scored)if(!result.contains(item.second) && result.size()<6)result.append(item.second);return result;
}
QStringList systemSuggestions(const QString& word) {
    QStringList result;
#ifdef Q_OS_WIN
    if(word.size()<3 || word.size()>48 || !QRegularExpression(QString::fromUtf8("^[А-Яа-яІіЇїЄєҐґ’']+$")).match(word).hasMatch())return result;
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    {
        Microsoft::WRL::ComPtr<ISpellCheckerFactory> factory;
        if(SUCCEEDED(CoCreateInstance(__uuidof(SpellCheckerFactory),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))){
            BOOL supported=FALSE;factory->IsSupported(L"uk-UA",&supported);
            Microsoft::WRL::ComPtr<ISpellChecker> checker;
            if(supported && SUCCEEDED(factory->CreateSpellChecker(L"uk-UA",&checker))){Microsoft::WRL::ComPtr<IEnumString> values;
                if(SUCCEEDED(checker->Suggest(reinterpret_cast<LPCWSTR>(word.utf16()),&values)) && values){LPOLESTR value=nullptr;ULONG fetched=0;while(result.size()<6 && values->Next(1,&value,&fetched)==S_OK){result.append(QString::fromWCharArray(value));CoTaskMemFree(value);}}}
        }
    }
    if(SUCCEEDED(initialized))CoUninitialize();
#endif
    return result;
}
}
