#include "LocalWorkshopBackend.h"
#include "core/util/FormatUtils.h"
#include "core/finance/ExactMoney.h"
#include "OrderClosureJournal.h"
#include "BaySnapshot.h"
#include "core/cloudsync/OrderPartIssueEvidence.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QUuid>
#include <cmath>
#include <limits>
#include <stdexcept>
#ifdef LOCAL_BACKEND_TEST
#include <cstdlib>
#endif

namespace {
struct Failure { int status; QString message; };
void require(bool yes, const QString& message, int status=409) { if(!yes)throw Failure{status,message}; }
QString encode(const QJsonObject& v) {return QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact));}
QString revision(const QJsonObject& v) {return QString::fromLatin1(QCryptographicHash::hash(encode(v).toUtf8(),QCryptographicHash::Sha256).toHex());}
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QSqlQuery query(QSqlDatabase db,const QString& sql,const QVariantList& args={}) {
    QSqlQuery q(db);require(q.prepare(sql),q.lastError().text(),500);
    for(const auto& a:args)q.addBindValue(a);
    require(q.exec(),q.lastError().text(),500);return q;
}
QJsonObject object(const QSqlQuery& q) {
    QJsonObject o;for(int i=0;i<q.record().count();++i)o[q.record().fieldName(i)]=QJsonValue::fromVariant(q.value(i));return o;
}
QJsonArray rows(QSqlDatabase db,const QString& sql,const QVariantList& args={}) {
    auto q=query(db,sql,args);QJsonArray a;while(q.next())a.append(object(q));return a;
}
int index(const QJsonArray& a,const QString& key,const QString& id) {
    for(int i=0;i<a.size();++i)if(a[i].toObject()[key].toString()==id)return i;return -1;
}
bool permission(QSqlDatabase db,const QString& key){return AccessControlService::currentUserHasPermission(db,key);}
QJsonObject order(QSqlDatabase db,const QString& uid) {
    auto q=query(db,"SELECT * FROM main.work_orders WHERE order_uid=?",{uid});
    require(q.next(),QString::fromUtf8("Наряд не знайдено."),404);
    require(AccessControlService::canAccessOrder(db,q.value("number").toInt()),QString::fromUtf8("Немає доступу до наряду."),403);
    return object(q);
}
struct Transaction {
    QSqlDatabase db;bool done=false;
    ~Transaction(){if(!done)db.rollback();}
    void commit(){require(db.commit(),db.lastError().text(),500);done=true;}
};
void schema(QSqlDatabase db) {
    QString error;require(OrderClosureJournal::ensureSchema(db,&error),error,500);
    query(db,"CREATE TABLE IF NOT EXISTS main.local_workshop_state(id INTEGER PRIMARY KEY CHECK(id=1),body TEXT NOT NULL)");
    query(db,"CREATE TABLE IF NOT EXISTS main.local_workshop_commands(request_uid TEXT PRIMARY KEY,command TEXT NOT NULL,response TEXT NOT NULL,actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL)");
}
QJsonObject state(QSqlDatabase db) {
    auto q=query(db,"SELECT body FROM main.local_workshop_state WHERE id=1");
    if(q.next()) {QJsonParseError e;auto d=QJsonDocument::fromJson(q.value(0).toString().toUtf8(),&e);require(e.error==QJsonParseError::NoError && d.isObject(),"Invalid local workshop state",500);return d.object();}
    return {{"bays",QJsonArray{}},{"states",QJsonArray{}},{"intervals",QJsonArray{}},{"audit",QJsonArray{}}};
}
QJsonObject occupancy(const QJsonObject& s,const QString& uid) {
    const auto a=s["states"].toArray();int i=index(a,"order_uid",uid);
    return i<0?QJsonObject{{"state","unknown"},{"version",0},{"bay_uid",QJsonValue::Null}}:a[i].toObject();
}
QJsonObject snapshot(QSqlDatabase db,QJsonObject s,qint64 now) {
    QJsonArray orders;
    auto labels=s["order_labels"].toObject();
    for(const auto& v:rows(db,"SELECT * FROM main.work_orders ORDER BY number")) {
        const auto o=v.toObject();
        orders.append(QJsonObject{{"uid",o["order_uid"]},{"number",o["number"].toVariant().toString()},{"status",o["status"]},{"revision",revision(o)}});
        labels.remove(o["order_uid"].toString());
    }
    for(auto it=labels.begin();it!=labels.end();++it)orders.append(QJsonObject{{"uid",it.key()},{"number",it.value()},{"status","deleted"}});
    s["orders"]=orders;s["as_of_ms"]=now;s["can_correct"]=permission(db,"access.manage");return s;
}
void validate(const QJsonObject& s,qint64 now) {
    auto copy=s;copy["as_of_ms"]=now;
    const auto parsed=BaySnapshot::parse(copy);require(parsed.error.isEmpty(),parsed.error,422);
    for(const auto& bay:parsed.bays) {
        for(int i=0;i<bay.calendars.size();++i) {
            const auto& a=bay.calendars[i];require(!a.until.isValid() || a.until>a.from,"Invalid calendar range",422);
            for(int j=i+1;j<bay.calendars.size();++j){const auto& b=bay.calendars[j];require((a.until.isValid() && a.until<=b.from) || (b.until.isValid() && b.until<=a.from),"Overlapping calendar versions",422);}
            for(int j=0;j<a.shifts.size();++j)for(int k=j+1;k<a.shifts.size();++k)
                require(qMax(a.shifts[j].begin,a.shifts[k].begin)>=qMin(a.shifts[j].end,a.shifts[k].end),"Overlapping work shifts",422);
        }
        for(const auto& occupied:bay.occupancy)require(occupied.begin>=bay.activeFrom,"Occupancy before bay existed",422);
    }
    const auto intervals=s["intervals"].toArray();
    for(int i=0;i<intervals.size();++i) {
        const auto a=intervals[i].toObject();
        for(int j=i+1;j<intervals.size();++j) {
            const auto b=intervals[j].toObject();
            if(a["bay_uid"]!=b["bay_uid"] && a["order_uid"]!=b["order_uid"])continue;
            auto end=[](const QJsonObject& o){return o["end_ms"].isNull()?std::numeric_limits<qint64>::max():o["end_ms"].toInteger();};
            require(qMax(a["begin_ms"].toInteger(),b["begin_ms"].toInteger())>=qMin(end(a),end(b)),QString::fromUtf8("Інтервали зайнятості перетинаються."));
        }
    }
    for(const auto& v:s["states"].toArray()) {
        const auto o=v.toObject();int count=0;
        for(const auto& iv:intervals) {const auto i=iv.toObject();if(i["order_uid"]==o["order_uid"] && i["end_ms"].isNull()){++count;require(o["bay_uid"]==i["bay_uid"],"State/interval mismatch");}}
        require(count==(o["state"]=="on_bay"?1:0),"State/interval mismatch");
    }
}
void move(QJsonObject& s,const QString& uid,const QString& target,int expected,qint64 now,const QString& actor,bool initial=false) {
    auto current=occupancy(s,uid);require(current["version"].toInt()==expected,QString::fromUtf8("Зайнятість змінилася. Оновіть дані."));
    auto bays=s["bays"].toArray();
    if(!target.isEmpty()) {
        const int i=index(bays,"uid",target);require(i>=0,"Unknown bay",422);
        auto b=bays[i].toObject();
        if(initial){require(b["confirmed_from"].isNull(),"Bay already confirmed");b["confirmed_from"]=now;bays[i]=b;}
        else require(!b["confirmed_from"].isNull(),QString::fromUtf8("Спочатку підтвердьте фактичний стан поста."));
    }
    auto intervals=s["intervals"].toArray();
    for(int i=0;i<intervals.size();++i) {
        auto interval=intervals[i].toObject();
        if(interval["order_uid"]==uid && interval["end_ms"].isNull()) {
            require(now>interval["begin_ms"].toInteger(),QString::fromUtf8("Повторіть дію через мить."));
            interval["end_ms"]=now;interval.remove("revision");interval["revision"]=revision(interval);intervals[i]=interval;
        }
    }
    if(!target.isEmpty()) {
        QJsonObject interval{{"uid",uuid()},{"bay_uid",target},{"order_uid",uid},{"begin_ms",now},{"end_ms",QJsonValue::Null},{"actor",actor}};
        interval["revision"]=revision(interval);intervals.append(interval);
    }
    auto states=s["states"].toArray();const int i=index(states,"order_uid",uid);
    QJsonObject next{{"order_uid",uid},{"state",target.isEmpty()?"off_bay":"on_bay"},{"bay_uid",target.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(target)},{"version",expected+1}};
    if(i<0)states.append(next);else states[i]=next;
    s["bays"]=bays;s["states"]=states;s["intervals"]=intervals;validate(s,now);
}
long double number(QJsonValue v) {
    bool ok=false;const double n=v.toVariant().toString().toDouble(&ok);
    require(ok && std::isfinite(n) && n>=0 && n<=90000000000000.0,"Invalid amount or quantity",422);return n;
}
qint64 cents(long double n) {
    require(std::isfinite(n) && n>=0 && n<=90000000000000.0,"Amount outside supported range",422);
    return std::llround(static_cast<double>(n)*100.0);
}
QJsonObject preview(QSqlDatabase db,const QJsonObject& s,const QString& uid) {
    const auto o=order(db,uid);
    const auto parts=OrderPartIssueEvidence::attach(db,rows(db,"SELECT * FROM main.work_order_parts WHERE order_uid=? ORDER BY part_uid",{uid}));
    const auto services=rows(db,"SELECT * FROM main.work_order_services WHERE order_uid=? ORDER BY service_uid",{uid});
    const auto payments=rows(db,"SELECT * FROM main.client_payments WHERE order_uid=? ORDER BY payment_uid",{uid});
    require(o["discount_mode"].toString() != "percent" || number(o["discount"]) <= 100, "Discount percent must be between 0 and 100",422);
    auto subtotal=[](const QJsonArray& lines) {
        qint64 total=0;
        for (const auto& value : lines) {
            const auto row=value.toObject();
            const auto result=ExactMoney::storedLineAmount(row["qty"].toVariant().toString().toStdString(),
                                                     row["price"].toVariant().toString().toStdString());
            const auto* amount=std::get_if<ExactMoney::Cents>(&result);
            require(amount && *amount<=9000000000000000LL-total,QStringLiteral("Некоректна кількість або ціна позиції."),422);
            total+=*amount;
        }
        return total;
    };
    const auto total=qMax(qint64(0),subtotal(parts)+subtotal(services)-cents(FormatUtils::discountAmount(number(o["discount"]),o["discount_mode"].toString(),subtotal(services)/100.0)));
    require(total<=9000000000000000LL,"Order amount outside supported range",422);
    qint64 received=0;for(const auto& p:payments){const auto amount=p.toObject()["amount_cents"].toInteger();require(amount>0 && amount<=9000000000000000LL-received,"Invalid payment ledger",422);received+=amount;}
    QJsonArray install,unresolved,bp,bs;
    for(const auto& v:parts) {
        const auto p=v.toObject(),e=p["closure_issue_evidence"].toObject();
        const bool excluded=QStringList{"returned","cancelled","canceled"}.contains(p["status"].toString().toLower());
        if(excluded || p["installed_at"].toString().trimmed().isEmpty()) {
            const bool issued=!excluded && e["version"]==1 && e["source"]=="workshop_spare_movements" && e["part_uid"]==p["part_uid"] && e["order_uid"]==uid && !e["movement_ids"].toArray().isEmpty() && number(p["qty"])>0 && qAbs(e["issued_quantity"].toDouble()-p["qty"].toDouble())<1e-9;
            if(issued)install.append(p["part_uid"]);
            else unresolved.append(QJsonObject{{"part_uid",p["part_uid"]},{"name",p["name"]}});
        }
        bp.append(QJsonObject{{"uid",p["part_uid"]},{"qty",p["qty"]},{"price",p["price"]},{"installed_at",p["installed_at"].toString()},{"closure_issue_evidence",p["closure_issue_evidence"]}});
    }
    for(const auto& v:services){auto p=v.toObject();bs.append(QJsonObject{{"uid",p["service_uid"]},{"qty",p["qty"]},{"price",p["price"]}});}
    QJsonObject view{{"order_uid",uid},{"order_number",o["number"].toVariant().toString()},{"status",o["status"]},{"total_cents",total},{"received_cents",received},{"balance_cents",total-received},{"install_part_uids",install},{"unresolved_parts",unresolved},{"occupancy",occupancy(s,uid)},{"basis",QJsonObject{{"discount",o["discount"]},{"discount_mode",o["discount_mode"]},{"parts",bp},{"services",bs}}}};
    view["revision"]=revision({{"order",o},{"parts",parts},{"services",services},{"payments",payments},{"view",view}});return view;
}
QJsonObject close(QSqlDatabase db,QJsonObject& s,const QJsonObject& p,qint64 now,const QString& actor) {
    const auto uid=p["order_uid"].toString(),id=p["request_uid"].toString();
    const auto v=preview(db,s,uid);require(v["revision"]==p["expected_revision"],QString::fromUtf8("Наряд або оплати змінилися. Оновіть і підтвердіть нові дані."));
    require(!QStringList{"closed","cancelled","canceled","archived"}.contains(v["status"].toString()),QString::fromUtf8("Наряд уже закрито або скасовано."));
    require(v["unresolved_parts"].toArray().isEmpty(),QString::fromUtf8("Спочатку оформіть видачу, повернення або виключення запчастин."));
    require(p["release_bay"].isBool(),"Missing bay release choice",422);
    auto payments=rows(db,"SELECT * FROM main.client_payments WHERE order_uid=? ORDER BY payment_uid",{uid});
    qint64 received=v["received_cents"].toInteger();QJsonValue paymentUid(QJsonValue::Null);
    if(!p["payment"].isNull()) {
        const auto pay=p["payment"].toObject();const auto amount=pay["amount_cents"].toInteger(-1);const auto date=QDate::fromString(pay["paid_on"].toString(),Qt::ISODate);
        require(pay["amount_cents"].isDouble() && pay["amount_cents"].toDouble()==double(amount) && amount>0 && amount<=v["balance_cents"].toInteger(),QString::fromUtf8("Оплата має бути додатною й не перевищувати залишок."),422);
        require(date.isValid() && date<=QDateTime::fromMSecsSinceEpoch(now,QTimeZone("Europe/Kyiv")).date() && QStringList{"cash","card","bank_transfer"}.contains(pay["method"].toString()),"Invalid payment date/method",422);
        paymentUid=uuid();payments.append(QJsonObject{{"payment_uid",paymentUid},{"request_uid",id},{"order_uid",uid},{"amount_cents",amount},{"method",pay["method"]},{"paid_on",pay["paid_on"]},{"actor",actor},{"recorded_ms",now}});received+=amount;
    }
    const auto occupied=occupancy(s,uid);
    if(p["release_bay"].toBool() && occupied["state"]=="on_bay")move(s,uid,{},occupied["version"].toInt(),now,actor);
    const auto date=QDateTime::fromMSecsSinceEpoch(now,QTimeZone("Europe/Kyiv"));QJsonArray installed;
    for(const auto& part:v["install_part_uids"].toArray())installed.append(QJsonObject{{"part_uid",part},{"order_uid",uid},{"installed_at",date.date().toString(Qt::ISODate)}});
    auto o=order(db,uid);o["status"]="closed";o["closed_at"]=date.toString(Qt::ISODateWithMs);
    QJsonObject result{{"schema_version",1},{"ok",true},{"request_uid",id},{"order_uid",uid},{"status","closed"},{"order",o},{"installed_parts",installed},{"payments",payments},{"payment_uid",paymentUid},{"total_cents",v["total_cents"]},{"received_cents",received},{"balance_cents",v["total_cents"].toInteger()-received}};
    QString error;require(OrderClosureJournal::apply(db,result,&error),error,500);return result;
}
}

