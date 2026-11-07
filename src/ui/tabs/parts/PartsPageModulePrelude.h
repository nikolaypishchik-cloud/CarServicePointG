#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/tabs/parts/PartsPage.cpp

#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"
#include "core/services/AppRuntimeService.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/PartsQueryService.h"
#include "core/perf/PerfWatchdog.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "ui/common/AdaptiveUi.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"
#include <QPointer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QDebug>
#include <QAbstractItemView>
#include <QVariant>
#include <QDate>
#include <QStyle>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>
#include <utility>
#include <QWidget>
#include <QTimer>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QVector>
#include <QScrollBar>
#include <QThread>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMap>
#include <QSet>
#include <QStyleOptionHeader>
#include <QStyleOptionButton>


// File-local helpers required by converted slices.

// P2 split: prelude helpers are preserved through ordered inline fragments.
#include "ui/tabs/parts/internal/PartsPagePreludeColumns.ipp"
#include "ui/tabs/parts/internal/PartsPagePreludeDelegates.ipp"
#include "ui/tabs/parts/internal/PartsPagePreludeHelpers.ipp"
