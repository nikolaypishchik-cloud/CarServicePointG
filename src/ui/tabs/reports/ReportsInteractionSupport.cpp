#include "ui/tabs/reports/ReportsInteractionSupport.h"

#include "core/services/AppRuntimeService.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include <functional>

#include "core/backup/BackupCommitPublisher.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include "core/import/InvoiceAttachmentPdfService.h"
#include "ui/common/dialogs/InvoiceDocumentOpenSupport.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QKeySequence>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QTableView>

#include <algorithm>

void ReportsInteractionSupport::openInvoiceFromReport(QWidget* parent, const QModelIndex& index)
{
    if (!index.isValid() || index.column() != 3) return;
    const QString number = index.data().toString().trimmed();
    if (number.isEmpty()) return;
    const auto attachment = index.data(Qt::UserRole).toMap();
    QString path = attachment.value(QStringLiteral("path")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::warning(parent, QString::fromUtf8("Накладна"),
                             QString::fromUtf8("PDF накладної №%1 не знайдено.").arg(number));
        return;
    }
    InvoiceDocumentOpenSupport::prepareAsync(parent, path, number,
        attachment.value(QStringLiteral("date")).toString(),
        attachment.value(QStringLiteral("store")).toString(), [parent, number](const QString& path, const QString& error) {
        if (path.isEmpty()) {
            QMessageBox::warning(parent, QString::fromUtf8("Накладна"), error);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) {
            QMessageBox::warning(parent, QString::fromUtf8("Накладна"),
                                 QString::fromUtf8("Не вдалося відкрити PDF накладної №%1.").arg(number));
        }
    });
}

namespace {

QString selectedTableText(QTableView* table)
{
    if (!table) return {};
    QString text;
    const auto indexes = table->selectionModel() ? table->selectionModel()->selectedIndexes() : QModelIndexList();
    if (!indexes.isEmpty()) {
        QModelIndexList sorted = indexes;
        std::sort(sorted.begin(), sorted.end(), [](const QModelIndex& a, const QModelIndex& b) {
            if (a.row() != b.row()) return a.row() < b.row();
            return a.column() < b.column();
        });
        int currentRow = sorted.first().row();
        QStringList rowParts;
        for (const QModelIndex& mi : sorted) {
            if (mi.row() != currentRow) {
                text += rowParts.join(QStringLiteral("\t")) + QStringLiteral("\n");
                rowParts.clear();
                currentRow = mi.row();
            }
            rowParts << mi.data().toString();
        }
        if (!rowParts.isEmpty()) text += rowParts.join(QStringLiteral("\t"));
    } else if (table->currentIndex().isValid()) {
        text = table->currentIndex().data().toString();
    }
    return text;
}

void alignCurrentSelectionForContextMenu(QTableView* table, const QPoint& pos)
{
    if (!table || !table->selectionModel()) return;
    const QModelIndex idx = table->indexAt(pos);
    if (!idx.isValid()) return;
    const bool alreadySelected = table->selectionModel()->isRowSelected(idx.row(), idx.parent());
    const auto flags = alreadySelected
        ? (QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::NoUpdate)
        : (QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    table->selectionModel()->setCurrentIndex(idx, flags);
}

} // namespace

namespace ReportsInteractionSupport {

void configurePayrollTableContextMenu(
    QWidget* parent,
    QTableView* payrollTable,
    const std::function<void()>& refreshPayroll,
    const std::function<void()>& queueRefreshAll,
    const std::function<QString()>& archiveDateIso)
{
    if (!payrollTable) return;
    payrollTable->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(payrollTable, &QWidget::customContextMenuRequested, parent, [=](const QPoint& pos) {
        if (!payrollTable) return;
        alignCurrentSelectionForContextMenu(payrollTable, pos);

        QMenu menu(payrollTable);
        QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати"));
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(payrollTable->currentIndex().isValid());

        QAction* actSelectAll = menu.addAction(QString::fromUtf8("Виділити все"));
        actSelectAll->setShortcut(QKeySequence::SelectAll);
        actSelectAll->setEnabled(payrollTable->model() && payrollTable->model()->rowCount() > 0);

        QAction* actClearSelection = menu.addAction(QString::fromUtf8("Зняти виділення"));
        actClearSelection->setEnabled(payrollTable->selectionModel() && payrollTable->selectionModel()->hasSelection());

        QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити"));
        actDelete->setShortcut(QKeySequence::Delete);
        const int selectedRowsCount = payrollTable->selectionModel() ? payrollTable->selectionModel()->selectedRows().size() : 0;
        actDelete->setEnabled(selectedRowsCount > 0);
        if (selectedRowsCount > 1) actDelete->setText(QString::fromUtf8("Видалити вибрані рядки"));

        QAction* picked = menu.exec(payrollTable->viewport()->mapToGlobal(pos));
        if (!picked) return;

        if (picked == actCopy) {
            QApplication::clipboard()->setText(selectedTableText(payrollTable));
            return;
        }

        if (picked == actSelectAll) {
            payrollTable->selectAll();
            return;
        }

        if (picked == actClearSelection) {
            if (payrollTable->selectionModel()) payrollTable->selectionModel()->clearSelection();
            return;
        }

        if (picked != actDelete) return;

        QModelIndexList rows = payrollTable->selectionModel() ? payrollTable->selectionModel()->selectedRows() : QModelIndexList();
        if (rows.isEmpty() && payrollTable->currentIndex().isValid()) {
            rows << payrollTable->model()->index(payrollTable->currentIndex().row(), 0);
        }
        if (rows.isEmpty()) return;

        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
            return a.row() > b.row();
        });

        const QString prompt = rows.size() > 1
            ? QString::fromUtf8("Видалити/архівувати вибраних співробітників (%1)?").arg(rows.size())
            : QString::fromUtf8("Видалити/архівувати вибраного співробітника?");
        if (QMessageBox::question(parent,
                                  QString::fromUtf8("Підтвердження"),
                                  prompt,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        QSet<QString> processedIds;
        const QString archiveDate = archiveDateIso ? archiveDateIso() : QString();
        for (const QModelIndex& mi : rows) {
            if (!mi.isValid()) continue;
            const QString masterId = payrollTable->model()
                ? payrollTable->model()->index(mi.row(), 0).data(Qt::UserRole).toString().trimmed()
                : QString();
            if (masterId.isEmpty() || processedIds.contains(masterId)) continue;
            QString deleteError;
            if (!PersonnelSettingsService::deleteMasters({masterId}, &deleteError, archiveDate)) {
                QMessageBox::critical(parent,
                                      QString::fromUtf8("Помилка"),
                                      QString::fromUtf8("Не вдалося видалити або архівувати співробітника."));
                return;
            }
            processedIds.insert(masterId);
        }

        if (refreshPayroll) refreshPayroll();
        if (queueRefreshAll) queueRefreshAll();
    });
}

void openOrderFromReportTable(
    QWidget* parent,
    QTableView* table,
    int orderColumn,
    int row,
    const std::function<void()>& onAccepted)
{
    if (!table || !table->model() || row < 0 || row >= table->model()->rowCount()) return;
    const QModelIndex orderIndex = table->model()->index(row, orderColumn);
    if (!orderIndex.isValid()) return;
    bool ok = false;
    int orderNumber = orderIndex.data(Qt::UserRole).toInt(&ok);
    if (!ok || orderNumber <= 0) {
        orderNumber = orderIndex.data(Qt::DisplayRole).toString().trimmed().toInt(&ok);
    }
    if (!ok || orderNumber <= 0) return;
    const QString orderUid = AppRuntimeService::resolveOrderUid(orderNumber);
    QString routingError;
    if (!orderUid.trimmed().isEmpty()) {
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, orderNumber, &orderNumber, &routingError)) {
            QMessageBox::critical(parent, QString::fromUtf8("Помилка"), routingError);
            return;
        }
    }
    table->selectRow(row);
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(parent, orderNumber);
    dlg.setWindowTitle(QString::fromUtf8("Редагування наряду"));
    dlg.openWorkPartsTab();
    if (dlg.exec() == QDialog::Accepted && onAccepted) {
        onAccepted();
    }
}

