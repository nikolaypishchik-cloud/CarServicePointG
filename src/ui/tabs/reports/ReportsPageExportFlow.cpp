// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/reports/ReportsPageModulePrelude.h"
#include "ui/tabs/reports/ReportsPdfPresentation.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include "core/services/InvoiceReportService.h"
#include <QTemporaryDir>
#include <QSharedPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include "ui/theme/ThemeManager.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QProcess>
#include <QImageReader>
#include <QScreen>
#include <algorithm>
#include "ui/tabs/reports/InvoicesPage.h"

namespace {
class OrderReportPdfPreview final : public QScrollArea {
public:
    explicit OrderReportPdfPreview(QWidget* parent) : QScrollArea(parent) {
        setObjectName("orderReportPdfPreview"); setWidgetResizable(true);
        setStyleSheet("QScrollArea{background:#d7dde3;} QLabel{background:white;color:black;}");
        auto* body = new QWidget(this); pagesLayout = new QVBoxLayout(body);
        setWidget(body);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { renderVisiblePages(); });
    }
    bool load(const QString& path) {
        QString exe = QCoreApplication::applicationDirPath() + "/resources/windows/ocr-layer/poppler/bin/pdftoppm.exe";
        if (!QFileInfo::exists(exe)) exe = QCoreApplication::applicationDirPath() + "/../resources/windows/ocr-layer/poppler/bin/pdftoppm.exe";
        if (!QFileInfo::exists(exe) || !previewDirectory->isValid()) return false;
        auto* loading = new QLabel(tr("Підготовка прев’ю…"), widget()); pagesLayout->addWidget(loading);
        // The render process outlives a closing preview until its asynchronous
        // cancellation completes. Its temporary output must outlive it as well.
        auto* process = new QProcess(qApp);
        process->setObjectName("orderReportPdfRenderer");
        const auto directoryLifetime = previewDirectory;
        connect(process, &QObject::destroyed, qApp, [directoryLifetime] {});
        connect(this, &QObject::destroyed, process, [process] {
            if (process->state() != QProcess::NotRunning) process->kill();
            else process->deleteLater();
        });
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) process->deleteLater();
        });
        auto* timeout = new QTimer(process); timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, process, [process] { process->kill(); });
        connect(process, &QProcess::errorOccurred, loading, [loading] { loading->setText(tr("Не вдалося підготувати прев’ю PDF.")); });
        connect(process, &QProcess::finished, this, [this, process, timeout, loading, path](int code, QProcess::ExitStatus state) {
            timeout->stop();
            if (code != 0 || state != QProcess::NormalExit) { loading->setText(tr("Не вдалося підготувати прев’ю PDF.")); return; }
            auto files = QDir(previewDirectory->path()).entryInfoList({"preview-*.png"}, QDir::Files);
            std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
                return a.baseName().section('-', -1).toInt() < b.baseName().section('-', -1).toInt();
            });
            if (files.isEmpty()) { loading->setText(tr("PDF не містить сторінок.")); return; }
            loading->deleteLater();
            for (const auto& file : files) {
                auto* page = new QLabel(widget()); page->setObjectName("orderReportPdfPage");
                page->setAlignment(Qt::AlignCenter); pagesLayout->addWidget(page, 0, Qt::AlignHCenter); pages.append(page);
                imagePaths.append(file.absoluteFilePath());
                pageSizes.append(QImageReader(file.absoluteFilePath()).size());
            }
            resizePages(); process->deleteLater();
        });
        process->start(exe, {"-r", "180", "-png", path, previewDirectory->filePath("preview")});
        timeout->start(30000); return true;
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QScrollArea::resizeEvent(event); resizePages(); }
private:
    void resizePages() {
        width = qRound(qMax(120, qBound(240, viewport()->width() - 30, 1100) / 2) * 1.30);
        for (int i = 0; i < pages.size(); ++i) {
            const QSizeF size = pageSizes[i];
            if (size.width() <= 0) continue;
            pages[i]->clear(); pages[i]->setFixedSize(width, qRound(width * size.height() / size.width()));
            pages[i]->setProperty("rendered", false);
        }
        const auto margins = pagesLayout->contentsMargins();
        int documentHeight = margins.top() + margins.bottom();
        for (const auto* page : pages) documentHeight += page->height();
        if (!pages.isEmpty()) documentHeight += (pages.size() - 1) * pagesLayout->spacing();
        widget()->setMinimumHeight(documentHeight);
        QTimer::singleShot(0, this, [this] { renderVisiblePages(); });
    }
    void renderVisiblePages() {
        for (int i = 0; i < pages.size(); ++i) {
            auto* page = pages[i];
            const int y = page->mapTo(viewport(), QPoint()).y();
            const bool visible = y < viewport()->height() && y + page->height() > 0;
            if (!visible) { page->clear(); page->setProperty("rendered", false); continue; }
            if (page->property("rendered").toBool()) continue;
            QImageReader reader(imagePaths[i]); reader.setScaledSize(page->size() * devicePixelRatioF());
            auto pixmap = QPixmap::fromImage(reader.read()); pixmap.setDevicePixelRatio(devicePixelRatioF());
            page->setPixmap(pixmap); page->setProperty("rendered", true);
        }
    }
    QSharedPointer<QTemporaryDir> previewDirectory = QSharedPointer<QTemporaryDir>::create();
    QStringList imagePaths;
    QList<QSize> pageSizes;
    QVBoxLayout* pagesLayout = nullptr;
    QList<QLabel*> pages;
    int width = 800;
};
}

