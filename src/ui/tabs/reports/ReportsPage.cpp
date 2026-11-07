#include "ui/tabs/reports/ReportsPage.h"
#include <functional>

#include "core/services/AppRuntimeService.h"
#include "core/services/ReportsPdfSupport.h"
#include "core/services/settings/ReportsSettingsService.h"
#include "core/services/settings/FinanceSettingsService.h"
#include "core/services/ReportsSyncSupport.h"
#include "core/services/ReportsPdfRenderer.h"
#include "core/services/ReportsDataLoader.h"
#include "core/cache/ReportsSnapshotCache.h"
#include "core/perf/PerfWatchdog.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "ui/tabs/reports/ReportsDialogSupport.h"
#include "ui/tabs/reports/ReportsExpensesSupport.h"
#include "ui/tabs/reports/ReportsUiSupport.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include "ui/tabs/reports/ReportsInteractionSupport.h"
#include "ui/tabs/reports/ReportsAsyncSupport.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "ui/common/AdaptiveUi.h"

#include <QAbstractItemView>
#include <QApplication>
#include <algorithm>
#include <numeric>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QFont>
#include <QFileDialog>
#include <QFrame>
#include <QLineEdit>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QDebug>
#include <QMap>
#include <QMargins>
#include <QPointer>
#include <QProgressDialog>
#include <QThread>
#include <memory>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtMath>


namespace {
using namespace ReportsExpensesSupport;
using namespace ReportsUiSupport;
} // namespace

ReportsPage::ReportsPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    QObject::connect(FinanceSettingsService::markupSettingsBroadcaster(),
                     &FinanceSettingsService::MarkupSettingsBroadcaster::markupSettingsChanged,
                     this,
                     [this]() {
                         markDirty();
                         queueRefreshAll(30);
                     },
                     Qt::QueuedConnection);
    // The first refresh is driven by MainWindow after the page becomes active.
}


