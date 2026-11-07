#include "core/services/planning/PlanningRepository.h"
#include "core/services/AccessControlService.h"
#include "core/util/DateFormats.h"

#include "core/backup/BackupCommitPublisher.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "infrastructure/db/sqlite/Db.h"

#include "core/services/planning/PlanningDispatch.h"
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

static QString toIso(const QDate& d) { return d.toString(Qt::ISODate); }

QString ownPlanningName()
{
    QSqlQuery q(Database::db());
    q.prepare("SELECT name FROM masters WHERE id=? AND "
              "(SELECT COUNT(*) FROM masters other WHERE trim(other.name)=trim(masters.name))=1");
    q.addBindValue(AccessControlService::currentMasterId(Database::db()));
    return q.exec() && q.next() ? q.value(0).toString().trimmed() : QString();
}

bool ownPlanningEntry(const QString& text, const QString& name)
{
    if (text.contains(QStringLiteral(" || "))) {
        const auto entries = text.split(QStringLiteral(" || "), Qt::SkipEmptyParts);
        for (const auto& entry : entries) if (!ownPlanningEntry(entry, name)) return false;
        return !entries.isEmpty();
    }
    static const QRegularExpression masterIdRe(
        QString::fromUtf8(R"((?:^|\|)\s*Майстер ID:\s*([^|]+)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const auto identity = masterIdRe.match(text);
    if (identity.hasMatch()) {
        return identity.captured(1).trimmed() == AccessControlService::currentMasterId(Database::db());
    }
    if (name.isEmpty()) return false;
    const auto fields = text.split(QLatin1Char('|'));
    // Mobile appointments carry the master's name in the seventh structured field.
    if (fields.size() >= 9 && fields.value(6).trimmed() == name) return true;
    return fields.last().trimmed() == QString::fromUtf8("Майстер: ") + name;
}

static QString serializeDay(const QMap<int, QString>& map)
{
    QStringList lines;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const int h = it.key();
        const QString t = it.value().trimmed();
        if (t.isEmpty()) continue;
        lines << QString("%1:00 %2").arg(h, 2, 10, QChar('0')).arg(t);
    }
    return lines.join("\n");
}

static QMap<int, QString> parseDay(const QString& note)
{
    QMap<int, QString> out;
    const QStringList lines = note.split('\n');
    static QRegularExpression re(R"(^\s*(\d{1,2})\s*[:\.]\s*(\d{2})\s*(.*)$)");
    for (const QString& ln : lines) {
        const QString s = ln.trimmed();
        if (s.isEmpty()) continue;
        const auto m = re.match(s);
        if (!m.hasMatch()) continue;
        const int h = m.captured(1).toInt();
        QString txt = m.captured(3).trimmed();
        if (h < 0 || h > 23) continue;
        if (txt.isEmpty()) continue;
        if (!out.contains(h)) out.insert(h, txt);
    }
    return out;
}

static const QString& reminderActiveMarker()
{
    static const QString marker = QStringLiteral("@reminder:on");
    return marker;
}

static bool noteHasReminderActiveMarker(const QString& note)
{
    const QStringList lines = note.split('\n');
    for (const QString& line : lines) {
        if (line.trimmed().compare(reminderActiveMarker(), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

static QString noteWithoutReminderActiveMarker(const QString& note)
{
    QStringList kept;
    const QStringList lines = note.split('\n');
    for (const QString& line : lines) {
        if (line.trimmed().compare(reminderActiveMarker(), Qt::CaseInsensitive) == 0) {
            continue;
        }
        kept << line;
    }
    return kept.join("\n").trimmed();
}

static QString serializePlanningNote(const QMap<int, QString>& map, bool reminderActive)
{
    QString note = serializeDay(map).trimmed();
    if (reminderActive) {
        if (!note.isEmpty()) note += QLatin1Char('\n');
        note += reminderActiveMarker();
    }
    return note;
}

void publishPlanningBackupEvent(const QDate& day,
                                const QString& operationType,
                                const QString& description)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("PlanningRepository"),
                                         QStringLiteral("planning"),
                                         QStringLiteral("planning_day"),
                                         day.toString(Qt::ISODate),
                                         QString(),
                                         0,
                                         operationType,
                                         description,
                                         {QStringLiteral("planning_calendar")}),
        nullptr);
}

QMap<QDate, QString> loadMonthNotes(int year, int month)
{
    QMap<QDate, QString> out;
    const QDate first(year, month, 1);
    if (!first.isValid()) return out;
    const QDate last(year, month, first.daysInMonth());

    QSqlQuery q(Database::db());
    q.setForwardOnly(true);
    const QString daySql = DateFormats::isoSql("day");
    q.prepare("SELECT day, note FROM planning_calendar WHERE " + daySql + ">=? AND " + daySql + "<=? ORDER BY " + daySql);
    q.addBindValue(toIso(first));
    q.addBindValue(toIso(last));
    if (!q.exec()) return out;

    while (q.next()) {
        const QDate day = DateFormats::parseDate(q.value(0).toString());
        if (!day.isValid()) continue;
        out.insert(day, q.value(1).toString());
    }
    return out;
}

} // namespace

namespace PlanningDispatch {
Entry decode(QDate day,int hour,const QString& text)
{
    Entry e; e.day=day; e.hour=hour; e.start=hour*60; e.raw=text;
    e.cancelled=text.contains(QStringLiteral("❌")) || text.contains(QStringLiteral("✕"));
    const auto fields=text.split('|');
    e.work=text;
    if(fields.size()>=9) {
        e.plate=fields[0].trimmed(); e.vehicle=fields[1].trimmed();
        e.client=fields[3].trimmed(); e.work=fields[5].trimmed(); e.mechanic=fields[6].trimmed();
    }
    for(const auto& part:fields) {
        const auto f=part.trimmed();
        if(f.startsWith(QStringLiteral("Майстер ID:"))) e.mechanicId=f.section(':',1).trimmed();
        if(f.startsWith(QStringLiteral("Майстер:"))) e.mechanic=f.section(':',1).trimmed();
        if(f.startsWith(QStringLiteral("Пост:"))) e.bay=f.section(':',1).trimmed().toInt();
        if(f.startsWith(QStringLiteral("Інтервал:"))) {
            const auto times=f.sliced(QStringLiteral("Інтервал:").size()).trimmed().split('-');
            const auto a=QTime::fromString(times.value(0),"HH:mm"), b=QTime::fromString(times.value(1),"HH:mm");
            if(a.isValid() && b>a) {e.start=a.hour()*60+a.minute(); e.minutes=a.secsTo(b)/60;}
        }
    }
    return e;
}
QString encode(const Entry& e)
{
    const QTime begin(e.start/60,e.start%60);
    return QStringList{e.plate,e.vehicle,{},e.client,{},e.work,e.mechanic,{},QStringLiteral("Запис СТО"),
        QStringLiteral("Пост: %1").arg(e.bay),QStringLiteral("Інтервал: %1-%2").arg(begin.toString("HH:mm"),begin.addSecs(e.minutes*60).toString("HH:mm")),
        QStringLiteral("Майстер ID: %1").arg(e.mechanicId)}.join(QStringLiteral(" | ")).trimmed();
}
bool overlaps(const Entry& a,const Entry& b)
{
    return a.day.isValid() && a.day==b.day && !a.cancelled && !b.cancelled && a.minutes>0 && b.minutes>0
        && a.start<b.start+b.minutes && b.start<a.start+a.minutes
        && ((a.bay>0 && a.bay==b.bay) || (!a.mechanicId.isEmpty() && a.mechanicId==b.mechanicId)
            || ((a.mechanicId.isEmpty() || b.mechanicId.isEmpty()) && !a.mechanic.isEmpty() && a.mechanic==b.mechanic));
}
static bool ensureQueue(QSqlDatabase db,QString* error)
{
    QSqlQuery q(db);
    if(q.exec("CREATE TABLE IF NOT EXISTS planning_dispatch_queue(uid TEXT PRIMARY KEY,entry TEXT NOT NULL)"))return true;
    if(error)*error=q.lastError().text();return false;
}
QList<Entry> queue(QString* error)
{
    QList<Entry> out;
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"nav.planning") || !ensureQueue(db,error))return out;
    QSqlQuery q(db);q.prepare("SELECT uid,entry FROM planning_dispatch_queue ORDER BY rowid");
    if(!q.exec()){if(error)*error=q.lastError().text();return out;}
    const bool limited=AccessControlService::currentActorIsMaster();
    const auto id=limited?AccessControlService::currentMasterId(db):QString();
    while(q.next()){auto e=decode({},0,q.value(1).toString());e.queueId=q.value(0).toString();
        if(!limited || (!id.isEmpty() && e.mechanicId==id))out.append(e);}
    return out;
}
bool save(const Entry& before,const Entry& after,QString* error)
{
    auto db=Database::db();
    auto fail=[&](QString text){if(error)*error=text;return false;};
    if(!AccessControlService::currentUserHasPermission(db,"nav.planning"))return fail(QStringLiteral("Бракує прав на планування."));
    const auto mine=AccessControlService::currentMasterId(db);
    if(AccessControlService::currentActorIsMaster() && (mine.isEmpty() || after.mechanicId!=mine || (!before.raw.isEmpty() && decode(before.day,before.hour,before.raw).mechanicId!=mine)))
        return fail(QStringLiteral("Доступні лише власні записи."));
    for(const auto& field:{after.plate,after.vehicle,after.client,after.work,after.mechanic,after.mechanicId})
        if(field.contains('|') || field.contains('\n') || field.contains('\r'))return fail(QStringLiteral("Вилучіть роздільник або перенесення рядка."));
    if(after.vehicle.trimmed().isEmpty() || after.work.trimmed().isEmpty() || after.bay<0 || after.bay>3 || (after.day.isValid() && (!after.bay || after.mechanicId.isEmpty()))
        || after.minutes<15 || after.minutes%15 || after.start%15 || after.start<480 || after.start+after.minutes>1080)
        return fail(QStringLiteral("Перевірте авто, роботу, механіка та час у межах 08:00–18:00."));
    if(!ensureQueue(db,error))return false;
    if(!db.transaction())return fail(db.lastError().text());
    auto abort=[&](QString text){db.rollback();return fail(text);};
    const auto stale=QStringLiteral("Запис змінився. Оновіть планування.");
    QMap<QDate,QMap<int,QString>> maps;
    QMap<QDate,QString> original;
    for(auto day:{before.day,after.day})if(day.isValid() && !maps.contains(day)){
        QSqlQuery read(db);read.prepare("SELECT note FROM planning_calendar WHERE day IN (?,?)");
        read.addBindValue(toIso(day));read.addBindValue(day.toString("dd.MM.yy"));
        if(!read.exec())return abort(read.lastError().text());
        original[day]=read.next()?read.value(0).toString():QString();
        if(read.next())return abort(QStringLiteral("Знайдено два записи календаря з різним форматом цієї дати."));
        maps[day]=parseDay(original.value(day));
    }
    if(before.day.isValid()){
        auto& m=maps[before.day]; auto items=m.value(before.hour).split(QStringLiteral(" || "),Qt::SkipEmptyParts);
        if(items.count(before.raw)!=1)return abort(stale);
        items.removeOne(before.raw);if(items.isEmpty())m.remove(before.hour);else m[before.hour]=items.join(QStringLiteral(" || "));
    }
    if(!before.queueId.isEmpty()){
        QSqlQuery q(db);q.prepare("DELETE FROM planning_dispatch_queue WHERE uid=? AND entry=?");
        q.addBindValue(before.queueId);q.addBindValue(before.raw);
        if(!q.exec())return abort(q.lastError().text());if(q.numRowsAffected()!=1)return abort(stale);
    }
    const auto raw=encode(after);
    if(after.day.isValid()){
        auto& m=maps[after.day];
        for(auto it=m.cbegin();it!=m.cend();++it)for(const auto& item:it.value().split(QStringLiteral(" || "),Qt::SkipEmptyParts))
            if(overlaps(decode(after.day,it.key(),item),after))return abort(QStringLiteral("Пост або механік зайняті в цей проміжок часу."));
        auto& slot=m[after.start/60];
        if(AccessControlService::currentActorIsMaster() && !slot.isEmpty() && !ownPlanningEntry(slot,ownPlanningName()))
            return abort(QStringLiteral("Ця година вже має призначення іншого працівника."));
        if(!slot.isEmpty())slot+=QStringLiteral(" || ");slot+=raw;
    }else{
        QSqlQuery q(db);q.prepare("INSERT INTO planning_dispatch_queue(uid,entry) VALUES(?,?)");
        q.addBindValue(before.queueId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):before.queueId);q.addBindValue(raw);
        if(!q.exec())return abort(q.lastError().text());
    }
    for(auto it=maps.cbegin();it!=maps.cend();++it){
        const auto day=it.key();
        const auto beforeJson=QString::fromUtf8(QJsonDocument(QJsonObject{{"note",original.value(day)}}).toJson(QJsonDocument::Compact));
        // Preserve unrelated lines, including reminder metadata and imported text.
        QStringList lines=original.value(day).split('\n',Qt::SkipEmptyParts);
        QSet<int> hours;
        if(before.day==day)hours.insert(before.hour);
        if(after.day==day)hours.insert(after.start/60);
        for(int hour:hours){
            int index=-1;
            for(int i=0;i<lines.size();++i)if(parseDay(lines[i]).contains(hour)){
                if(index!=-1)return abort(QStringLiteral("Неоднозначна година в старому записі."));
                index=i;
            }
            const auto replacement=it.value().contains(hour)?serializeDay({{hour,it.value().value(hour)}}):QString();
            if(index<0){if(!replacement.isEmpty())lines.append(replacement);}
            else if(replacement.isEmpty())lines.removeAt(index);
            else lines[index]=replacement;
        }
        const auto note=lines.join('\n');
        QSqlQuery q(db);q.prepare("DELETE FROM planning_calendar WHERE day IN (?,?)");q.addBindValue(toIso(day));q.addBindValue(day.toString("dd.MM.yy"));
        if(!q.exec())return abort(q.lastError().text());
        if(!note.isEmpty()){
            q.prepare("INSERT INTO planning_calendar(day,note) VALUES(?,?)");q.addBindValue(day.toString("dd.MM.yy"));q.addBindValue(note);
            if(!q.exec())return abort(q.lastError().text());
        }
        MutationResult event;event.ok=true;event.sourceModule="PlanningRepository";event.entityType="planning_day";event.entityId=toIso(day);event.auditBeforeJson=beforeJson;event.operation=note.isEmpty()?"delete":"upsert";event.changedTables=QStringList{"planning_calendar"};
        if(!CloudSyncOutboxService::appendMutation(db,event))return abort(QStringLiteral("Не вдалося зафіксувати синхронізацію планування."));
    }
    if(!db.commit())return abort(db.lastError().text());
    for(const auto day:maps.keys())publishPlanningBackupEvent(day,"planning_save",QStringLiteral("Оновлено розклад майстерні"));
    if(!after.day.isValid() || !before.queueId.isEmpty())
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent("PlanningRepository","planning","planning_queue",
            before.queueId,{},0,"planning_queue_save",QStringLiteral("Змінено чергу майстерні"),{"planning_dispatch_queue"}));
    return true;
}
}

