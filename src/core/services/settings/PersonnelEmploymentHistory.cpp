#include "infrastructure/db/sqlite/Db.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
#include "core/services/ActivityAuditService.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>

namespace PersonnelEmploymentHistory {
namespace {
QString key(const QString& id) {return "settings.staff.employment."+id;}
QDate date(const QString& value) {
    auto result=QDate::fromString(value,Qt::ISODate);
    if(!result.isValid())result=QDate::fromString(value,"dd.MM.yyyy");
    if(!result.isValid()){result=QDate::fromString(value,"dd.MM.yy");if(result.isValid() && result.year()<2000)result=result.addYears(100);}
    return result;
}
QJsonObject snapshot(QSqlDatabase db,const QString& id) {
    QSqlQuery q(db);q.prepare("SELECT name,role,active,active_from,active_to FROM masters WHERE id=?");q.addBindValue(id);
    if(!q.exec() || !q.next())return {};
    return {{"name",q.value(0).toString()},{"role",q.value(1).toString()},{"active",q.value(2).toBool()},
        {"from",q.value(3).toString()},{"to",q.value(4).toString()}};
}
QJsonObject period(const QJsonObject& row) {
    return {{"from",row["from"]},{"to",row["active"].toBool()?QString():row["to"].toString()},
        {"name",row["name"]},{"role",row["role"]}};
}
bool fail(QSqlDatabase db,QString* error,const QString& message) {db.rollback();if(error)*error=message;return false;}
bool finish(QSqlDatabase db,const QString& id,QJsonObject history,const QJsonObject& before,const QString& operation,QString* error) {
    const auto actor=ActivityAuditService::currentActor();
    auto changes=history["changes"].toArray();changes.append(QJsonObject{{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"operation",operation},{"actor_id",actor.accountId},{"actor_name",actor.displayName},{"before",before},{"after",snapshot(db,id)}});history["changes"]=changes;
    QSqlQuery q(db);q.prepare("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    q.addBindValue(key(id));q.addBindValue(QString::fromUtf8(QJsonDocument(history).toJson(QJsonDocument::Compact)));
    if(!q.exec() || !db.commit())return fail(db,error,QString::fromUtf8("Не вдалося зберегти дані та історію працівника."));
    if(error)error->clear();return true;
}
bool begin(QSqlDatabase db,QString* error) {
    QSqlQuery q(db);if(q.exec("BEGIN IMMEDIATE"))return true;
    if(error)*error=QString::fromUtf8("Дані зайняті іншою операцією. Повторіть після оновлення.");return false;
}
}
QJsonObject load(QSqlDatabase db,const QString& id,QString* error) {
    if(error)error->clear();
    QSqlQuery q(db);q.prepare("SELECT value FROM settings WHERE key=?");q.addBindValue(key(id));
    if(!q.exec()){if(error)*error=q.lastError().text();return {};}
    if(!q.next()) {
        const auto row=snapshot(db,id);
        return row.isEmpty()?QJsonObject():QJsonObject{{"periods",QJsonArray{period(row)}},{"changes",QJsonArray{}}};
    }
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(q.value(0).toString().toUtf8(),&parse);
    const auto obj=doc.object();
    if(parse.error!=QJsonParseError::NoError || !obj["periods"].isArray() || obj["periods"].toArray().isEmpty() || !obj["changes"].isArray()) {
        if(error)*error=QString::fromUtf8("Пошкоджено історію працівника. Збереження заблоковано.");return {};
    }
    const auto periods=obj["periods"].toArray();QDate previousEnd;
    for(int i=0;i<periods.size();++i) {
        const auto p=periods[i].toObject();const auto from=p["from"].toString(),to=p["to"].toString();
        if(!p["from"].isString() || !p["to"].isString() || !p["name"].isString() || !p["role"].isString()
           || (!from.isEmpty() && !date(from).isValid()) || (!to.isEmpty() && !date(to).isValid())
           || (date(from).isValid() && date(to).isValid() && date(to)<date(from))
           || (i+1<periods.size() && to.isEmpty()) || (i>0 && (!date(from).isValid() || date(from)<previousEnd))) {
            if(error)*error=QString::fromUtf8("Некоректні періоди роботи. Збереження заблоковано.");return {};
        }
        previousEnd=date(to);
    }
    return obj;
}
bool overlaps(QSqlDatabase db,const QString& id,const QDate& from,const QDate& to) {
    QString error;const auto history=load(db,id,&error);
    if(!error.isEmpty()){qWarning()<<error;return false;}
    for(const auto value:history["periods"].toArray()) {
        const auto p=value.toObject();const auto start=date(p["from"].toString()),end=date(p["to"].toString());
        if((!start.isValid() || start<=to) && (!end.isValid() || end>=from))return true;
    }
    return false;
}
bool edit(QSqlDatabase db,const MasterRow& expected,const QString& name,const QString& role,QString* error) {
    if(!begin(db,error))return false;
    const auto before=snapshot(db,expected.id);
    if(before.isEmpty() || before["name"].toString()!=expected.name || before["role"].toString()!=expected.role || before["active"].toBool()!=expected.active
       || before["from"].toString()!=expected.activeFrom || before["to"].toString()!=expected.activeTo)
        return fail(db,error,QString::fromUtf8("Дані працівника змінилися. Оновіть список."));
    if(name.simplified().isEmpty() || (role!="master" && role!="admin"))return fail(db,error,QString::fromUtf8("Вкажіть ім’я та посаду."));
    QSqlQuery duplicate(db);duplicate.prepare("SELECT id,name FROM masters WHERE active=1 AND id<>?");duplicate.addBindValue(expected.id);
    if(!duplicate.exec())return fail(db,error,duplicate.lastError().text());
    while(duplicate.next())if(expected.active && duplicate.value(1).toString().simplified().compare(name.simplified(),Qt::CaseInsensitive)==0)
        return fail(db,error,QString::fromUtf8("Працівник із таким ім’ям уже працює."));
    QString problem;auto history=load(db,expected.id,&problem);if(!problem.isEmpty())return fail(db,error,problem);
    QSqlQuery q(db);q.prepare("UPDATE masters SET name=?,role=? WHERE id=?");q.addBindValue(name.simplified());q.addBindValue(role);q.addBindValue(expected.id);
    if(!q.exec())return fail(db,error,q.lastError().text());
    return finish(db,expected.id,history,before,"edit",error);
}
bool setActive(QSqlDatabase db,const QString& id,bool active,const QString& effectiveDate,QString* error,const QString& previousEnd) {
    if(!begin(db,error))return false;
    const auto before=snapshot(db,id);const auto day=date(effectiveDate);
    if(before.isEmpty() || !day.isValid() || day>QDate::currentDate())return fail(db,error,QString::fromUtf8("Працівника не знайдено або дата недійсна."));
    if(before["active"].toBool()==active)return fail(db,error,QString::fromUtf8("Стан працівника вже змінено. Оновіть список."));
    QString problem;auto history=load(db,id,&problem);if(!problem.isEmpty())return fail(db,error,problem);
    auto periods=history["periods"].toArray();auto last=periods.last().toObject();
    if(active) {
        if(last["to"].toString().isEmpty() && date(previousEnd).isValid()) {
            if(date(last["from"].toString()).isValid() && date(previousEnd)<date(last["from"].toString()))return fail(db,error,QString::fromUtf8("Звільнення не може бути раніше прийняття."));
            last["to"]=date(previousEnd).toString(Qt::ISODate);periods[periods.size()-1]=last;
        }
        const auto end=date(last["to"].toString());
        if(!end.isValid() || day<end)return fail(db,error,QString::fromUtf8("Потрібна відома дата звільнення; поновлення не може бути раніше неї."));
        periods.append(QJsonObject{{"from",day.toString(Qt::ISODate)},{"to",""},{"name",before["name"]},{"role",before["role"]}});
    } else {
        const auto start=date(last["from"].toString());
        if(start.isValid() && day<start)return fail(db,error,QString::fromUtf8("Звільнення не може бути раніше прийняття."));
        last["to"]=day.toString(Qt::ISODate);periods[periods.size()-1]=last;
    }
    history["periods"]=periods;
    QSqlQuery q(db);q.prepare("UPDATE masters SET active=?,active_to=? WHERE id=?");q.addBindValue(active?1:0);q.addBindValue(active?QStringLiteral(""):day.toString(Qt::ISODate));q.addBindValue(id);
    if(!q.exec())return fail(db,error,q.lastError().text());
    return finish(db,id,history,before,active?"reinstate":"dismiss",error);
}
}

namespace PersonnelEmploymentHistory {
QJsonObject load(const QString& id, QString* error) { return load(Database::db(), id, error); }
bool edit(const MasterRow& expected, const QString& name, const QString& role, QString* error) { return edit(Database::db(), expected, name, role, error); }
bool setActive(const QString& id, bool active, const QString& effectiveDate, QString* error, const QString& previousEnd) { return setActive(Database::db(), id, active, effectiveDate, error, previousEnd); }
}
