#pragma once

#include <QString>

class QSqlDatabase;

namespace CatalogMutationService {

struct SaveParams {
    int id = 0;
    QString brand;
    QString model;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuel;
    QString group;
    QString oem;
    QString name;
    QString brandName;
    QString supplier;
    double price = 0.0;
    QString note;
};

struct SaveResult {
    bool ok = false;
    int recordId = 0;
    QString errorText;
    QString operation;
};

struct DeleteResult {
    bool ok = false;
    QString errorText;
};

SaveResult save(QSqlDatabase db, const SaveParams& params);
DeleteResult remove(QSqlDatabase db, int id);

} // namespace CatalogMutationService
