#include "core/services/orders/ClientReceiptReader.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/reports/ReportsPageModulePrelude.h"
#include "ui/tabs/reports/PurchaseDebtsPage.h"
#include "ui/tabs/reports/InvoicesPage.h"
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTextDocument>
#include <QTextTable>
#include <QAbstractTextDocumentLayout>
#include "ui/theme/ThemeManager.h"
#include "ui/tabs/reports/ReportDashboard.h"

namespace {
void fitReportSearchRows(QWidget* page, bool invoices) {
    auto* row=page->findChild<QWidget*>("reportsHeaderRow");
    auto* filters=page->findChild<QWidget*>("reportsFiltersFrame");
    auto* search=page->findChild<QLineEdit*>(invoices?"invoiceSearch":"reportsOrdersSearch");
    auto* content=page->findChild<QWidget*>("reportsHeaderContent");
    if(!row || !filters || !search || !content)return;
    const int height=qRound(7.0*page->logicalDpiY()/25.4);
    const int gap=qRound((invoices?1.5:1.0)*page->logicalDpiY()/25.4);
    row->layout()->setContentsMargins(4,qRound(page->logicalDpiY()/25.4),4,0);
    row->layout()->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    filters->layout()->setContentsMargins(4,0,0,0);
    content->layout()->setSpacing(gap);
    search->setFixedHeight(height);
    auto dates=filters->findChildren<QDateEdit*>();
    for(auto* date:dates)date->setFixedHeight(height);
    row->layout()->activate();filters->layout()->activate();
    const int searchLeft=filters->x()+filters->width()+row->layout()->spacing();
    search->setFixedWidth(qMax(120,qRound(page->width()*0.46)-searchLeft));
    if(invoices) {
        auto* controls=page->findChild<QWidget*>("invoiceControlsHost");
        auto* summary=page->findChild<QLabel*>("invoiceSummary");
        if(!controls || !summary || dates.isEmpty())return;
        const int left=dates.first()->mapTo(content,QPoint()).x();
        controls->layout()->setContentsMargins(0,0,0,0);
        if(auto* bar=controls->layout()->itemAt(0)->layout())bar->setSpacing(qRound(0.5*page->logicalDpiX()/25.4));
        for(const auto& name:{"invoiceStatusFilter","invoiceSort","invoiceRefresh"}) {
            if(auto* field=page->findChild<QWidget*>(name)) {
                field->ensurePolished();
                field->setFixedWidth(field->sizeHint().width());field->setFixedHeight(height);
            }
        }
        controls->setFixedWidth(controls->layout()->sizeHint().width());
        if(auto* host=page->findChild<QWidget*>("invoiceSummaryHost"))host->layout()->setContentsMargins(left,0,4,0);
        summary->setMinimumWidth(0);summary->setMaximumWidth(QWIDGETSIZE_MAX);
        summary->setMinimumHeight(0);summary->setMaximumHeight(QWIDGETSIZE_MAX);
        const int available=content->width()-searchLeft-controls->sizeHint().width()-row->layout()->spacing()-4;
        search->setFixedWidth(qMin(search->width(),qMax(120,available)));
    }
    row->layout()->activate();
}

void fitPartsSummary(QWidget* page) {
    auto* top=page->findChild<QWidget*>("reportsTopHost");
    auto* row=page->findChild<QWidget*>("reportsHeaderRow");
    auto* banner=page->findChild<QWidget*>("reportsBannersHost");
    auto* label=page->findChild<QLabel*>("reportsOilStatsLabel");
    auto* filters=page->findChild<QWidget*>("reportsFiltersFrame");
    if(!top || !row || !banner || !label || !filters)return;
    // Transfer only the unused part of the former 23% stock column to the dates.
    if(!label->text().isEmpty()) {
        QTextDocument doc;doc.setDefaultFont(label->font());doc.setDocumentMargin(0);
        doc.setHtml(label->text());
        const auto cursor=doc.find(QString::fromUtf8("На складі:"));
        if(auto* table=cursor.currentTable()) {
            auto* combined=table;
            const auto first=combined->cellAt(0,0).firstCursorPosition();
            const auto gapCell=combined->cellAt(0,2).firstCursorPosition();
            doc.setTextWidth(2000);doc.documentLayout()->documentSize();
            const qreal left=doc.documentLayout()->blockBoundingRect(first.block()).left();
            const qreal right=doc.documentLayout()->blockBoundingRect(gapCell.block()).left();
            const int base=filters->property("reportFiltersBaseWidth").toInt();
            const int available=qMax(0,row->width()-8-row->layout()->spacing()-base-16);
            const int freed=qMax(0,qRound(available*0.23-(right-left)-2.0*page->logicalDpiX()/25.4));
            filters->setFixedWidth(base+freed);
            filters->setProperty("reportTransferredWidth",freed);
        }
    }
    const int bottom=qMax(1,qRound(page->logicalDpiY()/25.4));
    banner->layout()->setContentsMargins(8,6,8,bottom);
    label->setMinimumHeight(0);
    top->layout()->activate();row->layout()->activate();banner->layout()->activate();
    const int content=qMax(label->heightForWidth(label->width()),label->fontMetrics().height());
    const auto margins=row->layout()->contentsMargins();
    const int height=qMax(content+6+bottom,filters->layout()->minimumSize().height())+margins.top()+margins.bottom();
    if(!label->text().isEmpty())page->setProperty("reportsReferenceHeight",height);
    top->setFixedHeight(page->property("reportsReferenceHeight").toInt()>0?page->property("reportsReferenceHeight").toInt():85);
}
}