QWidget* ReportsDialogSupport::createInvoicePdfPreview(QWidget* parent, const QString& path)
{
    auto* preview = new OrderReportPdfPreview(parent);
    if (preview->load(path)) return preview;
    delete preview;
    return new QLabel(QString::fromUtf8("Не вдалося підготувати прев’ю PDF."), parent);
}

void ReportsPage::payReportOrder()
{
    const int row=m_ordersTable->currentIndex().row();
    const int number=m_ordersModel->cellUserValue(row,0).toInt();
    if(!InvoiceReportService::canViewOrder(number))return;
    QString error;
    const auto summary=InvoiceReportService::orderSummary(number,&error);
    if(!error.isEmpty()) {QMessageBox::warning(this,tr("Оплата"),error);return;}
    const auto& debts=summary.debts;
    const auto& invoices=summary.invoices;
    QStringList choices,keys;
    if(!debts.isEmpty()) {choices.append(tr("Сплатити за наряд"));keys.append(QString());}
    for(const auto& invoice:invoices)if(invoice.orders.contains(number) && !invoice.paid()) {
        choices.append(tr("Накладна %1 · %2").arg(invoice.number,invoice.supplier));keys.append(invoice.key);
    }
    if(choices.isEmpty()) {
        markDirty();queueRefreshAll();
        QMessageBox::information(this,tr("Оплата"),tr("Оплата вже здійснена. Заборгованість відсутня."));
        return;
    }
    QInputDialog chooser(this);
    chooser.setWindowTitle(tr("Оплата запчастин"));
    chooser.setLabelText(tr("Оплата"));
    chooser.setComboBoxItems(choices);
    chooser.setComboBoxEditable(false);
    chooser.setStyleSheet(ThemeManager::currentTheme()=="light"
        ? "QInputDialog { background:#f6f8fb; } QLabel { color:black; background:transparent; }"
        : "QInputDialog { background:#343b43; } QLabel { color:white; background:transparent; }");
    if(choices.size()>1 && chooser.exec()!=QDialog::Accepted)return;
    const auto selected=choices.size()>1?chooser.textValue():choices.first();
    const int selectedIndex=choices.indexOf(selected);
    if(selectedIndex<0)return;
    if(!keys.value(selectedIndex).isEmpty()){m_invoicesPage->payInvoiceByKey(keys.value(selectedIndex));markDirty();queueRefreshAll();return;}
    if(!openOrderPartsPayment(this,number))return;
    markDirty();queueRefreshAll();
}

