#include "ui/tabs/settings/RestoreManagerDialog.h"

#include "core/backup/BackupManager.h"
#include "core/backup/RestoreCoordinator.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/tabs/orders/WorkPartsDialog.h"

#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <QApplication>
#include <QDialog>
#include <QPointer>
#include <QProcess>


namespace {

QString settingsDialogChromeStyle()
{
    return QStringLiteral(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #2d3b4d, stop:0.52 #5e6d7f, stop:1 #2a3543); color:#f4f7fb;}"
        "QLabel{color:#f4f7fb;}"
    );
}

QString settingsDialogEditorStyle()
{
    return QStringLiteral(
        "background:#ffffff;"
        "color:#111111;"
        "border:1px solid rgba(22,28,35,0.22);"
        "border-radius:10px;"
        "padding:6px 10px;"
        "selection-background-color:#dfe8f5;"
        "selection-color:#111111;"
    );
}

QString settingsDialogComboStyle()
{
    return settingsDialogEditorStyle() + QStringLiteral(
        "QComboBox{padding-right:28px;}"
        "QComboBox::drop-down{width:24px; border:none; background:transparent;}"
        "QComboBox QAbstractItemView{background:#ffffff; color:#111111; border:1px solid rgba(22,28,35,0.22); selection-background-color:#dfe8f5; selection-color:#111111;}"
    );
}

QString settingsDialogButtonStyle()
{
    return QStringLiteral(
        "QPushButton{min-height:40px; padding:8px 16px; font-weight:700; color:#f8fbff;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(113,129,149,0.98), stop:0.16 rgba(87,102,121,0.98), stop:0.54 rgba(50,60,73,0.99), stop:1 rgba(25,32,40,0.99));"
        "border:1px solid rgba(255,255,255,0.30); border-radius:12px;}"
        "QPushButton:hover{border:1px solid rgba(255,255,255,0.48);}"
        "QPushButton:pressed{padding-top:9px; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(34,42,52,0.99), stop:1 rgba(17,22,29,0.99));}"
    );
}

QString settingsDialogTableStyle()
{
    return QStringLiteral(
        "QTableWidget{background:#ffffff; color:#111111; gridline-color:rgba(22,28,35,0.22); border:1px solid rgba(255,255,255,0.22); border-radius:10px; selection-background-color:#dfe8f5; selection-color:#111111; alternate-background-color:#f4f7fb;}"
        "QHeaderView::section{background:#ffffff; color:#111111; border:1px solid rgba(22,28,35,0.18); padding:8px 10px; font-weight:700;}"
        "QScrollBar:vertical,QScrollBar:horizontal{background:rgba(19,25,32,0.22); border:none; margin:0px;}"
    );
}

void styleDialogButton(QPushButton* button)
{
    if (!button) return;
    button->setStyleSheet(settingsDialogButtonStyle());
    button->setMinimumHeight(42);
}

bool isRestoreSensitiveDialog(QWidget* widget, const QWidget* self)
{
    if (!widget || widget == self || !widget->isVisible()) return false;
    if (qobject_cast<const QMessageBox*>(widget)) return false;
    return qobject_cast<OrderDialog*>(widget) || qobject_cast<WorkPartsDialog*>(widget);
}

QVector<QPointer<QWidget>> collectRestoreSensitiveDialogs(const QWidget* self)
{
    QVector<QPointer<QWidget>> out;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels) {
        if (isRestoreSensitiveDialog(widget, self)) {
            out.push_back(QPointer<QWidget>(widget));
        }
    }
    return out;
}

bool closeRestoreSensitiveDialogs(const QVector<QPointer<QWidget>>& dialogs, QString* errorText)
{
    for (const QPointer<QWidget>& dialog : dialogs) {
        if (!dialog) continue;
        dialog->close();
        if (dialog && dialog->isVisible()) {
            if (errorText) {
                *errorText = QString::fromUtf8("Restore скасовано: не вдалося безпечно закрити діалог редагування \"%1\".")
                                 .arg(dialog->windowTitle());
            }
            return false;
        }
    }
    return true;
}

void restartApplicationLater()
{
    const QString program = QCoreApplication::applicationFilePath();
    const QStringList args = QCoreApplication::arguments().mid(1);
    QProcess::startDetached(program, args);
    QCoreApplication::quit();
}

} // namespace

