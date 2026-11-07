#include "ui/tabs/settings/RecoveryManagerDialog.h"

#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RecoveryCoordinator.h"
#include "ui/common/dialogs/MessageBoxUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString settingsDialogChromeStyle()
{
    return QStringLiteral(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #2d3b4d, stop:0.52 #5e6d7f, stop:1 #2a3543); color:#f4f7fb;}"
        "QLabel{color:#f4f7fb;}"
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

QString buildRecoveryResultText(const RecoveryRestoreResult& result)
{
    QString text = QString::fromUtf8("Відновлено: %1\nПропущено: %2\nКонфліктів: %3")
                       .arg(result.restoredCount)
                       .arg(result.skippedCount)
                       .arg(result.conflictCount);
    if (!result.warnings.isEmpty()) {
        text += QString::fromUtf8("\n\nПопередження:\n- ") + result.warnings.join(QString::fromUtf8("\n- "));
    }
    if (!result.errors.isEmpty()) {
        text += QString::fromUtf8("\n\nПомилки:\n- ") + result.errors.join(QString::fromUtf8("\n- "));
    }
    return text;
}
}

RecoveryManagerDialog::RecoveryManagerDialog(CrashRecoveryManager* manager, RecoveryCoordinator* coordinator, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_coordinator(coordinator)
{
    setWindowTitle(QString::fromUtf8("Менеджер recovery-чернеток"));
    resize(1160, 640);
    setStyleSheet(settingsDialogChromeStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(10);

    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(settingsDialogTableStyle());
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("Session ID"),
        QString::fromUtf8("Last autosave"),
        QString::fromUtf8("Module / Form"),
        QString::fromUtf8("Entity"),
        QString::fromUtf8("Draft size"),
        QString::fromUtf8("Validity / Conflict")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(m_table, 1);

    auto* actions = new QHBoxLayout();
    auto* restoreAll = new QPushButton(QString::fromUtf8("Відновити все"), this);
    auto* restoreSelected = new QPushButton(QString::fromUtf8("Відновити вибране"), this);
    auto* details = new QPushButton(QString::fromUtf8("Деталі"), this);
    auto* ignoreSession = new QPushButton(QString::fromUtf8("Пропустити"), this);
    auto* deleteDrafts = new QPushButton(QString::fromUtf8("Видалити drafts"), this);
    auto* openFolder = new QPushButton(QString::fromUtf8("Відкрити папку recovery"), this);
    auto* refresh = new QPushButton(QString::fromUtf8("Оновити"), this);
    for (auto* button : {restoreAll, restoreSelected, details, ignoreSession, deleteDrafts, openFolder, refresh}) {
        styleDialogButton(button);
    }
    actions->addWidget(restoreAll);
    actions->addWidget(restoreSelected);
    actions->addWidget(details);
    actions->addWidget(ignoreSession);
    actions->addWidget(deleteDrafts);
    actions->addWidget(openFolder);
    actions->addWidget(refresh);
    actions->addStretch(1);
    root->addLayout(actions);

    connect(refresh, &QPushButton::clicked, this, &RecoveryManagerDialog::rebuild);
    connect(openFolder, &QPushButton::clicked, this, [this]() {
        if (m_manager && m_manager->hasRecoverableSession()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_manager->recoverableSession().sessionDir));
        }
    });
    connect(details, &QPushButton::clicked, this, [this]() {
        const QVector<DraftPayload> drafts = selectedDrafts();
        if (drafts.isEmpty()) return;
        const DraftPayload draft = drafts.constFirst();
        const QString runtimeConflictState = m_coordinator ? m_coordinator->effectiveConflictState(draft)
                                                           : (draft.conflictState.trimmed().isEmpty() ? QStringLiteral("unknown") : draft.conflictState);
        const QString runtimeConflictReason = m_coordinator ? m_coordinator->conflictReasonForDraft(draft) : QString();
        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Деталі recovery draft"),
                        QString::fromUtf8("Session: %1\\nLast autosave: %2\\nModule: %3\\nForm: %4\\nEntity: %5/%6\\nOrder UID: %7\\nDraft size: %8 bytes\\nValidity: %9\\nConflict: %10\\nCommitted base: %11\\nПричина конфлікту: %12\\nФайл: %13")
                            .arg(draft.recoverySessionId,
                                 draft.updatedAt.toString(QStringLiteral("dd.MM.yy HH:mm:ss")),
                                 draft.module,
                                 draft.formType,
                                 draft.entityType,
                                 draft.entityId,
                                 draft.orderUid,
                                 QString::number(draft.fileSizeBytes),
                                 draft.validationState,
                                 runtimeConflictState,
                                 draft.committedBaseVersion,
                                 runtimeConflictReason.isEmpty() ? QString::fromUtf8("—") : runtimeConflictReason,
                                 QDir::toNativeSeparators(draft.filePath)),
                        QMessageBox::Ok,
                        this);
        styleMessageBoxReadable(&box);
        box.exec();
    });
    connect(restoreAll, &QPushButton::clicked, this, [this]() {
        if (!m_coordinator) return;
        const RecoveryRestoreResult result = m_coordinator->restoreRecoverableDrafts(parentWidget());
        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Recovery"),
                        buildRecoveryResultText(result),
                        QMessageBox::Ok,
                        this);
        styleMessageBoxReadable(&box);
        box.exec();
        rebuild();
    });
    connect(restoreSelected, &QPushButton::clicked, this, [this]() {
        if (!m_coordinator) return;
        const RecoveryRestoreResult result = m_coordinator->restoreSelectedDrafts(selectedDrafts(), parentWidget());
        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Recovery"),
                        buildRecoveryResultText(result),
                        QMessageBox::Ok,
                        this);
        styleMessageBoxReadable(&box);
        box.exec();
        rebuild();
    });
    connect(ignoreSession, &QPushButton::clicked, this, [this]() {
        if (m_manager) {
            m_manager->forgetRecoverableSession();
        }
        accept();
    });
    connect(deleteDrafts, &QPushButton::clicked, this, [this]() {
        if (!m_coordinator) return;
        const QVector<DraftPayload> drafts = selectedDrafts();
        QString errorText;
        const bool ok = drafts.isEmpty()
            ? m_coordinator->deleteRecoverableSession(&errorText)
            : m_coordinator->deleteDrafts(drafts, &errorText);
        if (!ok) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Recovery"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
            return;
        }
        rebuild();
    });

    rebuild();
}

