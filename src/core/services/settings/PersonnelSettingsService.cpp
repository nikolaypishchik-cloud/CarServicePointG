#include "core/services/settings/PersonnelSettingsService.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QtMath>
#include <QSqlQuery>
#include <QSqlError>

namespace PersonnelSettingsService {

namespace {

QJsonObject loadAdminSources()
{
    const auto doc = QJsonDocument::fromJson(Database::getSetting(QStringLiteral("settings.staff.admin_salary_sources"), QStringLiteral("{}")).toString().toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void saveAdminSources(const QJsonObject& obj)
{
    Database::setSetting(QStringLiteral("settings.staff.admin_salary_sources"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

QString todayIso()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

QString normalizeAdminSalarySourceKey(QString source)
{
    source = source.trimmed();
    if (source == QStringLiteral("client_total")
        || source == QStringLiteral("purchase_total")
        || source == QStringLiteral("margin")) {
        return source;
    }
    if (source == QStringLiteral("all") || source == QStringLiteral("parts")) {
        return QStringLiteral("client_total");
    }
    if (source == QStringLiteral("services")) {
        return QStringLiteral("margin");
    }
    return QStringLiteral("margin");
}

} // namespace


QList<MasterRow> loadPersonnel(bool onlyActive)
{
    return Database::masters(onlyActive);
}

QMap<int, QString> workPostOptions()
{
    return {{0, QString::fromUtf8("Без поста")},
            {1, QString::fromUtf8("Пост 1 — авто")},
            {2, QString::fromUtf8("Пост 2 — авто")},
            {3, QString::fromUtf8("Пост 3 — ДВЗ та КПП")}};
}

QMap<QString, int> loadWorkPostAssignments(QString* errorText)
{
    if(errorText) errorText->clear();
    QSqlQuery query(Database::db());
    query.prepare("SELECT value FROM settings WHERE key=?");
    query.addBindValue("settings.staff.work_posts");
    if(!query.exec()) { if(errorText)*errorText=query.lastError().text(); return {}; }
    if(!query.next()) return {};
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(query.value(0).toString().toUtf8(),&parseError);
    QMap<QString,int> result;
    bool valid=parseError.error==QJsonParseError::NoError && document.isObject();
    const auto object=document.object();
    const auto options=workPostOptions();
    for(auto it=object.begin();valid && it!=object.end();++it) {
        const int post=it.value().toInt(-1);
        valid=!it.key().trimmed().isEmpty() && it.value().isDouble()
            && it.value().toDouble()==post && options.contains(post);
        if(valid) result.insert(it.key(),post);
    }
    if(!valid) {
        if(errorText)*errorText=QString::fromUtf8("Не вдалося прочитати розподіл працівників за постами.");
        return {};
    }
    return result;
}

bool saveWorkPostAssignments(const QMap<QString, int>& assignments, QString* errorText)
{
    QString error;
    auto merged=loadWorkPostAssignments(&error);
    if(!error.isEmpty()) { if(errorText)*errorText=error; return false; }
    QSet<QString> activeIds;
    for(const auto& employee:loadPersonnel(true)) activeIds.insert(employee.id);
    const auto options=workPostOptions();
    for(auto it=assignments.begin();it!=assignments.end();++it) {
        if(!activeIds.contains(it.key()) || !options.contains(it.value())) {
            if(errorText)*errorText=QString::fromUtf8("Працівник неактивний або пост недоступний. Оновіть список.");
            return false;
        }
        merged.insert(it.key(),it.value());
    }
    // Employee assignments are independent of payroll, orders and actual bay occupancy.
    QJsonObject object;
    for(auto it=merged.begin();it!=merged.end();++it) object.insert(it.key(),it.value());
    const bool ok=Database::setSetting("settings.staff.work_posts",QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    if(errorText)*errorText=ok?QString():QString::fromUtf8("Не вдалося зберегти розподіл працівників.");
    return ok;
}

double loadEffectivePayrollPercent(const QString& masterId, const QString& masterName)
{
    return Database::effectivePayrollPercent(masterId, masterName);
}

bool saveMaster(const MasterRow& master, double payrollPercent, QString* errorText)
{
    if (!Database::upsertMaster(master)) {
        if (errorText) {
            *errorText = master.role == QStringLiteral("admin")
                ? QString::fromUtf8("Не вдалося зберегти адміністратора.")
                : QString::fromUtf8("Не вдалося зберегти майстра.");
        }
        return false;
    }
    if (master.role == QStringLiteral("master") && !Database::setPayrollPercent(master.id, payrollPercent)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося зберегти відсоток майстра.");
        return false;
    }
    return true;
}

bool hireEmployee(const QString& name, const QString& role, const QString& fromDateIso, QString* errorText)
{
    const auto date=QDate::fromString(fromDateIso,Qt::ISODate);
    if(name.simplified().isEmpty() || (role!="master" && role!="admin") || !date.isValid() || date>QDate::currentDate()) {
        if(errorText)*errorText=QString::fromUtf8("Вкажіть ім’я, посаду та дійсну дату прийняття не пізніше сьогодні.");
        return false;
    }
    for(const auto& person:loadPersonnel(false))if(person.active && person.name.simplified().compare(name.simplified(),Qt::CaseInsensitive)==0) {
        if(errorText)*errorText=QString::fromUtf8("Працівник із таким ім’ям уже працює. Уточніть повне ім’я.");
        return false;
    }
    MasterRow person;person.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    person.name=name.simplified();person.role=role;person.active=true;person.activeFrom=fromDateIso;
    // Hiring does not rewrite payroll percentages or existing financial records.
    if(!Database::upsertMaster(person)) {
        if(errorText)*errorText=QString::fromUtf8("Не вдалося додати працівника.");
        return false;
    }
    return true;
}

bool setMasterActive(const QString& masterId, bool active, QString* errorText)
{
    if (!Database::setMasterActive(masterId, active, QDate::currentDate().toString(Qt::ISODate))) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося змінити стан майстра.");
        return false;
    }
    return true;
}

PersonnelSnapshot loadSnapshot()
{
    PersonnelSnapshot snapshot;
    const auto personnel = Database::masters(false);
    const QMap<QString, double> payrollById = Database::payrollPercents();
    const QJsonObject adminSources = loadAdminSources();

    MasterRow firstAdmin;
    for (const auto& row : personnel) {
        if (!row.active) {
            continue;
        }
        if (row.role == QStringLiteral("master")) {
            MasterInput input;
            input.id = row.id;
            input.name = row.name;
            input.payrollPercent = payrollById.contains(row.id)
                ? payrollById.value(row.id, 0.0)
                : Database::effectivePayrollPercent(row.id, row.name);
            snapshot.masters.push_back(input);
        } else if (row.role == QStringLiteral("admin") && firstAdmin.id.isEmpty()) {
            firstAdmin = row;
        }
    }

    snapshot.adminId = firstAdmin.id;
    snapshot.adminName = firstAdmin.name;
    snapshot.adminPercent = firstAdmin.id.isEmpty() ? 0.0 : firstAdmin.adminPercent;
    snapshot.adminSource = normalizeAdminSalarySourceKey(adminSources.value(firstAdmin.id).toString(QStringLiteral("margin")));
    return snapshot;
}

PersonnelSaveResult save(const PersonnelSavePayload& payload)
{
    PersonnelSaveResult result;

    const auto allPersonnel = Database::masters(false);
    const QMap<QString, double> existingPayrollById = Database::payrollPercents();
    QMap<QString, MasterRow> existingById;
    QMap<QString, QString> activeMasterIdByName;
    QMap<QString, QString> activeAdminIdByName;
    for (const auto& row : allPersonnel) {
        existingById.insert(row.id, row);
        if (row.active && row.role == QStringLiteral("master")) {
            const QString key = row.name.simplified().toCaseFolded();
            if (!key.isEmpty() && !activeMasterIdByName.contains(key)) {
                activeMasterIdByName.insert(key, row.id);
            }
        }
        if (row.active && row.role == QStringLiteral("admin")) {
            const QString key = row.name.simplified().toCaseFolded();
            if (!key.isEmpty() && !activeAdminIdByName.contains(key)) {
                activeAdminIdByName.insert(key, row.id);
            }
        }
    }

    QSet<QString> seenMasterIds;
    for (const MasterInput& input : payload.masters) {
        const QString name = input.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }

        QString id = input.id.trimmed();
        const QString nameKey = name.simplified().toCaseFolded();
        if (id.isEmpty() && activeMasterIdByName.contains(nameKey)) {
            id = activeMasterIdByName.value(nameKey);
        }
        if (id.isEmpty()) {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        const MasterRow existing = existingById.value(id);
        const bool requiresMasterSave = existing.id.isEmpty()
            || existing.role != QStringLiteral("master")
            || !existing.active
            || existing.name.trimmed() != name;
        const double existingPayrollPercent = existingPayrollById.value(id, Database::effectivePayrollPercent(id, name));
        const bool requiresPayrollSave = !existingPayrollById.contains(id)
            || qAbs(existingPayrollPercent - input.payrollPercent) > 0.0001;

        if (requiresMasterSave || requiresPayrollSave) {
            result.anyCommittedChange = true;
            ++result.affectedMasterRows;
        }

        MasterRow master;
        master.id = id;
        master.name = name;
        master.role = QStringLiteral("master");
        master.active = true;
        master.adminPercent = 0.0;

        if (requiresMasterSave && !Database::upsertMaster(master)) {
            result.errorText = QString::fromUtf8("Не вдалося зберегти майстра '%1'.").arg(name);
            return result;
        }
        if (!Database::setPayrollPercent(master.id, input.payrollPercent)) {
            result.errorText = QString::fromUtf8("Не вдалося зберегти відсоток майстра '%1'.").arg(name);
            return result;
        }
        seenMasterIds.insert(master.id);
    }

    for (const auto& row : allPersonnel) {
        if (!row.active) {
            continue;
        }
        if (row.role == QStringLiteral("master") && !seenMasterIds.contains(row.id)) {
            if (!Database::deleteMasterIfUnusedOrArchive(row.id, todayIso())) {
                result.errorText = QString::fromUtf8("Не вдалося видалити або архівувати майстра '%1'.").arg(row.name);
                return result;
            }
            result.anyCommittedChange = true;
        }
    }

    QJsonObject adminSources = loadAdminSources();
    const QJsonObject originalAdminSources = adminSources;
    const QString adminNameText = payload.admin.name.trimmed();
    const QString adminNameKey = adminNameText.simplified().toCaseFolded();

    if (adminNameText.isEmpty()) {
        if (!payload.admin.currentId.trimmed().isEmpty()) {
            if (!Database::deleteMasterIfUnusedOrArchive(payload.admin.currentId.trimmed(), todayIso())) {
                result.errorText = QString::fromUtf8("Не вдалося видалити адміністратора.");
                return result;
            }
            adminSources.remove(payload.admin.currentId.trimmed());
            result.anyCommittedChange = true;
            result.adminChanged = true;
            result.currentAdminId.clear();
        }
    } else {
        QString resolvedAdminId = payload.admin.currentId.trimmed();
        MasterRow existingAdmin = existingById.value(resolvedAdminId);
        if ((resolvedAdminId.isEmpty() || existingAdmin.id.isEmpty() || existingAdmin.role != QStringLiteral("admin"))
            && activeAdminIdByName.contains(adminNameKey)) {
            resolvedAdminId = activeAdminIdByName.value(adminNameKey);
            existingAdmin = existingById.value(resolvedAdminId);
        }

        const bool isNewAdmin = resolvedAdminId.isEmpty();
        MasterRow admin = existingAdmin;
        admin.id = isNewAdmin ? QUuid::createUuid().toString(QUuid::WithoutBraces) : resolvedAdminId;
        admin.name = adminNameText;
        admin.role = QStringLiteral("admin");
        admin.active = true;
        admin.adminPercent = payload.admin.adminPercent;

        const bool requiresAdminSave = isNewAdmin
            || existingAdmin.id.isEmpty()
            || existingAdmin.role != QStringLiteral("admin")
            || !existingAdmin.active
            || existingAdmin.name.trimmed() != admin.name
            || qAbs(existingAdmin.adminPercent - admin.adminPercent) > 0.0001;
        if (requiresAdminSave) {
            result.anyCommittedChange = true;
            result.adminChanged = true;
        }

        if (requiresAdminSave && !Database::upsertMaster(admin)) {
            result.errorText = QString::fromUtf8("Не вдалося зберегти адміністратора.");
            return result;
        }

        result.currentAdminId = admin.id;
        const QString adminSourceValue = normalizeAdminSalarySourceKey(payload.admin.source.trimmed());
        adminSources.insert(admin.id, adminSourceValue);

        for (const auto& row : allPersonnel) {
            if (row.role != QStringLiteral("admin")) {
                continue;
            }
            if (row.name.simplified().toCaseFolded() != adminNameKey) {
                continue;
            }
            if (row.id != admin.id) {
                MasterRow historicalAdmin = row;
                historicalAdmin.name = admin.name;
                historicalAdmin.adminPercent = admin.adminPercent;
                if (!Database::upsertMaster(historicalAdmin)) {
                    result.errorText = QString::fromUtf8("Не вдалося синхронізувати адміністратора '%1'.").arg(admin.name);
                    return result;
                }
                result.anyCommittedChange = true;
                result.adminChanged = true;
            }
            adminSources.insert(row.id, adminSourceValue);
        }
    }

    const auto adminSourcesJson = [](const QJsonObject& obj) {
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    };
    if (adminSourcesJson(originalAdminSources) != adminSourcesJson(adminSources)) {
        result.anyCommittedChange = true;
        result.adminChanged = true;
    }
    saveAdminSources(adminSources);

    result.success = true;
    if (result.currentAdminId.isEmpty()) {
        result.currentAdminId = payload.admin.currentId.trimmed();
    }
    return result;
}

bool deleteMaster(const QString& id, QString* errorText, const QString& effectiveDateIso)
{
    return deleteMasters(QStringList{id}, errorText, effectiveDateIso);
}

bool deleteMasters(const QStringList& ids, QString* errorText, const QString& effectiveDateIso)
{
    const QString archiveDate = effectiveDateIso.trimmed().isEmpty() ? todayIso() : effectiveDateIso.trimmed();
    for (const QString& rawId : ids) {
        const QString id = rawId.trimmed();
        if (id.isEmpty()) {
            continue;
        }
        if (!Database::deleteMasterIfUnusedOrArchive(id, archiveDate)) {
            if (errorText) {
                *errorText = QString::fromUtf8("Не вдалося видалити або архівувати майстра.");
            }
            return false;
        }
    }
    return true;
}

} // namespace PersonnelSettingsService