void ReportsPage::setTableBaseStyle(QTableView* table)
{
    ReportsTableSupport::setTableBaseStyle(table);
}

ReportsPage::ReportTab ReportsPage::activeTab() const
{
    return static_cast<ReportTab>(m_tabs ? m_tabs->currentIndex() : 0);
}

void ReportsPage::markDirty()
{
    AsyncTaskHandle::cancel(m_asyncCancelToken);
    RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Reports);
    ++m_asyncRefreshGeneration;
    m_tabKeys.clear();
    SnapshotCacheInvalidator::invalidateReportsAndStats(QStringLiteral("ReportsPage/markDirty"));
}

void ReportsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if(activeTab()==ReportTab::Parts&&!ReportDashboard::supports(static_cast<int>(activeTab()))) {
        fitPartsSummary(this);
    }
    if(m_dashboard && ReportDashboard::supports(static_cast<int>(activeTab())))updateDashboard();
    updatePdfButtonPosition();
}

void ReportsPage::closeEvent(QCloseEvent* event)
{
    AsyncTaskHandle::cancel(m_asyncCancelToken);
    RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Reports);
    ++m_asyncRefreshGeneration;
    QWidget::closeEvent(event);
}

void ReportsPage::updatePdfButtonPosition()
{
    if (!m_tabs || !m_pdfButton) return;

    // QTabWidget reserves space for its corner widget, including after DPI changes.
    m_pdfButton->updateGeometry();
}

void ReportsPage::refresh()
{
    scheduleRefresh(0);
}

void ReportsPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    if (delayMs <= 0) {
        m_refreshAllDebounce->stop();
        m_refreshAllDebounce->start(0);
        return;
    }
    if (!m_refreshAllDebounce->isActive() || m_refreshAllDebounce->remainingTime() > delayMs) {
        m_refreshAllDebounce->start(delayMs);
    }
}

void ReportsPage::scheduleRefresh(int delayMs)
{
    if (!m_refreshDebounce) {
        refreshActiveTab();
        return;
    }
    if (delayMs <= 0) {
        m_refreshDebounce->stop();
        m_refreshDebounce->start(0);
        return;
    }
    if (!m_refreshDebounce->isActive() || m_refreshDebounce->remainingTime() > delayMs) {
        m_refreshDebounce->start(delayMs);
    }
}

// ReportsAsyncSupport/RuntimeJobDispatcher startLatest replaces AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken)
// for tab-load jobs and keeps the one-active Reports family invariant.
void ReportsPage::refreshActiveTab()
{
    if (m_refreshDebounce) m_refreshDebounce->stop();
    const ReportTab tab = activeTab();
    const int idx = static_cast<int>(tab);
    const bool debtTab = tab==ReportTab::Debts || tab==ReportTab::DebtAnalysis || tab==ReportTab::Invoices;
    applyCachedTabUi(tab);
    if (debtTab) {
        if (tab==ReportTab::Debts) m_debtsPage->refresh();
        else if (tab==ReportTab::Invoices) {
            m_invoicesPage->setPeriod(m_from->date(),m_to->date());
            m_invoicesPage->refresh();
        }
        else {
            m_debtAnalysisPage->setExpensePeriod(m_from->date(),m_to->date());
            m_debtAnalysisPage->refresh();
        }
        updateStats({});
        m_oilStatsLabel->hide(); m_descLabel->clear(); return;
    }
    const bool asyncTab = ReportsAsyncSupport::isAsyncTab(idx);
    const QString key = ReportsAsyncSupport::buildTabKey(idx, m_from ? m_from->date() : QDate(), m_to ? m_to->date() : QDate())
        + (tab == ReportTab::Orders ? QStringLiteral("|search:") + m_searchEdit->text().trimmed() : QString());
    if (m_tabKeys.value(idx) == key) {
        applyCachedTabUi(tab);
        return;
    }
    QElapsedTimer t; t.start();
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("ReportsPage::refreshActiveTab"), 120);
    switch (tab) {
    case ReportTab::Period:    refreshPeriod(); break;
    case ReportTab::Orders:    refreshOrders(); break;
    case ReportTab::Parts:     refreshParts(); break;
    case ReportTab::Outsource: refreshOutsource(); break;
    case ReportTab::Expenses:  refreshExpenses(); break;
    case ReportTab::TotalExpenses: refreshTotalExpenses(); break;
    case ReportTab::Payroll:   refreshPayroll(); break;
    }
    if (!asyncTab) {
        m_tabKeys[idx] = key;
        applyCachedTabUi(tab);
    }
    qDebug() << "ReportsPage refresh tab=" << idx << "ms=" << t.elapsed() << "async=" << asyncTab;
}

