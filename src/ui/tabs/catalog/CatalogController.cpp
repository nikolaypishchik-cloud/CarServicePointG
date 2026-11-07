#include "ui/tabs/catalog/CatalogController.h"

#include "core/backup/BackupCommitPublisher.h"
#include "core/services/catalog/CatalogAdminService.h"
#include "ui/common/dialogs/MessageBoxUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QUrl>

namespace {
void publishCatalogBackupEvent(const QString& operationType,
                               int entityId,
                               const QString& description)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("CatalogController"),
                                         QStringLiteral("catalog"),
                                         QStringLiteral("catalog_part"),
                                         entityId > 0 ? QString::number(entityId) : QString(),
                                         QString(),
                                         0,
                                         operationType,
                                         description,
                                         {QStringLiteral("parts_catalog")}),
        nullptr);
}

void styleBox(QMessageBox* box)
{
    applyStandardMessageBoxStyle(box);
}
}

bool CatalogController::saveRow(QWidget* parent, const CatalogRowData& row)
{
    QString schemaError;
    if (!CatalogAdminService::ensureImportSchema(&schemaError)) {
        QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Каталог"), schemaError, QMessageBox::Ok, parent);
        styleBox(&box);
        box.exec();
        return false;
    }

    const QString beforeJson = CatalogAdminService::snapshotItem(row.id);

    const auto saveResult = CatalogAdminService::saveRow(row);
    if (!saveResult.ok) {
        QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Каталог"), saveResult.errorText, QMessageBox::Ok, parent);
        styleBox(&box);
        box.exec();
        return false;
    }

    QString logError;
    CatalogAdminService::logMutation(
        saveResult.operation,
        saveResult.recordId,
        beforeJson,
        CatalogAdminService::snapshotItem(saveResult.recordId),
        &logError);

    publishCatalogBackupEvent(row.id > 0 ? QStringLiteral("catalog_save") : QStringLiteral("catalog_create"),
                              saveResult.recordId,
                              row.id > 0
                                  ? QString::fromUtf8("Оновлено картку каталогу: %1").arg(row.name)
                                  : QString::fromUtf8("Створено картку каталогу: %1").arg(row.name));
    return true;
}

bool CatalogController::deleteRowWithConfirmation(QWidget* parent, int id)
{
    if (id <= 0) return false;

    QMessageBox confirm(QMessageBox::Question,
                        QString::fromUtf8("Каталог"),
                        QString::fromUtf8("Видалити вибрану позицію з каталогу?"),
                        QMessageBox::Yes | QMessageBox::No,
                        parent);
    styleBox(&confirm);
    if (confirm.exec() != QMessageBox::Yes) return false;

    const QString beforeJson = CatalogAdminService::snapshotItem(id);
    const auto deleteResult = CatalogAdminService::deleteRow(id);
    if (!deleteResult.ok) {
        QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Каталог"), deleteResult.errorText, QMessageBox::Ok, parent);
        styleBox(&box);
        box.exec();
        return false;
    }

    QString logError;
    CatalogAdminService::logMutation(QStringLiteral("remove"), id, beforeJson, QStringLiteral("{}"), &logError);
    publishCatalogBackupEvent(QStringLiteral("catalog_delete"),
                              id,
                              QString::fromUtf8("Видалено позицію каталогу #%1").arg(id));
    return true;
}

bool CatalogController::importFromOrders(QWidget* parent)
{
    QString schemaError;
    if (!CatalogAdminService::ensureImportSchema(&schemaError)) {
        QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Каталог"), schemaError, QMessageBox::Ok, parent);
        styleBox(&box);
        box.exec();
        return false;
    }

    QMessageBox confirm(QMessageBox::Question,
                        QString::fromUtf8("Каталог"),
                        QString::fromUtf8("Буде виконано повний безпечний імпорт карток запчастин із нарядів.\n"
                                          "У каталог потраплять усі валідні запчастини з усіх доступних шарів БД.\n"
                                          "Існуючі картки буде дооновлено без стирання ручних правок.\n\n"
                                          "Продовжити?"),
                        QMessageBox::Yes | QMessageBox::No,
                        parent);
    confirm.setDefaultButton(QMessageBox::Yes);
    styleBox(&confirm);
    if (confirm.exec() != QMessageBox::Yes) return false;

    CatalogImportReport report;
    QString importError;
    if (!CatalogAdminService::importFromOrders(&report, &importError)) {
        QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Каталог"), importError, QMessageBox::Ok, parent);
        styleBox(&box);
        box.exec();
        return false;
    }

    publishCatalogBackupEvent(QStringLiteral("catalog_import"),
                              report.addedCount + report.updatedCount,
                              QString::fromUtf8("Імпортовано каталог із нарядів: +%1 / ~%2")
                                  .arg(report.addedCount)
                                  .arg(report.updatedCount));

    QMessageBox box((report.errorCount > 0 || report.skippedCount > 0 || report.conflictCount > 0)
                        ? QMessageBox::Warning
                        : QMessageBox::Information,
                    QString::fromUtf8("Каталог"),
                    report.summaryText,
                    QMessageBox::NoButton,
                    parent);
    auto* openSummaryBtn = box.addButton(QString::fromUtf8("Відкрити звіт"), QMessageBox::ActionRole);
    auto* openFolderBtn = box.addButton(QString::fromUtf8("Відкрити папку звітів"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    styleBox(&box);
    box.exec();
    if (box.clickedButton() == openSummaryBtn) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(report.reportsDir).filePath(QStringLiteral("CatalogImportSummary.md"))));
    } else if (box.clickedButton() == openFolderBtn) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(report.reportsDir));
    }
    return true;
}
