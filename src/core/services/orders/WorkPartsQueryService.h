#pragma once

#include <QString>
#include <QDate>
#include <QList>

struct WorkPartsHeaderSnapshot {
    bool found = false;
    QString displayNumber;
    QString displayYear;
    QString plate;
    QString vin;
    QString car;
    QString client;
    QString phone;
    QString master;
    QString status;
    QString closed;
    QString mobileState;
    QString opened;
    double worksSum = 0.0;
    double partsSum = 0.0;
    QString errorText;
};

struct WorkPartsMasterSelection {
    QString currentMasterId;
    QString currentMasterName;
};

struct WorkPartsServiceEditorRow {
    int id = 0;
    QString title;
    double price = 0.0;
    double qty = 1.0;
    QString masterName;
    QString status;
    QString outsourceCompany;
};

struct WorkPartsPartEditorRow {
    int id = 0;
    QString name;
    double qty = 1.0;
    double price = 0.0;
    double amount = 0.0;
    QString store;
    QString partNumber;
    QString orderedAt;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double markupPercent = 0.0;
    bool manualMarkupOverride = false;
    QString markupSource = QStringLiteral("policy");
    QString manufacturer;
};

struct WorkPartsOutsourceJobSnapshot {
    bool found = false;
    QString companyId;
    QString companyName;
    QString sentDate;
    QString returnDate;
    QString partName;
    double companyPrice = 0.0;
    double markupPercent = 0.0;
    double clientPrice = 0.0;
    QString note;
};

class WorkPartsQueryService
{
public:
    static WorkPartsHeaderSnapshot loadHeaderSnapshot(const QString& orderUid);
    static WorkPartsMasterSelection loadMasterSelection(const QString& orderUid);
    static QString loadPartDeliveryDate(int partId);
    static QList<WorkPartsServiceEditorRow> loadServiceRowsForOrder(const QString& orderUid);
    static QList<WorkPartsPartEditorRow> loadPartRowsForOrder(const QString& orderUid);
    static QList<int> loadPartIdsForOrder(const QString& orderUid);
    static WorkPartsOutsourceJobSnapshot loadOutsourceJob(const QString& orderUid, int serviceId);
};
