#pragma once

#include "core/services/orders/CurrentOrdersInvoiceImportService.h"

#include <QList>
#include <QMap>
#include <QString>
#include <functional>

class QWidget;

struct CurrentOrdersImportDraftRow {
    QString orderedAt;
    QString deliveryDate;
    QString orderNumberText;
    QString store;
    QString oemNumber;
    QString name;
    QString brandName;
    QString qtyText;
    QString priceText;
};

class CurrentOrdersCoordinator {
public:
    struct RoutedOrderResult {
        bool ok = false;
        int orderNumber = 0;
        QString errorText;
    };

    struct DialogRunResult {
        bool launched = false;
        bool accepted = false;
        int orderNumber = 0;
        QString errorText;
    };

    struct MutationResult {
        bool success = false;
        QString errorText;
        QList<int> affectedOrderNumbers;
        QMap<int, QString> orderUidsByNumber;
    };

    struct ImportValidationResult {
        bool success = false;
        QString errorText;
        QList<CurrentOrdersImportServiceRow> serviceRows;
        QList<int> uniqueOrderNumbers;
    };

    static RoutedOrderResult resolveOrderForOpen(int selectedOrderNumber,
                                                 const QString& orderUid);

    static DialogRunResult openExistingOrderEditor(QWidget* parent,
                                                   int selectedOrderNumber,
                                                   const QString& orderUid,
                                                   const std::function<void()>& beforeDialog = {});

    static DialogRunResult openVehicleAsNewOrder(QWidget* parent,
                                                 int sourceOrderNumber);

    static DialogRunResult exportOrderPdf(QWidget* parent,
                                          int selectedOrderNumber,
                                          const QString& orderUid,
                                          bool partsReport = false);

    static MutationResult setOrdersClosed(const QList<int>& orderNumbers,
                                          bool closed);

    static MutationResult deleteOrders(const QList<int>& orderNumbers);

    static ImportValidationResult buildInvoiceImportRows(const QList<CurrentOrdersImportDraftRow>& rows);

    static bool importInvoiceRows(const QList<CurrentOrdersImportServiceRow>& rows,
                                  const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                                  CurrentOrdersInvoiceImportResult* result,
                                  QString* errorText = nullptr);
};