void ReportsPage::previewOrderReport()
{
    if (!m_ordersTable || !m_ordersModel) return;
    const int number = m_ordersModel->cellUserValue(m_ordersTable->currentIndex().row(), 0).toInt();
    if (!InvoiceReportService::canViewOrder(number)) return;
    const auto routed = CurrentOrdersCoordinator::resolveOrderForOpen(number, AppRuntimeService::resolveOrderUid(number));
    if (!routed.ok) { QMessageBox::warning(this, tr("Наряд"), routed.errorText); return; }
    QTemporaryDir temporary;
    if (!temporary.isValid()) { QMessageBox::warning(this, tr("Прев’ю"), tr("Не вдалося підготувати тимчасовий документ.")); return; }
    const QString path = temporary.filePath("order.pdf");
    QString error;
    {
        OrderDialog exporter(this, routed.orderNumber); exporter.setReadOnly(true);
        if (!exporter.exportPartsReportToFile(path, &error)) { QMessageBox::warning(this, tr("Прев’ю"), error); return; }
    }
    QDialog dialog(this); dialog.setObjectName("orderReportPreviewDialog");
    const auto orderTitle=tr("Наряд №%1").arg(m_ordersModel->cellText(m_ordersTable->currentIndex().row(), 1));
    dialog.setWindowTitle(orderTitle);
#ifdef Q_OS_WIN
    // Qt's Windows decoration appends applicationDisplayName; this preview uses only its report title.
    const auto syncNativeTitle = [&dialog] {
        QTimer::singleShot(0,&dialog,[&dialog] {
            if (dialog.isVisible()) SetWindowTextW(reinterpret_cast<HWND>(dialog.winId()),reinterpret_cast<LPCWSTR>(dialog.windowTitle().utf16()));
        });
    };
    connect(&dialog,&QWidget::windowTitleChanged,&dialog,syncNativeTitle);
    syncNativeTitle();
#endif
    dialog.resize(1000, 750);
    auto* layout = new QVBoxLayout(&dialog);
    auto* pdf = new OrderReportPdfPreview(&dialog); layout->addWidget(pdf, 1);
    if (!pdf->load(path)) { QMessageBox::warning(this, tr("Прев’ю"), tr("Не вдалося відобразити PDF наряду.")); return; }
    auto* status = new QLabel(&dialog); status->setWordWrap(true); layout->insertWidget(0,status);
    status->setStyleSheet("QLabel{color:#ffffff;background:#263746;font-size:16pt;font-weight:600;padding:12px;}");
    auto* actions = new QHBoxLayout;
    auto* payAll = new QPushButton(tr("Сплатити запчастини наряду"), &dialog); payAll->setObjectName("reportPayOrderParts");
    auto* payPart = new QPushButton(tr("Сплатити позицію"), &dialog); payPart->setObjectName("reportPayPart");
    auto* close = new QPushButton(tr("Закрити"), &dialog);
    for (auto* button : {payAll, payPart, close}) { button->setMinimumHeight(48); button->setAutoDefault(false); actions->addWidget(button); }
    layout->addLayout(actions);
    auto update = [&] {
        QString error;
        const auto summary=InvoiceReportService::orderSummary(routed.orderNumber,&error);
        const auto total=summary.remainingCents;
        const auto due=summary.dueDate;
        const auto paid=summary.paidDate;
        const bool datesKnown=summary.paymentDateKnown;
        const QString text=total>0?tr("Запчастини не сплачені, сплатити до %1 · %2 грн")
            .arg(due.isValid()?due.toString("dd.MM.yyyy"):tr("не вказано"),FormatUtils::moneyCents(total))
            :tr("Запчастини сплачені%1").arg(datesKnown && paid.isValid()?" "+paid.toString("dd.MM.yyyy"):tr(" · дату не зафіксовано"));
        status->setText(error.isEmpty()?text:error);
        dialog.setWindowTitle(orderTitle+" · "+status->text());
        const bool allowed = error.isEmpty() && total > 0 && PurchasePaymentService::canPay("part");
        payAll->setEnabled(allowed); payPart->setEnabled(allowed);
    };
    auto changed = [&] { update(); markDirty(); queueRefreshAll(); };
    connect(payAll, &QPushButton::clicked, &dialog, [&] {
        if (!openOrderPartsPayment(&dialog, routed.orderNumber)) return;
        changed();
    });
    connect(payPart, &QPushButton::clicked, &dialog, [&] {
        QString error; const auto debts = InvoiceReportService::orderSummary(routed.orderNumber, &error).debts;
        if (!error.isEmpty()) { QMessageBox::warning(&dialog, tr("Оплата"), error); return; }
        QStringList choices; QList<PurchaseDebt> candidates;
        for (const auto& debt : debts) if (debt.remainingCents() > 0) {
            candidates << debt; choices << tr("%1 | %2 | %3 грн | №%4").arg(debt.name, debt.supplier, FormatUtils::moneyCents(debt.remainingCents()), QString::number(debt.id));
        }
        if (choices.isEmpty()) { update(); return; }
        bool accepted = false; const QString choice = QInputDialog::getItem(&dialog, tr("Оплата позиції"), tr("Запчастина"), choices, 0, false, &accepted);
        const int index = choices.indexOf(choice); if (!accepted || index < 0) return;
        const auto debt = candidates[index];
        if (openPurchasePayment(&dialog, debt.source, debt.id, debt.orderUid)) changed();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    update();
    QTimer::singleShot(0, &dialog, [&dialog] {
        const auto* screen = dialog.screen();
        if (!screen) return;
        const int margin = qRound(screen->logicalDotsPerInch() * 10.0 / 25.4);
        const QRect safe = screen->availableGeometry().adjusted(margin, margin, -margin, -margin);
        const QSize decoration = dialog.frameGeometry().size() - dialog.size();
        dialog.resize(qMin(1000, safe.width() - decoration.width()), safe.height() - decoration.height());
        dialog.move(safe.center().x() - dialog.frameGeometry().width() / 2, safe.top());
    });
    dialog.exec();
}

void ReportsPage::exportElectricityPdf()
{
    runPdfExportTask(QString::fromUtf8("Формування PDF по електроенергії..."), []() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderElectricityPdf(&result);
        return result;
    });
}

void ReportsPage::refreshPayroll()
{
    m_tabKeys.remove(static_cast<int>(ReportTab::Payroll));
    if(m_pdfButton)m_pdfButton->setEnabled(false);
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Payroll), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::Payroll, QString::fromUtf8("Зарплата — нарахування майстрів та адміна у межах вибраного періоду."));
    setTabAuxStats(ReportTab::Payroll, QString());
    updateStats({QString::fromUtf8("Завантаження зарплати…")});
    ReportsAsyncSupport::showLoadingRow(m_payrollTable, m_payrollModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startPayrollLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const PayrollSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Payroll) return;
        applyPayrollSnapshot(snapshot);
        if(snapshot.error.isEmpty())m_tabKeys[static_cast<int>(ReportTab::Payroll)] = key;
        else m_tabKeys.remove(static_cast<int>(ReportTab::Payroll));
        applyCachedTabUi(ReportTab::Payroll);
    });
}

