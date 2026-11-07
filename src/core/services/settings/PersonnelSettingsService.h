#pragma once

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include "infrastructure/db/sqlite/Db.h"

namespace PersonnelSettingsService {

struct MasterInput {
    QString id;
    QString name;
    double payrollPercent = 0.0;
};

struct AdminInput {
    QString currentId;
    QString name;
    double adminPercent = 0.0;
    QString source;
};

struct PersonnelSavePayload {
    QList<MasterInput> masters;
    AdminInput admin;
};

struct PersonnelSnapshot {
    QList<MasterInput> masters;
    QString adminId;
    QString adminName;
    double adminPercent = 0.0;
    QString adminSource = QStringLiteral("margin");
};

struct PersonnelSaveResult {
    bool success = false;
    bool anyCommittedChange = false;
    bool adminChanged = false;
    int affectedMasterRows = 0;
    QString currentAdminId;
    QString errorText;
};

QList<MasterRow> loadPersonnel(bool onlyActive);
bool hireEmployee(const QString& name, const QString& role, const QString& fromDateIso, QString* errorText = nullptr);
QMap<int, QString> workPostOptions();
QMap<QString, int> loadWorkPostAssignments(QString* errorText = nullptr);
bool saveWorkPostAssignments(const QMap<QString, int>& assignments, QString* errorText = nullptr);
double loadEffectivePayrollPercent(const QString& masterId, const QString& masterName);
bool saveMaster(const MasterRow& master, double payrollPercent, QString* errorText = nullptr);
bool setMasterActive(const QString& masterId, bool active, QString* errorText = nullptr);
PersonnelSnapshot loadSnapshot();
PersonnelSaveResult save(const PersonnelSavePayload& payload);
bool deleteMaster(const QString& id, QString* errorText = nullptr, const QString& effectiveDateIso = QString());
bool deleteMasters(const QStringList& ids, QString* errorText = nullptr, const QString& effectiveDateIso = QString());

} // namespace PersonnelSettingsService