QString PlanningRepository::getNote(const QDate& day) const
{
    if (!day.isValid()) return {};
    QSqlQuery q(Database::db());
    q.prepare("SELECT note FROM planning_calendar WHERE day IN (?,?)");
    q.addBindValue(toIso(day));
    q.addBindValue(day.toString("dd.MM.yy"));
    if (!q.exec()) return {};
    if (q.next()) return q.value(0).toString();
    return {};
}

void PlanningRepository::setNote(const QDate& day, const QString& note) const
{
    if (!day.isValid()) return;
    const QString dayIso = toIso(day);
    const QString beforeJson = LegacyMutationLogSupport::snapshotPlanningDay(Database::db(), dayIso);
    const QString n = note.trimmed();
    auto capture = [&]() {
        MutationResult event;
        event.ok = true;
        event.sourceModule = QStringLiteral("PlanningRepository");
        event.entityType = QStringLiteral("planning_day");
        event.entityId = dayIso;
        event.operation = QStringLiteral("upsert");
        event.changedTables = {QStringLiteral("planning_calendar")};
        event.auditBeforeJson = beforeJson;
        return CloudSyncOutboxService::appendMutation(Database::db(), event);
    };
    if (!Database::db().transaction()) return;
    QSqlQuery q(Database::db());
    if (n.isEmpty()) {
        q.prepare("DELETE FROM planning_calendar WHERE day IN (?,?)");
        q.addBindValue(dayIso);
        q.addBindValue(day.toString("dd.MM.yy"));
        if (!q.exec() || !capture()) { Database::db().rollback(); return; }
        if (!Database::db().commit()) { Database::db().rollback(); return; }
        QString logError;
        LegacyMutationLogSupport::logMutation(Database::db(), QStringLiteral("planning"), QStringLiteral("delete"), 0, QString(), dayIso, beforeJson, QStringLiteral("{}"), &logError);
        publishPlanningBackupEvent(day,
                                   QStringLiteral("planning_delete"),
                                   QString::fromUtf8("Видалено планування на %1").arg(day.toString(QStringLiteral("dd.MM.yyyy"))));
        return;
    }
    // Remove the legacy spelling inside the same transaction before upserting the canonical key.
    q.prepare("DELETE FROM planning_calendar WHERE day=?");
    q.addBindValue(dayIso);
    if (!q.exec()) { Database::db().rollback(); return; }
    q.prepare(
        "INSERT INTO planning_calendar(day,note) VALUES(?,?) "
        "ON CONFLICT(day) DO UPDATE SET note=excluded.note"
    );
    q.addBindValue(day.toString("dd.MM.yy"));
    q.addBindValue(n);
    if (!q.exec() || !capture()) { Database::db().rollback(); return; }
    if (!Database::db().commit()) { Database::db().rollback(); return; }
    QString logError;
    LegacyMutationLogSupport::logMutation(Database::db(), QStringLiteral("planning"), beforeJson == QStringLiteral("{}") ? QStringLiteral("insert") : QStringLiteral("update"), 0, QString(), dayIso, beforeJson, LegacyMutationLogSupport::snapshotPlanningDay(Database::db(), dayIso), &logError);
    publishPlanningBackupEvent(day,
                               beforeJson == QStringLiteral("{}") ? QStringLiteral("planning_create") : QStringLiteral("planning_save"),
                               QString::fromUtf8("Оновлено планування на %1").arg(day.toString(QStringLiteral("dd.MM.yyyy"))));
}

