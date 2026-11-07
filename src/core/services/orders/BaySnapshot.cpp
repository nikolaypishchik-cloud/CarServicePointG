#include "core/services/orders/BaySnapshot.h"
#include "core/services/orders/LocalWorkshopBackend.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
#include <QMutex>
#include <QMutexLocker>
#include "core/cache/OrderSnapshotCache.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>

namespace {
QMutex cacheMutex;
QMap<QString,int> cachedPosts;
qint64 integer(QJsonValue value) {
    const double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n < 0 || n > 9007199254740991.0 || std::floor(n) != n)
        throw std::runtime_error("Invalid snapshot integer");
    return static_cast<qint64>(n);
}

}
void BaySnapshot::remember(const Data& data) {
    if(!data.error.isEmpty() || !data.asOf.isValid()) return;
    QMap<QString,int> posts;
    for(auto it=data.states.begin();it!=data.states.end();++it) {
        const auto state=it.value().toObject();
        posts.insert(it.key(),state.value("state")=="on_bay"?state.value("bay_uid").toString().toInt():0);
    }
    bool changed=false;
    { const QMutexLocker lock(&cacheMutex);changed=cachedPosts!=posts;cachedPosts=posts; }
    if(changed) OrderSnapshotCache::invalidateCurrentOrders("bay_authority_snapshot");
}
QMap<QString,int> BaySnapshot::currentPosts() {
    const QMutexLocker lock(&cacheMutex);return cachedPosts;
}
void BaySnapshot::refresh(QObject* owner, const std::function<void()>& updated) {
    if(!owner || owner->property("baySnapshotFetch").toBool()) return;
    if(CloudSyncSettings::localBackendEnabled()) {
        const auto before=currentPosts();
        const auto reply=LocalWorkshopBackend::request(Database::db(),"snapshot");
        if(reply.ok){remember(parse(reply.body));if(updated && before!=currentPosts())updated();}return;
    }
    if(!CloudSyncSettings::serverRequestsAllowed())return;
    const auto stored=Database::getSetting("settings.cloudsync.api_token",QString()).toString().trimmed();
    const auto token=stored.startsWith("dpapi:v1:")?CloudSyncCredentials::unprotectToken(stored):stored;
    if(token.isEmpty()) return;
    owner->setProperty("baySnapshotFetch",true);
    auto* manager=new QNetworkAccessManager(owner);
    QNetworkRequest request(QUrl(CloudSyncSettings::fixedServerUrl()+"/api/v1/bays/snapshot"));
    request.setRawHeader("Authorization","Bearer "+token.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply=manager->get(request);
    QObject::connect(reply,&QNetworkReply::finished,owner,[owner,manager,reply,updated]{
        owner->setProperty("baySnapshotFetch",false);
        const auto before=currentPosts();
        const auto document=QJsonDocument::fromJson(reply->readAll());
        const auto data=parse(document.object());
        if(reply->error()==QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200 && data.error.isEmpty()) remember(data);
        else { QMutexLocker lock(&cacheMutex);cachedPosts.clear(); }
        manager->deleteLater();
        if(before!=currentPosts()) {OrderSnapshotCache::invalidateCurrentOrders("bay_authority_refresh");if(updated) updated();}
    });
}
namespace {
QString requiredText(QJsonValue value) {
    if (!value.isString() || value.toString().isEmpty()) throw std::runtime_error("Missing snapshot identifier");
    return value.toString();
}
QJsonArray array(QJsonValue value) {
    if (!value.isArray()) throw std::runtime_error("Missing snapshot collection");
    return value.toArray();
}
std::optional<qint64> optionalTime(QJsonValue value) {
    if (value.isNull()) return {};
    return integer(value);
}
}

BaySnapshot::Data BaySnapshot::parse(const QJsonObject& object) {
    Data result;
    try {
        const auto now = integer(object.value("as_of_ms"));
        result.asOf = QDateTime::fromMSecsSinceEpoch(now, QTimeZone::UTC);
        QMap<QString, int> indices;
        for (const auto value : array(object.value("bays"))) {
            const auto b = value.toObject();
            BayLoadReport::Bay bay;
            bay.uid = requiredText(b.value("uid"));
            if (indices.contains(bay.uid)) throw std::runtime_error("Duplicate bay");
            result.names.insert(bay.uid, requiredText(b.value("name")));
            bay.activeFrom = integer(b.value("active_from"));
            bay.inactiveFrom = optionalTime(b.value("inactive_from"));
            for(const auto value:b.value("unavailable").toArray()) {
                const auto span=value.toObject();
                const auto begin=integer(span.value("begin_ms")), end=integer(span.value("end_ms"));
                if(end<=begin) throw std::runtime_error("Invalid unavailability");
                bay.unavailable.append({begin,end});
            }
            const auto confirmed = optionalTime(b.value("confirmed_from"));
            if (confirmed) {
                if (*confirmed < bay.activeFrom || *confirmed > now) throw std::runtime_error("Invalid coverage boundary");
                if (*confirmed < now) bay.coverage.append({*confirmed, now});
            }
            for (const auto cValue : array(b.value("calendars"))) {
                const auto c = cValue.toObject();
                if (c.value("timezone").toString() != "Europe/Kyiv") throw std::runtime_error("Unsupported calendar timezone");
                BayLoadReport::CalendarVersion calendar;
                calendar.from = QDate::fromString(c.value("from").toString(), Qt::ISODate);
                if (!c.value("until").isNull()) calendar.until = QDate::fromString(c.value("until").toString(), Qt::ISODate);
                if (!calendar.from.isValid() || (!c.value("until").isNull() && !calendar.until.isValid()))
                    throw std::runtime_error("Invalid calendar date");
                for (const auto day : array(c.value("weekdays"))) {
                    const auto d = integer(day);
                    if (d < 1 || d > 7) throw std::runtime_error("Invalid weekday");
                    calendar.weekdays.insert(static_cast<int>(d));
                }
                for (const auto shiftValue : array(c.value("shifts"))) {
                    const auto shift = array(shiftValue);
                    if (shift.size() != 2) throw std::runtime_error("Invalid shift");
                    const auto begin = QTime::fromString(shift[0].toString(), "HH:mm");
                    const auto end = QTime::fromString(shift[1].toString(), "HH:mm");
                    if (!begin.isValid() || !end.isValid() || begin >= end) throw std::runtime_error("Invalid shift times");
                    calendar.shifts.append({begin, end});
                }
                bay.calendars.append(calendar);
            }
            indices.insert(bay.uid, result.bays.size());
            result.bays.append(bay);
        }
        QSet<QString> intervalIds;
        for (const auto value : array(object.value("intervals"))) {
            const auto i = value.toObject();
            const auto uid = requiredText(i.value("uid"));
            const auto bayUid = requiredText(i.value("bay_uid"));
            if (intervalIds.contains(uid) || !indices.contains(bayUid)) throw std::runtime_error("Invalid interval reference");
            intervalIds.insert(uid);
            BayLoadReport::Occupancy occupancy;
            occupancy.orderUid = requiredText(i.value("order_uid"));
            occupancy.begin = integer(i.value("begin_ms"));
            occupancy.end = optionalTime(i.value("end_ms"));
            if (occupancy.begin > now || (occupancy.end && (*occupancy.end > now || *occupancy.end <= occupancy.begin)))
                throw std::runtime_error("Invalid occupancy boundaries");
            result.bays[indices.value(bayUid)].occupancy.append(occupancy);
        }
        for (const auto value : array(object.value("states"))) {
            const auto s = value.toObject();
            const auto uid = requiredText(s.value("order_uid"));
            const auto state = s.value("state").toString();
            if (result.states.contains(uid) || integer(s.value("version")) < 1 ||
                (state != "on_bay" && state != "off_bay") ||
                (state == "on_bay" && !indices.contains(s.value("bay_uid").toString())) ||
                (state == "off_bay" && !s.value("bay_uid").isNull()))
                throw std::runtime_error("Invalid occupancy state");
            result.states.insert(uid, s);
        }
    } catch (const std::exception& error) {
        result = {};
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}
