#include "ui/tabs/settings/InvoiceRecognitionSettingsPage.h"
#include "ui/common/dialogs/InvoiceColumnMappingEditor.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"
#include "core/import/InvoiceRecognitionProfiles.h"
#include "core/util/FormatUtils.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QShowEvent>
#include <functional>

namespace {
class ProfilesPage : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void()> reload;
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (reload) reload();
    }
};
}
QWidget* buildInvoiceRecognitionSettingsPage(QWidget* parent) {
    using namespace InvoiceImport;
    auto* page = new ProfilesPage(parent);
    page->setObjectName("invoiceRecognitionSettings");
    auto* layout = new QVBoxLayout(page);
    auto* title = new QLabel(QString::fromUtf8("Розпізнавання накладних"), page);
    layout->addWidget(title);
    auto* toolbar = new QHBoxLayout;
    auto* add = new QPushButton(QString::fromUtf8("Налаштувати за файлом"), page);
    auto* edit = new QPushButton(QString::fromUtf8("Змінити колонки"), page);
    edit->setObjectName("editInvoiceRecognitionProfile");
    auto* remove = new QPushButton(QString::fromUtf8("Видалити шаблон"), page);
    toolbar->addWidget(add);
    toolbar->addWidget(edit);
    toolbar->addWidget(remove);
    toolbar->addStretch();
    layout->addLayout(toolbar);
    auto* table = new QTableWidget(0, 3, page);
    table->setObjectName("invoiceRecognitionProfiles");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Магазин"), QString::fromUtf8("Колонки формату"), QString::fromUtf8("Оновлено")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);
    auto* status = new QLabel(page);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto refresh = [=]() {
        const auto profiles = InvoiceRecognitionProfiles::list();
        table->setRowCount(profiles.size());
        for (int i = 0; i < profiles.size(); ++i) {
            const auto& p = profiles[i];
            auto* shop = new QTableWidgetItem(p.mapping.supplierDisplayName);
            shop->setData(Qt::UserRole, p.key);
            table->setItem(i, 0, shop);
            table->setItem(i, 1, new QTableWidgetItem(p.sampleRows.value(p.mapping.headerRow).join(QStringLiteral(" | "))));
            table->setItem(i, 2, new QTableWidgetItem(p.updatedAt));
        }
        table->clearSelection();
        table->setCurrentCell(-1, -1);
        edit->setEnabled(false);
        remove->setEnabled(false);
        status->setText(profiles.isEmpty() ? QString::fromUtf8("Збережених шаблонів немає.") : QString());
    };
    page->reload = refresh;
    auto approve = [=](const QList<QStringList>& rows, const InvoiceColumnMappingPreview& preview, const QString& file) {
        InvoiceColumnMappingPreview confirmed;
        if (!editInvoiceColumnMapping(page, rows, preview, file, &confirmed)) return;
        WorkPartsDialogExcelSupport::runConfirmedVatMappingParseAsync(page, rows, confirmed, QString(),
            [](const QString& s) { return s; },
            [](QString s, bool* ok) { s.remove(QChar(0x00a0)); s.remove(' '); s.replace(',', '.'); return s.toDouble(ok); },
            [](double v) { return FormatUtils::money(v, 2); },
            [=](const QList<ImportedPartDraft>& drafts, const QString& error) {
                if (!error.isEmpty() || drafts.isEmpty()) {
                    QMessageBox::warning(page, QString::fromUtf8("Розпізнавання накладних"), error);
                    return;
                }
                refresh();
                status->setText(QString::fromUtf8("Шаблон збережено. Перевірено позицій: %1. Товари не імпортовано.").arg(drafts.size()));
            });
    };
    QObject::connect(table, &QTableWidget::itemSelectionChanged, page, [=]() {
        edit->setEnabled(table->currentRow() >= 0);
        remove->setEnabled(table->currentRow() >= 0);
    });
    QObject::connect(edit, &QPushButton::clicked, page, [=]() {
        const auto* item = table->item(table->currentRow(), 0);
        if (!item) return;
        const QString key = item->data(Qt::UserRole).toString();
        for (const auto& p : InvoiceRecognitionProfiles::list()) {
            if (p.key == key) { approve(p.sampleRows, p.mapping, QString()); break; }
        }
    });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, page, [=](int, int) { edit->click(); });
    QObject::connect(remove, &QPushButton::clicked, page, [=]() {
        const auto* item = table->item(table->currentRow(), 0);
        if (!item) return;
        if (QMessageBox::question(page, QString::fromUtf8("Видалити шаблон"),
            QString::fromUtf8("Наступна накладна цього формату вимагатиме ручного підтвердження. Продовжити?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!InvoiceRecognitionProfiles::remove(item->data(Qt::UserRole).toString(), &error)) QMessageBox::warning(page, title->text(), error);
        refresh();
    });
    QObject::connect(add, &QPushButton::clicked, page, [=]() {
        const QString file = QFileDialog::getOpenFileName(page, QString::fromUtf8("Приклад накладної"), QString(),
            QString::fromUtf8("Накладні (*.pdf *.xlsx *.xls *.csv *.txt)"));
        if (file.isEmpty()) return;
        WorkPartsDialogExcelSupport::runExcelReadAsync(page, QString::fromUtf8("Читання накладної..."), file,
            [=](const ExcelReadResult& data, const QString& error) {
                if (error == WorkPartsDialogExcelSupport::cancelledReadMarker()) return;
                if (!error.isEmpty()) { QMessageBox::warning(page, title->text(), error); return; }
                if (data.columnMappingPreview.templateKey.isEmpty()) {
                    QMessageBox::warning(page, title->text(), QString::fromUtf8("Не визначено постачальника або серію і заголовки таблиці. Шаблон неможливо безпечно прив'язати до цього формату."));
                    return;
                }
                approve(data.rows, data.columnMappingPreview, file);
            }, false);
    });
    refresh();
    return page;
}
