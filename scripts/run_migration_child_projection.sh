#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-child-collision
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" tests/MigrationChildKeyProjectionTests.cpp src/infrastructure/db/sqlite/DbFacadeSupport.cpp src/infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.cpp src/infrastructure/db/sqlite/DbCanonicalRewriteSupport.cpp src/core/services/legacyimport/LegacyOrderChildMapper.cpp -Wl,--gc-sections -L/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu -lQt6Sql -lQt6Core -o build-single-instance-audit/db-child-collision/projection-test
build-single-instance-audit/db-child-collision/projection-test
