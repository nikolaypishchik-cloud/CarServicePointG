#include "core/services/StaffAccountService.h"
#include "ui/tabs/inventory/SpareReservePresentation.h"
#include "ui/tabs/inventory/SpareUnpaidDaysDelegate.h"
#include "ui/tabs/inventory/InventoryDetailsDialog.h"
#include "ui/tabs/inventory/InventoryEntryDialog.h"
#include "ui/common/dialogs/InvoiceReviewDialog.h"
#include "ui/common/widgets/InvoiceImportProgressDialog.h"
#include "ui/tabs/inventory/WorkshopInventoryPage.h"
#include "ui/common/filters/ClearOnEntryFilter.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/InvoiceDialogSupport.h"
#include "ui/common/dialogs/InvoiceColumnMappingEditor.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"
#include "core/import/ImportJob.h"
#include "core/import/InvoiceCatalogIdentity.h"
#include "core/util/DateFormats.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"
#include "core/services/catalog/CatalogQueryService.h"
#include <QTableWidget>
#include <QCompleter>
#include <QSet>
#include <functional>

#include "core/readmodel/TabularReadModel.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/InventoryStockSummary.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/AccessControlService.h"
#include "core/services/settings/UiStateSettingsService.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "core/util/FormatUtils.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "ui/common/AdaptiveUi.h"
#include "ui/common/TabularSnapshotTableModel.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "core/services/PartsQueryService.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDateEdit>
#include "core/services/InventoryPriceInput.h"
#include "ui/common/OptionalDatePopup.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QPainter>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QStyle>
#include <cmath>

namespace {




QString quantityText(double value)
{
    return FormatUtils::smartQty(value, 3);
}

QString moneyText(double value)
{
    return FormatUtils::money(value, 2);
}

QTableView* configureTable(QWidget* parent, TabularSnapshotTableModel*& model)
{
    auto* table = new QTableView(parent);
    model = new TabularSnapshotTableModel(table);
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(31);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setHighlightSections(false);
    table->setStyleSheet(QStringLiteral(
        "QTableView { background: rgba(255,255,255,0.96); alternate-background-color: #edf3fa; color: #121820; gridline-color: #d4dce6; selection-background-color: #dce9f7; selection-color: #111820; border: 1px solid #cbd4de; }"
        "QHeaderView::section { background: #e6edf5; color: #17202b; border: none; border-right: 1px solid #c4ced9; border-bottom: 1px solid #bac6d2; padding: 7px 8px; font-weight: 700; }"));
    return table;
}

QString cellText(const TabularReadModel& snapshot, int row, int column, const QString& fallback = QString::fromUtf8("—"))
{
    if (row < 0 || row >= snapshot.rows.size()) return fallback;
    const QVector<QVariant>& values = snapshot.rows.at(row);
    if (column < 0 || column >= values.size()) return fallback;
    const QString value = values.at(column).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}



QWidget* makeTableTab(QWidget* parent, QTableView*& table, TabularSnapshotTableModel*& model)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("workshopInventoryTableTab"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(0);
    table = configureTable(page, model);
    layout->addWidget(table, 1);
    return page;
}

void rememberColumnWidths(QTableView* table, const QString& key)
{
    const auto apply = [table, key] {
        table->setProperty("restoringStockWidths",true);
        auto* header = table->horizontalHeader();
        header->setStretchLastSection(false);
        header->setSectionResizeMode(QHeaderView::Interactive);
        const auto widths=QJsonDocument::fromJson(UiStateSettingsService::loadString(key).toUtf8()).object();
        for(int column=0;column<header->count();++column) {
            const auto title=table->model()->headerData(column,Qt::Horizontal).toString();
            const int fallback=qBound(90,header->fontMetrics().horizontalAdvance(title)+32,330);
            header->resizeSection(column,qBound(header->minimumSectionSize(),widths.value(title).toInt(fallback),header->maximumSectionSize()));
        }
        table->setProperty("restoringStockWidths",false);
    };
    QObject::connect(table->model(), &QAbstractItemModel::modelAboutToBeReset, table, [table] {table->setProperty("restoringStockWidths",true);});
    QObject::connect(table->model(), &QAbstractItemModel::modelReset, table, apply);
    QObject::connect(table->horizontalHeader(),&QHeaderView::sectionResized,table,[table,key](int column,int,int width) {
        if(table->property("restoringStockWidths").toBool() || width<=0 || table->isColumnHidden(column)) return;
        auto widths=QJsonDocument::fromJson(UiStateSettingsService::loadString(key).toUtf8()).object();
        widths.insert(table->model()->headerData(column,Qt::Horizontal).toString(),width);
        UiStateSettingsService::saveString(key,QString::fromUtf8(QJsonDocument(widths).toJson(QJsonDocument::Compact)));
    });
    apply();
}

} // namespace

