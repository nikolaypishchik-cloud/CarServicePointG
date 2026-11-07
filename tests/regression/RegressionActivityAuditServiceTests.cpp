#include "core/services/ActivityAuditService.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <iostream>

namespace {

bool require(bool value, const char* message)
{
    if (value) return true;
    std::cerr << message << std::endl;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("activity_audit_regression"));
    database.setDatabaseName(QStringLiteral(":memory:"));
    if (!require(database.open(), "Cannot open test database")) return 1;

    QSqlQuery setup(database);
    if (!require(setup.exec(QStringLiteral(
            "CREATE TABLE masters(id TEXT PRIMARY KEY, name TEXT NOT NULL, role TEXT NOT NULL DEFAULT 'master', active INTEGER NOT NULL DEFAULT 1)")),
            "Cannot create masters table")) return 1;
    if (!require(setup.exec(QStringLiteral(
            "INSERT INTO masters(id,name,role,active) VALUES('m1','Тестовий майстер','master',1)")),
            "Cannot seed master")) return 1;

    QString errorText;
    if (!require(StaffAccountService::ensureSchema(database, &errorText), qPrintable(errorText))) return 1;
    const QVector<StaffAccount> accounts = StaffAccountService::accounts(database, &errorText);
    if (!require(accounts.size() == 2, "Expected admin and master accounts")) return 1;
    if (!require(StaffAccountService::setPin(database, QStringLiteral("master:m1"), QStringLiteral("1234"), &errorText), qPrintable(errorText))) return 1;
    if (!require(StaffAccountService::setPin(database, QStringLiteral("admin"), QStringLiteral("5678"), &errorText), qPrintable(errorText))) return 1;

    ActivityAuditActor actor;
    if (!require(StaffAccountService::signIn(database, QStringLiteral("master:m1"), QStringLiteral("1234"), &actor, &errorText), qPrintable(errorText))) return 1;
    if (!require(actor.displayName == QString::fromUtf8("Тестовий майстер"), "Unexpected signed-in actor")) return 1;
    if (!require(ActivityAuditService::record(database,
                                              QStringLiteral("parts"),
                                              QStringLiteral("installed"),
                                              QString::fromUtf8("Встановлено оливний фільтр"),
                                              42,
                                              QStringLiteral("work_order_part"),
                                              QStringLiteral("7"),
                                              QString::fromUtf8("Оливний фільтр"),
                                              QStringLiteral("{}"),
                                              &errorText),
                 qPrintable(errorText))) return 1;

    ActivityAuditFilter filter;
    filter.from = QDate::currentDate();
    filter.to = QDate::currentDate();
    const QVector<ActivityAuditEvent> events = ActivityAuditService::load(database, filter, &errorText);
    if (!require(errorText.isEmpty(), qPrintable(errorText))) return 1;
    if (!require(events.size() >= 2, "Expected login and parts events")) return 1;

    bool foundPartEvent = false;
    for (const ActivityAuditEvent& event : events) {
        if (event.action == QStringLiteral("installed") && event.actor.accountId == QStringLiteral("master:m1") && event.orderNumber == 42) {
            foundPartEvent = true;
        }
    }
    if (!require(foundPartEvent, "Part event lost actor or order traceability")) return 1;