QMap<int, QString> PlanningRepository::getDayMap(const QDate& day) const
{
    return parseDay(getNote(day));
}

QMap<int, QString> PlanningRepository::visibleDayMap(const QDate& day) const
{
    if (!AccessControlService::currentUserHasPermission(Database::db(), QStringLiteral("nav.planning"))) return {};
    auto map = getDayMap(day);
    if (!AccessControlService::currentActorIsMaster()) return map;
    const auto name = ownPlanningName();
    for (auto it = map.begin(); it != map.end();) {
        if (!ownPlanningEntry(it.value(), name)) it = map.erase(it);
        else ++it;
    }
    return map;
}

QMap<QDate, QMap<int, QString>> PlanningRepository::visibleMonthMaps(int year, int month) const
{
    if (!AccessControlService::currentUserHasPermission(Database::db(), QStringLiteral("nav.planning"))) return {};
    auto maps = getMonthMaps(year, month);
    if (!AccessControlService::currentActorIsMaster()) return maps;
    const auto name = ownPlanningName();
    for (auto day = maps.begin(); day != maps.end();) {
        for (auto entry = day->begin(); entry != day->end();) {
            if (!ownPlanningEntry(entry.value(), name)) entry = day->erase(entry);
            else ++entry;
        }
        if (day->isEmpty()) day = maps.erase(day);
        else ++day;
    }
    return maps;
}