WorkshopInventoryPage::WorkshopInventoryPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void WorkshopInventoryPage::buildUi()
{
    setObjectName(QStringLiteral("workshopInventoryPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(R"(
        #workshopInventoryPage { background: #2b3d4f; border: none; }
        #workshopInventoryPage QWidget#workshopInventoryTopBar,
        #workshopInventoryPage QWidget#workshopInventoryTableTab,
        #workshopInventoryPage QWidget#workshopInventoryPurchasesTab,
        #workshopInventoryPage QWidget#workshopInventoryPurchaseForm,
        #workshopInventoryPage QTabWidget#workshopInventoryTabs,
        #workshopInventoryPage QTabWidget#workshopInventoryTabs::pane,
        #workshopInventoryPage QStackedWidget {
            background: #2b3d4f; border: none;
        }
        #workshopInventoryPage QTabBar#workshopInventoryTabBar { background: #2b3d4f; }
        #workshopInventoryPage QLabel { color: #f4f7fb; }
        #workshopInventoryPage QDateEdit, #workshopInventoryPage QComboBox,
        #workshopInventoryPage QLineEdit, #workshopInventoryPage QDoubleSpinBox {
            background: #ffffff; color: #111820; border: 1px solid #c6d0db; border-radius: 5px; padding: 4px 7px;
        }
        #workshopInventoryPage QTabWidget::pane { border: none; background: #2b3d4f; }
        #workshopInventoryPage QTabBar::tab { color: #f5f7fa; padding: 9px 16px; }
        #workshopInventoryPage QTabBar::tab:selected { color: #111820; font-weight: 700; background: rgba(245,250,255,0.84); border: 1px solid rgba(255,255,255,0.5); }
        #workshopInventoryPage QPushButton { color:white; border:1px solid #adbbc6; border-radius:7px; padding:8px 16px;
            background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8c9ba8,stop:0.48 #354653,stop:1 #202b34); }
        #workshopInventoryPage QPushButton#inventoryAddMaterial, #workshopInventoryPage QPushButton#inventoryAddSpare {
            background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #52a57d,stop:0.5 #18734d,stop:1 #095438); }
        #workshopInventoryPage QTabWidget#workshopStockKinds QTabBar::tab:selected { background:#f4a600;color:#172630; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* top = new QWidget(this);
    top->setObjectName(QStringLiteral("workshopInventoryTopBar"));
    top->setAttribute(Qt::WA_StyledBackground, true);
    auto* topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 4, 0, 4);
    topLayout->setSpacing(4);
    auto makeTile = [top](const QString& title, int kind) {
        auto* tile = new QWidget(top);
        tile->setProperty("inventoryTile", true);
        tile->setAttribute(Qt::WA_StyledBackground);
        tile->setStyleSheet("QWidget[inventoryTile=\"true\"] {border:1px solid black;border-radius:4px;background:transparent;} QLabel {border:none;background:transparent;}");
        auto* layout = new QVBoxLayout(tile);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(4);
        auto* heading = new QHBoxLayout;
        QPixmap pixmap(QSize(28, 28) * top->devicePixelRatioF());
        pixmap.setDevicePixelRatio(top->devicePixelRatioF());
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#67dcea"), 1.7));
        if (kind == 0) {
            p.drawRoundedRect(3, 5, 22, 21, 2, 2); p.drawLine(3, 11, 25, 11);
            p.drawLine(8, 1, 8, 8); p.drawLine(20, 1, 20, 8);
            for (int y : {16, 21}) for (int x : {8, 14, 20}) p.drawPoint(x, y);
        } else if (kind == 1) {
            p.drawPolygon(QPolygonF{{3,8},{14,2},{25,8},{25,21},{14,27},{3,21}});
            p.drawLine(3,8,14,14);p.drawLine(25,8,14,14);p.drawLine(14,14,14,27);
        } else if (kind == 2 || kind == 3) {
            p.drawPolyline(QPolygonF{{3,18},{3,25},{25,25},{25,18}});
            p.drawLine(14,2,14,20);
            const int tip = kind == 2 ? 20 : 2, base = kind == 2 ? 13 : 9;
            p.drawPolyline(QPolygonF{{7,qreal(base)},{14,qreal(tip)},{21,qreal(base)}});
        } else if (kind == 4) {
            for (int y : {3,10,17}) {p.drawEllipse(QRectF(3,y,22,8));p.drawLine(3,y+4,3,y+8);p.drawLine(25,y+4,25,y+8);}
        } else if (kind == 6) {
            p.drawPolyline(QPolygonF{{5,2},{18,2},{24,8},{24,26},{5,26},{5,2}});
            p.drawPolyline(QPolygonF{{18,2},{18,8},{24,8}});
            for (int y : {13,18,23}) p.drawLine(9,y,20,y);
        } else if (kind == 7) {
            p.drawRect(2,5,15,16);p.drawPolyline(QPolygonF{{17,10},{23,10},{27,16},{27,21},{17,21}});
            p.drawEllipse(QRectF(5,19,6,6));p.drawEllipse(QRectF(20,19,6,6));
        } else if (kind == 8) {
            p.drawEllipse(QRectF(2,2,24,24));p.drawLine(7,14,21,14);p.drawLine(14,7,14,21);
        } else {
            p.drawEllipse(QRectF(7,7,14,14));p.drawEllipse(QRectF(11,11,6,6));
            for(int i=0;i<8;++i){p.save();p.translate(14,14);p.rotate(i*45);p.drawLine(0,10,0,14);p.restore();}
        }
        p.end();
        auto* icon = new QLabel(tile); icon->setPixmap(pixmap); heading->addWidget(icon);
        auto* label = new QLabel(title, tile); label->setWordWrap(true);
        auto font = label->font();font.setBold(true);label->setFont(font);
        heading->addWidget(label,1);layout->addLayout(heading);
        return tile;
    };
    auto* periodTile = makeTile(QString::fromUtf8("ПЕРІОД"), 0);
    periodTile->setObjectName("inventoryPeriodTile");
    auto* dates = new QHBoxLayout;
    m_from = new QDateEdit(QDate::currentDate().addDays(-29), top);
    m_to = new QDateEdit(QDate::currentDate(), top);
    for (QDateEdit* editor : {m_from, m_to}) {
        editor->setDisplayFormat(QStringLiteral("dd.MM.yy"));
        editor->setCalendarPopup(true);
        editor->setKeyboardTracking(false);
        editor->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));
        editor->setMinimumWidth(editor->fontMetrics().horizontalAdvance("00.00.00") + 40);
        editor->setFixedHeight(qRound(7.0 * logicalDpiY() / 25.4));
        dates->addWidget(editor, 1);
    }
    m_summary = new QLabel(top);
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_summary->setWordWrap(true);
    static_cast<QVBoxLayout*>(periodTile->layout())->addLayout(dates);
    topLayout->addWidget(periodTile, 2);
    QList<QWidget*> materialTiles;
    const QStringList metricTitles = {QString::fromUtf8("ОЛИВИ"), QString::fromUtf8("АНТИФРИЗИ"), QString::fromUtf8("ІНШІ МАТЕРІАЛИ"), QString::fromUtf8("ВАРТІСТЬ ЗАЛИШКУ")};
    for (int i = 0; i < 4; ++i) {
        auto* tile = makeTile(metricTitles[i], i + 1);
        tile->setObjectName(QString("inventoryMetricTile%1").arg(i));
        tile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* value = new QLabel(tile);m_materialMetrics[i] = value;
        value->setObjectName(QString("inventoryMetricValue%1").arg(i));
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignCenter);
        auto font = value->font();font.setPointSize(18);font.setBold(true);value->setFont(font);
        static_cast<QVBoxLayout*>(tile->layout())->addWidget(value, 1);
        topLayout->addWidget(tile, 1);materialTiles.append(tile);
    }
    auto* actionTile = makeTile(QString::fromUtf8("ДІЇ"), 5);
    actionTile->setObjectName("inventoryActionsTile");
    auto* actionButtons = new QHBoxLayout;
    actionButtons->setSpacing(4);
    static_cast<QVBoxLayout*>(actionTile->layout())->addLayout(actionButtons);
    topLayout->addWidget(actionTile, 2);materialTiles.append(actionTile);
    QList<QWidget*> spareTiles;
    const QStringList spareTitles = {QString::fromUtf8("Замовлень"), QString::fromUtf8("Очікується"), QString::fromUtf8("Резерв"), QString::fromUtf8("Борг")};
    const int spareIcons[] = {6, 7, 1, 4};
    for (int i = 0; i < 4; ++i) {
        auto* tile = makeTile(spareTitles[i], spareIcons[i]);
        tile->setObjectName(QString("inventorySpareTile%1").arg(i));
        tile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* value = new QLabel(tile);
        m_spareMetrics[i] = value;
        value->setObjectName(QString("inventorySpareValue%1").arg(i));
        value->setAlignment(Qt::AlignCenter);
        auto font = value->font();font.setPointSize(18);font.setBold(true);value->setFont(font);
        static_cast<QVBoxLayout*>(tile->layout())->addWidget(value, 1);
        topLayout->addWidget(tile, 2);spareTiles.append(tile);
    }
    auto* spareActions = makeTile(QString::fromUtf8("Дії"), 8);
    spareActions->setObjectName("inventorySpareActionsTile");
    topLayout->addWidget(spareActions, 2);spareTiles.append(spareActions);
    topLayout->addWidget(m_summary);
    root->addWidget(top);

    m_stockTabs = new QTabWidget(this);
    m_stockTabs->setObjectName(QStringLiteral("workshopStockKinds"));
    m_stockTabs->setDocumentMode(true);
    auto* sparePage = makeTableTab(m_stockTabs, m_spareTable, m_spareModel);
    m_spareTable->setItemDelegate(new SpareUnpaidDaysDelegate(m_spareTable));
    m_spareTable->setObjectName("workshopSpareTable");
    m_stockTabs->addTab(sparePage, QString::fromUtf8("Запчастини під авто"));
    auto* materialsPage = new QWidget(m_stockTabs);
    materialsPage->setObjectName("workshopMaterialsRegistry");
    auto* materialsLayout = new QVBoxLayout(materialsPage);
    materialsLayout->setContentsMargins(0, 8, 0, 0);
    materialsLayout->addWidget(new QLabel(QString::fromUtf8("Журнал руху матеріалів"), materialsPage));
    materialsLayout->addWidget(makeTableTab(materialsPage, m_periodTable, m_periodModel), 1);
    auto* purchasesPage = new QDialog(this);
    purchasesPage->setWindowTitle(QString::fromUtf8("Баланс складу"));
    auto* balanceButton = new QPushButton(QString::fromUtf8("Баланс складу"), m_stockTabs);
    balanceButton->setObjectName("inventoryBalance");
    purchasesPage->setObjectName(QStringLiteral("workshopInventoryPurchasesTab"));
    purchasesPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* purchasesLayout = new QVBoxLayout(purchasesPage);
    purchasesLayout->setContentsMargins(0, 8, 0, 0);
    purchasesLayout->setSpacing(8);
    auto* balanceTabs = new QTabWidget(purchasesPage);
    balanceTabs->setObjectName("inventoryBalanceTabs");
    purchasesLayout->addWidget(balanceTabs);
    auto* acquisitions = new QWidget(balanceTabs);
    auto* acquisitionsLayout = new QVBoxLayout(acquisitions);
    auto* balances = new QWidget(balanceTabs);
    auto* balancesLayout = new QVBoxLayout(balances);
    m_balanceMaterial = new QComboBox(balances);
    m_balanceMaterial->setObjectName("inventoryBalanceMaterial");
    balancesLayout->addWidget(m_balanceMaterial);
    balancesLayout->addWidget(makeTableTab(balances, m_balanceTable, m_balanceModel), 1);
    m_balanceTable->setObjectName("inventoryBalances");
    balanceTabs->addTab(balances, QString::fromUtf8("Залишки"));
    balanceTabs->addTab(acquisitions, QString::fromUtf8("Придбання"));
    connect(balanceButton, &QPushButton::clicked, this, [this, purchasesPage, balanceTabs] {
        m_dirty = true;
        refresh();
        balanceTabs->setCurrentIndex(0);
        const QRect available = screen()->availableGeometry();
        purchasesPage->resize(qRound(available.width() * 0.64),
                              qRound(available.height() * 0.63));
        purchasesPage->move(available.center() - purchasesPage->rect().center());
        purchasesPage->exec();
    });
    connect(m_balanceMaterial, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_dirty = true;
        refresh();
    });
    m_materialDialog = new InventoryEntryDialog(this);
    m_materialDialog->setWindowTitle(QString::fromUtf8("Додати матеріал або рідину"));
    m_materialDialog->resize(780, 600);
    m_materialDialog->setStyleSheet(QStringLiteral(
        "QDialog{background:#242e38;} QLabel{color:#ffffff;} QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox{background:white;color:#000;border:1px solid #a7b1bb;border-radius:5px;padding:6px;}"
        "QWidget#workshopInventoryPurchaseForm,QScrollArea,QScrollArea>QWidget {background:#242e38;}"
        "QComboBox QAbstractItemView {background:white;color:black;selection-background-color:#dbe8fa;selection-color:black;}"
        "QPushButton {color:white;border-color:#84909d;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #45494d,stop:0.45 #141619,stop:0.5 #08090b,stop:1 black);}"
        "QPushButton:disabled {color:#8a9299;}"));
    auto* dialogLayout = new QVBoxLayout(m_materialDialog);
    auto* form = new QWidget(m_materialDialog);
    form->setObjectName(QStringLiteral("workshopInventoryPurchaseForm"));
    form->setAttribute(Qt::WA_StyledBackground, true);
    auto* formLayout = new QGridLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(8);
    formLayout->setVerticalSpacing(4);
    m_purchaseDate = new QDateEdit(QDate::currentDate(), form);
    m_purchaseDate->setDisplayFormat(QStringLiteral("dd.MM.yy"));
    m_purchaseDate->setCalendarPopup(true);
    m_purchaseDate->setFixedWidth(m_purchaseDate->fontMetrics().horizontalAdvance(QStringLiteral("88.88.88")) + 42);
    m_purchaseDate->setMinimumHeight(34);
    m_materialName = new QComboBox(form);
    m_materialName->setEditable(true);
    m_materialName->setInsertPolicy(QComboBox::NoInsert);
    m_materialName->setMinimumWidth(150);
    m_materialName->setPlaceholderText(QString::fromUtf8("Матеріал"));
    m_unit = new QComboBox(form);
    m_unit->setEditable(true);
    m_unit->addItems({QString::fromUtf8("л"), QString::fromUtf8("шт."), QString::fromUtf8("кг"), QString::fromUtf8("мл"), QString::fromUtf8("компл.")});
    m_quantity = new QDoubleSpinBox(form);
    m_quantity->setObjectName(QStringLiteral("inventoryQuantity"));
    m_quantity->setDecimals(3);
    m_quantity->setRange(0.001, 1000000.0);
    m_quantity->setValue(1.0);
    m_unitCost = new QDoubleSpinBox(form);
    m_unitCost->setObjectName(QStringLiteral("inventoryUnitCost"));
    m_unitCost->setDecimals(2);
    m_unitCost->setRange(0.0, 100000000.0);
    m_priceBasis=new QComboBox(form);
    m_priceBasis->setObjectName("inventoryPriceBasis");
    m_priceBasis->addItem(QString::fromUtf8("Ціни за всю вказану кількість"),true);
    m_priceBasis->addItem(QString::fromUtf8("Ціни за одну одиницю (мл, л, шт…)"),false);
    m_clientPrice = new QDoubleSpinBox(form);
    m_clientPrice->setObjectName(QStringLiteral("inventoryClientPrice"));
    m_clientPrice->setDecimals(2);
    m_clientPrice->setRange(-0.01, 100000000.0);
    m_clientPrice->setSpecialValueText(QString::fromUtf8("Не задано"));
    m_clientPrice->setValue(-0.01);
    for (auto* editor : {m_quantity, m_unitCost, m_clientPrice}) {
        editor->setFixedWidth(qMax(104, editor->fontMetrics().horizontalAdvance(QStringLiteral("99999,99")) + 44));
        editor->installEventFilter(this);
        if (auto* input = editor->findChild<QLineEdit*>()) input->installEventFilter(this);
    }
    m_unit->setFixedWidth(qMax(64, m_unit->fontMetrics().horizontalAdvance(QString::fromUtf8("компл.")) + 32));
    m_supplier = new QLineEdit(form);
    m_supplier->setPlaceholderText(QString::fromUtf8("Постачальник"));
    m_addPurchaseButton = new QPushButton(QString::fromUtf8("Додати матеріал"), m_stockTabs);
    m_addSpareButton = new QPushButton(QString::fromUtf8("Додати запчастину під авто"), m_stockTabs);
    m_addPurchaseButton->setObjectName("inventoryAddMaterial");m_addSpareButton->setObjectName("inventoryAddSpare");
    m_addSpareButton->setEnabled(AccessControlService::currentUserHasPermission( QStringLiteral("inventory.purchase")));
    m_deletePurchaseButton = new QPushButton(QString::fromUtf8("Видалити вибране"), purchasesPage);
    m_addPurchaseButton->setEnabled(AccessControlService::currentUserHasPermission( QStringLiteral("inventory.purchase")));
    m_deletePurchaseButton->setEnabled(AccessControlService::currentUserHasPermission( QStringLiteral("inventory.delete")));
    for (QWidget* field : {static_cast<QWidget*>(m_purchaseDate), static_cast<QWidget*>(m_materialName), static_cast<QWidget*>(m_unit),
                           static_cast<QWidget*>(m_quantity), static_cast<QWidget*>(m_unitCost), static_cast<QWidget*>(m_clientPrice), static_cast<QWidget*>(m_supplier),
                           static_cast<QWidget*>(m_addPurchaseButton), static_cast<QWidget*>(m_deletePurchaseButton)}) {
        field->setMinimumHeight(34);
    }
    const QStringList labels = {QString::fromUtf8("Дата"), QString::fromUtf8("Матеріал"), QString::fromUtf8("Од."),
                                QString::fromUtf8("Кількість"), QString::fromUtf8("Вартість придбання, грн"), QString::fromUtf8("Вартість клієнту, грн"), QString::fromUtf8("Постачальник")};
    const QList<QWidget*> fields = {m_purchaseDate,m_materialName,m_unit,m_quantity,m_unitCost,m_clientPrice,m_supplier};
    for (int index = 0; index < labels.size(); ++index) {
        const int row = index / 2 * 2, column = index % 2;
        formLayout->addWidget(new QLabel(index==0 ? QString::fromUtf8("Фактична дата поставки") : labels.at(index),form),row,column);
        fields.at(index)->setMaximumWidth(QWIDGETSIZE_MAX);
        formLayout->addWidget(fields.at(index),row+1,column);
    }
    m_category=new QLineEdit(form); m_category->setPlaceholderText(QString::fromUtf8("Олива / антифриз / допоміжні матеріали"));
    formLayout->addWidget(new QLabel(QString::fromUtf8("Як указані обидві ціни"),form),6,1);
    formLayout->addWidget(m_priceBasis,7,1);
    m_article=new QLineEdit(form); m_batch=new QLineEdit(form); m_document=new QLineEdit(form);
    m_minimum=new QDoubleSpinBox(form); m_minimum->setDecimals(3); m_minimum->setRange(0,1000000);
    m_orderedDate=new QDateEdit(QDate::currentDate(),form); m_dueDate=new QDateEdit(QDate(1900,1,1),form);
    for(auto* date:{m_orderedDate,m_dueDate}) {date->setCalendarPopup(true);date->setDisplayFormat("dd.MM.yyyy");}
    m_dueDate->setSpecialValueText(QString::fromUtf8("Не задано"));
    m_dueDate->setMinimumDate(QDate(1900,1,1));
    m_expiryDate=new QDateEdit(QDate(1900,1,1),form);
    m_expiryDate->setMinimumDate(QDate(1900,1,1));m_expiryDate->setSpecialValueText(QString::fromUtf8("Не задано"));
    m_expiryDate->setCalendarPopup(true);m_expiryDate->setDisplayFormat("dd.MM.yyyy");
    new OptionalDatePopup(m_dueDate);
    new OptionalDatePopup(m_expiryDate);
    const QList<QPair<QString,QWidget*>> extras={{QString::fromUtf8("Категорія"),m_category},{QString::fromUtf8("Артикул"),m_article},
        {QString::fromUtf8("Партія"),m_batch},{QString::fromUtf8("Документ"),m_document},
        {QString::fromUtf8("Мінімальний залишок"),m_minimum},{QString::fromUtf8("Дата замовлення"),m_orderedDate},{QString::fromUtf8("Сплатити до"),m_dueDate},
        {QString::fromUtf8("Придатний до"),m_expiryDate}};
    for(int index=0;index<extras.size();++index) {const int row=8+index/2*2,column=index%2;
        formLayout->addWidget(new QLabel(extras.at(index).first,form),row,column); formLayout->addWidget(extras.at(index).second,row+1,column);}
    auto* total=new QLabel(form);
    auto updateTotal=[this,total] {
        const bool whole=m_priceBasis->currentData().toBool();
        const double rate=InventoryPriceInput::unitPrice(m_unitCost->value(),m_quantity->value(),whole);
        total->setText(QString::fromUtf8("Сума надходження: %1 грн · ціни %2")
            .arg(moneyText(qMax(0.0,rate)*m_quantity->value()),whole?QString::fromUtf8("за всю кількість"):QString::fromUtf8("за 1 %1").arg(m_unit->currentText())));
    };
    connect(m_priceBasis,qOverload<int>(&QComboBox::currentIndexChanged),this,[updateTotal](int){updateTotal();});
    connect(m_unit,&QComboBox::currentTextChanged,this,[updateTotal](const QString&){updateTotal();});
    connect(m_quantity,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updateTotal](double){updateTotal();});
    connect(m_unitCost,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updateTotal](double){updateTotal();}); updateTotal();
    auto* materialScroll=new QScrollArea(m_materialDialog);materialScroll->setWidgetResizable(true);materialScroll->setWidget(form);
    dialogLayout->addWidget(materialScroll); dialogLayout->addWidget(total);
    auto* materialButtons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,m_materialDialog);
    materialButtons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Зберегти надходження"));
    materialButtons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Скасувати"));
    for (auto* button : materialButtons->findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    connect(materialButtons,&QDialogButtonBox::accepted,this,&WorkshopInventoryPage::addPurchase);
    connect(materialButtons,&QDialogButtonBox::rejected,m_materialDialog,&QDialog::reject); dialogLayout->addWidget(materialButtons);
    auto* purchaseTools=new QHBoxLayout; purchaseTools->addStretch();purchaseTools->addWidget(m_deletePurchaseButton);acquisitionsLayout->addLayout(purchaseTools);
    m_purchasesTable = configureTable(purchasesPage, m_purchasesModel);
    acquisitionsLayout->addWidget(m_purchasesTable, 1);

    auto* expenseDetails = makeTableTab(this, m_expensesTable, m_expensesModel);
    expenseDetails->hide();
    m_stockTabs->addTab(materialsPage,QString::fromUtf8("Матеріали та рідини"));
    m_addSpareButton->setFixedHeight(qRound(7.0 * logicalDpiY() / 25.4));
    m_addSpareButton->setStyleSheet("QPushButton {padding:0px 6px;}");
    static_cast<QVBoxLayout*>(spareActions->layout())->addWidget(m_addSpareButton);
    m_addPurchaseButton->setText(QString::fromUtf8("+ Додати матеріал"));
    for (auto* button : {balanceButton, m_addPurchaseButton}) {
        button->setFixedHeight(qRound(7.0 * logicalDpiY() / 25.4));
        button->setStyleSheet("QPushButton {padding:0px 6px;}");
        actionButtons->addWidget(button, 1);
    }
    connect(m_stockTabs,&QTabWidget::currentChanged,this,[this,balanceButton,materialTiles,spareTiles](int index){
        for (auto* tile : materialTiles) tile->setVisible(index == 1);
        for (auto* tile : spareTiles) tile->setVisible(index == 0);
        m_summary->hide();
        balanceButton->setVisible(index==1);
        m_addSpareButton->setVisible(index==0);m_addPurchaseButton->setVisible(index==1);
        if (m_refreshDebounce) {markDirty();scheduleRefresh();}
    });
    m_stockTabs->setCurrentIndex(1);m_addSpareButton->hide();
    m_spareTable->setToolTip(QString::fromUtf8("Подвійний клік: приймання, видача у наряд або оплата поставки."));
    root->addWidget(m_stockTabs, 1);

    rememberColumnWidths(m_spareTable,"ui.workshop_inventory.spares.widths");
    rememberColumnWidths(m_periodTable,"ui.workshop_inventory.materials.widths");
    rememberColumnWidths(m_purchasesTable,"ui.workshop_inventory.purchases.widths");
    m_purchasesTable->setColumnHidden(0, true);
    rememberColumnWidths(m_expensesTable,"ui.workshop_inventory.expenses.widths");
    rememberColumnWidths(m_balanceTable,"ui.workshop_inventory.balance.widths");

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    connect(m_refreshDebounce, &QTimer::timeout, this, &WorkshopInventoryPage::refresh);
    connect(m_from, &QDateEdit::dateChanged, this, [this](const QDate&) { markDirty(); scheduleRefresh(90); });
    connect(m_to, &QDateEdit::dateChanged, this, [this](const QDate&) { markDirty(); scheduleRefresh(90); });
    m_periodTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває деталі складської операції."));
    m_purchasesTable->setToolTip(QString::fromUtf8("Подвійний клік по ціні клієнту змінює її; по інших колонках відкриває оплату придбання."));
    m_expensesTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває наряд, у якому витрачено матеріал."));
    m_balanceTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває деталі балансу матеріалу."));
    connect(m_periodTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) { showPeriodDetails(index.row()); });
    connect(m_purchasesTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.column()!=6) { showPurchaseDetails(index.row()); return; }
        if (!AccessControlService::currentUserHasPermission(QStringLiteral("inventory.purchase"))) return;
        const auto& row=m_purchasesModel->snapshot().rows.at(index.row());
        bool accepted=false;
        const double price=QInputDialog::getDouble(this,QString::fromUtf8("Ціна клієнту за одиницю"),
            QString::fromUtf8("%1 · ціна за 1 %2, грн").arg(row.at(2).toString(),row.at(3).toString()),
            PartImportTextSupport::parseLooseNumber(row.at(6).toString()),0,100000000,6,&accepted);
        if (!accepted) return;
        QString error;
        if (!WorkshopInventoryService::setClientPrice(row.at(0).toLongLong(),price,&error)) {
            QMessageBox::warning(this,QString::fromUtf8("Ціна клієнту"),error); return;
        }
        markDirty(); refresh(); emit requestRefreshAll();
    });
    connect(m_expensesTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) { openExpenseOrder(index.row()); });
    connect(m_balanceTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) { showBalanceDetails(index.row()); });
    connect(m_addPurchaseButton, &QPushButton::clicked, m_materialDialog, &QDialog::exec);
    connect(m_addSpareButton, &QPushButton::clicked, this, &WorkshopInventoryPage::addSpareOrder);
    connect(m_spareTable,&QTableView::doubleClicked,this,[this](const QModelIndex& index){manageSpare(index.row());});
    connect(m_deletePurchaseButton, &QPushButton::clicked, this, &WorkshopInventoryPage::deleteSelectedPurchase);
    auto installDelete = [this](QTableView* table, std::function<void(int)> remove) {
        auto* action = new QAction(QString::fromUtf8("Видалити"), table);
        action->setObjectName("contextRowDeleteAction");
        action->setShortcut(QKeySequence::Delete);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        table->addAction(action);
        connect(action, &QAction::triggered, this, [this, table, remove] {
            if (!AccessControlService::currentUserHasPermission( "inventory.delete")) return;
            const auto rows = table->selectionModel()->selectedRows();
            if (rows.size() == 1) remove(rows.first().row());
        });
        auto update = [table, action] {
            action->setEnabled(table->selectionModel()->selectedRows().size() == 1 &&
                AccessControlService::currentUserHasPermission( "inventory.delete"));
        };
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, update);
        update();
    };
    installDelete(m_spareTable, [this](int row) { manageSpare(row, true); });
    installDelete(m_purchasesTable, [this](int) { deleteSelectedPurchase(); });
    installDelete(m_periodTable, [this](int row) {
        const auto& rows = m_periodModel->snapshot().rows;
        if (row >= 0 && row < rows.size()) deletePurchaseById(rows.at(row).at(10).toLongLong());
    });
    connect(m_materialName, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString knownUnit = m_materialName->itemData(index).toString();
        if (!knownUnit.isEmpty()) m_unit->setEditText(knownUnit);
    });
    QTimer::singleShot(0, this, [this]() { refresh(); });
}

