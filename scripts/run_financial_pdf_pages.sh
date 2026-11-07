#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-financial-pdf
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export XDG_CACHE_HOME="$PWD/build-single-instance-audit/db-financial-pdf/cache"
mkdir -p "$XDG_CACHE_HOME"
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
sources=(tests/FinancialPdfPageTests.cpp src/core/services/ReportsPdfExpensesPageSupport.cpp src/core/services/ReportsPdfOutsourcePageSupport.cpp src/core/services/ReportsPdfExpenseSupport.cpp src/core/services/ReportsPdfLayoutSupport.cpp src/core/services/ReportsPdfPaintingSupport.cpp src/core/services/ReportsDateUtils.cpp src/core/util/FormatUtils.cpp src/core/util/DateFormats.cpp)
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" "${sources[@]}" -Wl,--gc-sections -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -lQt6Gui -o build-single-instance-audit/db-financial-pdf/page-test
QT_QPA_PLATFORM=offscreen build-single-instance-audit/db-financial-pdf/page-test