void ReportsPage::openPayrollOrdersDialog(int row)
{
    if (!m_payrollTable || !m_payrollModel || row < 0 || row >= m_payrollModel->rowCount()) return;

    const QString masterId = m_payrollModel->cellUserValue(row, 0).toString().trimmed();
    if (masterId.isEmpty()) return;

    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const auto orders = ReportsSyncSupport::collectPayrollOrdersForMaster(masterId, df, dt);

    ReportsDialogSupport::openPayrollOrdersDialog(this, masterId, m_payrollModel->cellText(row, 0), orders);
}

void ReportsPage::runPdfExportTask(const QString& progressText, std::function<ReportsPdfRenderer::ExportResult()> task)
{
    if (m_pdfExportInFlight || !task) {
        return;
    }

    m_pdfExportInFlight = true;
    if (m_pdfButton) m_pdfButton->setEnabled(false);
    if (m_electricityPdfButton) m_electricityPdfButton->setEnabled(false);

    const bool hadOverrideCursor = QApplication::overrideCursor() != nullptr;
    if (!hadOverrideCursor) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
    }

    AsyncTaskHandle::cancel(m_asyncCancelToken);
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::Reports,
        QStringLiteral("ReportsPage/pdf_export"),
        60000,
        40);
    m_asyncCancelToken = runtimeTicket.cancelToken;
    const auto cancelToken = m_asyncCancelToken;
    const AsyncTaskHandle asyncHandle(this, 0, cancelToken);
    auto result = std::make_shared<ReportsPdfRenderer::ExportResult>();
    auto* worker = AsyncThreadScope::startWorker(this,
                                               QStringLiteral("reports_pdf_export"),
                                               cancelToken,
                                               [task = std::move(task), result, runtimeTicket]() mutable {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        *result = task();
        AppRuntimeService::closeThreadConnection();
    });
    QPointer<ReportsPage> ownerGuard(this);
    connect(worker, &QThread::finished, this, [this, ownerGuard, result, hadOverrideCursor, asyncHandle, runtimeTicket]() {
        const bool canApply = asyncHandle.shouldApply() && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        m_pdfExportInFlight = false;
        if (m_pdfButton) m_pdfButton->setEnabled(true);
        if (m_electricityPdfButton) m_electricityPdfButton->setEnabled(true);
        if (!hadOverrideCursor && QApplication::overrideCursor() != nullptr) {
            QApplication::restoreOverrideCursor();
        }
        if (!ownerGuard || !canApply) return;

        if (!result->success) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("PDF"),
                                 result->errorText.trimmed().isEmpty()
                                    ? QString::fromUtf8("Не вдалося сформувати PDF")
                                    : result->errorText);
            return;
        }

        ReportsPdfPresentation::openExportedPdf(this, *result);
    });
    worker->start();
}

