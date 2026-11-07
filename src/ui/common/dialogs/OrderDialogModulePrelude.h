#include "core/util/DateFormats.h"
#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/common/dialogs/OrderDialog.cpp

#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/OrderDialogFlowController.h"
#include "ui/common/dialogs/OrderDialogSubmitSupport.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/BrandingSettingsService.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/OrderNumberService.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include <QPointer>
#include <QJsonObject>
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "app/AppContext.h"
#include "core/diagnostics/RuntimeLoggingControl.h"
#include "core/services/AppRuntimeService.h"
#include "core/services/OrderWorkspaceService.h"
#include "core/services/orders/OrderDialogReadQueryService.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/orders/OrderSnapshotService.h"
#include "core/services/orders/OrderDialogCoordinator.h"
#include "core/services/orders/OrderDialogPdfService.h"
#include "core/services/orders/OrderDialogStateSignature.h"
#include "core/services/orders/OrderDialogValidationService.h"
#include "core/services/orders/OrderDialogPrintQueryService.h"
#include "core/services/booking/BookingRepository.h"
#include "core/util/PlateUtils.h"
#include "ui/common/AdaptiveUi.h"
#include "core/util/VehicleNameUtils.h"
#include "core/util/FormatUtils.h"
#include "core/sync/SyncPayloadUtils.h"
#include "integrations/opendatabot/OpenDataBotClient.h"
#include <QStringList>
#include <QRegularExpression>
#include <QSqlRecord>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <optional>
#include <QMessageBox>
#include <functional>
#include <QFontMetricsF>
#include <QScopedValueRollback>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QWidget>
#include "ui/common/dialogs/MessageBoxUtils.h"
#include <QPixmap>
#include <QImage>
#include <QFileDialog>
#include <QUrl>
#include <QDesktopServices>
#include <QSqlError>
#include <QDateTime>
#include <QOverload>
#include <QList>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPainter>
#include <QTimer>
#include <QSignalBlocker>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QCursor>
#include <QWindow>
#include <QEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QFileInfo>

// File-local helpers required by converted slices.

// P2 split: dialog prelude helpers are preserved through ordered inline fragments.
#include "ui/common/dialogs/internal/OrderDialogPreludeValidators.ipp"
#include "ui/common/dialogs/internal/OrderDialogPreludeUiHelpers.ipp"
