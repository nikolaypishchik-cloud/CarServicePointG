#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-archive-workspace
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" tests/MigrationArchiveWorkspaceTests.cpp src/infrastructure/db/sqlite/OrderArchiveShardSqlSupport.cpp -Wl,--gc-sections -L/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu -lQt6Sql -lQt6Core -o build-single-instance-audit/db-archive-workspace/projection-test
build-single-instance-audit/db-archive-workspace/projection-test