RestoreManagerDialog::RestoreManagerDialog(BackupManager* backupManager, RestoreCoordinator* coordinator, QWidget* parent)
    : QDialog(parent)
    , m_backupManager(backupManager)
    , m_coordinator(coordinator)
{
    setWindowTitle(QString::fromUtf8("Менеджер відновлення backup-ів"));
    resize(1180, 660);
    setStyleSheet(settingsDialogChromeStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(10);

    auto* filters = new QHBoxLayout();
    m_scope = new QComboBox(this);
    m_scope->setMinimumHeight(42);
    m_scope->setStyleSheet(settingsDialogComboStyle());
    m_scope->addItem(QString::fromUtf8("Поточний день"), QStringLiteral("today"));
    m_scope->addItem(QString::fromUtf8("Поточний + попередній"), QStringLiteral("today_prev"));
    m_scope->addItem(QString::fromUtf8("Усі"), QStringLiteral("all"));
    m_operationFilter = new QComboBox(this);
    m_operationFilter->setMinimumHeight(42);
    m_operationFilter->setStyleSheet(settingsDialogComboStyle());
    m_operationFilter->addItem(QString::fromUtf8("Усі операції"), QString());
    m_operationFilter->addItem(QStringLiteral("manual_backup"), QStringLiteral("manual_backup"));
    m_operationFilter->addItem(QStringLiteral("emergency_backup"), QStringLiteral("emergency_backup"));
    m_operationFilter->addItem(QStringLiteral("order_save"), QStringLiteral("order_save"));
    m_operationFilter->addItem(QStringLiteral("order_details_commit"), QStringLiteral("order_details_commit"));
    m_operationFilter->addItem(QStringLiteral("order_create"), QStringLiteral("order_create"));
    m_operationFilter->addItem(QStringLiteral("order_delete"), QStringLiteral("order_delete"));
    m_operationFilter->addItem(QStringLiteral("part_paid_toggle"), QStringLiteral("part_paid_toggle"));
    m_operationFilter->addItem(QStringLiteral("catalog_create"), QStringLiteral("catalog_create"));
    m_operationFilter->addItem(QStringLiteral("catalog_save"), QStringLiteral("catalog_save"));
    m_operationFilter->addItem(QStringLiteral("catalog_delete"), QStringLiteral("catalog_delete"));
    m_operationFilter->addItem(QStringLiteral("planning_create"), QStringLiteral("planning_create"));
    m_operationFilter->addItem(QStringLiteral("planning_save"), QStringLiteral("planning_save"));
    m_operationFilter->addItem(QStringLiteral("planning_delete"), QStringLiteral("planning_delete"));
    m_sourceFilter = new QComboBox(this);
    m_sourceFilter->setMinimumHeight(42);
    m_sourceFilter->setStyleSheet(settingsDialogComboStyle());
    m_sourceFilter->addItem(QString::fromUtf8("Усі джерела"), QString());
    m_sourceFilter->addItem(QStringLiteral("autobackup"), QStringLiteral("autobackup"));
    m_sourceFilter->addItem(QStringLiteral("emergency"), QStringLiteral("emergency"));
    m_sourceFilter->addItem(QStringLiteral("compatibility_wrapper"), QStringLiteral("compatibility_wrapper"));
    m_moduleFilter = new QLineEdit(this);
    m_moduleFilter->setMinimumHeight(42);
    m_moduleFilter->setStyleSheet(settingsDialogEditorStyle());
    m_moduleFilter->setPlaceholderText(QString::fromUtf8("Фільтр module"));
    m_entitySearch = new QLineEdit(this);
    m_entitySearch->setMinimumHeight(42);
    m_entitySearch->setStyleSheet(settingsDialogEditorStyle());
    m_entitySearch->setPlaceholderText(QString::fromUtf8("Пошук entity / опис"));
    filters->addWidget(m_scope);
    filters->addWidget(m_operationFilter);
    filters->addWidget(m_sourceFilter);
    filters->addWidget(m_moduleFilter);
    filters->addWidget(m_entitySearch, 1);
    root->addLayout(filters);

    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(settingsDialogTableStyle());
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("Дата/час"),
        QString::fromUtf8("Операція"),
        QString::fromUtf8("Module"),
        QString::fromUtf8("Entity"),
        QString::fromUtf8("Опис"),
        QString::fromUtf8("Розмір"),
        QString::fromUtf8("Validity / Source")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_table, 1);

    auto* actions = new QHBoxLayout();
    auto* refresh = new QPushButton(QString::fromUtf8("Оновити"), this);
    auto* details = new QPushButton(QString::fromUtf8("Деталі"), this);
    auto* openFolder = new QPushButton(QString::fromUtf8("Відкрити папку"), this);
    auto* emergency = new QPushButton(QString::fromUtf8("Emergency backup поточного стану"), this);
    auto* restore = new QPushButton(QString::fromUtf8("Відновити вибраний snapshot"), this);
    for (auto* button : {refresh, details, openFolder, emergency, restore}) {
        styleDialogButton(button);
    }
    actions->addWidget(refresh);
    actions->addWidget(details);
    actions->addWidget(openFolder);
    actions->addWidget(emergency);
    actions->addWidget(restore);
    actions->addStretch(1);
    root->addLayout(actions);

    connect(refresh, &QPushButton::clicked, this, &RestoreManagerDialog::rebuild);
    connect(m_scope, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { rebuild(); });
    connect(m_operationFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { rebuild(); });
    connect(m_sourceFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { rebuild(); });
    connect(m_moduleFilter, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_entitySearch, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(openFolder, &QPushButton::clicked, this, [this]() {
        const SnapshotMetadata meta = selectedSnapshot();
        if (!meta.snapshotPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(meta.snapshotPath).absolutePath()));
        }
    });
    connect(details, &QPushButton::clicked, this, [this]() {
        const SnapshotMetadata meta = selectedSnapshot();
        if (meta.snapshotPath.isEmpty()) return;
        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Деталі snapshot"),
                        QString::fromUtf8("Файл: %1\nДата/час: %2\nОперація: %3\nModule: %4\nPage: %5\nEntity: %6/%7\nOrder UID: %8\nOrder #: %9\nОпис: %10\nSource: %11\nValidity: %12\nCommit ID: %13\nEvent ID: %14\nDB path: %15")
                            .arg(QDir::toNativeSeparators(meta.snapshotPath),
                                 meta.createdAt.toString(QStringLiteral("dd.MM.yy HH:mm:ss")),
                                 meta.operationType,
                                 meta.module,
                                 meta.page,
                                 meta.entityType,
                                 meta.entityId,
                                 meta.orderUid,
                                 QString::number(meta.orderNumber),
                                 meta.description,
                                 meta.source,
                                 meta.validationStatus,
                                 meta.commitId,
                                 meta.eventId,
                                 QDir::toNativeSeparators(meta.dbPath)),
                        QMessageBox::Ok,
                        this);
        styleMessageBoxReadable(&box);
        box.exec();
    });
    connect(emergency, &QPushButton::clicked, this, [this]() {
        if (!m_backupManager) return;
        QString errorText;
        if (!m_backupManager->createEmergencyBackupNow(QStringLiteral("Manual emergency backup from restore manager"), nullptr, &errorText)) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Emergency backup"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
            return;
        }
        rebuild();
    });
    connect(restore, &QPushButton::clicked, this, [this]() {
        const SnapshotMetadata meta = selectedSnapshot();
        if (meta.snapshotPath.isEmpty() || !m_coordinator) return;

        const QVector<QPointer<QWidget>> riskyDialogs = collectRestoreSensitiveDialogs(this);
        if (!riskyDialogs.isEmpty()) {
            QMessageBox confirm(QMessageBox::Question,
                                QString::fromUtf8("Restore"),
                                QString::fromUtf8("Перед restore потрібно закрити відкриті діалоги редагування (%1).\n\nПродовжити?").arg(riskyDialogs.size()),
                                QMessageBox::Yes | QMessageBox::No,
                                this);
            confirm.setDefaultButton(QMessageBox::No);
            styleMessageBoxReadable(&confirm);
            if (confirm.exec() != QMessageBox::Yes) {
                return;
            }
            QString closeError;
            if (!closeRestoreSensitiveDialogs(riskyDialogs, &closeError)) {
                QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Restore"), closeError, QMessageBox::Ok, this);
                styleMessageBoxReadable(&box);
                box.exec();
                return;
            }
        }

        NativeBackupRestoreResult result;
        QString errorText;
        if (!m_coordinator->restoreSnapshot(meta, &result, &errorText)) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Restore"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
            return;
        }
        if (m_backupManager) {
            m_backupManager->reconcile(nullptr);
        }
        rebuild();
        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Restore"),
                        QString::fromUtf8("Базу даних відновлено.\nДжерело: %1\nRollback: %2\nНарядів після restore: %3\n\nДля повного оновлення кешів і вкладок застосунок потрібно перезапустити.")
                            .arg(QDir::toNativeSeparators(result.sourcePath), QDir::toNativeSeparators(result.rollbackBackupPath))
                            .arg(result.restoredOrderCount),
                        QMessageBox::NoButton,
                        this);
        box.addButton(QString::fromUtf8("Перезапустити зараз"), QMessageBox::AcceptRole);
        styleMessageBoxReadable(&box);
        box.exec();
        restartApplicationLater();
    });

    rebuild();
}