void ReportsPage::updateStats(const QStringList& lines)
{
    m_statsLabel->setVisible(activeTab()!=ReportTab::Parts);
    const QString textColor=ThemeManager::currentTheme()=="light"?"#111111":"#ffffff";
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='0' style='color:%1;'>").arg(textColor);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        const int sep = line.indexOf(":");
        if (sep > 0) {
            const QString label = line.left(sep + 1).toHtmlEscaped();
            const QString value = line.mid(sep + 1).trimmed().toHtmlEscaped();
            html += QString::fromUtf8(R"(
                <tr>
                <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;">%1</td>
                <td style="padding:0; text-align:left; white-space:nowrap;">%2</td>
                </tr>)").arg(label, value);
        } else {
            html += QString::fromUtf8(R"(<tr><td colspan="2" style="padding:0; text-align:left; white-space:nowrap;">%1</td></tr>)")
                .arg(line.toHtmlEscaped());
        }
    }
    html += QString::fromUtf8("</table>");
    m_statsLabel->setText(html);
    if(m_dashboard && ReportDashboard::supports(static_cast<int>(activeTab()))) {
        // Loading/error text is not a zero-valued result.
        if(lines.size()==1 && !lines.first().startsWith(QString::fromUtf8("Нарядів:")))m_dashboard->values({lines.first()});
        else updateDashboard();
    }
}

void ReportsPage::setTabStats(ReportTab tab, const QStringList& lines)
{
    m_tabStats[static_cast<int>(tab)] = lines;
    if (activeTab() == tab) updateStats(lines);
}

void ReportsPage::setTabAuxStats(ReportTab tab, const QString& html)
{
    m_tabAuxStats[static_cast<int>(tab)] = html;
    if (!m_oilStatsLabel) return;
    if (activeTab() != tab) return;
    applyCachedTabUi(tab);
}

void ReportsPage::setTabDescription(ReportTab tab, const QString& text)
{
    m_tabDescriptions[static_cast<int>(tab)] = text;
    if (activeTab() == tab && m_descLabel) m_descLabel->setText(text);
}

