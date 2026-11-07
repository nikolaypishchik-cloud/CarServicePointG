#include "core/services/SupplierDebtApplicationService.h"
#include "core/services/AccessControlService.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlError>
#include <QScopeGuard>
#include <QUuid>

namespace SupplierDebtApplicationService {
namespace {
bool authorize(const QString& account, const QString& scope, QString* error) {
    GeneralSettingsService::ElitCredentials credentials;
    if (scope != AccessControlService::currentScopeKey() || !canRead() || !canReconcile()
        || !GeneralSettingsService::loadElitCredentials(&credentials, error)
        || ElitInvoiceStore::accountKey(credentials) != account) {
        if(error) *error=QString::fromUtf8("Доступ або облікові дані змінено. Оновіть реєстр.");
        return false;
    }
    return true;
}
}
bool canRead() {
    const auto db=Database::db();
    return !AccessControlService::currentOrderScope(db).restricted
        && AccessControlService::currentUserHasPermission(db,"nav.reports");
}
bool canReconcile() { return SupplierDebtReconciliationService::canReconcile(Database::db()); }
ReadTask prepareRead(const QString& account) {
    const auto path=Database::db().databaseName();
    const bool permitted=canRead();
    return [path,account,permitted](SupplierInvoiceLedger::Ledger& result,
                                   SupplierInvoiceLedger::Summary& summary,const std::atomic_bool& cancel) {
        if(!permitted) {result.error=QString::fromUtf8("Реєстр постачальників недоступний для цієї ролі.");return;}
        if(cancel.load()) return;
        const auto name="supplier_ledger_"+QUuid::createUuid().toString(QUuid::Id128);
        // The guard outlives the connection and every query, including exception unwinding.
        const auto remove=qScopeGuard([name]{QSqlDatabase::removeDatabase(name);});
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(path);db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000");
        if(!db.open() || !db.transaction()) {result.error=db.lastError().text();return;}
        const auto close=qScopeGuard([&db]{db.rollback();db.close();});
        if(cancel.load()) return;
        result=SupplierInvoiceLedger::load(db,account);
        if(!result.error.isEmpty() || cancel.load())return;
        const auto debts=PurchasePaymentService::debts(db,&result.error);
        if(!result.error.isEmpty() || cancel.load())return;
        const auto facts=SupplierDebtReconciliationService::load(db,&result.error);
        if(result.error.isEmpty() && !cancel.load())summary=SupplierInvoiceLedger::summarize(result,debts,facts);
    };
}
QVector<ElitDocumentAssociation::Candidate> candidates(const QString& account,const QString& scope,QString* error) {
    if(!authorize(account,scope,error))return {};
    return ElitDocumentAssociation::candidates(Database::db(),account,error);
}
bool confirmDocuments(const QString& account,const QString& scope,
                      const QVector<ElitDocumentAssociation::Candidate>& selected,QString* error) {
    return authorize(account,scope,error) && ElitDocumentAssociation::confirm(Database::db(),account,selected,false,error);
}
bool confirmLinks(const QString& account,const QString& scope,const QString& revision,
                  const QString& invoice,const QVector<QVector<QString>>& links,QString* error) {
    return authorize(account,scope,error) && SupplierInvoiceLedger::confirmLinks(Database::db(),account,revision,invoice,links,error);
}
}
