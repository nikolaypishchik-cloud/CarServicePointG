#!/bin/bash
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/db-repeat
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" tests/MigrationRepeatJournalTests.cpp src/core/services/legacyimport/LegacyImportJournalSupport.cpp src/runtime/AppRuntime.cpp src/app/App.cpp -Wl,--gc-sections -L/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu -lQt6Sql -lQt6Core -o build-single-instance-audit/db-repeat/journal-test
build-single-instance-audit/db-repeat/journal-test
