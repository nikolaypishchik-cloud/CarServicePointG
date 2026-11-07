#include "core/services/ElitPdfOpenService.h"
#include "core/services/ElitInvoicePdf.h"
#include "core/services/AccessControlService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QUuid>
#include <QScopeGuard>

namespace ElitPdfOpenService {
bool prepare(const QString& identity, Request* request, QString* error) {
    if(error) error->clear();
    if(!request) return false;
    *request={};
    const auto db=Database::db();
    if(identity.isEmpty() || AccessControlService::currentOrderScope(db).restricted
        || !AccessControlService::currentUserHasPermission(db,"nav.reports")) return false;
    GeneralSettingsService::ElitCredentials credentials;
    if(!GeneralSettingsService::loadElitCredentials(&credentials,error)) return false;
    *request={db.databaseName(),ElitInvoiceStore::accountKey(credentials),identity,
        AccessControlService::currentScopeKey(),credentials};
    return true;
}
bool isCurrent(const Request& request) {
    if(request.identity.isEmpty() || request.scope!=AccessControlService::currentScopeKey()) return false;
    GeneralSettingsService::ElitCredentials now;
    return GeneralSettingsService::loadElitCredentials(&now)
        && ElitInvoiceStore::accountKey(now)==request.account && now.password==request.credentials.password;
}
Result execute(const Request& request, const std::function<bool()>& continueWork) {
    Result result;
    auto current=[&]{return continueWork && continueWork() && isCurrent(request);};
    if(!current()) return result;
    if(!QFileInfo::exists(request.databasePath)) {
        result.error=QString::fromUtf8("База даних недоступна.");return result;
    }
    const auto connection="elit_open_pdf_"+QUuid::createUuid().toString(QUuid::Id128);
    const auto cleanup=qScopeGuard([&]{QSqlDatabase::removeDatabase(connection);});
    auto local=QSqlDatabase::addDatabase("QSQLITE",connection);
    local.setDatabaseName(request.databasePath);local.setConnectOptions("QSQLITE_BUSY_TIMEOUT=3000");
    if(!local.open()) {result.error=QString::fromUtf8("База даних недоступна.");return result;}
    auto snapshot=ElitInvoiceStore::load(local,request.account,&result.error);
    QVector<ElitInvoiceStore::Document> selected;
    for(const auto& document:snapshot.documents) if(document.identity==request.identity) selected.append(document);
    if(result.error.isEmpty() && selected.size()!=1)
        result.error=QString::fromUtf8("Накладну не знайдено в поточному акаунті. Оновіть дані Еліт.");
    if(result.error.isEmpty() && current()) {
        result.path=ElitInvoicePdf::paths(local,request.account).value(request.identity);
        if(result.path.isEmpty()) {
            if(request.credentials.login.trimmed().isEmpty() || request.credentials.password.isEmpty())
                result.error=QString::fromUtf8("PDF ще не збережено. Для завантаження з eCat збережіть логін і пароль у налаштуваннях Еліт.");
            else {
                snapshot.documents=selected;
                const auto fetched=ElitInvoicePdf::synchronize(local,request.credentials,snapshot,current);
                result.error=fetched.errors.join('\n');
                if(current() && !fetched.cancelled) result.path=ElitInvoicePdf::paths(local,request.account).value(request.identity);
                if(result.path.isEmpty() && result.error.isEmpty())
                    result.error=QString::fromUtf8("eCat не повернув PDF цієї накладної. Спробуйте ще раз пізніше.");
            }
        }
    }
    if(!current()) return {};
    return result;
}
}