void ReportsPage::applyCachedTabUi(ReportTab tab)
{
    if (tab != activeTab()) return;
    const bool parts = tab == ReportTab::Parts;
    const bool debtTab = tab==ReportTab::Debts || tab==ReportTab::DebtAnalysis || tab==ReportTab::Invoices;
    const bool filtersVisible = !debtTab || tab==ReportTab::Invoices;
    layout()->setContentsMargins(0,filtersVisible?qRound(logicalDpiY()/25.4)-qRound(0.5*logicalDpiY()/25.4):0,0,0);
    // Shared header geometry belongs to the selected tab, not to an async result.
    m_searchEdit->setVisible(tab==ReportTab::Orders);
    m_invoicesPage->searchField()->setVisible(tab==ReportTab::Invoices);
    m_invoicesPage->controlsHost()->setVisible(tab==ReportTab::Invoices);
    if(auto* host=findChild<QWidget*>("invoiceSummaryHost"))host->setVisible(tab==ReportTab::Invoices);
    if(auto* summary=findChild<QLabel*>("invoiceSummary"))summary->setVisible(tab==ReportTab::Invoices);
    if(auto* controls=findChild<QWidget*>("additionalExpensesFormScroll"))
        controls->setVisible(tab==ReportTab::TotalExpenses);
    m_from->setEnabled(filtersVisible);
    m_to->setEnabled(filtersVisible);
    if(m_pdfButton) {
        const bool visible = tab!=ReportTab::TotalExpenses && !debtTab;
        m_pdfButton->setVisible(visible);
        m_pdfButton->setEnabled(!m_pdfExportInFlight && (tab!=ReportTab::Payroll || m_tabKeys.contains(static_cast<int>(ReportTab::Payroll))));
        if(auto* corner=m_tabs->cornerWidget(Qt::TopRightCorner)) corner->setVisible(visible);
    }
    if(auto* banners=findChild<QWidget*>("reportsBannersHost"))
        banners->setVisible(!debtTab && tab!=ReportTab::Orders && tab!=ReportTab::TotalExpenses);
    if(auto* top=findChild<QWidget*>("reportsTopHost")) {
        top->setVisible(filtersVisible);
        // All ordinary reports share the measured parts-summary viewport.
        if(!parts)top->setFixedHeight(property("reportsReferenceHeight").toInt()>0?property("reportsReferenceHeight").toInt():85);
        top->layout()->setAlignment(Qt::Alignment());
        if(auto* content=findChild<QWidget*>("reportsHeaderContent")) {
            content->layout()->setAlignment(parts?Qt::Alignment():Qt::AlignTop);
            content->setSizePolicy(QSizePolicy::Preferred,parts?QSizePolicy::Ignored:QSizePolicy::Preferred);
            content->layout()->setSizeConstraint(parts?QLayout::SetNoConstraint:QLayout::SetDefaultConstraint);
            content->setMinimumHeight(0);
        }
        if(auto* row=findChild<QWidget*>("reportsHeaderRow")) {
            row->layout()->setAlignment(parts?Qt::Alignment():Qt::AlignTop);
            row->layout()->setContentsMargins(4,0,4,parts?qMax(1,qRound(logicalDpiY()/25.4)):0);
        }
    }
    const QString textColor=ThemeManager::currentTheme()=="light"?"#111111":"#ffffff";
    m_statsLabel->setMinimumHeight(0);
    m_statsLabel->setStyleSheet(QStringLiteral("#reportsStatsLabel { color:%1; background:transparent; font-size:11pt; font-weight:normal; }").arg(textColor));
    if (auto* form=findChild<QWidget*>("additionalExpensesForm"))
        form->setStyleSheet(QStringLiteral("QWidget#additionalExpensesForm {background:%2;} QWidget#additionalExpensesForm QLabel {color:%1;background:transparent;} QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox {background:white;color:black;}").arg(textColor,ThemeManager::currentTheme()=="light"?"#f5f6f7":"#293541"));
    m_tabs->tabBar()->setStyleSheet(QStringLiteral("QTabBar::tab:!selected {color:%1;}").arg(textColor));
    m_oilStatsLabel->setStyleSheet(QStringLiteral("#reportsOilStatsLabel { color:%1; background:transparent; font-size:11pt; font-weight:normal; }").arg(textColor));
    if (auto* frame=findChild<QWidget*>("reportsFiltersFrame")) {
        frame->setStyleSheet(QStringLiteral("QWidget#reportsFiltersFrame {border:%1;background:transparent;} QLabel {border:none;background:transparent;color:%2;}")
            .arg(parts ? QStringLiteral("1px solid black") : QStringLiteral("none"), textColor));
        const bool compact = filtersVisible;
        auto* filters = qobject_cast<QHBoxLayout*>(frame->layout());
        auto* top = qobject_cast<QHBoxLayout*>(frame->parentWidget()->layout());
        const int gap = qMax(1, qRound(frame->logicalDpiX()*0.5/25.4));
        if (filters && top) {
            filters->setSpacing(compact ? gap : 12);
            filters->setContentsMargins(4,filtersVisible?qRound(logicalDpiY()/25.4):0,compact ? 0 : 4,0);
            top->setSpacing(compact ? gap : 4);
            int width = 4 + 3*gap;
            for (int i=0;i<filters->count();++i) {
                auto* control=filters->itemAt(i)->widget();
                if (!control) continue;
                if (compact) {
                    control->ensurePolished();
                    int required=qMax(control->minimumSizeHint().width(),control->sizeHint().width());
                    if(qobject_cast<QDateEdit*>(control)) {
                        control->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
                        if(!control->property("reportDateBaseWidth").isValid())control->setProperty("reportDateBaseWidth",required);
                        required=control->property("reportDateBaseWidth").toInt()+qRound(5.0*logicalDpiX()/25.4);
                    }
                    control->setFixedWidth(required);
                    width+=required;
                } else {
                    control->setMaximumHeight(QWIDGETSIZE_MAX);
                    control->setMinimumHeight(qobject_cast<QDateEdit*>(control)?AdaptiveUi::px(control,34):0);
                    control->setMaximumWidth(QWIDGETSIZE_MAX);
                    control->setMinimumWidth(qobject_cast<QDateEdit*>(control) ? AdaptiveUi::px(control,124) : 0);
                }
            }
            if (compact) {frame->setFixedWidth(width);frame->setProperty("reportFiltersBaseWidth",width);}
            else if(parts) frame->setFixedWidth(qMax(filters->minimumSize().width(),qRound(this->width()*0.27)));
            else { frame->setMinimumWidth(0); frame->setMaximumWidth(QWIDGETSIZE_MAX); }
            top->setAlignment(frame,parts?Qt::Alignment():Qt::AlignTop);
        }
    }
    if (auto* frame=findChild<QWidget*>("reportsBannersHost"))
        frame->setStyleSheet(parts ? "QWidget#reportsBannersHost { border:1px solid black; background:transparent; } QLabel {border:none;background:transparent;}" : "QWidget#reportsBannersHost {border:none;background:transparent;}");
    const int idx = static_cast<int>(tab);
    if (m_descLabel) m_descLabel->setText(m_tabDescriptions.value(idx));
    updateStats(m_tabStats.value(idx));
    if (m_oilStatsLabel) {
        QString html = m_tabAuxStats.value(idx);
        html.replace("REPORT_COLUMN_GAP",QString::number(qMax(1,qRound(5.0*logicalDpiX()/25.4))));
        html.replace("REPORT_VALUE_GAP",QString::number(qMax(1,qRound(7.0*logicalDpiX()/25.4))));
    html.replace("REPORT_TILE_VALUE_GAP",QString::number(qMax(1,qRound(5.0*logicalDpiX()/25.4))));
        html.replace("REPORT_STOCK_GAP",QString::number(qMax(1,qRound(7.0*logicalDpiX()/25.4))));
        html.replace("REPORT_RULE_GAP",QString::number(qMax(1,qRound(0.6*logicalDpiY()/25.4))));
        html.replace("REPORT_RULE_COLOR",textColor);
        m_oilStatsLabel->setText(html);
        m_oilStatsLabel->setVisible(!html.trimmed().isEmpty());
        if(parts&&!ReportDashboard::supports(static_cast<int>(tab)))fitPartsSummary(this);
    }
    // Commit header and tab geometry together when returning from the compact summary.
    if(layout())layout()->activate();
    updateDashboard();
}

