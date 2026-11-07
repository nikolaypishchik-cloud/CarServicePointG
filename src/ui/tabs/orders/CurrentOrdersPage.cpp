#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/common/TabularSnapshotTableModel.h"

#include "core/services/AppRuntimeService.h"
#include "core/services/OrderIdentityResolver.h"
#include "core/services/settings/UiStateSettingsService.h"
#include "core/services/OrderSearchAggregator.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "core/services/orders/CurrentOrdersInvoiceImportService.h"
#include "core/services/settings/FinanceSettingsService.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "ui/common/widgets/ImportFlightAnimation.h"
#include "ui/common/widgets/InvoiceImportScannerOverlay.h"
#include "ui/tabs/orders/PartImportDraftUiSupport.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"
#include "core/services/OrderWorkspaceService.h"
#include "core/util/FormatUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/util/PlateUtils.h"
#include "core/perf/PerfWatchdog.h"
#include "ui/common/AdaptiveUi.h"
#include "ui/common/OrderTableUiMapper.h"
#include "ui/common/TabularReadModelMapper.h"
#include "core/readmodel/orders/CurrentOrdersViewModelBuilder.h"
#include "runtime/AsyncTaskHandle.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSqlError>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QTableView>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QScreen>
#include <QShowEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <functional>
#include <memory>

CurrentOrdersPage::CurrentOrdersPage(QWidget* parent)
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
}

CurrentOrdersPage::~CurrentOrdersPage()
{
    cancelActiveRefreshForOwnerShutdown("destructor");
    saveHeaderState();
}

void CurrentOrdersPage::closeEvent(QCloseEvent* event)
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    if (m_refreshWorker) {
        m_refreshWorker->requestInterruption();
    }
    saveHeaderState();
    QWidget::closeEvent(event);
}

void CurrentOrdersPage::cancelActiveRefreshForOwnerShutdown(const char* reason)
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    m_refreshQueued = false;
    m_refreshInProgress = false;

    if (m_refreshWorker) {
        qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE current_orders_owner_shutdown reason=")
                             << QString::fromLatin1(reason ? reason : "unknown")
                             << QStringLiteral(" generation=") << m_refreshGeneration;
        QObject::disconnect(m_refreshWorker, nullptr, this, nullptr);
        m_refreshWorker->requestInterruption();
        m_refreshWorker.clear();
    }
}