void openOutsourceOrOrderFromReportTable(
    QWidget* parent,
    QTableView* table,
    int row,
    int orderColumn,
    const std::function<void()>& onAccepted)
{
    if (!table || !table->model() || row < 0 || row >= table->model()->rowCount()) return;
    const QModelIndex dateIndex = table->model()->index(row, 0);
    const QModelIndex orderIndex = table->model()->index(row, orderColumn);
    bool okOrder = false;
    int orderNumber = orderIndex.data(Qt::UserRole).toInt(&okOrder);
    if (!okOrder || orderNumber <= 0) {
        orderNumber = orderIndex.data(Qt::DisplayRole).toString().trimmed().toInt(&okOrder);
    }
    const int serviceId = dateIndex.data(Qt::UserRole).toInt();
    if (okOrder && orderNumber > 0 && serviceId > 0) {
        table->selectRow(row);
        if (WorkPartsDialog::editOutsourceJob(parent, orderNumber, serviceId) && onAccepted) {
            onAccepted();
        }
        return;
    }
    openOrderFromReportTable(parent, table, orderColumn, row, onAccepted);
}

void publishExpensesBackupEvent(
    const QString& dateFromIso,
    const QString& dateToIso,
    double totalAmount,
    double electricityTariff)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("ReportsPage"),
                                         QStringLiteral("reports_expenses"),
                                         QStringLiteral("expenses_period"),
                                         QStringLiteral("%1_%2").arg(dateFromIso, dateToIso),
                                         QString(),
                                         0,
                                         QStringLiteral("expenses_save"),
                                         QString::fromUtf8("Збережено витрати за період %1 — %2; сума %3; тариф %4")
                                             .arg(dateFromIso, dateToIso)
                                             .arg(QString::number(totalAmount, 'f', 2))
                                             .arg(QString::number(electricityTariff, 'f', 4)),
                                         {QStringLiteral("expenses"), QStringLiteral("settings")}),
        nullptr);
}

} // namespace ReportsInteractionSupport
