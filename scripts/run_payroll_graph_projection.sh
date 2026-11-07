#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/payroll-graph
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -O2 -std=c++17 -fPIC -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" tests/PayrollGraphProjectionTests.cpp src/core/services/ReportsDateUtils.cpp src/core/util/DateFormats.cpp src/core/util/FormatUtils.cpp src/core/util/PlateUtils.cpp -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -o build-single-instance-audit/payroll-graph/projection-test
build-single-instance-audit/payroll-graph/projection-test
