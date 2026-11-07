#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-parts-payroll-pdf
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
sources=(src/core/services/ReportsDataLoaderSnapshotFlow.cpp src/core/services/ReportsDataLoaderLocalSupport.cpp src/core/services/settings/PersonnelEmploymentHistory.cpp src/core/cache/ReportsSnapshotCache.cpp tests/PartsPdfReadTests.cpp src/core/services/ReportsPdfSupportLocalSupport.cpp src/core/services/orders/PartPricingEngine.cpp src/core/services/orders/PartMarkupPolicyService.cpp src/core/services/WorkshopInventoryService.cpp src/core/util/StoreNameNormalizer.cpp src/core/util/OilServiceClassifier.cpp src/core/util/PlateUtils.cpp src/core/diagnostics/RuntimeLoggingControl.cpp src/core/services/ReportsPdfExpenseSupport.cpp src/core/services/ReportsPdfSupportSnapshotFlow.cpp src/core/services/AccessControlService.cpp src/core/services/ActivityAuditService.cpp src/infrastructure/db/sqlite/DbFacadeSupport.cpp src/core/services/ReportsDateUtils.cpp src/core/util/FormatUtils.cpp src/core/util/DateFormats.cpp src/infrastructure/db/sqlite/ScopedConnectionManager.cpp src/infrastructure/db/sqlite/DbThreadConnectionSupport.cpp)
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" "${sources[@]}" -Wl,--gc-sections -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -lQt6Gui -o build-single-instance-audit/db-parts-payroll-pdf/parts-read-test
build-single-instance-audit/db-parts-payroll-pdf/parts-read-test