bool WorkshopInventoryPage::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusOut) {
        auto* editor = qobject_cast<QDoubleSpinBox*>(watched);
        if (editor && (editor == m_quantity || editor == m_unitCost || editor == m_clientPrice)) {
            if (auto* input = editor->findChild<QLineEdit*>(); input && input->text().isEmpty())
                editor->setValue(editor->value());
        }
    }
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        auto* input = qobject_cast<QLineEdit*>(watched);
        if (input && input->objectName()==QStringLiteral("spareIssueOrderInput")) {
            input->clear();
        }
        auto* editor = input ? qobject_cast<QDoubleSpinBox*>(input->parentWidget()) : nullptr;
        if (editor && (editor == m_quantity || editor == m_unitCost || editor == m_clientPrice)
            && editor->isEnabled() && !editor->isReadOnly()) {
            editor->clear();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WorkshopInventoryPage::refresh()
{
    if (m_dirty) refreshModels();
}

void WorkshopInventoryPage::markDirty()
{
    m_dirty = true;
}

void WorkshopInventoryPage::scheduleRefresh(int delayMs)
{
    if (m_refreshDebounce) m_refreshDebounce->start(qMax(0, delayMs));
}
void WorkshopInventoryPage::refreshModels()
{
    QString spareError;
    auto spareOrders=WorkshopSpareStockService::orders(&spareError);
    if(!spareError.isEmpty()) {setSummary(spareError,true);return;}
    // Keep history in the service; the warehouse view shows only unissued stock/orders.
    for(int i=spareOrders.size()-1;i>=0;--i)
        if(qMax(spareOrders[i].quantity,spareOrders[i].received)-spareOrders[i].issued<=0.000001)
            spareOrders.removeAt(i);
    TabularReadModel spare;
    spare.headers={"ID",QString::fromUtf8("Запчастина / OEM"),QString::fromUtf8("Автомобіль"),QString::fromUtf8("Замовив майстер"),
        QString::fromUtf8("Замовлено"),QString::fromUtf8("Поставка"),QString::fromUtf8("Замовлено\nПрийнято\nВидано"),
        QString::fromUtf8("Постачальник"),QString::fromUtf8("Сплатити до"),QString::fromUtf8("Протерміновано"),QString::fromUtf8("Борг, грн")};
    for(const auto& r:spareOrders) spare.rows.append({r.id,r.name+" | "+r.article,r.brand+" "+r.model+" | "+r.plate,r.masterName,
        r.orderedOn.toString("dd.MM.yy"),r.deliveredOn.isValid()?r.deliveredOn.toString("dd.MM.yy"):r.expectedOn.toString("dd.MM.yy")+QString::fromUtf8(" (план)"),
        QString("%1 / %2 / %3").arg(quantityText(r.quantity),quantityText(r.received),quantityText(r.issued)),r.supplier,
        r.dueOn.toString("dd.MM.yy"),r.unpaidDays>=0?QString::number(r.unpaidDays):QString::fromUtf8("—"),moneyText(r.debtCents/100.0)});
    m_spareModel->setSnapshot(spare);m_spareTable->setColumnHidden(0,true);
    const WorkshopInventorySnapshot snapshot = WorkshopInventoryService::buildSnapshot(m_from->date(), m_to->date());
    if (!snapshot.errorText.trimmed().isEmpty()) {
        setSummary(snapshot.errorText, true);
        return;
    }

    TabularReadModel period;
    period.headers = {QString::fromUtf8("Дата"), QString::fromUtf8("Операція"), QString::fromUtf8("Наряд"),
                      QString::fromUtf8("Матеріал"), QString::fromUtf8("Од."), QString::fromUtf8("Надійшло"),
                      QString::fromUtf8("Витрачено"), QString::fromUtf8("Залишок"), QString::fromUtf8("Ціна"),
                      QString::fromUtf8("Сума"), QStringLiteral("PurchaseID")};
    for (const WorkshopInventoryPeriodRow& row : snapshot.periodRows) {
        period.rows.append({row.date.toString(QStringLiteral("dd.MM.yy")), row.operation,
                            row.orderNumber > 0 ? QString::number(row.orderNumber) : QString(), row.materialName, row.unit,
                            row.receivedQuantity > 0.0 ? quantityText(row.receivedQuantity) : QString(),
                            row.consumedQuantity > 0.0 ? quantityText(row.consumedQuantity) : QString(),
                            quantityText(row.balanceQuantity), row.unitCost > 0.0 ? moneyText(row.unitCost) : QString(),
                            moneyText(row.totalCost), row.purchaseId});
    }
    m_periodModel->setSnapshot(period);
    m_periodTable->setObjectName("inventoryMovementJournal");
    m_periodTable->setColumnHidden(10, true);

    TabularReadModel purchases;
    purchases.headers = {QStringLiteral("ID"), QString::fromUtf8("Дата"), QString::fromUtf8("Матеріал"),
                         QString::fromUtf8("Од."), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна за 1 од."),
                         QString::fromUtf8("Клієнту за 1 од."),
                         QString::fromUtf8("Сума"), QString::fromUtf8("Постачальник"),
                         QString::fromUtf8("Оплата"), QString::fromUtf8("Сплачено, грн"), QString::fromUtf8("Борг, грн")};
    for (const WorkshopInventoryPurchaseRow& row : snapshot.purchases) {
        purchases.rows.append({row.id, row.purchasedOn.toString(QStringLiteral("dd.MM.yy")), row.materialName, row.unit,
                               quantityText(row.quantity), moneyText(row.unitCost), row.clientPrice < 0.0 ? QString() : moneyText(row.clientPrice), moneyText(row.quantity * row.unitCost),
                               row.supplier,
                               row.paidAmount + 0.005 >= row.quantity * row.unitCost ? QString::fromUtf8("Сплачено")
                                   : row.paidAmount > 0 ? QString::fromUtf8("Частково") : QString::fromUtf8("Не сплачено"),
                               moneyText(row.paidAmount), moneyText(qMax(0.0, row.quantity * row.unitCost-row.paidAmount))});
    }
    m_purchasesModel->setSnapshot(purchases);
    m_purchasesTable->setColumnHidden(0, true);

    TabularReadModel expenses;
    expenses.headers = {QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"), QString::fromUtf8("Авто / клієнт"),
                        QString::fromUtf8("Матеріал"), QString::fromUtf8("Од."), QString::fromUtf8("Кількість"),
                        QString::fromUtf8("Собівартість"), QString::fromUtf8("Сума")};
    for (const WorkshopInventoryConsumptionRow& row : snapshot.consumptions) {
        QStringList carAndClientParts;
        if (!row.car.isEmpty()) carAndClientParts.append(row.car);
        if (!row.client.isEmpty()) carAndClientParts.append(row.client);
        expenses.rows.append({row.consumedOn.toString(QStringLiteral("dd.MM.yy")),
                              row.orderNumber > 0 ? QString::number(row.orderNumber) : QString(), carAndClientParts.join(QStringLiteral(" | ")),
                              row.materialName, row.unit, quantityText(row.quantity), moneyText(row.unitCost), moneyText(row.totalCost)});
    }
    m_expensesModel->setSnapshot(expenses);

    TabularReadModel balance;
    balance.headers = {QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"), QString::fromUtf8("Од"),
                       QString::fromUtf8("Витрата"), QString::fromUtf8("Залишок"),
                       QString::fromUtf8("Вартість витрати"), QString::fromUtf8("Вартість залишку")};
    const QString selectedMaterial = m_balanceMaterial->currentData().toString();
    const QSignalBlocker balanceSelectionBlocker(m_balanceMaterial);
    m_balanceMaterial->clear();
    for (const auto& row : snapshot.balanceRows) m_balanceMaterial->addItem(row.materialName, row.materialKey);
    const int selectedIndex = m_balanceMaterial->findData(selectedMaterial);
    if (selectedIndex >= 0) m_balanceMaterial->setCurrentIndex(selectedIndex);
    const QString materialKey = m_balanceMaterial->currentData().toString();
    for (const auto& row : snapshot.periodRows) {
        if (WorkshopInventoryService::normalizedMaterialKey(row.materialName) != materialKey) continue;
        balance.rows.append({row.date.toString("dd.MM.yy"), row.orderNumber > 0 ? QString::number(row.orderNumber) : QString(),
            row.unit, row.consumedQuantity > 0 ? quantityText(row.consumedQuantity) : QString(),
            quantityText(row.balanceQuantity), row.consumedQuantity > 0 ? moneyText(row.totalCost) : QString(), moneyText(row.stockValue)});
    }
    double stockValueTotal = 0.0;
    for (const WorkshopInventoryBalanceRow& row : snapshot.balanceRows) {
        stockValueTotal += row.stockValue;
        if (balance.rows.isEmpty() && row.materialKey == materialKey)
            balance.rows.append({m_to->date().toString("dd.MM.yy"), QString(), row.unit, QString(),
                quantityText(row.balanceQuantity), QString(), moneyText(row.stockValue)});
    }
    m_balanceModel->setSnapshot(balance);

    refreshMaterialChoices();
    const auto currentStock=m_to->date()==QDate::currentDate() ? snapshot
        : WorkshopInventoryService::buildSnapshot(QDate::currentDate(),QDate::currentDate());
    if(currentStock.errorText.isEmpty()) {
        const auto totals=InventoryStockSummary::calculate(currentStock.balanceRows);
        for(int i=0;i<3;++i) {
            QStringList amounts;
            for(auto it=totals.quantities[i].cbegin();it!=totals.quantities[i].cend();++it)
                amounts.append(quantityText(it.value())+" "+it.key());
            m_materialMetrics[i]->setText(amounts.isEmpty()?(i<2?QString::fromUtf8("0 л"):QString("0")):amounts.join(QString::fromUtf8(" · ")));
            m_materialMetrics[i]->setToolTip(QString::fromUtf8("Фактичний залишок на %1\n%2").arg(QDate::currentDate().toString("dd.MM.yy"),totals.details[i].join('\n')));
        }
        m_materialMetrics[3]->setText(moneyText(totals.value)+QString::fromUtf8(" грн"));
        m_materialMetrics[3]->setToolTip(QString::fromUtf8("Вартість фактичного залишку на %1").arg(QDate::currentDate().toString("dd.MM.yy")));
    } else for(auto* metric:m_materialMetrics) {metric->setText(QString::fromUtf8("—"));metric->setToolTip(currentStock.errorText);}
    setSummary(QString::fromUtf8("Матеріалів: %1 | Придбань: %2 | Витрат: %3 | Залишок: %4 грн")
               .arg(snapshot.balanceRows.size())
               .arg(snapshot.purchases.size())
               .arg(snapshot.consumptions.size())
               .arg(moneyText(stockValueTotal)));
    if (m_stockTabs->currentIndex()==0) {
        double reserved=0, awaiting=0;qint64 debt=0;
        for(const auto& order:spareOrders) {reserved+=order.received-order.issued;awaiting+=order.quantity-order.received;debt+=order.debtCents;}
        m_spareMetrics[0]->setText(QString::number(spareOrders.size()));
        m_spareMetrics[1]->setText(quantityText(awaiting) + QString::fromUtf8(" шт."));
        m_spareMetrics[2]->setText(quantityText(reserved) + QString::fromUtf8(" шт."));
        m_spareMetrics[3]->setText(moneyText(debt/100.0) + QString::fromUtf8(" грн"));
        setSummary(QString::fromUtf8("Замовлень: %1 | Очікується: %2 шт. | Резерв: %3 шт. | Борг: %4 грн")
            .arg(spareOrders.size()).arg(quantityText(awaiting),quantityText(reserved),moneyText(debt/100.0)));
    }
    m_dirty = false;
}

void WorkshopInventoryPage::refreshMaterialChoices()
{
    QString errorText;
    const QVector<WorkshopInventoryMaterial> materials = WorkshopInventoryService::loadMaterials(&errorText);
    if (!errorText.trimmed().isEmpty()) return;

    const QString currentMaterial = m_materialName->currentText();
    const QString currentUnit = m_unit->currentText();
    const QSignalBlocker materialBlocker(m_materialName);
    m_materialName->clear();
    for (const WorkshopInventoryMaterial& material : materials) m_materialName->addItem(material.name, material.unit);
    m_materialName->setEditText(currentMaterial);
    if (m_materialName->currentIndex() < 0 && !currentUnit.isEmpty()) m_unit->setEditText(currentUnit);
}

qint64 WorkshopInventoryPage::selectedPurchaseId() const
{
    if (!m_purchasesTable || !m_purchasesModel || !m_purchasesTable->selectionModel()) return 0;
    const QModelIndexList rows = m_purchasesTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return 0;
    const TabularReadModel& snapshot = m_purchasesModel->snapshot();
    const int row = rows.constFirst().row();
    if (row < 0 || row >= snapshot.rows.size() || snapshot.rows.at(row).isEmpty()) return 0;
    return snapshot.rows.at(row).at(0).toLongLong();
}

void WorkshopInventoryPage::addPurchase()
{
    WorkshopInventoryPurchaseInput input;
    input.purchasedOn = m_purchaseDate->date();
    input.materialName = m_materialName->currentText();
    input.unit = m_unit->currentText();
    input.quantity = m_quantity->value();
    input.unitCost = InventoryPriceInput::unitPrice(m_unitCost->value(),input.quantity,m_priceBasis->currentData().toBool());
    input.clientPrice = m_clientPrice->value() < 0.0 ? -1.0 : InventoryPriceInput::unitPrice(m_clientPrice->value(),input.quantity,m_priceBasis->currentData().toBool());
    input.supplier = m_supplier->text();
    input.category=m_category->text(); input.article=m_article->text(); input.batch=m_batch->text();input.document=m_document->text();
    input.minimumQuantity=m_minimum->value();input.orderedOn=m_orderedDate->date();
    if(m_dueDate->date()>m_dueDate->minimumDate()) input.dueOn=m_dueDate->date();
    if(m_expiryDate->date()>m_expiryDate->minimumDate()) input.expiresOn=m_expiryDate->date();
    if(input.purchasedOn<input.orderedOn || input.purchasedOn>QDate::currentDate() || (input.dueOn.isValid() && input.dueOn<input.orderedOn)) {
        QMessageBox::warning(m_materialDialog,QString::fromUtf8("Дати"),QString::fromUtf8("Перевірте дати замовлення, фактичної поставки та оплати."));return;
    }

    PurchaseDebt purchase;
    purchase.source = "inventory"; purchase.name = input.materialName; purchase.supplier = input.supplier;
    purchase.date = input.purchasedOn.toString("dd.MM.yy"); purchase.quantity = input.quantity;
    purchase.unit = input.unit; purchase.note = input.note;
    purchase.totalCents = PurchasePaymentService::cents(input.quantity * input.unitCost);
    if (input.materialName.trimmed().isEmpty() || input.quantity <= 0 || purchase.totalCents < 0) {
        QMessageBox::warning(this, QString::fromUtf8("Придбання"), QString::fromUtf8("Вкажіть матеріал, кількість та ціну.")); return;
    }
    qint64 paidCents = 0;
    if (!choosePurchasePayment(this, purchase, true, &paidCents, &input.deferredDays)) return;
    input.paidAmount = paidCents / 100.0;

    QString errorText;
    if (!WorkshopInventoryService::addPurchase(input, nullptr, &errorText)) {
        QMessageBox::warning(this, QString::fromUtf8("Склад СТО"), errorText);
        return;
    }
    m_supplier->clear();
    m_materialDialog->accept();
    m_dirty = true;
    refreshModels();
    emit requestRefreshAll();
}

void WorkshopInventoryPage::addSpareOrder()
{
    InventoryEntryDialog dialog(this);dialog.setWindowTitle(QString::fromUtf8("Додати запчастину під авто"));dialog.resize(800,520);
    dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    dialog.setFont(QFont("Segoe UI",10));
    dialog.setStyleSheet(QStringLiteral(
        "QDialog {background:#242e38;border:1px solid #677785;border-radius:10px;}"
        "QWidget {font-family:'Segoe UI';color:white;} QLabel {background:transparent;border:0;}"
        "QWidget#spareHeader {background:black;}"
        "QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox {background:white;color:black;border:1px solid #a7b1bb;border-radius:5px;padding:6px;}"
        "QComboBox QAbstractItemView {background:white;color:black;selection-background-color:#dbe8fa;}"
        "QPushButton {color:white;border:1px solid #84909d;border-radius:7px;padding:9px 14px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #45494d,stop:0.45 #141619,stop:0.5 #08090b,stop:1 black);}"
        "QPushButton:disabled {color:#8a9299;}"));
    auto* root=new QVBoxLayout(&dialog);root->setContentsMargins(10,8,10,8);root->setSpacing(6);
    auto* header=new QWidget(&dialog);header->setObjectName("spareHeader");auto* headerLayout=new QHBoxLayout(header);
    auto* heading=new QLabel(QString::fromUtf8("Запчастини під авто"),header);heading->setStyleSheet("font:600 18pt 'Segoe UI';padding:8px;");headerLayout->addWidget(heading,1);
    auto* close=new QPushButton(QString::fromUtf8("×"),header);close->setAutoDefault(false);close->setStyleSheet("background:transparent;border:0;font:20pt 'Segoe UI';");headerLayout->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);root->addWidget(header);
    dialog.enableHeaderDrag(header);
    auto* form=new QWidget(&dialog);form->setObjectName("spareEntryFields");
    form->setStyleSheet("QWidget#spareEntryFields {background:transparent;}");
    auto* grid=new QGridLayout(form);int index=0;
    QMap<QWidget*,QLabel*> fieldLabels;
    grid->setContentsMargins(4,2,4,2);grid->setHorizontalSpacing(10);grid->setVerticalSpacing(2);
    grid->setAlignment(Qt::AlignTop);
    auto* clearOnEntry=new ClearOnEntryFilter(form);
    auto field=[&](const QString& label,QWidget* widget,bool optional=false){
        const bool vehicle=index<6;
        const int row=vehicle?(index/2)*2:6+((index-6)/3)*2;
        const int column=vehicle?(index%2)*3:((index-6)%3)*2;
        const int span=vehicle?3:2;++index;
        clearOnEntry->attach(widget,label,optional);
        auto* caption=new QLabel(label,form);caption->setWordWrap(true);
        fieldLabels.insert(widget,caption);
        grid->addWidget(caption,row,column,1,span);grid->addWidget(widget,row+1,column,1,span);
    };
    auto line=[&](const QString& label,bool optional=false){auto* edit=new QLineEdit(form);field(label,edit,optional);return edit;};
    auto combo=[&](const QString& label){auto* c=new QComboBox(form);c->setEditable(true);c->setInsertPolicy(QComboBox::NoInsert);field(label,c,true);c->completer()->setCaseSensitivity(Qt::CaseInsensitive);c->completer()->setFilterMode(Qt::MatchContains);return c;};
    auto* brandCombo=combo(QString::fromUtf8("Марка авто *"));auto* modelCombo=combo(QString::fromUtf8("Модель авто *"));
    QStringList brands=QStringLiteral("Acura|Alfa Romeo|Audi|BMW|BYD|Chery|Chevrolet|Citroen|Dacia|Daewoo|Dodge|Fiat|Ford|Geely|Honda|Hyundai|Infiniti|Jaguar|Jeep|Kia|Land Rover|Lexus|Mazda|Mercedes-Benz|MG|Mini|Mitsubishi|Nissan|Opel|Peugeot|Porsche|Renault|Seat|Skoda|Subaru|Suzuki|Tesla|Toyota|Volkswagen|Volvo|ZAZ").split('|');
    const auto knownVehicles=WorkshopSpareStockService::orders();
    for(const auto& v:knownVehicles)if(!brands.contains(v.brand,Qt::CaseInsensitive))brands<<v.brand;
    brandCombo->addItems(VehicleNameUtils::uniqueModelOptions(brands));brandCombo->setCurrentIndex(-1);
    auto* brand=brandCombo->lineEdit();auto* model=modelCombo->lineEdit();
    connect(brandCombo,&QComboBox::currentTextChanged,&dialog,[=](const QString& value){
        const QString keep=model->text();QSignalBlocker block(modelCombo);modelCombo->clear();
        auto models=CatalogQueryService::modelOptionsForBrand(value);
        for(const auto& v:knownVehicles)if(v.brand.compare(value,Qt::CaseInsensitive)==0 && !models.contains(v.model,Qt::CaseInsensitive))models<<v.model;
        modelCombo->addItems(VehicleNameUtils::uniqueModelOptions(models));modelCombo->setCurrentText(keep);
    });
    auto* plate=line(QString::fromUtf8("Держномер"),true);auto* vin=line(QString::fromUtf8("VIN (необов'язково)"));vin->setMaxLength(17);
    brand->setObjectName("spareVehicleBrand"); model->setObjectName("spareVehicleModel");
    plate->setObjectName("spareVehiclePlate");
    auto* previous=combo(QString::fromUtf8("Дозамовлення для авто"));previous->addItem(QString::fromUtf8("Нове авто"),-1);
    QSet<QString> seenPlates;
    for(int i=0;i<knownVehicles.size();++i){const auto& v=knownVehicles[i];const QString key=PlateUtils::normalizePlate(v.plate);if(key.isEmpty() || seenPlates.contains(key))continue;seenPlates.insert(key);previous->addItem(v.plate+" · "+v.brand+" "+v.model,i);}
    connect(previous,qOverload<int>(&QComboBox::activated),&dialog,[=](int){int i=previous->currentData().toInt();if(i<0 || i>=knownVehicles.size())return;const auto& v=knownVehicles[i];brandCombo->setCurrentText(v.brand);modelCombo->setCurrentText(v.model);plate->setText(v.plate);vin->setText(v.vin);});
    const auto normalizeVehicle = [=] {
        // The application filter owns manual correction and undo. Keep this
        // fallback for callers/tests that do not install the application filter.
        if (!brand->property("inputCorrectionObserved").toBool())
            brand->setText(VehicleNameUtils::normalizeVehicleFreeText(brand->text()).toUpper());
        if (!model->property("inputCorrectionObserved").toBool())
            model->setText(VehicleNameUtils::normalizeVehicleModel(brand->text(), model->text()).toUpper());
        if (!plate->property("inputCorrectionObserved").toBool())
            plate->setText(PlateUtils::formatPlateDisplay(plate->text()));
    };
    for (auto* edit : {brand, model, plate})
        connect(edit, &QLineEdit::editingFinished, &dialog, normalizeVehicle);
    auto* master=new QComboBox(form);master->addItem(QString::fromUtf8("Оберіть майстра"),QString());
    for(const auto& row:StaffAccountService::orderingMasters()) master->addItem(row.second,row.first);
    field(QString::fromUtf8("Майстер, який замовив *"),master);
    auto* supplier=line(QString::fromUtf8("Постачальник *"));auto* name=line(QString::fromUtf8("Назва запчастини *"));
    auto* article=line(QString::fromUtf8("OEM / артикул"));
    name->setObjectName("sparePartName"); article->setObjectName("sparePartArticle");
    auto* quantity=new QDoubleSpinBox(form);quantity->setDecimals(0);quantity->setRange(1,1000000);field(QString::fromUtf8("Кількість, шт. *"),quantity);
    auto* price=new QDoubleSpinBox(form);price->setDecimals(2);price->setRange(0,100000000);field(QString::fromUtf8("Ціна закупівлі за одиницю, грн"),price);
    price->setObjectName("spareUnitCost");
    auto date=[&](const QString& label,bool optional){auto* edit=new DraftDateEdit(QDate::currentDate(),form);
        edit->setCalendarPopup(true);edit->setDisplayFormat("dd.MM.yyyy");if(optional) {edit->setMinimumDate(QDate(1900,1,1));edit->setSpecialValueText(QString::fromUtf8("Не задано"));}field(label,edit,optional);return edit;};
    auto* ordered=date(QString::fromUtf8("Дата замовлення *"),false);auto* expected=date(QString::fromUtf8("Очікувана поставка"),true);
    ordered->setObjectName("spareOrderedDate");
    auto* due=date(QString::fromUtf8("Сплатити до"),true);
    auto* document=line(QString::fromUtf8("Номер накладної"));
    document->setObjectName("spareInvoiceNumber");
    document->setToolTip(QString::fromUtf8("Номер спільної накладної поставки. Зберігається із запчастиною та передається в наряд; файл не обов'язковий."));
    QString invoicePath;
    QJsonObject invoiceIdentity;
    auto* receiptState=new QComboBox(form);receiptState->addItems({QString::fromUtf8("Замовлено"),QString::fromUtf8("Отримано на склад")});
    field(QString::fromUtf8("Стан поставки"),receiptState);
    auto* actual=date(QString::fromUtf8("Фактична дата поставки"),false);actual->setEnabled(false);
    connect(receiptState,qOverload<int>(&QComboBox::currentIndexChanged),actual,[actual](int value){actual->setEnabled(value==1);});
    QVector<QPair<QString,int>> targets;
    QMap<int,int> importOrderIds;
    QVector<WorkshopSpareStockService::ImportTarget> importTargets;
    QString targetError;
    if (!WorkshopSpareStockService::importTargets(&importTargets,&targetError)) {
        QMessageBox::warning(&dialog,QString::fromUtf8("Наряди для імпорту"),targetError);
        return;
    }
    for (const auto& target : importTargets) {
        importOrderIds.insert(target.visibleNumber,target.number);
        targets.append({QString::fromUtf8("№%1 · %2 · %3").arg(target.visibleNumber).arg(target.car,target.plate),target.number});
    }
    auto targetCombo=[&](QWidget* parent){auto* c=new QComboBox(parent);c->addItem(QString::fromUtf8("Склад під це авто"),0);
        for(const auto& target:targets)c->addItem(target.first,target.second);return c;};
    auto* firstTarget=targetCombo(form);field(QString::fromUtf8("Наряд / склад"),firstTarget);
    auto* extra=new QTableWidget(0,5,&dialog);extra->setHorizontalHeaderLabels({QString::fromUtf8("Запчастина"),QString::fromUtf8("OEM"),QString::fromUtf8("Кількість"),QString::fromUtf8("Ціна"),QString::fromUtf8("Наряд / склад")});
    extra->setObjectName("spareAdditionalParts");
    extra->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);extra->setMinimumHeight(96);extra->setMaximumHeight(150);
    extra->verticalHeader()->setMinimumSectionSize(48);
    extra->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    std::function<void()> update;
    auto addExtra=[&](const QString& title=QString(),const QString& oem=QString(),double qty=1,double cost=0,const QJsonObject& identity=QJsonObject()){
        int r=extra->rowCount();extra->insertRow(r);auto* item=new QTableWidgetItem(title);item->setData(Qt::UserRole,identity);extra->setItem(r,0,item);extra->setItem(r,1,new QTableWidgetItem(oem));
        auto* q=new QDoubleSpinBox(extra);q->setDecimals(0);q->setRange(1,1000000);q->setValue(qty);extra->setCellWidget(r,2,q);
        auto* p=new QDoubleSpinBox(extra);p->setDecimals(2);p->setRange(0,100000000);p->setValue(cost);extra->setCellWidget(r,3,p);extra->setCellWidget(r,4,targetCombo(extra));
        for(auto* spin:{q,p})connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){if(update)update();});if(update)update();
    };
    root->addWidget(form);
    // Keep draft controls alive for the existing atomic save/import path, but
    // edit line items in the dedicated preview rather than in the vehicle form.
    while(auto* item=grid->takeAt(0))delete item;
    for(auto it=fieldLabels.begin();it!=fieldLabels.end();++it){it.key()->hide();it.value()->hide();}
    const QList<QWidget*> vehicleFields={brandCombo,modelCombo,plate,vin,previous,master,expected,due,document,receiptState,actual,firstTarget};
    int cell=0;for(auto* widget:vehicleFields){const int row=(cell/3)*2;const int col=cell%3;grid->addWidget(fieldLabels.value(widget),row,col);grid->addWidget(widget,row+1,col);fieldLabels.value(widget)->show();widget->show();++cell;}
    grid->setVerticalSpacing(8);grid->setHorizontalSpacing(16);for(int col=0;col<3;++col)grid->setColumnStretch(col,1);
    auto* more=new QPushButton(QString::fromUtf8("+ Додати запчастини"),&dialog);more->setObjectName("spareAddParts");more->setAutoDefault(false);
    auto* reviewParts=new QPushButton(QString::fromUtf8("Переглянути / змінити"),&dialog);reviewParts->setObjectName("spareReviewParts");reviewParts->setAutoDefault(false);
    auto* rowsBar=new QHBoxLayout;rowsBar->addWidget(more,2);rowsBar->addWidget(reviewParts,1);root->addLayout(rowsBar);extra->hide();
    const int totalGap=qRound(3.0*dialog.logicalDpiY()/25.4);
    root->addSpacing(qMax(0,totalGap-root->spacing()));
    auto* total=new QLabel(&dialog);total->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    root->addSpacing(qMax(0,totalGap-root->spacing()));update=[=]{
        double amount=quantity->value()*price->value();for(int r=0;r<extra->rowCount();++r)amount+=qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,2))->value()*qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,3))->value();
        total->setText(clearOnEntry->empty(quantity) || clearOnEntry->empty(price)
            ? QString::fromUtf8("Сума замовлення: введіть кількість і ціну")
            : QString::fromUtf8("Сума замовлення: %1 грн").arg(moneyText(amount)));
    };
    for(auto* spin:{quantity,price})connect(spin->findChild<QLineEdit*>(),&QLineEdit::textChanged,&dialog,[&dialog,update]{QTimer::singleShot(0,&dialog,update);});
    connect(quantity,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[update](double){update();});
    connect(price,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[update](double){update();});update();
    auto* invoiceRow = new QHBoxLayout;
    auto* importInvoice = new QPushButton(QString::fromUtf8("Імпорт накладної"), &dialog);
    importInvoice->setObjectName("spareImportInvoice");
    importInvoice->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    importInvoice->setAutoDefault(false);
    auto* invoiceLabel = new QLabel(&dialog); invoiceLabel->setWordWrap(true);
    invoiceLabel->setObjectName("spareInvoiceFile");invoiceLabel->hide();
    auto* clearInvoice = new QPushButton(&dialog);
    clearInvoice->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    clearInvoice->setToolTip(QString::fromUtf8("Прибрати накладну"));
    clearInvoice->setAutoDefault(false); clearInvoice->setVisible(false);
    invoiceRow->addWidget(invoiceLabel, 1); invoiceRow->addWidget(clearInvoice);
    root->addLayout(invoiceRow);
    connect(clearInvoice, &QPushButton::clicked, &dialog, [&] {
        invoicePath.clear(); invoiceIdentity={}; invoiceLabel->clear(); invoiceLabel->hide(); invoiceLabel->setToolTip({}); clearInvoice->setVisible(false);
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    auto* importJob = new InvoiceImport::ImportJob(&dialog);
    importJob->setObjectName("spareInvoiceImportJob");
    const auto setImportBusy = [&](bool busy) {
        importInvoice->setEnabled(!busy); clearInvoice->setEnabled(!busy);
        buttons->button(QDialogButtonBox::Save)->setEnabled(!busy);
    };
    const auto applyDrafts = [&](const QList<InvoiceImport::ImportedPartDraft>& parsed, const InvoiceImport::ExcelReadResult& sourceData, bool manual = false) {
        QMap<QString,QJsonObject> identities;identities.insert(article->text()+"|"+name->text(),invoiceIdentity);
        for(int r=0;r<extra->rowCount();++r)identities.insert(extra->item(r,1)->text()+"|"+extra->item(r,0)->text(),extra->item(r,0)->data(Qt::UserRole).toJsonObject());
        InvoiceReviewDialog review(parsed,sourceData,&dialog,QString(),manual);review.useWhiteInputs();
        review.commit=[&](QString* error){for(const auto& row:review.selectedDrafts()){
            bool ok=false;double qty=PartImportTextSupport::parseLooseNumber(row.qtyText,&ok);
            if(!ok||qty<1||qty>1000000||std::floor(qty)!=qty){*error=QString::fromUtf8("Для складської запчастини потрібна ціла кількість від 1 до 1 000 000.");return false;}
            if(!row.orderNumberText.isEmpty()&&!importOrderIds.contains(row.orderNumberText.toInt())){*error=QString::fromUtf8("Наряд №%1 не відкритий або недоступний.").arg(row.orderNumberText);return false;}}
            return true;};
        if(review.exec()!=QDialog::Accepted)return;
        const auto drafts=review.selectedDrafts();auto data=sourceData;
        data.documentNumber=review.invoiceNumber();data.documentDate=review.invoiceDate();
        int selected=0;
        if(drafts.isEmpty())return;
        for(int i=0;i<drafts.size();++i){bool qtyOk=false,costOk=false;const auto& d=drafts[i];double qty=PartImportTextSupport::parseLooseNumber(d.qtyText,&qtyOk),cost=PartImportTextSupport::parseLooseNumber(d.priceText,&costOk);
            if(!qtyOk || qty<1 || qty>1000000 || std::floor(qty)!=qty || !costOk || cost<0 || cost>100000000){QMessageBox::warning(&dialog,QString::fromUtf8("Накладна"),QString::fromUtf8("Перевірте кількість та ціну всіх рядків накладної."));return;}}
        const auto& draft=drafts[selected];
        extra->setRowCount(0);firstTarget->setCurrentIndex(qMax(0,firstTarget->findData(importOrderIds.value(draft.orderNumberText.toInt(),0))));
        for(int i=1;i<drafts.size();++i){const auto& d=drafts[i];addExtra(d.name,d.oemNumber,PartImportTextSupport::parseLooseNumber(d.qtyText,nullptr),PartImportTextSupport::parseLooseNumber(d.priceText,nullptr),InvoiceImport::catalogIdentity(data.rows,d));extra->item(i-1,0)->setData(Qt::UserRole+1,d.store);extra->item(i-1,0)->setData(Qt::UserRole+2,d.deliveryDate);if(auto* target=qobject_cast<QComboBox*>(extra->cellWidget(i-1,4)))target->setCurrentIndex(qMax(0,target->findData(importOrderIds.value(d.orderNumberText.toInt(),0))));}
        expected->setDate(DateFormats::parseDate(draft.deliveryDate));expected->setProperty("entryPending",false);
        invoiceIdentity=InvoiceImport::catalogIdentity(data.rows,draft);
        name->setText(draft.name); article->setText(draft.oemNumber);
        QDate invoiceDate=DateFormats::parseDate(draft.orderedAt);
        if (!invoiceDate.isValid()) invoiceDate=DateFormats::parseDate(data.documentDate);
        if (invoiceDate.isValid()) {ordered->setDate(invoiceDate);ordered->setProperty("entryPending",false);}
        if (!data.documentNumber.isEmpty()) document->setText(data.documentNumber);
        if (!draft.store.isEmpty()) supplier->setText(draft.store);
        bool valid=false;
        const double count=PartImportTextSupport::parseLooseNumber(draft.qtyText,&valid);
        if (valid && count>=1 && std::floor(count)==count && count<=quantity->maximum()) {quantity->setValue(count);quantity->setProperty("entryPending",false);}
        const double cost=PartImportTextSupport::parseLooseNumber(draft.priceText,&valid);
        if (valid && cost>=0 && cost<=price->maximum()) {price->setValue(cost);price->setProperty("entryPending",false);}
        if(manual){invoiceIdentity=identities.value(article->text()+"|"+name->text());for(int r=0;r<extra->rowCount();++r)extra->item(r,0)->setData(Qt::UserRole,identities.value(extra->item(r,1)->text()+"|"+extra->item(r,0)->text()));}
        update();
    };
    auto editParts=[&](bool append){
        QList<InvoiceImport::ImportedPartDraft> drafts;
        auto appendDraft=[&](const QString& title,const QString& oem,double qty,double cost,const QString& store,int target,const QString& delivery){InvoiceImport::ImportedPartDraft d;d.name=title;d.oemNumber=oem;d.qtyText=QString::number(qty);d.priceText=QString::number(cost,'f',2);d.store=store;d.deliveryDate=delivery;if(target>0)d.orderNumberText=QString::number(importOrderIds.key(target));drafts.append(d);};
        if(!name->text().trimmed().isEmpty())appendDraft(name->text(),article->text(),quantity->value(),price->value(),supplier->text(),firstTarget->currentData().toInt(),expected->date().toString("dd.MM.yy"));
        for(int r=0;r<extra->rowCount();++r)appendDraft(extra->item(r,0)->text(),extra->item(r,1)->text(),qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,2))->value(),qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,3))->value(),extra->item(r,0)->data(Qt::UserRole+1).isValid()?extra->item(r,0)->data(Qt::UserRole+1).toString():supplier->text(),qobject_cast<QComboBox*>(extra->cellWidget(r,4))->currentData().toInt(),extra->item(r,0)->data(Qt::UserRole+2).toString());
        if(append||drafts.isEmpty())appendDraft({},{},1,0,supplier->text(),firstTarget->currentData().toInt(),expected->date().toString("dd.MM.yy"));
        InvoiceImport::ExcelReadResult data;data.documentNumber=document->text();data.documentDate=ordered->date().toString(Qt::ISODate);applyDrafts(drafts,data,true);
    };
    connect(more,&QPushButton::clicked,&dialog,[&]{editParts(true);});connect(reviewParts,&QPushButton::clicked,&dialog,[&]{editParts(false);});
    auto* importProgress = new InvoiceImportProgressDialog(&dialog, importJob, [&, applyDrafts](const InvoiceImport::ImportJobResult& result) {
        if (result.status == InvoiceImport::ImportJobStatus::Cancelled) { setImportBusy(false); return; }
        if (!result.filePath.isEmpty()) {
            invoicePath=result.filePath;
            invoiceLabel->setText(QFileInfo(invoicePath).fileName()); invoiceLabel->show(); invoiceLabel->setToolTip(invoicePath);
            clearInvoice->setVisible(true);
        }
        if (!result.ok() || (result.data.rows.isEmpty() && result.data.parsedPartDrafts.isEmpty())) {
            setImportBusy(false);
            QMessageBox::warning(&dialog, QString::fromUtf8("Накладна"), result.error.message.isEmpty()
                ? QString::fromUtf8("Не знайдено позицій накладної.") : result.error.message);
            return;
        }
        if (!result.data.parsedPartDrafts.isEmpty()) {
            setImportBusy(false); applyDrafts(result.data.parsedPartDrafts, result.data); return;
        }
        InvoiceImport::InvoiceColumnMappingPreview confirmed;
        if (!editInvoiceColumnMapping(&dialog, result.data.rows, result.data.columnMappingPreview, invoicePath, &confirmed)) {
            setImportBusy(false); return;
        }
        setImportBusy(true);
        WorkPartsDialogExcelSupport::runConfirmedVatMappingParseAsync(&dialog, result.data.rows, confirmed,
            result.data.documentDate,
            [](const QString& value) { return PartImportTextSupport::normalizePartName(value); },
            [](QString value, bool* valid) { return PartImportTextSupport::parseLooseNumber(value, valid); },
            [](double value) { return FormatUtils::money(value, 2); },
            [&, applyDrafts, data=result.data](const QList<InvoiceImport::ImportedPartDraft>& drafts, const QString& error) {
                setImportBusy(false);
                if (drafts.isEmpty()) {
                    QMessageBox::warning(&dialog, QString::fromUtf8("Накладна"), error.isEmpty()
                        ? QString::fromUtf8("Перевірте вибрані колонки накладної.") : error); return;
                }
                applyDrafts(drafts, data);
            });
    });
    connect(importInvoice, &QPushButton::clicked, &dialog, [&] {
        const QString path=InvoiceDialogSupport::selectFile(&dialog,QString::fromUtf8("Імпорт накладної"),
            QString::fromUtf8("Накладні (*.pdf *.png *.jpg *.jpeg *.webp *.xlsx *.xls *.csv);;Усі файли (*)"));
        if (path.isEmpty()) return;
        invoiceIdentity={}; invoicePath=path; invoiceLabel->setText(QFileInfo(path).fileName());
        invoiceLabel->setToolTip(path); invoiceLabel->show(); clearInvoice->setVisible(true);
        setImportBusy(true);
        InvoiceImport::ImportJobRequest request; request.filePath=path; request.sourceHint="spare_stock";
        importProgress->start(request);
    });
    buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Зберегти замовлення"));buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Скасувати"));
    for(auto* button:buttons->findChildren<QPushButton*>()) {button->setAutoDefault(false);button->setDefault(false);}
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(name->text().trimmed().isEmpty()){editParts(true);return;}
        if(auto* missing=clearOnEntry->firstIncomplete()) {
            QMessageBox::warning(&dialog,QString::fromUtf8("Заповніть поле"),QString::fromUtf8("Введіть або виберіть значення: %1").arg(missing->property("entryLabel").toString()));
            missing->setFocus();return;
        }
        normalizeVehicle();
        WorkshopSpareOrder input;input.brand=brand->text();input.model=model->text();input.plate=plate->text();input.vin=vin->text();
        input.masterId=master->currentData().toString();input.name=name->text();input.article=article->text();input.supplier=supplier->text();
        input.quantity=quantity->value();input.unitCost=price->value();input.orderedOn=ordered->date();
        if(!clearOnEntry->empty(expected) && expected->date()>expected->minimumDate()) input.expectedOn=expected->date();
        if(!clearOnEntry->empty(due) && due->date()>due->minimumDate()) input.dueOn=due->date();
        input.invoiceCatalogIdentity=invoiceIdentity;input.document=document->text();input.invoicePath=invoicePath;input.invoiceName=QFileInfo(invoicePath).fileName();QString error;
        QVector<WorkshopSpareStockService::BatchLine> batch;batch.append({input,firstTarget->currentData().toInt()});
        for(int r=0;r<extra->rowCount();++r){auto part=input;part.name=extra->item(r,0)->text();part.article=extra->item(r,1)->text();part.invoiceCatalogIdentity=extra->item(r,0)->data(Qt::UserRole).toJsonObject();if(extra->item(r,0)->data(Qt::UserRole+1).isValid())part.supplier=extra->item(r,0)->data(Qt::UserRole+1).toString();if(extra->item(r,0)->data(Qt::UserRole+2).isValid())part.expectedOn=DateFormats::parseDate(extra->item(r,0)->data(Qt::UserRole+2).toString());part.quantity=qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,2))->value();part.unitCost=qobject_cast<QDoubleSpinBox*>(extra->cellWidget(r,3))->value();batch.append({part,qobject_cast<QComboBox*>(extra->cellWidget(r,4))->currentData().toInt()});}
        if(!WorkshopSpareStockService::addBatch(batch,receiptState->currentIndex()==1?actual->date():QDate(),&error)){QMessageBox::warning(&dialog,QString::fromUtf8("Склад"),error);return;}
        dialog.accept();markDirty();refresh();emit requestRefreshAll();
    });
    auto* footer=new QHBoxLayout;
    invoiceRow->addStretch();invoiceRow->addWidget(importInvoice);footer->addWidget(total);footer->addStretch();footer->addWidget(buttons);
    header->ensurePolished();const int originalHeaderHeight=header->sizeHint().height();
    headerLayout->setContentsMargins(9,0,9,0);heading->setStyleSheet("font:600 18pt 'Segoe UI';padding:2px 8px;");
    close->setStyleSheet("background:transparent;border:0;font:20pt 'Segoe UI';padding:0px 14px;");
    header->setFixedHeight(qRound(originalHeaderHeight*0.7));
    root->addLayout(footer);dialog.setFixedWidth(qMin(960,dialog.screen()->availableGeometry().width()-40));dialog.adjustSize();
    const QRect available=dialog.screen()->availableGeometry();
    const QPoint initial=available.topLeft()+QPoint(qRound(available.width()*0.205),qRound(available.height()*0.135));
    dialog.move(qBound(available.left(),initial.x(),qMax(available.left(),available.right()-dialog.width()+1)),qBound(available.top(),initial.y(),qMax(available.top(),available.bottom()-dialog.height()+1)));dialog.exec();
}

