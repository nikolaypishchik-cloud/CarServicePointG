#include "../TestSourceAssertions.h"
#include <QFile>
#include <QIODevice>
#include <QString>

#include <iostream>

namespace {

bool contains(const QString& text, const QString& needle)
{
    return text.contains(needle, Qt::CaseSensitive);
}

} // namespace

int main()
{
    const QString workPartsAsync = QStringLiteral("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    const QString importJobHeader = QStringLiteral("src/core/import/ImportJob.h");
    const QString importJobImpl = QStringLiteral("src/core/import/ImportJob.cpp");
    const QString partsPageHeader = QStringLiteral("src/ui/tabs/parts/PartsPage.h");
    const QString partsPageImpl = QStringLiteral("src/ui/tabs/parts/PartsPage.cpp");
    const QString partsRuntime = QStringLiteral("src/ui/tabs/parts/PartsPageRuntime.cpp");

    auto readFile = [](const QString& path) -> QString {
        QFile f(cs::tests::projectRoot() + QLatin1Char('/') + path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Cannot open " << path.toStdString() << "\n";
            return QString();
        }
        return QString::fromUtf8(f.readAll());
    };

    const QString workParts = readFile(workPartsAsync);
    const QString importH = readFile(importJobHeader);
    const QString importCpp = readFile(importJobImpl);
    const QString partsH = readFile(partsPageHeader);
    const QString partsCpp = readFile(partsPageImpl);
    const QString runtime = readFile(partsRuntime);
    const QString asyncScope = readFile(QStringLiteral("src/runtime/AsyncThreadScope.h"));

    if (workParts.isEmpty() || importH.isEmpty() || importCpp.isEmpty() || partsH.isEmpty() || partsCpp.isEmpty() || runtime.isEmpty()) {
        return 1;
    }

    const auto progress = cs::tests::readSourceFile("src/ui/common/widgets/InvoiceImportProgressDialog.h");
    if (!cs::tests::invoiceOwnerCancellationWired(workParts, progress, importCpp)
        || !cs::tests::invoiceCancellationRejectsBrokenLinks(workParts, progress, importCpp)) {
        std::cerr << "Invoice dialog ownership/cancellation contract is broken.\n";
        return 2;
    }

    if (!contains(importH, QStringLiteral("detachWorkerForOwnerShutdown")) ||
        !contains(importCpp, QStringLiteral("QObject::disconnect(m_thread, nullptr, this, nullptr)")) ||
        !contains(importCpp, QStringLiteral("m_thread->requestInterruption()")) ||
        !contains(importCpp, QStringLiteral("++m_generation"))) {
        std::cerr << "ImportJob must cancel, detach callbacks, request interruption and invalidate generation on owner shutdown.\n";
        return 3;
    }

    if (!contains(partsH, QStringLiteral("cancelActiveRefreshForOwnerShutdown")) ||
        !contains(partsCpp, QStringLiteral("QObject::disconnect(m_refreshWorker, nullptr, this, nullptr)")) ||
        !contains(partsCpp, QStringLiteral("m_refreshWorker->requestInterruption()")) ||
        !contains(partsCpp, QStringLiteral("m_refreshWorker.clear()"))) {
        std::cerr << "PartsPage must cancel/detach async refresh when the owner is destroyed.\n";
        return 4;
    }

    if (!contains(runtime, QStringLiteral("AsyncResult localResult")) ||
        !contains(runtime, QStringLiteral("*result = std::move(localResult)")) ||
        !(contains(runtime, QStringLiteral("isInterruptionRequested"))
          || (contains(runtime, QStringLiteral("AsyncThreadScope::isCancellationRequested(cancelToken)"))
              && contains(asyncScope, QStringLiteral("isInterruptionRequested"))))) {
        std::cerr << "PartsPage worker must write into local value-result and publish only if not cancelled/interrupted.\n";
        return 5;
    }

    std::cout << "P1 async lifecycle p3-p4 owner shutdown guards verified.\n";
    return 0;
}