QVector<SnapshotMetadata> RestoreManagerDialog::filteredSnapshots() const
{
    QVector<SnapshotMetadata> all = m_coordinator ? m_coordinator->listSnapshots() : QVector<SnapshotMetadata>{};
    QVector<SnapshotMetadata> out;
    const QString scope = m_scope->currentData().toString();
    const QDate today = QDate::currentDate();
    const QDate prev = today.addDays(-1);
    const QString operationNeedle = m_operationFilter->currentData().toString().trimmed();
    const QString sourceNeedle = m_sourceFilter->currentData().toString().trimmed();
    const QString moduleNeedle = m_moduleFilter->text().trimmed();
    const QString entityNeedle = m_entitySearch->text().trimmed();

    for (const SnapshotMetadata& meta : all) {
        const QDate day = meta.createdAt.date();
        if (scope == QStringLiteral("today") && day != today) continue;
        if (scope == QStringLiteral("today_prev") && day != today && day != prev) continue;
        if (!operationNeedle.isEmpty()) {
            const bool detailsAliasMatch = (operationNeedle.compare(QStringLiteral("order_details_commit"), Qt::CaseInsensitive) == 0
                                            && meta.operationType.compare(QStringLiteral("details_commit"), Qt::CaseInsensitive) == 0)
                || (operationNeedle.compare(QStringLiteral("details_commit"), Qt::CaseInsensitive) == 0
                    && meta.operationType.compare(QStringLiteral("order_details_commit"), Qt::CaseInsensitive) == 0);
            if (!detailsAliasMatch && meta.operationType.compare(operationNeedle, Qt::CaseInsensitive) != 0) continue;
        }
        if (!sourceNeedle.isEmpty() && meta.source.compare(sourceNeedle, Qt::CaseInsensitive) != 0) continue;
        if (!moduleNeedle.isEmpty() && !meta.module.contains(moduleNeedle, Qt::CaseInsensitive)) continue;
        if (!entityNeedle.isEmpty()) {
            const QString blob = meta.entityType + QStringLiteral(" ") + meta.entityId + QStringLiteral(" ") + meta.description;
            if (!blob.contains(entityNeedle, Qt::CaseInsensitive)) continue;
        }
        out.push_back(meta);
    }
    std::sort(out.begin(), out.end(), [](const SnapshotMetadata& a, const SnapshotMetadata& b) { return a.createdAt > b.createdAt; });
    return out;
}