QMap<QDate, QMap<int, QString>> PlanningRepository::getMonthMaps(int year, int month) const
{
    QMap<QDate, QMap<int, QString>> out;
    const auto notes = loadMonthNotes(year, month);
    for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
        const QMap<int, QString> dayMap = parseDay(it.value());
        bool any = false;
        for (auto jt = dayMap.cbegin(); jt != dayMap.cend(); ++jt) {
            if (!jt.value().trimmed().isEmpty()) { any = true; break; }
        }
        if (any) out.insert(it.key(), dayMap);
    }
    return out;
}

void PlanningRepository::setVisibleDayMap(const QDate& day, const QMap<int, QString>& map) const
{
    if (!AccessControlService::currentUserHasPermission(Database::db(), QStringLiteral("nav.planning"))) return;
    if (AccessControlService::currentActorIsMaster()) {
        const QString name = ownPlanningName();
        if (name.isEmpty()) return;
        auto merged = getDayMap(day);
        // Keep other employees' slots, even though they are absent from this editor.
        for (auto entry = map.cbegin(); entry != map.cend(); ++entry) {
            if (merged.contains(entry.key()) && !ownPlanningEntry(merged.value(entry.key()), name)) return;
        }
        for (auto entry = merged.begin(); entry != merged.end();) {
            if (ownPlanningEntry(entry.value(), name)) entry = merged.erase(entry);
            else ++entry;
        }
        for (auto entry = map.cbegin(); entry != map.cend(); ++entry) {
            QString value = entry.value().trimmed();
            if (value.isEmpty()) continue;
            if (!ownPlanningEntry(value, name)) value += QString::fromUtf8(" | Майстер: ") + name;
            merged.insert(entry.key(), value);
        }
        setNote(day, serializePlanningNote(merged, noteHasReminderActiveMarker(getNote(day))));
        return;
    }
    setDayMap(day, map);
}

void PlanningRepository::setDayMap(const QDate& day, const QMap<int, QString>& map) const
{
    const bool reminderActive = noteHasReminderActiveMarker(getNote(day));
    setNote(day, serializePlanningNote(map, reminderActive));
}

bool PlanningRepository::isDayReminderActive(const QDate& day) const
{
    return day.isValid() && noteHasReminderActiveMarker(getNote(day));
}

QSet<QString> PlanningRepository::getMonthReminderActiveDayKeys(int year, int month) const
{
    if (!AccessControlService::currentUserHasPermission(Database::db(), QStringLiteral("nav.planning"))) return {};
    QSet<QString> out;
    const auto notes = loadMonthNotes(year, month);
    for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
        if (noteHasReminderActiveMarker(it.value())) {
            if (AccessControlService::currentActorIsMaster() && visibleDayMap(it.key()).isEmpty()) continue;
            out.insert(it.key().toString(Qt::ISODate));
        }
    }
    return out;
}

void PlanningRepository::setDayReminderActive(const QDate& day, bool active) const
{
    if (!day.isValid()) return;
    const QString current = getNote(day);
    const QMap<int, QString> map = parseDay(noteWithoutReminderActiveMarker(current));
    setNote(day, serializePlanningNote(map, active));
}
