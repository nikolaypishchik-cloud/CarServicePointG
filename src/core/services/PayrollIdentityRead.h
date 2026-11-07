#pragma once
#include "core/services/ReportsSyncSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "core/util/DateFormats.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QSet>
#include <QHash>
#include <algorithm>
#include <utility>

namespace PayrollIdentityRead {
inline QString composePlateCarForUi(const QString& plate,const QString& car){
    QString p=PlateUtils::formatPlateDisplay(plate).trimmed();if(p.isEmpty())p=plate.trimmed();
    const auto c=car.trimmed();return p.isEmpty()?c:c.isEmpty()?p:p+QStringLiteral(" | ")+c;
}
using Record=QMap<QString,QVariant>;
inline bool reject(QString* error,const QString& text){if(error)*error=text;return false;}
inline bool load(QSqlDatabase db,const QString& table,const QStringList& required,const QStringList& optional,QList<Record>* rows,QString* error)
{
    QSqlQuery q(db);
    // Metadata only: optional UID/display columns vary in older databases.
    if(!q.exec("SELECT * FROM "+table+" LIMIT 0"))return reject(error,q.lastError().text());
    const auto columns=q.record();
    for(const auto& column:required)if(columns.indexOf(column)<0)return reject(error,"Payroll column unavailable: "+table+"."+column);
    QStringList selected;
    for(const auto& column:required+optional) {
        const int index=columns.indexOf(column);
        if(index>=0)selected.append(columns.fieldName(index));
    }
    q.finish();
    QStringList quoted;for(const auto& column:selected)quoted.append(QStringLiteral("\"%1\"").arg(QString(column).replace('"',"\"\"")));
    if(!q.exec("SELECT "+quoted.join(',')+" FROM "+table))return reject(error,q.lastError().text());
    while(q.next()) {Record row;for(int i=0;i<selected.size();++i)row.insert(selected[i],q.value(i));rows->append(row);}
    return !q.lastError().isValid()||reject(error,q.lastError().text());
}

struct Index {
    QHash<QString,QList<int>> uids,numbers;
    Index()=default;
    Index(const QList<Record>& rows,const QString& uid,const QString& number){
        for(int i=0;i<rows.size();++i){const auto u=rows[i].value(uid).toString().trimmed();if(!u.isEmpty())uids[u].append(i);numbers[rows[i].value(number).toString()].append(i);}
    }
    QList<int> matches(const Record& row,const QString& uid,const QString& number)const {
        const auto u=row.value(uid).toString().trimmed();return u.isEmpty()?numbers.value(row.value(number).toString()):uids.value(u);
    }
};
// Admin detail uses header totals, but service captions still require a proven owner.
struct AdminTitles {
    QList<Record> headers;
    Index index;
    QHash<int,QStringList> titles;
    QSet<int> invalid;
    bool hasUid=false;
    bool loadFrom(QSqlDatabase db,QString* error) {
        QList<Record> services;
        if(!load(db,"router_work_orders",{"number"},{"order_uid"},&headers,error)
            ||!load(db,"router_work_order_services",{"order_number","title"},{"order_uid"},&services,error))return false;
        index=Index(headers,"order_uid","number");
        hasUid=!headers.isEmpty()&&headers.first().contains("order_uid");
        for(const auto& service:services) {
            const auto owners=index.matches(service,"order_uid","order_number");
            if(owners.size()!=1) {
                const auto affected=owners.isEmpty()?index.numbers.value(service.value("order_number").toString()):owners;
                for(int owner:affected)invalid.insert(owner);
                continue;
            }
            if(!service.value("title").isNull())titles[owners.first()].append(service.value("title").toString());
        }
        return true;
    }
    bool text(const QVariant& number,const QVariant& uid,QString* output,QString* error)const {
        const auto owners=index.matches({{"number",number},{"order_uid",uid}},"order_uid","number");
        if(owners.size()!=1 || invalid.contains(owners.first()))
            return reject(error,"Payroll admin service owner is ambiguous or unavailable");
        *output=titles.value(owners.first()).join(", ");return true;
    }
};
struct Graph {
    QList<Record> headers,services,assignments,adjustments,outsources;
    Index orderIndex,serviceIndex;
};
inline bool loadGraph(QSqlDatabase db,Graph* graph,QString* error) {
    *graph={};
    Graph next;
    if(!load(db,"router_work_orders",{"number","closed_at","status","discount","discount_mode","works_sum"},
             {"order_uid","order_number","opened_at","plate","car","engine_type","engine_volume"},&next.headers,error)
        ||!load(db,"router_work_order_services",{"id","order_number","amount"},{"order_uid","service_uid","title"},&next.services,error)
        ||!load(db,"router_service_masters",{"service_id","master_id","share_percent"},{"service_uid","order_uid"},&next.assignments,error)
        ||!load(db,"payroll_order_adjustments",{"master_id","order_number","absent","deleted"},{"order_uid"},&next.adjustments,error)
        ||!load(db,"outsource_jobs",{"service_id"},{"service_uid","order_uid"},&next.outsources,error))return false;
    next.orderIndex=Index(next.headers,"order_uid","number");
    next.serviceIndex=Index(next.services,"service_uid","id");
    *graph=std::move(next);
    return true;
}

inline QDate closedDate(const Record& row){return DateFormats::parseDate(row.value("closed_at").toString().trimmed().left(10));}
inline QList<PayrollOrderRow> read(QSqlDatabase db,const QString& master,const QString& from,const QString& to,
                                  double percent,QString* error,const QString& internalOnly={},const Graph* prepared=nullptr)
{
    if(error)error->clear();
    QList<PayrollOrderRow> out;
    auto fail=[&](const QString& text){reject(error,text);return QList<PayrollOrderRow>{};};
    if(!db.isOpen())return fail("Payroll database is not open");
    Graph local;
    if(!prepared&&!loadGraph(db,&local,error))return {};
    const auto& graph=prepared?*prepared:local;
    const auto& headers=graph.headers;const auto& services=graph.services;const auto& assignments=graph.assignments;
    const auto& adjustments=graph.adjustments;const auto& outsources=graph.outsources;
    const auto& orderIndex=graph.orderIndex;const auto& serviceIndex=graph.serviceIndex;
    if(!internalOnly.isEmpty()&&orderIndex.numbers.value(internalOnly).size()!=1)return fail("Payroll order identity is ambiguous or unavailable");
    const QDate start=DateFormats::parseDate(from),end=DateFormats::parseDate(to);
    QSet<int> selected;
    for(int i=0;i<headers.size();++i){const auto& h=headers[i];
        if(!internalOnly.isEmpty()?h.value("number").toString()==internalOnly
            :h.value("status").toString()=="closed"&&closedDate(h)>=start&&closedDate(h)<=end)selected.insert(i);
    }
    if(selected.isEmpty())return out;
    QHash<int,QPair<bool,bool>> flags;
    for(const auto& adj:adjustments)if(adj.value("master_id").toString()==master){
        const auto matches=orderIndex.matches(adj,"order_uid","order_number");
        if(matches.size()!=1){
            bool relevant=matches.isEmpty();for(int i:matches)relevant|=selected.contains(i);
            if(relevant)return fail("Payroll adjustment owner is ambiguous or unavailable");
            continue;
        }
        const int owner=matches.first();if(!selected.contains(owner))continue;
        if(flags.contains(owner))return fail("Duplicate payroll adjustment owner");
        flags.insert(owner,{adj.value("absent").toBool(),adj.value("deleted").toBool()});
    }
    QHash<int,int> owners;
    QHash<int,QList<QPair<QString,double>>> shares;
    for(const auto& assignment:assignments){
        const auto hits=serviceIndex.matches(assignment,"service_uid","service_id");
        if(hits.size()!=1){
            if(assignment.value("master_id").toString()==master)return fail("Payroll assignment service owner is ambiguous or unavailable");
            continue;
        }
        const int service=hits.first();
        const auto serviceUid=services[service].value("service_uid").toString().trimmed();
        if(!serviceUid.isEmpty()&&serviceIndex.uids.value(serviceUid).size()!=1)return fail("Duplicate payroll service UID");
        const auto orderHits=orderIndex.matches(services[service],"order_uid","order_number");
        if(orderHits.size()!=1){if(assignment.value("master_id").toString()==master)return fail("Payroll service order owner is ambiguous or unavailable");continue;}
        const int owner=orderHits.first();if(!selected.contains(owner))continue;
        const auto ownerUid=headers[owner].value("order_uid").toString().trimmed();
        if(!ownerUid.isEmpty()&&orderIndex.uids.value(ownerUid).size()!=1)return fail("Duplicate payroll order UID");
        const auto claimed=assignment.value("order_uid").toString().trimmed();
        if(!claimed.isEmpty()&&claimed!=headers[owner].value("order_uid").toString().trimmed())return fail("Payroll assignment order UID conflicts with service");
        owners.insert(service,owner);
        const QString employee=assignment.value("master_id").toString();const double share=assignment.value("share_percent").toDouble();
        if(!qIsFinite(share)||share<0||share>100)return fail("Invalid payroll work share");
        double total=share;
        for(const auto& existing:shares[service]){if(existing.first==employee)return fail("Duplicate payroll work assignment");total+=existing.second;}
        if(total>100.0000001)return fail("Payroll work shares exceed 100 percent");
        shares[service].append({employee,share});
    }
    QSet<int> excluded;
    for(const auto& outsourced:outsources){
        const auto hits=serviceIndex.matches(outsourced,"service_uid","service_id");
        if(hits.size()>1){for(int i:hits)if(owners.contains(i))return fail("Outsource service owner is ambiguous");}
        if(hits.isEmpty()&&!outsourced.value("service_uid").toString().trimmed().isEmpty())
            for(int i:serviceIndex.numbers.value(outsourced.value("service_id").toString()))
                if(owners.contains(i))return fail("Outsource service UID is unavailable");
        if(hits.size()==1){const int i=hits.first();
            const auto claimed=outsourced.value("order_uid").toString().trimmed();
            if(owners.contains(i)&&!claimed.isEmpty()&&claimed!=headers[owners[i]].value("order_uid").toString().trimmed())return fail("Outsource order UID conflicts with service");
            excluded.insert(i);
        }
    }
    QHash<int,qint64> baseCents,payCents;QHash<int,QStringList> text;QSet<int> used;
    QList<int> serviceOrder=owners.keys();std::sort(serviceOrder.begin(),serviceOrder.end(),[&](int a,int b){return services[a].value("id").toLongLong()<services[b].value("id").toLongLong();});
    for(int service:serviceOrder){if(excluded.contains(service))continue;const int owner=owners[service];if(flags.value(owner).second)continue;
        const auto& h=headers[owner];const auto& s=services[service];
        const double discount=FormatUtils::discountPercent(h.value("discount").toDouble(),h.value("discount_mode").toString(),h.value("works_sum").toDouble());
        for(const auto& share:shares[service])if(share.first==master){
            const double amount=s.value("amount").toDouble();if(!qIsFinite(amount)||!qIsFinite(percent))return fail("Invalid payroll amount");
            const double base=qMax(0.0,FormatUtils::roundMoneyValue(amount*share.second/100.0*(1-discount/100.0)));
            const double pay=FormatUtils::roundMoneyValue(base*percent/100.0);
            baseCents[owner]+=qRound64(base*100);payCents[owner]+=qRound64(pay*100);used.insert(owner);
            text[owner].append(QString::fromUtf8("%1 — %2 грн").arg(s.value("title").toString(),FormatUtils::money(pay,2)));
        }
    }
    QList<int> ordered=used.values();std::sort(ordered.begin(),ordered.end(),[&](int a,int b){const auto da=closedDate(headers[a]),dbDate=closedDate(headers[b]);if(da!=dbDate)return da<dbDate;
        const auto na=headers[a].value("number").toLongLong(),nb=headers[b].value("number").toLongLong();return na==nb?headers[a].value("order_uid").toString()<headers[b].value("order_uid").toString():na<nb;});
    for(int owner:ordered){const auto& h=headers[owner];PayrollOrderRow row;
        row.internalOrderNumber=h.value("number").toInt();row.orderNumber=h.value("order_number").isNull()?h.value("number").toString():h.value("order_number").toString();
        row.dateStart=ReportsDateUtils::displayDate(h.value("opened_at").toString());row.dateEnd=ReportsDateUtils::displayDate(h.value("closed_at").toString(),h.value("opened_at").toString());
        row.car=composePlateCarForUi(h.value("plate").toString(),h.value("car").toString());row.engine=h.value("engine_type").toString()+"\n"+h.value("engine_volume").toString();
        row.amount=baseCents[owner]/100.0;row.absent=flags.value(owner).first;row.masterAmount=row.absent?0:payCents[owner]/100.0;row.works=text[owner].join("\n");out.append(row);
    }
    return out;
}
}
