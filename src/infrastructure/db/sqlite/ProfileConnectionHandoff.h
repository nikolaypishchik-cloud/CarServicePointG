#pragma once

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSemaphore>
#include <QThread>
#include <atomic>
#include <memory>

class ProfileConnectionHandoff {
public:
    bool closeMain(QString* error)
    {
        if (DbThreadConnectionSupport::trackedConnectionPath(mainConnectionName()).isEmpty()) return true;
        return onOwner([&] {
            const auto main = mainConnectionName();
            if (!QSqlDatabase::contains(main)) return true;
            {
                const auto connection = QSqlDatabase::database(main, false);
                if (!connection.isOpen()) return true;
                if (QFileInfo(connection.databaseName()) != QFileInfo(AppRuntime::activeDbFilePath()))
                    return fail(error, "The open workspace belongs to another profile");
            }
            for (const auto& name : {main, dbRegistryConnectionName()}) {
                if (!QSqlDatabase::contains(name)) continue;
                auto db = QSqlDatabase::database(name, false);
                if (!db.isOpen()) continue;
                // BEGIN also detects transactions started outside TransactionContext.
                if (TransactionContext::hasActiveTransaction(db) || !db.transaction())
                    return fail(error, "Profile migration requires the active transaction to finish");
                if (!db.rollback()) return fail(error, "Cannot release the transaction probe");
            }
            if (!RuntimeDbPreflightValidator::validateFile(AppRuntime::activeDbFilePath(),
                    RuntimeDbPreflightValidator::DbRole::Workspace, error)) return false;
            Database::close();
            if (QSqlDatabase::contains(main)) return fail(error, "Main workspace connection is still in use");
            closed = true;
            return true;
        }, error);
    }

    bool reopenMain(QString* error)
    {
        if (!closed) return true;
        return onOwner([&] {
            // The service already materialized or restored this generation.
            // Do not run crash recovery underneath the worker connection.
            return RuntimeDbPreflightValidator::validateFile(AppRuntime::activeDbFilePath(),
                RuntimeDbPreflightValidator::DbRole::Workspace, error) && Database::openPrepared(error);
        }, error);
    }

private:
    bool closed = false;
    static bool fail(QString* error, const QString& message)
    {
        if (error) *error = message;
        return false;
    }
    template<typename Fn>
    static bool onOwner(Fn action, QString* error)
    {
        auto* app = QCoreApplication::instance();
        if (!app) return fail(error, "Profile owner is unavailable");
        if (QThread::currentThread() == app->thread()) return action();
        struct Request { std::atomic_bool claimed{false}; QSemaphore finished; bool ok = false; };
        const auto request = std::make_shared<Request>();
        if (!QMetaObject::invokeMethod(app, [request, action] {
            if (request->claimed.exchange(true)) return;
            request->ok = action();
            request->finished.release();
        }, Qt::QueuedConnection)) return fail(error, "Cannot dispatch profile handoff");
        if (!request->finished.tryAcquire(1, 5000)) {
            if (!request->claimed.exchange(true)) return fail(error, "Profile owner did not respond");
            request->finished.acquire();
        }
        return request->ok;
    }
};