void WorkshopInventoryPage::manageSpare(int row, bool deleteOnly)
{
    const auto& snapshot=m_spareModel->snapshot();if(row<0 || row>=snapshot.rows.size()) return;
    const auto id=snapshot.rows.at(row).at(0).toLongLong();QString error;
    WorkshopSpareOrder selected;for(const auto& item:WorkshopSpareStockService::orders(&error)) if(item.id==id) selected=item;
    if(!selected.id){QMessageBox::warning(this,QString::fromUtf8("Склад"),error);return;}
    InventoryEntryDialog dialog(this);
    auto* root=SpareReservePresentation::build(dialog,selected);
    SpareReservePresentation::addInvoice(dialog,root,selected);
    auto* orderMetrics=SpareReservePresentation::metricStrip(dialog,selected,false);
    auto* balanceMetrics=SpareReservePresentation::metricStrip(dialog,selected,true);
    root->addWidget(orderMetrics);
    root->addWidget(balanceMetrics);
    const auto receiptRows=WorkshopSpareStockService::receipts(id,&error);
    const auto remoteLinks=SupplierInvoiceLedger::activeLinks();
    QStringList remoteDocuments;
    QSet<QString> remotePdfPaths;
    for(const auto& receipt:receiptRows) {
        const auto linked=remoteLinks.value(PurchasePaymentService::key("inventory",receipt.purchaseId));
        if(!linked.number.isEmpty()) remoteDocuments.append(linked.number+QString::fromUtf8(" від ")+linked.date);
        for(const auto& document:linked.documents) {
            if(document.filePath.isEmpty() || remotePdfPaths.contains(document.filePath) || document.filePath==selected.invoicePath)continue;
            remotePdfPaths.insert(document.filePath);
            auto* pdfButton=new QPushButton(QString::fromUtf8("PDF Еліт: %1 від %2").arg(document.number,document.date),&dialog);
            pdfButton->setAutoDefault(false);root->addWidget(pdfButton);
            connect(pdfButton,&QPushButton::clicked,&dialog,[&,path=document.filePath]{
                if(!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                    QMessageBox::warning(&dialog,QString::fromUtf8("Накладна"),QString::fromUtf8("Файл PDF недоступний. Оновіть дані Еліт."));
            });
        }
    }
    remoteDocuments.removeDuplicates();
    if(!remoteDocuments.isEmpty()) {
        auto* linkedLabel=new QLabel(QString::fromUtf8("Прив’язані накладні Еліт: %1").arg(remoteDocuments.join(", ")),&dialog);
        linkedLabel->setWordWrap(true); root->addWidget(linkedLabel);
    }
    if(!receiptRows.isEmpty()) {
        QDate firstReceipt;
        for(const auto& receipt:receiptRows) if(receipt.date.isValid() && (!firstReceipt.isValid() || receipt.date<firstReceipt)) firstReceipt=receipt.date;
        auto* receivedInfo=new QLabel(QString::fromUtf8("Фактичне приймання на склад: %1. Днів від приймання: %2.")
            .arg(firstReceipt.toString("dd.MM.yyyy")).arg(qMax(0,int(firstReceipt.daysTo(QDate::currentDate())))),&dialog);
        receivedInfo->setObjectName("spareReceiptInfo");receivedInfo->setWordWrap(true);root->addWidget(receivedInfo);
    }
    auto* operationCard=new QWidget(&dialog);operationCard->setObjectName("reserveCard");
    auto* form=new QGridLayout(operationCard);
    auto* quantity=new QDoubleSpinBox(&dialog);quantity->setDecimals(0);quantity->setRange(1,selected.quantity);
    quantity->setValue(qMax(1.0,selected.quantity-selected.received));quantity->setObjectName("spareOperationQuantity");
    auto* date=new QDateEdit(QDate::currentDate(),&dialog);date->setCalendarPopup(true);date->setDisplayFormat("dd.MM.yyyy");
    auto* paid=new QDoubleSpinBox(&dialog);paid->setDecimals(2);paid->setRange(0,selected.quantity*selected.unitCost);
    auto* quantityLabel=new QLabel(QString::fromUtf8("Кількість"),&dialog);
    auto* dateLabel=new QLabel(QString::fromUtf8("Дата поставки"),&dialog);
    auto* paidLabel=new QLabel(QString::fromUtf8("Сплачено, грн"),&dialog);
    form->addWidget(quantityLabel,0,0);form->addWidget(quantity,1,0);
    form->addWidget(dateLabel,0,1);form->addWidget(date,1,1);form->addWidget(paidLabel,0,2);form->addWidget(paid,1,2);
    auto* operationHint=new QLabel(&dialog);operationHint->setWordWrap(true);form->addWidget(operationHint,2,0,1,3);
    auto* target=new QComboBox(&dialog);target->setObjectName("spareIssueOrder");target->setEditable(true);
    target->setInsertPolicy(QComboBox::NoInsert);target->addItem(QString(),0);
    target->lineEdit()->setObjectName("spareIssueOrderInput");
    target->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть або введіть номер наряду"));
    target->lineEdit()->installEventFilter(this);
    const auto eligible=WorkshopSpareStockService::eligibleOrders(selected,&error);
    for(const auto& order:eligible) target->addItem(order.second,order.first);
    root->addWidget(new QLabel(QString::fromUtf8("Наряд для імпорту запчастини"),&dialog));root->addWidget(target);
    auto* mode=new QTabBar(&dialog);mode->setObjectName("spareOperationMode");mode->setExpanding(true);mode->setFont(QFont("Segoe UI",10));
    mode->addTab(QString::fromUtf8("Приймання"));mode->addTab(QString::fromUtf8("Видача в наряд"));mode->addTab(QString::fromUtf8("Повернення"));
    mode->setTabIcon(0,style()->standardIcon(QStyle::SP_DialogSaveButton));mode->setTabIcon(1,style()->standardIcon(QStyle::SP_FileIcon));mode->setTabIcon(2,style()->standardIcon(QStyle::SP_ArrowBack));
    root->addWidget(mode);root->addWidget(operationCard);
    auto* actions=new QHBoxLayout;auto* receive=new QPushButton(QString::fromUtf8("Прийняти на склад"),&dialog);auto* issue=new QPushButton(QString::fromUtf8("Видати в наряд"),&dialog);
    const bool permitted=AccessControlService::currentUserHasPermission("inventory.purchase");
    receive->setObjectName("receiveSpareToStock");receive->setAutoDefault(false);
    if(selected.received>=selected.quantity) {
        receive->setText(QString::fromUtf8("Вже прийнято на склад"));
        receive->setToolTip(QString::fromUtf8("Уся замовлена кількість уже оприбуткована. Відлік почався з фактичної дати приймання."));
    }
    issue->setObjectName("issueSpareToOrder");
    receive->setEnabled(permitted && selected.received<selected.quantity);issue->setEnabled(permitted && selected.received>selected.issued
        && AccessControlService::currentUserHasPermission("parts.order"));
    receive->hide();issue->hide();
    auto finish=[&](bool ok){if(!ok){QMessageBox::warning(&dialog,QString::fromUtf8("Склад"),error);return;}dialog.accept();markDirty();refresh();emit requestRefreshAll();};
    auto* returnPart=new QPushButton(QString::fromUtf8("Повернути запчастину"),&dialog);
    returnPart->setObjectName("returnSpareToSupplier");returnPart->setAutoDefault(false);
    returnPart->setEnabled(AccessControlService::currentUserHasPermission("inventory.delete"));
    returnPart->hide();
    connect(returnPart,&QPushButton::clicked,&dialog,[&]{
        const auto answer=QMessageBox::question(&dialog,QString::fromUtf8("Повернути запчастину"),
            QString::fromUtf8("Повернути запчастину «%1» постачальнику та видалити резерв?").arg(selected.name),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
        if(answer!=QMessageBox::Yes)return;
        const qint64 paidBefore=qMax<qint64>(0,PurchasePaymentService::cents(selected.received*selected.unitCost)-selected.debtCents);
        QString settlement;
        if(paidBefore>0) {
            const auto refund=QMessageBox::question(&dialog,QString::fromUtf8("Повернення коштів"),
                QString::fromUtf8("Постачальник уже повернув %1 грн?").arg(moneyText(paidBefore/100.0)),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
            if(refund==QMessageBox::Yes)settlement="refund";
            else {
                const auto credit=QMessageBox::question(&dialog,QString::fromUtf8("Аванс постачальнику"),
                    QString::fromUtf8("Залишити %1 грн авансом у постачальника?").arg(moneyText(paidBefore/100.0)),
                    QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
                if(credit!=QMessageBox::Yes)return;
                settlement="credit";
            }
        }
        finish(WorkshopSpareStockService::returnReserve(selected,{},{},&error,settlement,paidBefore));
    });
    if (deleteOnly) { returnPart->click(); return; }
    connect(receive,&QPushButton::clicked,&dialog,[&]{finish(WorkshopSpareStockService::receive(selected,quantity->value(),date->date(),paid->value(),&error));});
    connect(issue,&QPushButton::clicked,&dialog,[&]{
        int number=0;
        const int exact=target->findText(target->currentText());
        if(exact>0) number=target->itemData(exact).toInt();
        else if(target->currentText()!=target->itemText(0) && !target->currentText().trimmed().isEmpty()) {
            for(int i=1;i<target->count();++i) {
                const QString display=target->itemText(i).section(QChar(0x00b7),0,0).remove(QString::fromUtf8("Наряд №")).trimmed();
                if(display==target->currentText().trimmed()) {
                    if (number) {QMessageBox::warning(&dialog,QString::fromUtf8("Склад"),QString::fromUtf8("Знайдено кілька нарядів із цим номером. Оберіть потрібний зі списку."));return;}
                    number=target->itemData(i).toInt();
                }
            }
            if(!number) {QMessageBox::warning(&dialog,QString::fromUtf8("Склад"),QString::fromUtf8("Не знайдено доступного відкритого наряду для видачі. Оберіть наряд зі списку."));return;}
        }
        if(number) {
            bool differentVehicle=false;QString destination;
            if (!WorkshopSpareStockService::checkIssueTarget(selected,number,&differentVehicle,&destination,&error)) {finish(false);return;}
            if (differentVehicle) {
                const auto answer=QMessageBox::question(&dialog,QString::fromUtf8("Видача для іншого автомобіля"),
                    QString::fromUtf8("Запчастина: %1\nРезерв: %2 %3 · %4\nВидати до: %5\nКількість: %6\n\nЗапчастина замовлена для іншого автомобіля. Ви впевнені, що хочете передати її разом із накладною в цей наряд?")
                        .arg(selected.name,selected.brand,selected.model,selected.plate,destination,quantityText(quantity->value())),
                    QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
                if (answer!=QMessageBox::Yes) return;
            }
            finish(WorkshopSpareStockService::issue(selected,quantity->value(),number,&error,differentVehicle));return;
        }
        QDialog choice(&dialog);choice.setObjectName("spareIssueChoice");choice.setWindowTitle(QString::fromUtf8("Додати запчастину в наряд"));
        auto* layout=new QVBoxLayout(&choice);
        auto* heading=new QLabel(choice.windowTitle(),&choice);heading->setFont(QFont("Times New Roman",14));
        heading->setStyleSheet("font-family:'Times New Roman';font-size:14pt;color:white;");layout->addWidget(heading);
        auto* buttons=new QHBoxLayout;auto* create=new QPushButton(QString::fromUtf8("Відкрити новий"),&choice);
        auto* existing=new QPushButton(QString::fromUtf8("Додати у відкритий"),&choice);
        create->setObjectName("createOrderForSpare");existing->setObjectName("selectOrderForSpare");
        buttons->addWidget(create);buttons->addWidget(existing);layout->addLayout(buttons);
        connect(create,&QPushButton::clicked,&choice,[&]{choice.done(2);});
        connect(existing,&QPushButton::clicked,&choice,[&]{choice.done(1);});
        const int action=choice.exec();
        if(action==1) {target->setFocus();target->lineEdit()->selectAll();target->showPopup();return;}
        if(action!=2) return;
        OrderDialog order(this);order.prefillFromSpareStock(selected,quantity->value());order.exec();
        dialog.accept();markDirty();refresh();emit requestRefreshAll();
    });
    for(const auto& receipt:receiptRows) {
        auto* payment=new QPushButton(QString::fromUtf8("Оплата поставки %1 · %2 шт.").arg(receipt.date.toString("dd.MM.yy"),quantityText(receipt.quantity)),&dialog);
        root->addWidget(payment);connect(payment,&QPushButton::clicked,&dialog,[&,purchaseId=receipt.purchaseId]{
            if(openPurchasePayment(&dialog,"inventory",purchaseId)){dialog.accept();markDirty();refresh();emit requestRefreshAll();}});
    }
    auto* close=new QPushButton(QString::fromUtf8("Закрити"),&dialog);close->setAutoDefault(false);
    connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
    auto* primary=new QPushButton(&dialog);primary->setObjectName("reservePrimary");primary->setAutoDefault(false);
    auto updateMode=[&]{
        const int current=mode->currentIndex();auto* action=current==0?receive:current==1?issue:returnPart;
        const bool showBalance=current==1 && SpareReservePresentation::hasIssueTarget(*target);
        orderMetrics->setVisible(!showBalance);
        balanceMetrics->setVisible(showBalance);
        primary->setText(action->text());primary->setEnabled(action->isEnabled());
        quantity->setVisible(current!=2);quantityLabel->setVisible(current!=2);
        date->setVisible(current==0);dateLabel->setVisible(current==0);paid->setVisible(current==0);paidLabel->setVisible(current==0);
        operationHint->setText(current==0?QString::fromUtf8("Буде прийнято на склад: %1 шт.").arg(quantityText(quantity->value())):
            current==1?QString::fromUtf8("Буде видано %1 шт. разом із прив’язаною накладною. Доступно: %2 шт.").arg(quantityText(quantity->value()),quantityText(selected.received-selected.issued)):
            QString::fromUtf8("Повернення постачальнику потребує підтвердження. Видані позиції спочатку поверніть із наряду."));
    };
    connect(target,&QComboBox::currentTextChanged,&dialog,[&]{updateMode();});
    connect(mode,&QTabBar::currentChanged,&dialog,[&]{quantity->setValue(qMax(1.0,mode->currentIndex()==1?selected.received-selected.issued:selected.quantity-selected.received));updateMode();});
    connect(quantity,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&]{updateMode();});
    connect(primary,&QPushButton::clicked,&dialog,[&]{(mode->currentIndex()==0?receive:mode->currentIndex()==1?issue:returnPart)->click();});
    if(!receive->isEnabled() && issue->isEnabled())mode->setCurrentIndex(1);updateMode();
    actions->addStretch();actions->addWidget(primary);actions->addWidget(close);root->addLayout(actions);
    const QRect available=dialog.screen()->availableGeometry();dialog.resize(qMin(900,available.width()-40),dialog.sizeHint().height());
    dialog.move(available.center()-dialog.rect().center());dialog.exec();
}

void WorkshopInventoryPage::deleteSelectedPurchase()
{
    deletePurchaseById(selectedPurchaseId());
}

void WorkshopInventoryPage::deletePurchaseById(qint64 purchaseId)
{
    if (!AccessControlService::currentUserHasPermission( "inventory.delete")) return;
    if (purchaseId <= 0) {
        QMessageBox::information(this, QString::fromUtf8("Склад СТО"), QString::fromUtf8("Оберіть запис придбання."));
        return;
    }
    if (QMessageBox::question(this, QString::fromUtf8("Видалення придбання"),
            QString::fromUtf8("Видалити вибране придбання матеріалу?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString errorText;
    if (!WorkshopInventoryService::removePurchase(purchaseId, &errorText)) {
        QMessageBox::warning(this, QString::fromUtf8("Склад СТО"), errorText);
        return;
    }
    m_dirty = true;
    refreshModels();
    emit requestRefreshAll();
}

void WorkshopInventoryPage::showPeriodDetails(int row)
{
    if (!m_periodModel) return;
    const TabularReadModel& snapshot = m_periodModel->snapshot();
    if (row < 0 || row >= snapshot.rows.size()) return;
    const QString material = cellText(snapshot, row, 3);
    const qint64 purchaseId = snapshot.rows.at(row).at(10).toLongLong();
    const auto invoice=SupplierInvoiceLedger::activeLinks().value(PurchasePaymentService::key("inventory",purchaseId));
    showInventoryDetails(this, QString::fromUtf8("Деталі складської операції"), {
        {QString::fromUtf8("Накладна Еліт"), invoice.number.isEmpty()?QString::fromUtf8("Не прив’язано"):invoice.number+" / "+invoice.date},
        {QString::fromUtf8("Дата"), cellText(snapshot, row, 0)},
        {QString::fromUtf8("Операція"), cellText(snapshot, row, 1)},
        {QString::fromUtf8("Наряд"), cellText(snapshot, row, 2, QString::fromUtf8("Не прив'язано"))},
        {QString::fromUtf8("Матеріал"), cellText(snapshot, row, 3)},
        {QString::fromUtf8("Одиниця"), cellText(snapshot, row, 4)},
        {QString::fromUtf8("Надійшло"), cellText(snapshot, row, 5)},
        {QString::fromUtf8("Витрачено"), cellText(snapshot, row, 6)},
        {QString::fromUtf8("Залишок після операції"), cellText(snapshot, row, 7)},
        {QString::fromUtf8("Ціна"), cellText(snapshot, row, 8)},
        {QString::fromUtf8("Сума"), cellText(snapshot, row, 9)}
    }, [this, material, purchaseId](QWidget* parent) { return payMaterial(parent, material, purchaseId); });
}

bool WorkshopInventoryPage::payMaterial(QWidget* parent, const QString& material, qint64 purchaseId)
{
    if (!purchaseId) {
        const auto snapshot = WorkshopInventoryService::buildSnapshot(QDate(1900,1,1), QDate::currentDate());
        if (!snapshot.errorText.isEmpty()) {
            QMessageBox::warning(parent, QString::fromUtf8("Оплата матеріалу"), snapshot.errorText); return false;
        }
        QStringList choices; QList<qint64> ids;
        for (const auto& purchase : snapshot.purchases) {
            if (purchase.materialKey != WorkshopInventoryService::normalizedMaterialKey(material)) continue;
            const double debt = purchase.quantity * purchase.unitCost - purchase.paidAmount;
            if (debt < 0.005) continue;
            ids.append(purchase.id);
            choices.append(QString::fromUtf8("%1 | %2 | %3 грн | №%4")
                .arg(purchase.purchasedOn.toString("dd.MM.yyyy"), purchase.supplier, moneyText(debt), QString::number(purchase.id)));
        }
        if (ids.isEmpty()) { QMessageBox::information(parent, QString::fromUtf8("Оплата матеріалу"), QString::fromUtf8("Немає неоплачених придбань цього матеріалу.")); return false; }
        bool accepted = true;
        int index = 0;
        if (ids.size() > 1) {
            const QString choice = QInputDialog::getItem(parent, QString::fromUtf8("Оплата матеріалу"), QString::fromUtf8("Оберіть надходження"), choices, 0, false, &accepted);
            index = choices.indexOf(choice);
        }
        if (!accepted || index < 0) return false;
        purchaseId = ids.at(index);
    }
    if (!openPurchasePayment(parent, QStringLiteral("inventory"), purchaseId)) return false;
    markDirty(); refresh(); emit requestRefreshAll(); return true;
}

void WorkshopInventoryPage::focusPurchase(qint64 purchaseId)
{
    const auto debt=PurchasePaymentService::detail("inventory",purchaseId,{});
    if(!debt.id) return;
    const QSignalBlocker fromBlocked(m_from), toBlocked(m_to);
    const auto date=PartsQueryService::parseBusinessDate(debt.date);
    if(date.isValid()) m_from->setDate(qMin(m_from->date(),date));
    m_to->setDate(qMax(QDate::currentDate(),date));
    m_dirty=true;
    refreshModels();
    m_refreshDebounce->stop();
    for(const auto& spare:WorkshopSpareStockService::orders()) {
        for(const auto& receipt:WorkshopSpareStockService::receipts(spare.id)) {
            if(receipt.purchaseId!=purchaseId) continue;
            m_stockTabs->setCurrentIndex(0);
            m_refreshDebounce->stop();
            m_dirty=false;
            const auto& rows=m_spareModel->snapshot().rows;
            for(int row=0;row<rows.size();++row) if(rows[row][0].toLongLong()==spare.id) {
                m_spareTable->selectRow(row);
                m_spareTable->scrollTo(m_spareModel->index(row,1));
                return;
            }
        }
    }
    m_stockTabs->setCurrentIndex(1);
    m_refreshDebounce->stop();
    m_dirty=false;
    const auto& rows=m_periodModel->snapshot().rows;
    for(int row=0;row<rows.size();++row) if(rows[row][10].toLongLong()==purchaseId) {
        m_periodTable->selectRow(row);
        m_periodTable->scrollTo(m_periodModel->index(row,0));
        break;
    }
}

void WorkshopInventoryPage::openPurchase(qint64 purchaseId)
{
    const auto debt=PurchasePaymentService::detail("inventory",purchaseId,{});
    if (!debt.id) return;
    const auto date=PartsQueryService::parseBusinessDate(debt.date);
    const QSignalBlocker fromBlocked(m_from), toBlocked(m_to);
    m_from->setDate(date.isValid()?date:QDate(1900,1,1));
    m_to->setDate(qMax(QDate::currentDate(),m_from->date()));
    m_dirty=true;
    refreshModels();
    m_refreshDebounce->stop();
    const auto& rows=m_purchasesModel->snapshot().rows;
    for(int row=0;row<rows.size();++row) {
        if(rows[row][0].toLongLong()!=purchaseId) continue;
        m_purchasesTable->selectRow(row);
        m_purchasesTable->scrollTo(m_purchasesModel->index(row,0));
        break;
    }
    auto* dialog=findChild<QDialog*>("workshopInventoryPurchasesTab");
    dialog->findChild<QTabWidget*>("inventoryBalanceTabs")->setCurrentIndex(1);
    dialog->exec();
}

void WorkshopInventoryPage::showPurchaseDetails(int row)
{
    if (!m_purchasesModel) return;
    const TabularReadModel& snapshot = m_purchasesModel->snapshot();
    if (row < 0 || row >= snapshot.rows.size()) return;
    const auto id = snapshot.rows[row][0].toLongLong();
    if (openPurchasePayment(this, QStringLiteral("inventory"), id)) {
        m_dirty = true; refreshModels(); emit requestRefreshAll();
    }
}

void WorkshopInventoryPage::showExpenseDetails(int row)
{
    if (!m_expensesModel) return;
    const TabularReadModel& snapshot = m_expensesModel->snapshot();
    showInventoryDetails(this, QString::fromUtf8("Деталі витрати матеріалу"), {
        {QString::fromUtf8("Дата витрати"), cellText(snapshot, row, 0)},
        {QString::fromUtf8("Наряд"), cellText(snapshot, row, 1, QString::fromUtf8("Не прив'язано"))},
        {QString::fromUtf8("Авто / клієнт"), cellText(snapshot, row, 2, QString::fromUtf8("Не вказано"))},
        {QString::fromUtf8("Матеріал"), cellText(snapshot, row, 3)},
        {QString::fromUtf8("Одиниця"), cellText(snapshot, row, 4)},
        {QString::fromUtf8("Кількість"), cellText(snapshot, row, 5)},
        {QString::fromUtf8("Собівартість за одиницю"), cellText(snapshot, row, 6)},
        {QString::fromUtf8("Сума"), cellText(snapshot, row, 7)}
    });
}

void WorkshopInventoryPage::openExpenseOrder(int row)
{
    if (!m_expensesModel) return;
    const TabularReadModel& snapshot = m_expensesModel->snapshot();
    bool validNumber = false;
    const int orderNumber = cellText(snapshot, row, 1, QString()).toInt(&validNumber);
    if (!validNumber || orderNumber <= 0) {
        showExpenseDetails(row);
        return;
    }

    const CurrentOrdersCoordinator::DialogRunResult result =
        CurrentOrdersCoordinator::openExistingOrderEditor(this, orderNumber, QString());
    if (!result.launched) {
        QMessageBox::warning(this, QString::fromUtf8("Не вдалося відкрити наряд"),
                             result.errorText.trimmed().isEmpty()
                                 ? QString::fromUtf8("Наряд №%1 недоступний.").arg(orderNumber)
                                 : result.errorText);
        return;
    }

    m_dirty = true;
    refresh();
}

void WorkshopInventoryPage::showBalanceDetails(int row)
{
    if (!m_balanceModel) return;
    const TabularReadModel& snapshot = m_balanceModel->snapshot();
    showInventoryDetails(this, QString::fromUtf8("Деталі балансу матеріалу"), {
        {QString::fromUtf8("Період"), QString::fromUtf8("%1 - %2").arg(m_from->date().toString(QStringLiteral("dd.MM.yy")), m_to->date().toString(QStringLiteral("dd.MM.yy")))},
        {QString::fromUtf8("Матеріал"), m_balanceMaterial->currentText()},
        {QString::fromUtf8("Дата"), cellText(snapshot, row, 0)},
        {QString::fromUtf8("Наряд"), cellText(snapshot, row, 1)},
        {QString::fromUtf8("Одиниця"), cellText(snapshot, row, 2)},
        {QString::fromUtf8("Витрачено"), cellText(snapshot, row, 3)},
        {QString::fromUtf8("Залишок"), cellText(snapshot, row, 4)},
        {QString::fromUtf8("Вартість витрати"), cellText(snapshot, row, 5)},
        {QString::fromUtf8("Вартість залишку"), cellText(snapshot, row, 6)}
    });
}

void WorkshopInventoryPage::setSummary(const QString& text, bool error)
{
    if (!m_summary) return;
    m_summary->setText(text);
    m_summary->setVisible(error);
    m_summary->setStyleSheet(error
        ? QStringLiteral("color: #ffb4b4; font-weight: 700;")
        : QStringLiteral("color: #f4f7fb; font-weight: 600;"));
}
