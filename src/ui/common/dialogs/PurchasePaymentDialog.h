#pragma once
#include "core/services/PurchasePaymentService.h"
class QWidget;
// Returns an additional payment; cancellation never changes storage.
bool choosePurchasePayment(QWidget* parent, const PurchaseDebt& debt, bool newPurchase, qint64* amount, int* deferredDays = nullptr,
                           const QVector<PurchaseDebt>* group = nullptr, bool paymentAllowed = true);
bool chooseGroupedPurchasePayment(QWidget* parent, const QString& title, const QVector<PurchaseDebt>& debts, qint64* amount);
bool openOrderPartsPayment(QWidget* parent, int orderNumber);
bool openInvoicePurchasePayment(QWidget* parent, const RegisteredInvoice& invoice);
void showPurchasePaymentSaved(QWidget* parent, qint64 amountCents);
bool openPurchasePayment(QWidget* parent, const QString& source, qint64 id, const QString& orderUid = {});
