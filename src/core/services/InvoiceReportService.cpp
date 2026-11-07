#include "core/services/InvoiceReportService.h"
#include "core/services/AccessControlService.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/ElitInvoiceStore.h"
#include "core/services/PartsQueryService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include <QFileInfo>
#include <algorithm>
#include <limits>
#include <QSet>

namespace InvoiceReportService {
QString activeAccount() {
    GeneralSettingsService::ElitCredentials credentials;
    return GeneralSettingsService::loadElitCredentials(&credentials)?ElitInvoiceStore::accountKey(credentials):QString();
}
Context context() {
    const auto db=Database::db();
    return {db.databaseName(),AccessControlService::currentScopeKey(),activeAccount(),
        AccessControlService::currentUserHasPermission(db,"nav.reports")};
}
bool isCurrent(const Context& expected) {
    const auto now=context();
    return expected.allowed && now.allowed && expected.databasePath==now.databasePath
        && expected.scope==now.scope && expected.account==now.account;
}
QVector<RegisteredInvoice> localInvoices(QString* error) {
    return InvoiceRegistryService::load(Database::db(),error);
}
bool canPayFullInvoice(const RegisteredInvoice& invoice) {
    const bool stock=std::any_of(invoice.parts.cbegin(),invoice.parts.cend(),[](const auto& p){return p.source=="inventory";});
    const auto db=Database::db();
    return !invoice.parts.isEmpty() && !invoice.apportioned && !stock && !invoice.paid()
        && !invoice.amountMismatch && PurchasePaymentService::canPay(db,"part")
        && !AccessControlService::currentOrderScope(db).restricted;
}
bool payOrders(const RegisteredInvoice& invoice,const QVector<int>& orders,QString* error,qint64 amount) {
    return PurchasePaymentService::payInvoiceOrders(Database::db(),invoice,orders,error,amount);
}
bool canViewOrder(int number) {
    const auto db=Database::db();
    return number>0 && AccessControlService::currentUserHasPermission(db,"nav.reports")
        && AccessControlService::canAccessOrder(db,number);
}
OrderSummary orderSummary(int number,QString* diagnostic) {
    if(diagnostic) diagnostic->clear();
    QString error;
    const auto fail=[&](const QString& reason) {
        if(diagnostic) *diagnostic=reason;
        return OrderSummary{};
    };
    if(!canViewOrder(number)) return fail(QString::fromUtf8("Наряд недоступний для перегляду."));
    const auto db=Database::db();
    OrderSummary result;
    result.debts=PurchasePaymentService::orderPartDebts(db,number,&error);
    if(!error.isEmpty()) return fail(error);
    for(const auto& debt:result.debts) {
        const auto remaining=debt.remainingCents();
        if(remaining<0 || remaining>std::numeric_limits<qint64>::max()-result.remainingCents)
            return fail(QString::fromUtf8("Некоректна сума боргу наряду."));
        result.remainingCents+=remaining;
        if(remaining>0 && debt.dueDate.isValid() && (!result.dueDate.isValid() || debt.dueDate<result.dueDate))
            result.dueDate=debt.dueDate;
    }
    const auto invoices=InvoiceRegistryService::load(db,&error);
    if(!error.isEmpty()) return fail(error);
    bool allDates=true;
    QDate defaultDue;
    QSet<int> coveredParts;
    for(const auto& invoice:invoices) if(invoice.orders.contains(number)) {
        result.invoices.append(invoice);
        if(!invoice.paidAt.isValid()) allDates=false;
        const auto paidDate=invoice.paidAt.date();
        if(paidDate.isValid() && (!result.paidDate.isValid() || paidDate>result.paidDate)) result.paidDate=paidDate;
        for(const auto& part:invoice.parts)
            if(part.source=="part" && part.orderNumber==number) coveredParts.insert(int(part.id));
        for(const auto& slice:invoice.distribution)
            if(slice.orderNumber==number && slice.partId>0) coveredParts.insert(slice.partId);
        const auto candidate=invoice.date.addDays(7);
        if(!invoice.paid() && candidate.isValid() && (!defaultDue.isValid() || candidate<defaultDue)) defaultDue=candidate;
    }
    if(!result.dueDate.isValid()) result.dueDate=defaultDue;
    const auto parts=PartsQueryService::loadRows(db,QString(),QDate(),QDate(),&error);
    if(!error.isEmpty()) return fail(error);
    for(const auto& part:parts)
        if(part.orderNumber==number && part.purchaseAmount>0 && !coveredParts.contains(part.id)) allDates=false;
    result.paymentDateKnown=!result.invoices.isEmpty() && allDates;
    return result;
}
QVector<RegisteredInvoice> load(const Context& context,QString* diagnostic) {
    if(diagnostic) diagnostic->clear();
    QString error;
    QVector<RegisteredInvoice> invoices;
    if(!context.allowed) error=QString::fromUtf8("Недостатньо прав для перегляду накладних.");
    else if(!QFileInfo::exists(context.databasePath)) error=QString::fromUtf8("База даних недоступна.");
    else
    {
        ScopedSqliteConnection connection(context.databasePath, "invoice_registry", &error);
        if (connection.isOpen()) {
            invoices = InvoiceRegistryService::load(connection.db(), &error);
            if (error.isEmpty() && !context.account.isEmpty() && !AccessControlService::currentOrderScope(connection.db()).restricted) {
                const auto snapshot = ElitInvoiceStore::load(connection.db(), context.account, &error);
                for (const auto& document : snapshot.documents) {
                    const auto& remote = document.invoice;
                    auto it = std::find_if(invoices.begin(), invoices.end(), [&](const auto& invoice) {
                        return SupplierInvoiceLedger::isElit(invoice.supplier)
                            && invoice.number.trimmed().compare(remote.number.trimmed(), Qt::CaseInsensitive)==0
                            && invoice.date == remote.posted;
                    });
                    if (it == invoices.end()) {
                        RegisteredInvoice invoice;
                        invoice.key = "remote:" + document.identity;
                        invoice.number = remote.number;
                        invoice.date = remote.posted;
                        invoice.supplier = QString::fromUtf8("Еліт");
                        invoices.append(invoice);
                        it = invoices.end()-1;
                    }
                    it->remoteIdentity = document.identity;
                    it->dueDate = remote.due;
                    it->totalCents = remote.grossCents;
                    it->paidCents = qMax<qint64>(0, remote.grossCents-remote.remainingCents);
                    it->amountMismatch = false;
                }
            }
        }
    }

    if(diagnostic) *diagnostic=error;
    return error.isEmpty()?invoices:QVector<RegisteredInvoice>{};
}
}