SnapshotMetadata RestoreManagerDialog::selectedSnapshot() const
{
    if (!m_table || m_table->currentRow() < 0) return {};
    const QVariant v = m_table->item(m_table->currentRow(), 0)->data(Qt::UserRole);
    return SnapshotMetadata::fromJson(v.toJsonObject());
}

void RestoreManagerDialog::rebuild()
{
    const QVector<SnapshotMetadata> items = filteredSnapshots();
    m_table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const SnapshotMetadata& meta = items.at(row);
        auto* t0 = new QTableWidgetItem(meta.createdAt.toString(QStringLiteral("dd.MM.yy HH:mm:ss")));
        t0->setData(Qt::UserRole, meta.toJson());
        m_table->setItem(row, 0, t0);
        m_table->setItem(row, 1, new QTableWidgetItem(meta.operationType));
        m_table->setItem(row, 2, new QTableWidgetItem(meta.module));
        m_table->setItem(row, 3, new QTableWidgetItem(meta.entityType + QStringLiteral("/") + meta.entityId));
        m_table->setItem(row, 4, new QTableWidgetItem(meta.description));
        m_table->setItem(row, 5, new QTableWidgetItem(QString::number(meta.snapshotSizeBytes / 1024) + QStringLiteral(" KB")));
        m_table->setItem(row, 6, new QTableWidgetItem(meta.validationStatus + QStringLiteral(" / ") + meta.source));
    }
    if (!items.isEmpty()) m_table->selectRow(0);
}
