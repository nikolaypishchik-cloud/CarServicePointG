#pragma once

#include "core/services/MutationResult.h"

#include <QDateTime>
#include <QString>
#include <QUuid>

struct OrderWorkspaceInput {
    int number = 0;
    int visibleNumber = 0;
    QDateTime openedAt;
    QString status;
    QString plate;
    QString plateNorm;
    QString vin;
    QString car;
    QString color;
    int carYear = 0;
    int odometer = 0;
    QString engineType;
    QString engineVolume;
    QString fuelType;
    QString client;
    QString clientPhone;
    QString masterId;
    double worksSum = 0.0;
    double partsSum = 0.0;
    double discount = 0.0;
    QString discountMode = QStringLiteral("legacy");
    QString creationRequestUid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime createdAt;
};

class OrderWorkspaceService
{
public:
    static MutationResult ensureEditableOrderDetailed(int orderNumber);
    static MutationResult createDraftEditableOrderDetailed(const OrderWorkspaceInput& input, int* orderNumber = nullptr);
    static MutationResult saveEditableOrderDetailed(const OrderWorkspaceInput& input, int* orderNumber = nullptr);
    static bool ensureEditableOrder(int orderNumber, QString* errorText = nullptr);
    static bool createDraftEditableOrder(const OrderWorkspaceInput& input, int* orderNumber, QString* errorText = nullptr);
    static bool saveEditableOrder(const OrderWorkspaceInput& input, int* orderNumber, QString* errorText = nullptr);
};