void ReportsPage::updateDashboard()
{
    if(!m_dashboard)return;
    const int tab=static_cast<int>(activeTab());
    const bool enabled=ReportDashboard::supports(tab);
    m_dashboard->setVisible(enabled);
    if(auto* scroll=findChild<QScrollArea*>("reportsHeaderScroll"))scroll->setVisible(!enabled);
    m_invoicesPage->controlsHost()->hide();
    if(!enabled)return;
    m_dashboard->configure(tab,ThemeManager::currentTheme()=="light");
    if(tab==3)m_dashboard->parts(m_partsTileTitles,m_partsTileBodies);
    m_dashboard->status(m_orderStatusFilter);
    {
        QSignalBlocker blocker(m_dashboard->search);
        auto* source=tab==2?m_invoicesPage->searchField():m_searchEdit;
        m_dashboard->search->setText(source->text());
        m_dashboard->search->setPlaceholderText(source->placeholderText());
    }
    QStringList values;
    // The existing stats are presentation strings; retain their formatted values.
    for(const auto& line:m_tabStats.value(tab)){
        const int colon=line.indexOf(':');values.append(colon>=0?line.mid(colon+1).trimmed():line);
    }
    if(tab==2)values=m_invoiceMetrics;
    if(tab==6)values=m_totalExpenseMetrics;
    if(tab==7)values.append(m_from->date().toString("dd.MM.yy")+QString::fromUtf8(" – ")+m_to->date().toString("dd.MM.yy"));
    if(tab==5)values={m_expensesTotalEdit->text()+QString::fromUtf8(" грн"),m_elecCostEdit->text()+QString::fromUtf8(" грн"),
        m_expensesFound?QString::fromUtf8("Витрати внесені"):QString::fromUtf8("Витрати не внесені\nПеревірте записи та обрані дати")};
    m_dashboard->values(values);
    if(auto* top=findChild<QWidget*>("reportsTopHost")){
        top->setFixedHeight(m_dashboard->compactHeight());
    }
}

void ReportsPage::applyPeriodSnapshot(const PeriodSnapshot& snapshot)
{
    ReportsTableSupport::applyPeriodSnapshot(m_periodTable, m_periodModel, snapshot);
    setTabStats(ReportTab::Period, snapshot.stats);
}

void ReportsPage::refreshPeriod()
{
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Period), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::Period, QString::fromUtf8("Звіт за період — табличний перегляд по поточному діапазону дат."));
    setTabAuxStats(ReportTab::Period, QString());
    updateStats({QString::fromUtf8("Завантаження звіту за період…")});
    ReportsAsyncSupport::showLoadingRow(m_periodTable, m_periodModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startPeriodLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const PeriodSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Period) return;
        applyPeriodSnapshot(snapshot);
        m_tabKeys[static_cast<int>(ReportTab::Period)] = key;
        applyCachedTabUi(ReportTab::Period);
    });
}

