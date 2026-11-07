#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-payroll-errors
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
sources=(tests/PayrollReadFailureTests.cpp src/core/services/ReportsDataLoaderSnapshotFlow.cpp src/core/services/ReportsDataLoaderLocalSupport.cpp src/core/services/settings/PersonnelEmploymentHistory.cpp src/core/cache/ReportsSnapshotCache.cpp src/core/util/PlateUtils.cpp src/core/services/ReportsDateUtils.cpp src/core/util/FormatUtils.cpp src/core/util/DateFormats.cpp src/infrastructure/db/sqlite/ScopedConnectionManager.cpp src/infrastructure/db/sqlite/DbThreadConnectionSupport.cpp)
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" "${sources[@]}" -Wl,--gc-sections -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -lQt6Gui -o build-single-instance-audit/db-payroll-errors/read-test
build-single-instance-audit/db-payroll-errors/read-test
