#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/invoice-import-owners
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" tests/InvoiceImportOwnerTests.cpp src/infrastructure/db/sqlite/OrderDeletionPublication.cpp -Wl,--gc-sections -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -o build-single-instance-audit/invoice-import-owners/owner-test
build-single-instance-audit/invoice-import-owners/owner-test
