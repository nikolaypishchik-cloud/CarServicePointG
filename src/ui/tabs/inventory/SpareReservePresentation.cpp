#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>
#include <QStyle>
#include "ui/tabs/inventory/SpareReservePresentation.h"
#include "ui/tabs/inventory/InventoryEntryDialog.h"
#include "core/services/WorkshopSpareStockService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QIcon>
#include <QComboBox>
#include <QFrame>
#include "core/util/FormatUtils.h"
namespace SpareReservePresentation {
QVBoxLayout* build(InventoryEntryDialog& dialog, const WorkshopSpareOrder& selected) {
    dialog.setObjectName("spareReserveDialog");
    dialog.setWindowTitle(QString::fromUtf8("Резерв запчастини"));
    dialog.setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    dialog.setFont(QFont("Segoe UI",10));
    dialog.setStyleSheet(QStringLiteral(
        "QDialog{background:#243342;border:1px solid #71808c;border-radius:10px;}"
        "QLabel{color:#eef3fa;background:transparent;border:0;font-family:'Segoe UI';}"
        "QWidget#reserveHeader{background:#11191f;border-bottom:1px solid #657482;}"
        "QWidget#reserveCard{background:#263747;border:1px solid #536675;border-radius:8px;}"
        "QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox{background:white;color:#101820;border:1px solid #a8b7c4;border-radius:5px;padding:5px;font-family:'Segoe UI';}"
        "QComboBox QAbstractItemView{background:white;color:black;selection-background-color:#d6e5f6;}"
        "QPushButton{color:#edf3fc;border:1px solid #8596a8;border-radius:7px;padding:8px 12px;background:#344556;font-family:'Segoe UI';}"
        "QPushButton:disabled{color:#8c9aa8;}"
        "QPushButton#reservePrimary{color:black;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #e1ecfc,stop:1 #9cb2cf);font-weight:600;}"
        "QTabBar::tab{background:#263747;color:#dbe6f7;padding:9px 22px;border:1px solid #536675;}"
        "QTabBar::tab:selected{background:#c6d6ec;color:black;}"));
    auto* root=new QVBoxLayout(&dialog);root->setContentsMargins(12,8,12,12);root->setSpacing(8);
    auto* header=new QWidget(&dialog);header->setObjectName("reserveHeader");
    auto* headerLayout=new QHBoxLayout(header);
    auto* logo=new QLabel(header);logo->setPixmap(dialog.windowIcon().pixmap(28,28));headerLayout->addWidget(logo);
    auto* heading=new QLabel(dialog.windowTitle(),header);heading->setStyleSheet("font:600 13pt 'Segoe UI';");headerLayout->addWidget(heading,1);
    auto* headerClose=new QPushButton(QString::fromUtf8("×"),header);headerClose->setAutoDefault(false);headerClose->setStyleSheet("background:transparent;border:0;font:20pt 'Segoe UI';padding:0 8px;");headerLayout->addWidget(headerClose);
    QObject::connect(headerClose,&QPushButton::clicked,&dialog,&QDialog::reject);root->addWidget(header);dialog.enableHeaderDrag(header);
    auto* title=new QLabel(selected.name,&dialog);title->setWordWrap(true);title->setStyleSheet("font:600 23pt 'Segoe UI';");root->addWidget(title);
    auto* vehicleRow=new QHBoxLayout;
    auto* vehicle=new QLabel(QString("%1 %2 · %3").arg(selected.brand,selected.model,selected.plate),&dialog);vehicle->setWordWrap(true);vehicle->setStyleSheet("font:600 16pt 'Segoe UI';");vehicleRow->addWidget(vehicle,1);
    auto* badge=new QLabel(selected.received<selected.quantity?QString::fromUtf8("Очікується"):selected.issued>=selected.received?QString::fromUtf8("Видано"):QString::fromUtf8("На складі"),&dialog);
    badge->setStyleSheet("color:#ffdc58;border:1px solid #ffdc58;border-radius:14px;padding:5px 12px;");vehicleRow->addWidget(badge);root->addLayout(vehicleRow);
    auto* info=new QWidget(&dialog);info->setObjectName("reserveCard");auto* infoRow=new QHBoxLayout(info);
    for(const auto& pair:QList<QPair<QString,QString>>{{QString::fromUtf8("Замовив"),selected.masterName},{QString::fromUtf8("Замовлено"),selected.orderedOn.toString("dd.MM.yyyy")},{QString::fromUtf8("Очікується"),selected.expectedOn.toString("dd.MM.yyyy")}}) {
        auto* column=new QVBoxLayout;column->addWidget(new QLabel(pair.first,info));auto* value=new QLabel(pair.second,info);value->setWordWrap(true);value->setStyleSheet("font-weight:600;font-size:12pt;");column->addWidget(value);infoRow->addLayout(column,1);
    }
    root->addWidget(info);
    if(!selected.vin.isEmpty() || !selected.shelf.isEmpty())root->addWidget(new QLabel(QString::fromUtf8("VIN: %1 · Комірка: %2").arg(selected.vin,selected.shelf),&dialog));
    return root;
}
bool hasIssueTarget(const QComboBox& target) {
    const auto input=target.currentText().trimmed();
    if (input.isEmpty()) return false;
    if (target.findText(input)>0) return true;
    if (input.toInt()<=0) return false;
    int matches=0;
    for (int i=1; i<target.count(); ++i) {
        const auto number=target.itemText(i).section(QChar(0x00b7),0,0).remove(QStringLiteral("Наряд №")).trimmed();
        if (number==input) ++matches;
    }
    return matches==1;
}
QWidget* metricStrip(InventoryEntryDialog& dialog, const WorkshopSpareOrder& selected, bool stockBalance) {
    auto* strip = new QWidget(&dialog);
    strip->setObjectName(stockBalance ? "reserveBalanceStrip" : "reserveOrderStrip");
    strip->setStyleSheet(QStringLiteral(
        "QWidget#reserveBalanceStrip,QWidget#reserveOrderStrip{background:#08090a;border-radius:5px;}"
        "QFrame#reserveMetric{border:1px solid #76818a;border-radius:5px;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #333638,stop:0.5 #161819,stop:1 #08090a);}"
        "QLabel{background:transparent;color:#ffffff;border:0;}"
        "QLabel#reserveMetricValue{font-size:12pt;font-weight:bold;}"));
    auto* row = new QHBoxLayout(strip);
    row->setContentsMargins(3,3,3,3);
    row->setSpacing(4);
    auto add = [&](const QString& caption, const QString& value) {
        auto* tile = new QFrame(strip);
        tile->setObjectName("reserveMetric");
        auto* column = new QVBoxLayout(tile);
        column->setContentsMargins(4,3,4,3);
        column->setSpacing(0);
        auto* label = new QLabel(caption,tile);
        auto* amount = new QLabel(value,tile);
        amount->setObjectName("reserveMetricValue");
        label->setAlignment(Qt::AlignCenter);
        amount->setAlignment(Qt::AlignCenter);
        column->addWidget(label);
        column->addWidget(amount);
        row->addWidget(tile,1);
    };
    const auto units = [](double n) { return FormatUtils::smartQty(n,3) + QStringLiteral(" шт."); };
    if (stockBalance) {
        // Every unissued unit of this vehicle-specific order belongs to its reservation.
        const auto reserved = selected.received - selected.issued;
        add(QStringLiteral("Очікується"),units(selected.quantity-selected.received));
        add(QStringLiteral("На складі"),units(reserved));
        add(QStringLiteral("У резерві"),units(reserved));
        add(QStringLiteral("Вільно"),units(0));
    } else {
        add(QStringLiteral("Замовлено"),units(selected.quantity));
        add(QStringLiteral("Отримано"),units(selected.received));
        add(QStringLiteral("Видано"),units(selected.issued));
        add(QStringLiteral("Борг"),FormatUtils::money(selected.debtCents/100.0,2)+QStringLiteral(" грн"));
    }
    return strip;
}
void addInvoice(InventoryEntryDialog& dialog, QVBoxLayout* root, const WorkshopSpareOrder& selected) {
    if (!selected.invoicePath.isEmpty() || !selected.document.isEmpty()) {
        auto* invoiceCard=new QWidget(&dialog);invoiceCard->setObjectName("reserveCard");auto* invoiceLayout=new QHBoxLayout(invoiceCard);
        auto* icon=new QLabel(invoiceCard);icon->setPixmap(dialog.style()->standardIcon(QStyle::SP_FileIcon).pixmap(30,30));invoiceLayout->addWidget(icon);
        auto* description=new QLabel(QString("%1\n%2").arg(selected.invoiceName.isEmpty()?selected.document:selected.invoiceName,selected.document),invoiceCard);
        description->setWordWrap(true);description->setStyleSheet("font:600 11pt 'Segoe UI';");invoiceLayout->addWidget(description,1);
        auto* invoice = new QPushButton(QString::fromUtf8("Відкрити ↗"), invoiceCard);
        invoice->setObjectName("openSpareInvoice"); invoice->setAutoDefault(false);invoice->setEnabled(!selected.invoicePath.isEmpty());invoiceLayout->addWidget(invoice);root->addWidget(invoiceCard);
        QObject::connect(invoice, &QPushButton::clicked, &dialog, [&, path = selected.invoicePath] {
            if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                QMessageBox::warning(&dialog, QString::fromUtf8("Накладна"), QString::fromUtf8("Не вдалося відкрити файл накладної."));
        });
    }
}
}