void ReportsPage::exportActivePdf()
{
    switch (activeTab()) {
    case ReportTab::Period:    exportPeriodPdf(); break;
    case ReportTab::Orders: exportOrdersPdf(); break;
    case ReportTab::TotalExpenses:
    case ReportTab::Debts:
    case ReportTab::DebtAnalysis:
        return;
    case ReportTab::Parts:     exportPartsPdf(); break;
    case ReportTab::Outsource: exportOutsourcePdf(); break;
    case ReportTab::Expenses:  exportExpensesPdf(); break;
    case ReportTab::Payroll:   exportPayrollPdf(); break;
    }
}

void ReportsPage::exportPeriodPdf()
{
    const QDate fromDate = m_from->date();
    const QDate toDate = m_to->date();
    runPdfExportTask(QString::fromUtf8("Формування PDF звіту за період..."), [fromDate, toDate]() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderPeriodPdf(fromDate, toDate, &result);
        return result;
    });
}

void ReportsPage::exportOrdersPdf()
{
    if (m_pdfExportInFlight || !m_ordersTable || !m_ordersModel) return;
    const int row = m_ordersTable->currentIndex().row();
    const int orderNumber = m_ordersModel->cellUserValue(row, 0).toInt();
    if (orderNumber <= 0) {
        QMessageBox::information(this, QString::fromUtf8("Наряди"), QString::fromUtf8("Оберіть наряд зі списку."));
        return;
    }
    m_pdfExportInFlight = true;
    if (m_pdfButton) m_pdfButton->setEnabled(false);
    const auto result = CurrentOrdersCoordinator::exportOrderPdf(
        this, orderNumber, AppRuntimeService::resolveOrderUid(orderNumber), true);
    m_pdfExportInFlight = false;
    if (m_pdfButton) m_pdfButton->setEnabled(true);
    if (!result.errorText.isEmpty()) QMessageBox::warning(this, QString::fromUtf8("PDF"), result.errorText);
}

void ReportsPage::exportPartsPdf()
{
    const auto key=ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Parts),m_from->date(),m_to->date());
    if(m_tabKeys.value(static_cast<int>(ReportTab::Parts))!=key)return;
    const QDate fromDate = m_from->date();
    const QDate toDate = m_to->date();
    runPdfExportTask(QString::fromUtf8("Формування PDF звіту по запчастинах..."), [fromDate, toDate]() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderPartsPdf(fromDate, toDate, &result);
        return result;
    });
}

void ReportsPage::exportOutsourcePdf()
{
    const QDate fromDate = m_from->date();
    const QDate toDate = m_to->date();
    runPdfExportTask(QString::fromUtf8("Формування PDF звіту по аутсорсу..."), [fromDate, toDate]() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderOutsourcePdf(fromDate, toDate, &result);
        return result;
    });
}

void ReportsPage::exportExpensesPdf()
{
    const auto data = ReportsExpensesSupport::buildPdfData({
        m_expensesPage, m_elecPrevEdit, m_elecCurrEdit, m_elecTariffEdit, m_elecKwhEdit, m_elecCostEdit,
        m_cleaningEdit, m_cleaningNoteEdit, m_softwareEdit, m_softwareNoteEdit,
        m_heatingEdit, m_heatingNoteEdit, m_amortizationEdit, m_amortizationNoteEdit,
        m_rentEdit, m_rentNoteEdit, m_otherEdit, m_otherNoteEdit, m_expensesTotalEdit,
        m_expensesSaveButton, m_electricityPdfButton
    });
    const QDate fromDate = m_from->date();
    const QDate toDate = m_to->date();
    runPdfExportTask(QString::fromUtf8("Формування PDF звіту по витратах..."), [fromDate, toDate, data]() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderExpensesPdf(fromDate, toDate, data, &result);
        return result;
    });
}
void ReportsPage::exportPayrollPdf()
{
    const auto key=ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Payroll),m_from->date(),m_to->date());
    if(m_tabKeys.value(static_cast<int>(ReportTab::Payroll))!=key)return;
    const QDate fromDate = m_from->date();
    const QDate toDate = m_to->date();
    runPdfExportTask(QString::fromUtf8("Формування PDF звіту по зарплаті..."), [fromDate, toDate]() {
        ReportsPdfRenderer::ExportResult result;
        ReportsPdfRenderer::renderPayrollPdf(fromDate, toDate, &result);
        return result;
    });
}
