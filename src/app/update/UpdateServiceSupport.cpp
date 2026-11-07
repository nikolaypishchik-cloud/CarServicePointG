#include "app/update/UpdateServiceSupport.h"

#include "app/App.h"
#include "app/AppContext.h"
#include "AppVersion.h"
#include "core/services/AppRuntimeService.h"
#include "core/io/SafeFileOps.h"
#include "ui/common/dialogs/UpdateDialog.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QApplication>
#include <QAtomicInteger>
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <cerrno>
#include <csignal>
#include <sys/types.h>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif


// P2 split: update support is preserved through ordered implementation fragments.
#include "app/update/internal/UpdateReleaseResolver.ipp"
#include "app/update/internal/UpdateDownloader.ipp"
#include "app/update/internal/UpdateInstallerRunner.ipp"
#include "app/update/internal/UpdateCleanupGuard.ipp"
#include "app/update/internal/UpdateVersionDetector.ipp"