void ReportsPage::applyOrdersSnapshot(const OrdersSnapshot& snapshot)
{
    auto visible=snapshot;
    if(m_orderStatusFilter!=0)visible.rows.removeIf([this](const OrdersSnapshotRow& row){return m_orderStatusFilter==1?row.closed:!row.closed;});
    ReportsTableSupport::applyOrdersSnapshot(m_ordersTable, m_ordersModel, visible);
    QList<qint64> receipts;
    for(const auto& row:visible.rows)receipts.append(row.clientReceivedCents);
    const auto received=ClientReceiptReader::total(receipts);
    const QString error=snapshot.clientPaymentError.isEmpty()?received.error:snapshot.clientPaymentError;
    if(m_dashboard)m_dashboard->clientReceived(error.isEmpty()
        ? ClientReceiptReader::money(received.cents)
        : QStringLiteral("Недоступно"));
    auto stats=snapshot.stats;
    if(!error.isEmpty())stats.append(error);
    setTabStats(ReportTab::Orders, stats);
}

void ReportsPage::refreshOrders()
{
    if(m_dashboard)m_dashboard->clientReceived(QString::fromUtf8("Завантаження…"));
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString filter = m_searchEdit->text().trimmed();
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Orders), m_from->date(), m_to->date()) + QStringLiteral("|search:") + filter;
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::Orders,
                      QString::fromUtf8("Наряди — подвійний клік відкриває прев’ю з оплатою. Кнопка PDF відкриває документ вибраного наряду."));
    setTabAuxStats(ReportTab::Orders, QString());
    updateStats({QString::fromUtf8("Завантаження нарядів…")});
    ReportsAsyncSupport::showLoadingRow(m_ordersTable, m_ordersModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startOrdersLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const OrdersSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Orders) return;
        applyOrdersSnapshot(snapshot);
        m_tabKeys[static_cast<int>(ReportTab::Orders)] = key;
        applyCachedTabUi(ReportTab::Orders);
    }, filter);
}

void ReportsPage::applyPartsSnapshot(const PartsSnapshot& snapshot)
{
    m_partsTileTitles=snapshot.tileTitles;m_partsTileBodies=snapshot.tileBodies;
    ReportsTableSupport::applyPartsSnapshot(m_partsTable, m_partsModel, snapshot);
    setTabAuxStats(ReportTab::Parts, snapshot.oilHtml);
    setTabStats(ReportTab::Parts, snapshot.stats);
}

void ReportsPage::refreshParts()
{
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Parts), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);
    // Historical global default markups remain visible in Settings UI, but reports must
    // ignore them and follow the same store-only policy as the order editor.
    const double defaultParts = 0.0;
    const double defaultOil = 0.0;
    const QMap<QString,double> partsMap = ReportsPdfSupport::loadMarkupMapForReports(QStringLiteral("settings.fin.store_markups"));
    const QMap<QString,double> oilMap = ReportsPdfSupport::loadMarkupMapForReports(QStringLiteral("settings.fin.oil_store_markups"));

    setTabDescription(ReportTab::Parts,
                      QString::fromUtf8("Придбання запчастин — автосортування за датою та номером наряду (новіші зверху)."));
    updateStats({QString::fromUtf8("Завантаження запчастин…")});
    setTabAuxStats(ReportTab::Parts, QString());
    ReportsAsyncSupport::showLoadingRow(m_partsTable, m_partsModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startPartsLoad(this, asyncHandle, dbPath, df, dt, defaultParts, defaultOil, partsMap, oilMap, [this, generation, key](const PartsSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Parts) return;
        applyPartsSnapshot(snapshot);
        m_tabKeys[static_cast<int>(ReportTab::Parts)] = key;
        applyCachedTabUi(ReportTab::Parts);
    });
}

void ReportsPage::applyOutsourceSnapshot(const OutsourceSnapshot& snapshot)
{
    ReportsTableSupport::applyOutsourceSnapshot(m_outsourceTable, m_outsourceModel, snapshot);
    setTabStats(ReportTab::Outsource, snapshot.stats);
}

void ReportsPage::refreshOutsource()
{
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Outsource), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::Outsource, QString::fromUtf8("Аутсорс — перегляд відправлених на ремонт запчастин по поточному діапазону дат."));
    setTabAuxStats(ReportTab::Outsource, QString());
    updateStats({QString::fromUtf8("Завантаження аутсорсу…")});
    ReportsAsyncSupport::showLoadingRow(m_outsourceTable, m_outsourceModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startOutsourceLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const OutsourceSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Outsource) return;
        applyOutsourceSnapshot(snapshot);
        if (snapshot.error.isEmpty()) m_tabKeys[static_cast<int>(ReportTab::Outsource)] = key;
        else m_tabKeys.remove(static_cast<int>(ReportTab::Outsource));
        applyCachedTabUi(ReportTab::Outsource);
    });
}

void ReportsPage::applyPayrollSnapshot(const PayrollSnapshot& snapshot)
{
    ReportsTableSupport::applyPayrollSnapshot(m_payrollTable, m_payrollModel, snapshot);
    setTabStats(ReportTab::Payroll, snapshot.stats);
}