namespace LocalWorkshopBackend {
QString deletionBlockReason(QSqlDatabase db,const QString& uid) {
    try {
        if(db.tables().contains("order_closure_commands")) {
            auto q=query(db,"SELECT 1 FROM main.order_closure_commands WHERE order_uid=? AND state='pending'",{uid});
            if(q.next())return QString::fromUtf8("Спочатку завершіть або відновіть підтверджене закриття наряду.");
        }
        if(db.tables().contains("local_workshop_state") && occupancy(state(db),uid)["state"]=="on_bay")
            return QString::fromUtf8("Наряд займає пост. Спочатку підтвердьте фактичне звільнення поста у вікні зайнятості.");
        return {};
    }catch(const Failure& e){return e.message;}
}
bool hasChanges(QSqlDatabase db) {
    if(!db.tables().contains("local_workshop_commands"))return false;
    QSqlQuery q(db);return !q.exec("SELECT 1 FROM main.local_workshop_commands LIMIT 1") || q.next();
}
Reply request(QSqlDatabase db,const QString& endpoint,const QJsonObject& p) {
    try {
        require(ActivityAuditService::currentActor().isValid(),QString::fromUtf8("Спочатку увійдіть у застосунок."),403);
        const bool read=endpoint=="snapshot" || endpoint=="closure-preview";
        // The bay ledger is a station-wide administrative view, as on the server.
        require(endpoint.startsWith("closure-") || !AccessControlService::currentActorIsMaster(),QString::fromUtf8("Облік усіх постів доступний адміністратору."),403);
        require(permission(db,endpoint.startsWith("closure-")?"orders.close":endpoint.startsWith("correct-")?"access.manage":"orders.edit"),QString::fromUtf8("Немає права виконувати цю операцію."),403);
        schema(db);query(db,"BEGIN IMMEDIATE");Transaction tx{db};
        auto s=state(db);const auto before=s;const qint64 now=QDateTime::currentMSecsSinceEpoch();
        QJsonObject orderBefore;
        if(endpoint.startsWith("closure-"))orderBefore=order(db,p["order_uid"].toString());
        const QString actor=ActivityAuditService::currentActor().accountId;
        QJsonObject command{{"endpoint",endpoint},{"payload",p}},result;
        const auto id=p["request_uid"].toString();
        if(!read) {
            require(!id.isEmpty(),"Missing request ID",422);
            auto prior=query(db,"SELECT command,response FROM main.local_workshop_commands WHERE request_uid=?",{id});
            if(prior.next()){require(prior.value(0).toString()==encode(command),"Request ID reused");result=QJsonDocument::fromJson(prior.value(1).toByteArray()).object();tx.commit();return {200,result,true};}
        }
        if(endpoint=="snapshot")result=snapshot(db,s,now);
        else if(endpoint=="closure-preview")result=preview(db,s,p["order_uid"].toString());
        else if(endpoint=="closure-commit")result=close(db,s,p,now,actor);
        else if(endpoint=="provision") {
            require(s["bays"].toArray().isEmpty(),"Bays already exist");QJsonArray bays;
            const auto day=QDateTime::fromMSecsSinceEpoch(now,QTimeZone("Europe/Kyiv")).date().toString(Qt::ISODate);
            for(int i=1;i<=3;++i) {
                QJsonArray calendars{QJsonObject{{"from",day},{"until",QJsonValue::Null},{"timezone","Europe/Kyiv"},{"weekdays",QJsonArray{1,2,3,4,5,6}},{"shifts",QJsonArray{QJsonArray{"08:00","12:00"},QJsonArray{"13:00","18:00"}}},{"unavailable",QJsonArray{}}}};
                bays.append(QJsonObject{{"uid",QString::number(i)},{"name",i==3?QString::fromUtf8("Пост 3 — ДВЗ та КПП"):QString::fromUtf8("Пост %1 — авто").arg(i)},{"active_from",now},{"inactive_from",QJsonValue::Null},{"confirmed_from",QJsonValue::Null},{"calendars",calendars},{"unavailable",QJsonArray{}},{"calendar_revision",revision({{"calendars",calendars}})}});
            }s["bays"]=bays;
        } else if(endpoint=="confirm-free") {
            auto bays=s["bays"].toArray();int i=index(bays,"uid",p["bay_uid"].toString());require(i>=0,"Unknown bay",422);
            auto b=bays[i].toObject();require(b["confirmed_from"].isNull(),"Bay already confirmed");b["confirmed_from"]=now;bays[i]=b;s["bays"]=bays;
        } else if(endpoint=="change" || endpoint=="confirm-occupied") {
            const auto o=order(db,p["order_uid"].toString());const auto target=p["target_bay_uid"].toString();
            require(p["target_state"]==(target.isEmpty()?"off_bay":"on_bay"),"Invalid target state",422);
            require(target.isEmpty() || !QStringList{"closed","cancelled","canceled","archived"}.contains(o["status"].toString()),"Cannot occupy bay with closed order");
            move(s,p["order_uid"].toString(),target,p["expected_version"].toInt(-1),now,actor,endpoint=="confirm-occupied");
        } else if(endpoint=="close-orders") {
            require(p["target_status"]=="cancelled" || p["target_status"]=="canceled","Use payment confirmation to close orders",422);
            require(!p["orders"].toArray().isEmpty(),"Missing orders",422);
            for(const auto& v:p["orders"].toArray()) {
                const auto entry=v.toObject();const auto uid=entry["order_uid"].toString();const auto o=order(db,uid),occupied=occupancy(s,uid);
                require(revision(o)==entry["expected_order_revision"] && occupied["version"]==entry["expected_version"],QString::fromUtf8("Наряд змінився. Оновіть дані."));
                require(entry["release"].isBool(),"Missing release choice",422);
                if(entry["release"].toBool() && occupied["state"]=="on_bay")move(s,uid,{},occupied["version"].toInt(),now,actor);
                query(db,"UPDATE main.work_orders SET status=? WHERE order_uid=?",{p["target_status"].toString(),uid});
            }
        } else if(endpoint=="correct-interval") {
            require(p["reason"].toString().trimmed().size()>=3,"Correction reason required",422);
            auto intervals=s["intervals"].toArray();int i=index(intervals,"uid",p["interval_uid"].toString());require(i>=0,"Interval missing");
            auto v=intervals[i].toObject();require(v["revision"]==p["expected_revision"],"Interval changed");
            const auto uid=v["order_uid"].toString();order(db,uid);
            const bool wasOpen=v["end_ms"].isNull();
            v["bay_uid"]=p["bay_uid"];v["begin_ms"]=p["begin_ms"];v["end_ms"]=p["end_ms"];v.remove("revision");v["revision"]=revision(v);intervals[i]=v;s["intervals"]=intervals;
            if(wasOpen || v["end_ms"].isNull()) {
                auto states=s["states"].toArray();int n=index(states,"order_uid",uid);require(n>=0,"Order state missing");auto st=states[n].toObject();
                st["version"]=st["version"].toInt()+1;st["state"]=v["end_ms"].isNull()?"on_bay":"off_bay";st["bay_uid"]=v["end_ms"].isNull()?v["bay_uid"]:QJsonValue(QJsonValue::Null);states[n]=st;s["states"]=states;
            }
        } else if(endpoint=="correct-calendar") {
            require(p["reason"].toString().trimmed().size()>=3 && !p["calendars"].toArray().isEmpty(),"Calendar and reason required",422);
            auto bays=s["bays"].toArray();int i=index(bays,"uid",p["bay_uid"].toString());require(i>=0,"Unknown bay",422);auto b=bays[i].toObject();
            require(b["calendar_revision"]==p["expected_revision"],"Calendar changed");
            QJsonArray unavailable;for(const auto& v:p["calendars"].toArray())for(const auto& span:v.toObject()["unavailable"].toArray())unavailable.append(span);
            b["calendars"]=p["calendars"];b["unavailable"]=unavailable;b["calendar_revision"]=revision({{"calendars",p["calendars"]}});bays[i]=b;s["bays"]=bays;
        } else throw Failure{404,"Unknown local operation"};
        if(!read) {
            validate(s,now);
            auto labels=s["order_labels"].toObject();
            for(const auto& v:rows(db,"SELECT order_uid,number FROM main.work_orders")){const auto o=v.toObject();labels[o["order_uid"].toString()]=o["number"].toVariant().toString();}
            s["order_labels"]=labels;
            // Calculate validates calendar ordering and overlap, in addition to JSON structure.
            const auto parsed=BaySnapshot::parse(snapshot(db,s,now));const auto today=QDateTime::fromMSecsSinceEpoch(now,QTimeZone("Europe/Kyiv")).date();
            const auto report=BayLoadReport::calculate(parsed.bays,today,today,parsed.asOf);require(report.error.isEmpty(),report.error,422);
            auto audit=s["audit"].toArray();QJsonObject detail=p;detail["result"]=result;
            audit.append(QJsonObject{{"uid",id},{"actor",actor},{"effective_ms",now},{"recorded_ms",now},{"before",QJsonObject{{"states",before["states"]},{"order",orderBefore}}},{"after",detail}});s["audit"]=audit;
            query(db,"INSERT INTO main.local_workshop_state(id,body) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET body=excluded.body",{encode(s)});
            if(result.isEmpty())result={{"ok",true},{"snapshot",snapshot(db,s,now)}};
            query(db,"INSERT INTO main.local_workshop_commands(request_uid,command,response,actor,recorded_ms) VALUES(?,?,?,?,?)",{id,encode(command),encode(result),actor,now});
            QString error;require(ActivityAuditService::record(db,"orders",endpoint,QString::fromUtf8("Автономна операція"),0,"local_workshop",id,QStringLiteral(""),encode(detail),&error),error,500);
        }
#ifdef LOCAL_BACKEND_TEST
        if(!read && qEnvironmentVariable("CS_LOCAL_CRASH")=="before_commit")std::_Exit(87);
#endif
        tx.commit();
#ifdef LOCAL_BACKEND_TEST
        if(!read && qEnvironmentVariable("CS_LOCAL_CRASH")=="after_commit")std::_Exit(87);
#endif
        if(!read){SnapshotCacheInvalidator::invalidateOrdersAndParts("LocalWorkshopBackend");SnapshotCacheInvalidator::invalidateReportsAndStats("LocalWorkshopBackend");}
        return {200,result,true};
    } catch(const Failure& e) {return {e.status,{{"detail",e.message}},false};}
}
}
