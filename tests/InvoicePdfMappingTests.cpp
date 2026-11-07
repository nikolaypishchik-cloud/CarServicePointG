#include "ui/common/dialogs/InvoiceReviewDialog.h"
#include "ui/common/widgets/InvoiceImportProgressDialog.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QThread>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QScopeGuard>
#include "infrastructure/db/sqlite/Db.h"
#include <QTimer>
#include <QScreen>
#include "ui/common/dialogs/InvoiceColumnMappingEditor.h"
#include "ui/tabs/settings/InvoiceRecognitionSettingsPage.h"
#include "core/import/InvoiceRecognitionProfiles.h"
#include "core/import/ImportJob.h"
#include "core/import/parsers/InvoiceParsers.h"
#include "core/import/UniversalInvoiceColumnDetector.h"

using namespace InvoiceImport;

class InvoicePdfMappingTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settings;
    static double number(QString text, bool* ok) {
        text.remove(QChar(0x00a0));
        text.remove(' ');
        text.replace(',', '.');
        return text.toDouble(ok);
    }
    static QList<ImportedPartDraft> confirm(const ExcelReadResult& read, QString* error) {
        auto mapping = read.columnMappingPreview;
        mapping.priceWithVatConfirmed = true;
        mapping.supplierDisplayName = QStringLiteral("Test Store");
        mapping.templateApplied = false;
        return UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(read.rows, mapping,
            [](const QString& s) { return s; }, number,
            [](double n) { return QString::number(n, 'f', 2); }, error, read.documentDate);
    }