void ReportsPage::refreshExpenses()
{
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::Expenses), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::Expenses, QString::fromUtf8("Витрати — введення та збереження витрат за вибраний період."));
    setTabAuxStats(ReportTab::Expenses, QString());
    updateStats({QString::fromUtf8("Завантаження витрат…")});
    for (QLineEdit* e : {m_elecPrevEdit,m_elecCurrEdit,m_elecTariffEdit,m_cleaningEdit,m_cleaningNoteEdit,m_softwareEdit,m_softwareNoteEdit,m_heatingEdit,m_heatingNoteEdit,m_amortizationEdit,m_amortizationNoteEdit,m_rentEdit,m_rentNoteEdit,m_otherEdit,m_otherNoteEdit}) {
        if (e) e->setEnabled(false);
    }
    if (m_expensesSaveButton) m_expensesSaveButton->setEnabled(false);

    ReportsAsyncSupport::startExpensesLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const ExpensesSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::Expenses) return;
        applyExpensesSnapshot(snapshot);
        if (snapshot.error.isEmpty()) m_tabKeys[static_cast<int>(ReportTab::Expenses)] = key;
        else m_tabKeys.remove(static_cast<int>(ReportTab::Expenses));
        applyCachedTabUi(ReportTab::Expenses);
    });
}

void ReportsPage::applyExpensesSnapshot(const ExpensesSnapshot& snapshot)
{
    m_expensesFound=snapshot.foundRow;
    if (!m_expensesPage) return;
    m_expensesPage->setEnabled(snapshot.error.isEmpty());
    if (!snapshot.error.isEmpty()) {
        for (auto* field : m_expensesPage->findChildren<QLineEdit*>()) {
            const QSignalBlocker blocker(field);
            field->clear();
        }
        setTabStats(ReportTab::Expenses, {QStringLiteral("Не вдалося прочитати витрати: ") + snapshot.error});
        return;
    }
    ReportsExpensesSupport::applySnapshot(snapshot, {
        m_expensesPage, m_elecPrevEdit, m_elecCurrEdit, m_elecTariffEdit, m_elecKwhEdit, m_elecCostEdit,
        m_cleaningEdit, m_cleaningNoteEdit, m_softwareEdit, m_softwareNoteEdit,
        m_heatingEdit, m_heatingNoteEdit, m_amortizationEdit, m_amortizationNoteEdit,
        m_rentEdit, m_rentNoteEdit, m_otherEdit, m_otherNoteEdit, m_expensesTotalEdit,
        m_expensesSaveButton, m_electricityPdfButton
    });
    recalcExpensesForm();
}

void ReportsPage::applyTotalExpensesSnapshot(const TotalExpensesSnapshot& snapshot)
{
    m_totalExpenseMetrics = snapshot.error.isEmpty() ? QStringList{
        FormatUtils::money((snapshot.periodicCents + snapshot.additionalCents) / 100.0, 2) + QString::fromUtf8(" грн"),
        QString::number(snapshot.rows.size()),
        FormatUtils::money(snapshot.novaPoshtaCents / 100.0, 2) + QString::fromUtf8(" грн")
    } : QStringList{};
    ReportsTableSupport::applyTotalExpensesSnapshot(m_totalExpensesTable, m_totalExpensesModel, snapshot);
    setTabStats(ReportTab::TotalExpenses, snapshot.stats);
}

void ReportsPage::refreshTotalExpenses()
{
    updateExpenseCategories();
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");
    const QString key = ReportsAsyncSupport::buildTabKey(static_cast<int>(ReportTab::TotalExpenses), m_from->date(), m_to->date());
    const QString dbPath = ReportsDataLoader::currentDatabasePath();
    const quint64 generation = ++m_asyncRefreshGeneration;
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken);
    const AsyncTaskHandle asyncHandle(this, generation, cancelToken);

    setTabDescription(ReportTab::TotalExpenses,
                      QString::fromUtf8("Періодичні та додаткові витрати у вибраному діапазоні дат."));
    setTabAuxStats(ReportTab::TotalExpenses, QString());
    updateStats({QString::fromUtf8("Завантаження сукупних витрат…")});
    ReportsAsyncSupport::showLoadingRow(m_totalExpensesTable, m_totalExpensesModel, QString::fromUtf8("Завантаження…"));

    ReportsAsyncSupport::startTotalExpensesLoad(this, asyncHandle, dbPath, df, dt, [this, generation, key](const TotalExpensesSnapshot& snapshot) {
        if (generation != m_asyncRefreshGeneration) return;
        if (activeTab() != ReportTab::TotalExpenses) return;
        applyTotalExpensesSnapshot(snapshot);
        if (snapshot.error.isEmpty()) m_tabKeys[static_cast<int>(ReportTab::TotalExpenses)] = key;
        else m_tabKeys.remove(static_cast<int>(ReportTab::TotalExpenses));
        applyCachedTabUi(ReportTab::TotalExpenses);
    });
}

