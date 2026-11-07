#pragma once
#include <QDate>
#include <QDialog>
#include <QStringList>

struct OrderClosurePreview {
    QString orderNumber;
    QString revision;
    qint64 totalCents = 0;
    qint64 receivedCents = 0;
    QStringList unresolvedParts;
};

struct OrderClosureChoice {
    bool withPayment = false;
    qint64 amountCents = 0;
    QString method;
    QDate paidOn;
    QString expectedRevision;
};

// Collects intent only: the caller must atomically validate and save after Accepted.
class OrderClosureDialog : public QDialog {
public:
    explicit OrderClosureDialog(const OrderClosurePreview& preview, QWidget* parent = nullptr);
    OrderClosureChoice choice() const { return m_choice; }
private:
    OrderClosureChoice m_choice;
};