    QSqlQuery legacy(database);
    legacy.prepare(QStringLiteral("UPDATE staff_accounts SET pin_hash=? WHERE id='admin'"));
    legacy.addBindValue(QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("cs_staff_pin_v1:admin:5678"), QCryptographicHash::Sha256).toHex()));
    if (!require(legacy.exec(), "Cannot seed legacy PIN")) return 1;
    if (!require(StaffAccountService::signIn(database, QStringLiteral("admin"), QStringLiteral("5678"), &actor, &errorText), qPrintable(errorText))) return 1;
    if (!require(legacy.exec(QStringLiteral("SELECT pin_hash FROM staff_accounts WHERE id='admin'")) && legacy.next()
                 && legacy.value(0).toString().startsWith(QStringLiteral("pbkdf2-sha256$600000$")),
                 "Successful login must migrate legacy PIN")) return 1;
    legacy.finish();
    if (!require(ActivityAuditService::currentActor().accountId == QStringLiteral("admin"),
                 "User switch must activate the new account")) return 1;
    QSqlQuery closedSession(database);
    closedSession.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM staff_login_sessions "
        "WHERE account_id='master:m1' AND TRIM(COALESCE(logged_out_at,''))<>''"));
    if (!require(closedSession.exec() && closedSession.next() && closedSession.value(0).toInt() == 1,
                 "User switch must close the previous session")) return 1;

    StaffAccountService::closeCurrentSession(database);

    for (int i = 0; i < 5; ++i) {
        if (!require(!StaffAccountService::signIn(database, "admin", "wrong", nullptr, &errorText),
                     "Incorrect PIN must be rejected")) return 1;
    }
    if (!require(!StaffAccountService::signIn(database, "admin", "5678", nullptr, &errorText),
                 "Lockout must reject a correct PIN until expiry")) return 1;
    if (!require(!ActivityAuditService::currentActor().isValid(), "Failed login must not authenticate an actor")) return 1;
    if (!require(legacy.exec(QStringLiteral("UPDATE staff_login_attempts SET locked_until=1 WHERE account_id='admin'")),
                 "Cannot expire test lock")) return 1;
    if (!require(StaffAccountService::signIn(database, "admin", "5678", &actor, &errorText),
                 "Correct PIN must work after lock expiry")) return 1;
    StaffAccountService::closeCurrentSession(database);

    if (!require(AccessControlService::ensureSchema(database, &errorText), qPrintable(errorText))) return 1;
    if (!require(AccessControlService::hasPermission(database, QStringLiteral("owner"), QStringLiteral("access.manage")),
                 "Owner must have access management rights")) return 1;
    if (!require(AccessControlService::hasPermission(database, QStringLiteral("master"), QStringLiteral("nav.current_orders")),
                 "Master must be able to open current orders")) return 1;
    if (!require(!AccessControlService::hasPermission(database, QStringLiteral("master"), QStringLiteral("access.manage")),
                 "Master must not manage access policies")) return 1;

    ActivityAuditService::setCurrentActor({QStringLiteral("admin"), QString::fromUtf8("Власник"), QStringLiteral("owner"), QStringLiteral("test-owner")});
    if (!require(AccessControlService::setRolePermissions(database, QStringLiteral("master"),
                                                           QSet<QString>{QStringLiteral("nav.current_orders")},
                                                           &errorText),
                 qPrintable(errorText))) return 1;
    if (!require(AccessControlService::ensureSchema(database, &errorText), qPrintable(errorText))) return 1;
    if (!require(!AccessControlService::hasPermission(database, QStringLiteral("master"), QStringLiteral("orders.create")),
                 "Explicitly removed role right must persist after schema check")) return 1;
    if (!require(AccessControlService::assignRole(database, QStringLiteral("master:m1"), QStringLiteral("viewer"), &errorText),
                 qPrintable(errorText))) return 1;
    if (!require(AccessControlService::hasPermission(database, QStringLiteral("viewer"), QStringLiteral("nav.history")),
                 "Viewer must see history")) return 1;
    if (!require(!AccessControlService::hasPermission(database, QStringLiteral("viewer"), QStringLiteral("orders.create")),
                 "Viewer must not create orders")) return 1;
    if (!require(AccessControlService::resetAccountPin(database, QStringLiteral("master:m1"), &errorText), qPrintable(errorText))) return 1;

    bool masterPinReset = false;
    for (const AccessAccountRow& account : AccessControlService::accounts(database, &errorText)) {
        if (account.id == QStringLiteral("master:m1")) masterPinReset = !account.pinConfigured;
    }
    if (!require(errorText.isEmpty() && masterPinReset, "PIN reset must require worker to set a new PIN")) return 1;
    ActivityAuditService::clearCurrentActor();

    const int userEventCount = ActivityAuditService::load(database, filter, &errorText).size();
    if (!require(ActivityAuditService::record(database, QStringLiteral("system"), QStringLiteral("maintenance"),
                                               QStringLiteral("System maintenance"), 0, QStringLiteral("schema"),
                                               QStringLiteral("test"), QStringLiteral("schema"), QStringLiteral("{}"),
                                               &errorText), qPrintable(errorText))) return 1;
    if (!require(setup.exec(QStringLiteral(
            "CREATE TABLE legacy_mutation_log(id INTEGER PRIMARY KEY, created_at TEXT, entity_name TEXT, "
            "action TEXT, order_number INTEGER, record_key TEXT, after_json TEXT)")), "Cannot create legacy log")) return 1;
    if (!require(setup.exec(QStringLiteral(
            "INSERT INTO legacy_mutation_log VALUES(1,datetime('now'),'schema','maintenance',0,'','{}')")),
            "Cannot seed legacy event")) return 1;
    filter.includeSystem = false;
    const auto userEvents = ActivityAuditService::load(database, filter, &errorText);
    if (!require(errorText.isEmpty() && userEvents.size() == userEventCount,
                 "User-only filter must hide system and legacy events without hiding user actions")) return 1;
    filter.limit = 1;
    const auto limitedEvents = ActivityAuditService::load(database, filter, &errorText);
    if (!require(limitedEvents.size() == 1 && limitedEvents.first().actor.role != QStringLiteral("system"),
                 "System events must be filtered before applying the limit")) return 1;
    filter.includeSystem = true;
    filter.limit = 1000;
    if (!require(ActivityAuditService::load(database, filter, &errorText).size() == userEventCount + 2,
                 "Hidden audit entries must remain available in storage")) return 1;

    // A restored account table must not resurrect archived personnel, even
    // when several historical workers have the same display name.
    if (!require(setup.exec(QStringLiteral(
            "INSERT INTO masters VALUES('old1','Duplicate','master',0),('old2','Duplicate','master',0)")),
            "Cannot seed archived personnel")) return 1;
    if (!require(setup.exec(QStringLiteral(
            "INSERT INTO staff_accounts(id,master_id,display_name,role,active) VALUES"
            "('old1','old1','Duplicate','master',1),('old2','old2','Duplicate','master',1),"
            "('missing','missing','Missing worker','master',1),('independent','','Independent','master',1)")),
            "Cannot seed restored accounts")) return 1;
    auto visible = [&](const QString& id) {
        errorText.clear();
        const auto rows = StaffAccountService::accounts(database, &errorText);
        for (const auto& row : rows) if (row.id == id) return true;
        return false;
    };
    if (!require(!visible("old1") && !visible("old2") && !visible("missing")
                 && visible("independent") && visible("admin") && visible("master:m1")
                 && errorText.isEmpty(), "Login list must respect personnel status and independent accounts")) return 1;
    if (!require(!StaffAccountService::setPin(database, "old1", "1234", &errorText),
                 "Archived worker must not set a PIN")) return 1;
    if (!require(StaffAccountService::setPin(database, "master:m1", "1234", &errorText),
                 qPrintable(errorText))) return 1;
    if (!require(setup.exec("UPDATE masters SET active=0 WHERE id='m1'"), "Cannot archive worker")) return 1;
    if (!require(!StaffAccountService::signIn(database, "master:m1", "1234", &actor, &errorText),
                 "Stale login selection must not sign in archived personnel")) return 1;
    if (!require(setup.exec("UPDATE masters SET active=1 WHERE id='m1'"), "Cannot reactivate worker")) return 1;
    if (!require(setup.exec("UPDATE staff_accounts SET role='owner' WHERE id='master:m1'"), "Cannot assign personal owner")) return 1;
    if (!require(visible("master:m1") && !visible("admin"), "Personal owner must supersede bootstrap admin")) return 1;
    if (!require(!StaffAccountService::signIn(database, "admin", "5678", &actor, &errorText),
                 "Hidden bootstrap account must not bypass login policy")) return 1;
    if (!require(StaffAccountService::signIn(database, "master:m1", "1234", &actor, &errorText),
                 "Personal owner must remain usable")) return 1;
    StaffAccountService::closeCurrentSession(database);
    if (!require(setup.exec("UPDATE staff_accounts SET pin_hash='' WHERE id='master:m1'"), "Cannot reset owner PIN")) return 1;
    if (!require(visible("admin"), "Bootstrap recovery must remain available without a configured personal owner")) return 1;
    if (!require(setup.exec("SELECT COUNT(*) FROM staff_accounts WHERE id IN ('old1','old2','missing')")
                 && setup.next() && setup.value(0).toInt()==3,
                 "Filtering must preserve historical accounts")) return 1;

    // Calendar filtering includes both local midnight boundaries correctly,
    // including the autumn daylight-saving transition and legacy UTC rows.
    ActivityAuditFilter boundaryFilter;
    boundaryFilter.from = QDate(2026, 10, 25);
    boundaryFilter.to = boundaryFilter.from;
    boundaryFilter.includeSystem = true;
    boundaryFilter.includeLegacy = true;
    const QDateTime dayStart = boundaryFilter.from.startOfDay();
    const QDateTime nextDay = boundaryFilter.to.addDays(1).startOfDay();
    const QList<QDateTime> moments = {dayStart.addSecs(-1), dayStart, nextDay.addSecs(-1), nextDay};
    for (int index = 0; index < moments.size(); ++index) {
        if (!require(ActivityAuditService::recordExternal(database, QStringLiteral("boundary-%1").arg(index),
                moments[index], {QStringLiteral("boundary"), QStringLiteral("Boundary"), QStringLiteral("owner"), QStringLiteral("boundary-session")},
                QStringLiteral("boundary"), QStringLiteral("test"), QStringLiteral("Boundary test")),
                "Cannot seed boundary audit event")) return 1;
        QSqlQuery seed(database);
        seed.prepare("INSERT INTO legacy_mutation_log VALUES(?,?,'boundary','test',0,'','{}')");
        seed.addBindValue(100 + index);
        seed.addBindValue(moments[index].toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        if (!require(seed.exec(), "Cannot seed legacy boundary event")) return 1;
    }
    boundaryFilter.category = QStringLiteral("boundary");
    errorText.clear();
    if (!require(ActivityAuditService::load(database, boundaryFilter, &errorText).size() == 2 && errorText.isEmpty(),
                 "Local date range must include only its two UTC boundary events")) return 1;
    boundaryFilter.category = QStringLiteral("history");
    if (!require(ActivityAuditService::load(database, boundaryFilter, &errorText).size() == 2 && errorText.isEmpty(),
                 "Legacy date range must use the same local day boundaries")) return 1;

    database.close();
    QSqlDatabase::removeDatabase(QStringLiteral("activity_audit_regression"));
    return 0;
}