void ReportsPage::loadExpensesForm()
{
    refreshExpenses();
}

void ReportsPage::recalcExpensesForm()
{
    const auto result = ReportsExpensesSupport::recalcForm({
        m_expensesPage, m_elecPrevEdit, m_elecCurrEdit, m_elecTariffEdit, m_elecKwhEdit, m_elecCostEdit,
        m_cleaningEdit, m_cleaningNoteEdit, m_softwareEdit, m_softwareNoteEdit,
        m_heatingEdit, m_heatingNoteEdit, m_amortizationEdit, m_amortizationNoteEdit,
        m_rentEdit, m_rentNoteEdit, m_otherEdit, m_otherNoteEdit, m_expensesTotalEdit,
        m_expensesSaveButton, m_electricityPdfButton
    }, sender() == m_elecTariffEdit);

    QStringList stats = {
        QString::fromUtf8("Період:  %1 — %2").arg(m_from->date().toString("dd.MM.yy"), m_to->date().toString("dd.MM.yy"))
    };
    stats += result.stats;
    setTabStats(ReportTab::Expenses, stats);
}

void ReportsPage::saveExpenses()
{
    recalcExpensesForm();
    const QString df = m_from->date().toString("yyyy-MM-dd");
    const QString dt = m_to->date().toString("yyyy-MM-dd");

    auto payload = ReportsExpensesSupport::buildSavePayload({
        m_expensesPage, m_elecPrevEdit, m_elecCurrEdit, m_elecTariffEdit, m_elecKwhEdit, m_elecCostEdit,
        m_cleaningEdit, m_cleaningNoteEdit, m_softwareEdit, m_softwareNoteEdit,
        m_heatingEdit, m_heatingNoteEdit, m_amortizationEdit, m_amortizationNoteEdit,
        m_rentEdit, m_rentNoteEdit, m_otherEdit, m_otherNoteEdit, m_expensesTotalEdit,
        m_expensesSaveButton, m_electricityPdfButton
    });

    const auto existing = ReportsDataLoader::buildExpensesSnapshot(df, dt);
    if (!existing.error.isEmpty()) {
        applyExpensesSnapshot(existing);
        m_tabKeys.remove(static_cast<int>(ReportTab::Expenses));
        applyCachedTabUi(ReportTab::Expenses);
        return;
    }
    const double persistedTariffSetting = ReportsSettingsService::loadElectricityTariff(0.0);
    const auto nearlyEqual = [](double a, double b) {
        return qAbs(a - b) <= 0.0001;
    };
    const bool expensesChanged = !existing.foundRow
        || !nearlyEqual(existing.elecPrev, payload.elecPrev)
        || !nearlyEqual(existing.elecCurr, payload.elecCurr)
        || !nearlyEqual(existing.elecTariff, payload.elecTariff)
        || !nearlyEqual(existing.cleaning, payload.cleaning)
        || existing.cleaningNote != payload.cleaningNote
        || !nearlyEqual(existing.software, payload.software)
        || existing.softwareNote != payload.softwareNote
        || !nearlyEqual(existing.heating, payload.heating)
        || existing.heatingNote != payload.heatingNote
        || !nearlyEqual(existing.amortization, payload.amortization)
        || existing.amortizationNote != payload.amortizationNote
        || !nearlyEqual(existing.rent, payload.rent)
        || existing.rentNote != payload.rentNote
        || !nearlyEqual(existing.other, payload.other)
        || existing.otherNote != payload.otherNote
        || (payload.elecTariff > 0.0 && !nearlyEqual(persistedTariffSetting, payload.elecTariff));

    QString errorText;
    if (!ReportsSyncSupport::saveExpensesRecord(df, dt, payload, &errorText)) {
        QMessageBox::warning(this, QString::fromUtf8("Витрати"), errorText);
        return;
    }
    if (payload.elecTariff > 0.0 && !nearlyEqual(persistedTariffSetting, payload.elecTariff)
        && !ReportsSettingsService::saveElectricityTariff(payload.elecTariff)) {
        QMessageBox::warning(this, QStringLiteral("Витрати"),
                             QStringLiteral("Витрати збережено. Не вдалося оновити тариф за замовчуванням."));
    }

    if (expensesChanged) {
        ReportsInteractionSupport::publishExpensesBackupEvent(df, dt, payload.elecCost + payload.cleaning + payload.software + payload.heating + payload.amortization + payload.rent + payload.other, payload.elecTariff);
    }

    QMessageBox::information(this, QString::fromUtf8("Витрати"), QString::fromUtf8("Дані витрат збережено"));
    markDirty();
    refresh();
    queueRefreshAll(0);
}