private slots:
    void reviewSelectionBulkAndFailure() {
        ExcelReadResult data;data.documentNumber="KAM26F058793";data.documentDate="2026-10-23";
        ImportedPartDraft d;d.name=QString::fromUtf8("Насос омивача");d.store=QString::fromUtf8("Еліт");d.oemNumber="2220019";d.brandName="METZGER";d.qtyText="1";d.priceText="298.50";
        InvoiceReviewDialog review({d,d},data);review.show();
        auto* table=review.findChild<QTableView*>("invoiceReviewTable");auto* import=review.findChild<QPushButton*>("reviewImport");
        QVERIFY(table); QVERIFY(table->model()->setData(table->model()->index(1,0),Qt::Unchecked,Qt::CheckStateRole));
        review.findChild<QComboBox*>("reviewBulkStore")->setEditText("Test Store");
        review.findChild<QPushButton*>("reviewApply")->click();
        QCOMPARE(table->model()->index(0,3).data().toString(),QString("Test Store"));QCOMPARE(table->model()->index(1,3).data().toString(),d.store);
        const QString delivery=table->model()->index(0,1).data().toString();QVERIFY(!delivery.isEmpty());
        review.findChild<QComboBox*>("reviewBulkStore")->setEditText(QString());review.findChild<QPushButton*>("reviewApply")->click();
        QCOMPARE(table->model()->index(0,3).data().toString(),QString("Test Store"));QCOMPARE(table->model()->index(0,1).data().toString(),delivery);
        int commits=0;review.commit=[&](QString* error){++commits;*error="Test write failure";return false;};
        import->click();QCOMPARE(commits,1);QVERIFY(review.isVisible());QCOMPARE(table->model()->index(0,3).data().toString(),QString("Test Store"));
        QVERIFY(table->model()->setData(table->model()->index(0,7),QString("invalid")));import->click();QCOMPARE(commits,1);QVERIFY(!table->model()->index(0,7).data(Qt::ToolTipRole).toString().isEmpty());
        QVERIFY(table->model()->setData(table->model()->index(0,7),QString("2")));review.findChild<QLineEdit*>("reviewDocumentTotal")->setText("1");
        QMetaObject::invokeMethod(review.findChild<QLineEdit*>("reviewDocumentTotal"),"textEdited",Q_ARG(QString,QString("1")));
        import->click();QCOMPARE(commits,1);review.findChild<QCheckBox*>("reviewDifferenceVerified")->setChecked(true);
        review.commit=[&](QString*){++commits;return true;};import->click();QCOMPARE(commits,2);QCOMPARE(review.result(),int(QDialog::Accepted));
        import->click();QCOMPARE(commits,2);
    }
    void reviewCancelAndLayout() {
        ExcelReadResult data;data.documentNumber="KAM26F058793";data.documentDate="2026-10-23";
        ImportedPartDraft d;d.name=QString::fromUtf8("Насос омивача");d.store=QString::fromUtf8("Еліт");d.oemNumber="2220019";d.brandName="METZGER";d.qtyText="1";d.priceText="298.50";
        InvoiceReviewDialog review({d},data);int commits=0;review.commit=[&](QString*){++commits;return true;};review.show();QTest::qWait(50);
        auto* visibleTable=review.findChild<QTableView*>("invoiceReviewTable");QVERIFY(visibleTable);
        // The offscreen platform can expose a narrow work area. Bring the editable
        // name cell into view, as keyboard navigation does, before checking clipping.
        visibleTable->scrollTo(visibleTable->model()->index(0,5));
        QApplication::processEvents();
        const QRect firstCell=visibleTable->visualRect(visibleTable->model()->index(0,5));
        QVERIFY(!firstCell.isEmpty());
        QVERIFY2(visibleTable->viewport()->rect().contains(firstCell), qPrintable(QString("cell=%1,%2 %3x%4 viewport=%5x%6 header=%7 dialog=%8x%9")
            .arg(firstCell.x()).arg(firstCell.y()).arg(firstCell.width()).arg(firstCell.height())
            .arg(visibleTable->viewport()->width()).arg(visibleTable->viewport()->height())
            .arg(visibleTable->horizontalHeader()->height()).arg(review.width()).arg(review.height())));
        const auto capture=qEnvironmentVariable("CS_TEST_SCREENSHOTS");if(!capture.isEmpty())review.grab().save(QDir(capture).filePath("invoice-review.png"));
        auto* importButton=review.findChild<QPushButton*>("reviewImport");QVERIFY(importButton);
        QVERIFY(review.rect().contains(QRect(importButton->mapTo(&review,QPoint()),importButton->size())));
        auto* table=review.findChild<QTableView*>("invoiceReviewTable");QVERIFY(table); QVERIFY(table->model()->setData(table->model()->index(0,0),Qt::Unchecked,Qt::CheckStateRole));QVERIFY(!review.findChild<QPushButton*>("reviewImport")->isEnabled());
        QVERIFY(table->model()->setData(table->model()->index(0,5),QString("Edited")));
        QTimer::singleShot(0,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});
        review.reject();QVERIFY(!review.isVisible());QCOMPARE(commits,0);
    }
    void minimumLoadingTimeAndCancelAfterSuccess() {
        QWidget owner;owner.show();ImportJob job;int callbacks=0;
        InvoiceImportProgressDialog progress(&owner,&job,[&](const auto&){++callbacks;});
        ImportJobRequest request;request.filePath=settings.filePath("missing-minimum.pdf");
        QElapsedTimer elapsed;elapsed.start();progress.start(request);
        ImportJobResult success;success.status=ImportJobStatus::Succeeded;emit job.finished(success);
        QTest::qWait(100);QCOMPARE(callbacks,0);QVERIFY(progress.isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(callbacks,1,3500);QVERIFY(elapsed.elapsed()>=2450);
        progress.start(request);emit job.finished(success);progress.reject();QCOMPARE(callbacks,2);
        QTest::qWait(2600);QCOMPARE(callbacks,2);
    }
    void segmentedProgressAndCancellation() {
        InvoiceSegmentProgress bar; bar.resize(205, 16);
        bar.setValue(50); const QImage pixels = bar.grab().toImage().scaled(bar.size());
        QCOMPARE(pixels.pixelColor(120, 8), QColor("#536172"));
        QCOMPARE(pixels.pixelColor(180, 8), QColor("#536172"));
        QVERIFY(pixels.pixelColor(8, 8) != QColor("#536172"));
        QWidget owner; owner.resize(1000, 600); owner.show();
        ImportJob job; int calls = 0;
        InvoiceImportProgressDialog dialog(&owner, &job, [&](const auto& result) {
            ++calls; QCOMPARE(result.status, ImportJobStatus::Cancelled);
        });
        ImportJobRequest request; request.filePath = settings.filePath("missing.pdf");
        dialog.start(request);
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(calls, 1); QVERIFY(!dialog.isVisible());
        ImportJobResult late; late.status = ImportJobStatus::Succeeded;
        emit job.finished(late); QCoreApplication::processEvents();
        QCOMPARE(calls, 1); QVERIFY(!dialog.isVisible());
        for (const char* button : {"invoiceReadCancel", "invoiceReadClose"}) {
            dialog.start(request); dialog.findChild<QPushButton*>(button)->click();
            QVERIFY(!dialog.isVisible());
        }
        QCOMPARE(calls, 3);
    }
    void readErrorRetryAndResponsiveUi() {
        QWidget owner; owner.resize(1000, 600); owner.show();
        ImportJob job; int calls = 0; ImportJobResult completed;
        InvoiceImportProgressDialog dialog(&owner, &job, [&](const auto& result) { ++calls; completed = result; });
        ImportJobRequest request; request.filePath = settings.filePath("retry-invoice.csv");
        QFile::remove(request.filePath); dialog.start(request);
        auto* retry = dialog.findChild<QPushButton*>("invoiceReadRetry");
        QTRY_VERIFY(retry->isVisible()); QCOMPARE(calls, 0);
        const auto capture = qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if (!capture.isEmpty()) dialog.grab().save(QDir(capture).filePath("invoice-progress-error.png"));
        QFile file(request.filePath); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Name;Quantity;Price;Sum\nFilter;2;100;200\n"); file.close();
        int ticks = 0; QTimer pulse; pulse.setInterval(1);
        connect(&pulse, &QTimer::timeout, &owner, [&] { ++ticks; }); pulse.start();
        retry->click();
        ImportJobProgress stage; stage.percent=50; stage.message=QString::fromUtf8("Розпізнаємо позиції та перевіряємо дані…");
        emit job.progressChanged(stage);
        if (!capture.isEmpty()) dialog.grab().save(QDir(capture).filePath("invoice-progress.png"));
        QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 60000);
        QVERIFY2(completed.ok(), qPrintable(completed.error.message));
        QVERIFY(ticks > 0); QVERIFY(!dialog.isVisible());
    }
    void rejectsNonPurchaseDocuments() {
        QString error;
        const auto transport = GenericPdfParser::parseText(QString::fromUtf8(
            "АВТОНОВА-Д\nДата виконання заявки  05.10.2026\nВид оплати Перевізнику  НАЛ\n"
            "Контактний телефон  380673026636\nСума по документам: УТОЧНИТИ У МЕНЕДЖЕРА"), &error);
        QVERIFY(transport.unsupportedDocument);
        QVERIFY(transport.rows.isEmpty());
        QVERIFY(error.contains(QString::fromUtf8("транспортна заявка")));
        const auto order = GenericPdfParser::parseText(QString::fromUtf8("CarServicePointG\nНАРЯД-ЗАМОВЛЕННЯ\nЗапчастини"), &error);
        QVERIFY(order.unsupportedDocument);
        QVERIFY(order.rows.isEmpty());
        QVERIFY(error.contains(QString::fromUtf8("наряд-замовлення")));
    }
    void suppliedOctoberDocuments() {
        const QString dir = qEnvironmentVariable("CS_OCTOBER_INVOICE_TEST_DIR");
        if (dir.isEmpty()) QSKIP("Set CS_OCTOBER_INVOICE_TEST_DIR for supplied PDF verification");
        for (const QString& file : {QString("KAM26F041852.pdf"), QString("403.pdf"), QString("transport.pdf")}) {
            ImportJob job;
            QSignalSpy completed(&job, &ImportJob::finished);
            ImportJobRequest request; request.filePath = QDir(dir).filePath(file); request.timeoutMs = 90000;
            QVERIFY(job.start(request)); QVERIFY(completed.wait(100000));
            const auto result = qvariant_cast<ImportJobResult>(completed.first().first());
            if (file == "KAM26F041852.pdf") {
                QVERIFY2(result.status == ImportJobStatus::Succeeded, qPrintable(result.error.message));
                QCOMPARE(result.data.documentNumber, QString("KAM26F041852"));
                qInfo() << "Elit automatic import" << result.data.automaticImportAllowed;
                QString error;
                const auto drafts = result.data.automaticImportAllowed ? result.data.parsedPartDrafts : confirm(result.data, &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
                QCOMPARE(drafts.size(), 8);
                double sum = 0;
                for (const auto& draft : drafts) { bool ok; sum += number(draft.amountText, &ok); QVERIFY(ok); }
                QVERIFY(qAbs(sum - 3856.77) < 0.005);
            } else {
                QVERIFY(result.status != ImportJobStatus::Succeeded);
                QVERIFY2(result.error.message.contains(QString::fromUtf8("накладн")), qPrintable(result.error.message));
            }
        }
        for (const auto& profile : InvoiceRecognitionProfiles::list()) {
            QString error;
            QVERIFY2(InvoiceRecognitionProfiles::remove(profile.key, &error), qPrintable(error));
        }
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName("CarServiceTests");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("Safety-InvoicePdfMapping-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QString error;
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error));
    }
    void cleanupTestCase() {
        const auto closeDatabase=qScopeGuard([] { Database::close(); });
        for (const auto& p : InvoiceRecognitionProfiles::list()) {
            QString error;
            QVERIFY2(InvoiceRecognitionProfiles::remove(p.key, &error), qPrintable(error));
        }
    }
    void splitCombinedFields() {
        ImportedPartDraft draft;
        InvoiceRecognitionProfiles::splitDescription(QString::fromUtf8("Прокладка колектора ELRING 150111"), &draft);
        QCOMPARE(draft.name, QString::fromUtf8("Прокладка колектора"));
        QCOMPARE(draft.brandName, QStringLiteral("ELRING"));
        QCOMPARE(draft.oemNumber, QStringLiteral("150111"));
        draft = {};
        InvoiceRecognitionProfiles::splitDescription(QString::fromUtf8("Повітряний фільтр BOSCH F 026 400 553"), &draft);
        QCOMPARE(draft.brandName, QStringLiteral("BOSCH"));
        QCOMPARE(draft.oemNumber, QStringLiteral("F 026 400 553"));
        draft = {};
        InvoiceRecognitionProfiles::splitDescription(QString::fromUtf8("Кільце ущільнювальне VAG N90978801"), &draft);
        QCOMPARE(draft.oemNumber, QStringLiteral("N90978801"));
        draft = {};
        draft.name = QString::fromUtf8("Олива моторна 5W40 1L");
        InvoiceRecognitionProfiles::splitDescription(draft.name, &draft);
        QVERIFY(draft.brandName.isEmpty());
        QVERIFY(draft.oemNumber.isEmpty());
        draft = {};
        draft.brandName = "Existing";
        InvoiceRecognitionProfiles::splitDescription(QString::fromUtf8("Прокладка ELRING 150111"), &draft);
        QCOMPARE(draft.brandName, QStringLiteral("Existing"));
        QVERIFY(draft.oemNumber.isEmpty());
    }
    void profilesAreScopedEditableAndValidated() {
        const QList<QStringList> rows = {
            {"Name", "Qty", "Total", "Price"}, {"Gasket ELRING 150111", "2", "20", "10"}, {"Supplier Shop One"}
        };
        const auto key = InvoiceRecognitionProfiles::keyFor(rows, 0, "INV-001");
        QVERIFY(!key.isEmpty());
        QCOMPARE(key, InvoiceRecognitionProfiles::keyFor(rows, 0, "INV-002"));
        auto different = rows;
        different[2][0] = "Supplier Shop Two";
        QVERIFY(key != InvoiceRecognitionProfiles::keyFor(different, 0, "INV-001"));
        different = rows;
        different[0].swapItemsAt(1, 3);
        QVERIFY(key != InvoiceRecognitionProfiles::keyFor(different, 0, "INV-001"));
        InvoiceColumnMappingPreview m;
        m.headerRow = 0;
        m.templateKey = key;
        m.partNameColumn = 0;
        m.quantityColumn = 1;
        m.totalWithVatColumn = 2;
        m.priceWithVatColumn = 3;
        m.priceWithVatConfirmed = true;
        m.supplierDisplayName = "Shop One";
        QString error;
        QVERIFY(InvoiceRecognitionProfiles::validateRows(rows, m, number, &error));
        QVERIFY2(InvoiceRecognitionProfiles::save(rows, m, &error), qPrintable(error));
        InvoiceColumnMappingPreview loaded;
        QVERIFY(InvoiceRecognitionProfiles::load(key, &loaded));
        QCOMPARE(loaded.quantityColumn, 1);
        QCOMPARE(loaded.supplierDisplayName, QStringLiteral("Shop One"));
        m.splitCombinedDescription = false;
        m.supplierDisplayName = "Corrected Shop";
        QVERIFY(InvoiceRecognitionProfiles::save(rows, m, &error));
        QVERIFY(InvoiceRecognitionProfiles::load(key, &loaded));
        QVERIFY(!loaded.splitCombinedDescription);
        QCOMPARE(loaded.supplierDisplayName, QStringLiteral("Corrected Shop"));
        different = rows;
        different[1][2] = "200";
        QVERIFY(!InvoiceRecognitionProfiles::validateRows(different, m, number, &error));
        different[1][1] = "0";
        QVERIFY(!InvoiceRecognitionProfiles::validateRows(different, m, number, &error));
        m.priceWithVatColumn = m.quantityColumn;
        QVERIFY(!InvoiceRecognitionProfiles::validateRows(rows, m, number, &error));
        QVERIFY(InvoiceRecognitionProfiles::remove(key, &error));
        QVERIFY(!InvoiceRecognitionProfiles::load(key, &loaded));
    }
    void amountOnlyRequiresConfirmation() {
        const QString text = QString::fromUtf8(
            "ВИДАТКОВА НАКЛАДНА № TEST-123 від 10 серпня 2026 р.\n"
            "Найменування                  Кількість        Сума, грн\n"
            "Кільце VAG WHT001965          2                207.10\n"
            "Всього на суму                                207.10 грн\n");
        QString error;
        const auto read = GenericPdfParser::parseText(text, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(read.manualMappingRequired);
        QCOMPARE(read.columnMappingPreview.partNameColumn, 0);
        QCOMPARE(read.columnMappingPreview.priceWithVatColumn, 3);
        QVERIFY(!read.columnMappingPreview.priceWithVatConfirmed);
        auto blocked = UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(read.rows, read.columnMappingPreview,
            [](const QString& s) { return s; }, number, [](double n) { return QString::number(n); }, &error);
        QVERIFY(blocked.isEmpty());
        error.clear();
        const auto drafts = confirm(read, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(drafts[0].priceText, QStringLiteral("103.55"));
        QCOMPARE(drafts[0].amountText, QStringLiteral("207.10"));
        QCOMPARE(read.documentNumber, QStringLiteral("TEST-123"));
        QCOMPARE(read.documentDate, QStringLiteral("2026-08-10"));
    }
    void settingsEditSavedProfile() {
        const QList<QStringList> rows = {
            {QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна з ПДВ")},
            {QString::fromUtf8("Прокладка колектора ELRING 150111"), "1", "320.00"}
        };
        InvoiceColumnMappingPreview preview;
        preview.headerRow = 0;
        preview.partNameColumn = 0;
        preview.quantityColumn = 1;
        preview.priceWithVatColumn = 2;
        preview.templateKey = InvoiceRecognitionProfiles::keyFor(rows, 0, "UI-001");
        preview.supplierDisplayName = QString::fromUtf8("Постачальник згідно оригіналу");
        bool storeInitiallyEmpty = false;
        bool controlsFound = false;
        const QString screenshots = qEnvironmentVariable("CS_INVOICE_UI_SCREENSHOTS");
        auto fillDialog = [&](const QString& store, bool split, const QString& screenshot) {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* input = dialog->findChild<QLineEdit*>("invoiceTemplateStore");
            auto* separate = dialog->findChild<QCheckBox*>("invoiceSplitDescription");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            controlsFound = input && separate && buttons;
            controlsFound = controlsFound && dialog->screen()->availableGeometry().contains(dialog->frameGeometry())
                && dialog->rect().contains(buttons->geometry());
            if (!controlsFound) { dialog->reject(); return; }
            storeInitiallyEmpty = input->text().isEmpty();
            input->setText(store);
            for (auto* check : dialog->findChildren<QCheckBox*>()) check->setChecked(true);
            separate->setChecked(split);
            if (!screenshots.isEmpty()) {
                QDir().mkpath(screenshots);
                dialog->grab().save(QDir(screenshots).filePath(screenshot));
            }
            buttons->button(QDialogButtonBox::Ok)->click();
        };
        QTimer::singleShot(100, this, [&]() { fillDialog("Admin Store", true, "mapping.png"); });
        InvoiceColumnMappingPreview confirmed;
        QVERIFY(editInvoiceColumnMapping(nullptr, rows, preview, QString(), &confirmed));
        QVERIFY(controlsFound);
        QVERIFY(storeInitiallyEmpty);
        QCOMPARE(confirmed.supplierDisplayName, QStringLiteral("Admin Store"));
        QString error;
        const auto drafts = UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(rows, confirmed,
            [](const QString& s) { return s; }, number, [](double n) { return QString::number(n); }, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(drafts.first().store, QStringLiteral("Admin Store"));
        QCOMPARE(drafts.first().brandName, QStringLiteral("ELRING"));
        QScopedPointer<QWidget> page(buildInvoiceRecognitionSettingsPage(nullptr));
        page->resize(1100, 700);
        page->show();
        auto* table = page->findChild<QTableWidget*>("invoiceRecognitionProfiles");
        auto* edit = page->findChild<QPushButton*>("editInvoiceRecognitionProfile");
        QVERIFY(table && edit);
        QCOMPARE(table->rowCount(), 1);
        table->selectRow(0);
        QVERIFY(edit->isEnabled());
        QTimer::singleShot(100, this, [&]() { fillDialog("Corrected Admin Store", false, "mapping-edited.png"); });
        edit->click();
        InvoiceColumnMappingPreview reloaded;
        QTRY_VERIFY_WITH_TIMEOUT(InvoiceRecognitionProfiles::load(preview.templateKey, &reloaded)
            && reloaded.supplierDisplayName == "Corrected Admin Store", 10000);
        QVERIFY(!reloaded.splitCombinedDescription);
        QTRY_COMPARE(table->item(0, 0)->text(), QStringLiteral("Corrected Admin Store"));
        table->selectRow(0);
        QVERIFY(edit->isEnabled());
        if (!screenshots.isEmpty()) page->grab().save(QDir(screenshots).filePath("settings.png"));
        QVERIFY(InvoiceRecognitionProfiles::remove(preview.templateKey, &error));
    }
    void attachedPdfs_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("count");
        QTest::addColumn<double>("total");
        QTest::newRow("A") << QString::fromUtf8("А-04633898.pdf") << 5 << 1146.35;
        // Original supplied invoice: 76.25 net + 15.25 VAT = 91.50 gross.
        QTest::newRow("Elit-single") << QStringLiteral("KAM26F043589.pdf") << 1 << 91.50;
        QTest::newRow("Elit-eight") << QStringLiteral("KAM26F041852.pdf") << 8 << 3856.77;
    }
    void unknownHeadersRemainEditable() {
        QString error;
        const auto read = GenericPdfParser::parseText(QStringLiteral(
            "Custom supply document\nItem  Units  Gross\nFilter ABC123  2  240.00\n"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(read.manualMappingRequired);
        QVERIFY(!read.rows.isEmpty());
        QVERIFY(read.rows.contains(QStringList{"Filter ABC123", "2", "240.00"}));
    }
    void attachedPdfs() {
        const QString dir = qEnvironmentVariable("CS_INVOICE_PDF_TEST_DIR");
        if (dir.isEmpty()) QSKIP("Set CS_INVOICE_PDF_TEST_DIR to verify the supplied PDFs.");
        QFETCH(QString, file);
        QFETCH(int, count);
        QFETCH(double, total);
        ImportJob job;
        QSignalSpy completed(&job, &ImportJob::finished);
        ImportJobRequest request;
        request.filePath = QDir(dir).filePath(file);
        request.timeoutMs = 90000;
        QVERIFY(job.start(request));
        QVERIFY(completed.wait(100000));
        const auto result = qvariant_cast<ImportJobResult>(completed.first().first());
        QVERIFY2(result.status == ImportJobStatus::Succeeded, qPrintable(result.error.message));
        // The one-line Elit supplier layout has explicit recognized columns.
        // The eight-line indexed catalogue layout deliberately requires mapping
        // (GenericPdfParser::indexedPdfTable), even though its supplier is known.
        const bool knownLayout = file == QStringLiteral("KAM26F043589.pdf");
        QCOMPARE(result.data.automaticImportAllowed, knownLayout);
        QCOMPARE(result.data.manualMappingRequired, !knownLayout);
        QString error;
        const auto drafts = knownLayout ? result.data.parsedPartDrafts : confirm(result.data, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(drafts.size(), count);
        double sum = 0;
        for (const auto& row : drafts) {
            bool ok;
            sum += number(row.amountText, &ok);
            QVERIFY(ok);
        }
        QVERIFY(qAbs(sum - total) < 0.005);
        QCOMPARE(result.data.documentNumber, QFileInfo(file).completeBaseName());
        QVERIFY(!drafts[0].brandName.isEmpty());
        QVERIFY(!drafts[0].oemNumber.isEmpty());
        QCOMPARE(drafts[0].store, knownLayout ? QString::fromUtf8("Еліт") : QStringLiteral("Test Store"));
        if (!knownLayout) {
            completed.clear();
            QVERIFY(job.start(request));
            QVERIFY(completed.wait(100000));
            const auto second = qvariant_cast<ImportJobResult>(completed.first().first());
            QVERIFY2(second.data.automaticImportAllowed, qPrintable(second.data.automaticImportError));
            QVERIFY(!second.data.manualMappingRequired);
            QCOMPARE(second.data.parsedPartDrafts.size(), count);
        }
    }
};
QTEST_MAIN(InvoicePdfMappingTests)
#include "InvoicePdfMappingTests.moc"