void RecoveryManagerDialog::rebuild()
{
    const QVector<DraftPayload> drafts = m_manager ? m_manager->recoverableDrafts() : QVector<DraftPayload>{};
    m_table->setRowCount(drafts.size());
    for (int row = 0; row < drafts.size(); ++row) {
        const DraftPayload& draft = drafts.at(row);
        const QString runtimeConflictState = m_coordinator ? m_coordinator->effectiveConflictState(draft)
                                                           : (draft.conflictState.trimmed().isEmpty() ? QStringLiteral("unknown") : draft.conflictState);
        auto* t0 = new QTableWidgetItem(draft.recoverySessionId);
        t0->setData(Qt::UserRole, draft.toJson());
        m_table->setItem(row, 0, t0);
        m_table->setItem(row, 1, new QTableWidgetItem(draft.updatedAt.toString(QStringLiteral("dd.MM.yy HH:mm:ss"))));
        m_table->setItem(row, 2, new QTableWidgetItem(draft.module + QStringLiteral(" / ") + draft.formType));
        m_table->setItem(row, 3, new QTableWidgetItem(draft.entityType + QStringLiteral("/") + draft.entityId));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(draft.fileSizeBytes) + QStringLiteral(" bytes")));
        m_table->setItem(row, 5, new QTableWidgetItem(draft.validationState + QStringLiteral(" / ") + runtimeConflictState));
    }
    if (!drafts.isEmpty()) m_table->selectRow(0);
}

QVector<DraftPayload> RecoveryManagerDialog::selectedDrafts() const
{
    QVector<DraftPayload> out;
    const QModelIndexList rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList{};
    for (const QModelIndex& idx : rows) {
        const QTableWidgetItem* item = m_table->item(idx.row(), 0);
        if (!item) continue;
        out.push_back(DraftPayload::fromJson(item->data(Qt::UserRole).toJsonObject()));
    }
    return out;
}
