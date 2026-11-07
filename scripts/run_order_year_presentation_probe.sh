#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/dby07/cache
export XDG_CACHE_HOME="$PWD/build-single-instance-audit/dby07/cache"
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -ffunction-sections -fdata-sections -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" tests/OrderYearPresentationProbe.cpp src/core/services/OrderSearchRepository.cpp src/core/services/OrderSearchIndex.cpp src/ui/common/dialogs/OrderDialogFlowController.cpp -Wl,--gc-sections -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -lQt6Gui -lQt6Widgets -o build-single-instance-audit/dby07/widget-search
QT_QPA_PLATFORM=offscreen build-single-instance-audit/dby07/widget-search
