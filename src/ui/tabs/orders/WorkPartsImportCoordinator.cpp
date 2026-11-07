#include "core/util/DateFormats.h"
#include "ui/common/dialogs/InvoiceReviewDialog.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "core/services/orders/PartMarkupPolicyService.h"

#include "ui/tabs/orders/PartImportDraftUiSupport.h"
#include "ui/tabs/orders/WorkPartsImportedRowsViewModel.h"
#include "core/perf/PerfWatchdog.h"
#include "core/import/UniversalInvoiceColumnDetector.h"
#include "ui/common/dialogs/InvoiceDialogSupport.h"
#include <QScrollArea>
#include <QAbstractTableModel>
#include <QLineEdit>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTableView>
#include <QBrush>
#include <QColor>

namespace {

static QTableWidgetItem* makeImportedPartIndexItem()
{
    auto* item = new QTableWidgetItem(QString());
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

static QTableWidgetItem* makeImportedPartTextItem()
{
    return new QTableWidgetItem(QString());
}

static QString normalizedInvoiceMappingHeader(QString text)
{
    return text.trimmed().toLower()
        .replace(QChar(0x00A0), QLatin1Char(' '))
        .replace(QString::fromUtf8("ндс"), QString::fromUtf8("пдв"))
        .replace(QString::fromUtf8("vat"), QString::fromUtf8("пдв"))
        .replace(QString::fromUtf8("tax"), QString::fromUtf8("пдв"));
}

static bool invoiceMappingHeaderHasVatExcludedMarker(const QString& normalized)
{
    const QString text = normalizedInvoiceMappingHeader(normalized);
    return text.contains(QString::fromUtf8("без пдв")) ||
           text.contains(QString::fromUtf8("без  пдв")) ||
           text.contains(QStringLiteral("excl")) ||
           text.contains(QStringLiteral("without"));
}

static bool invoiceMappingHeaderHasVatIncludedMarker(const QString& normalized)
{
    const QString text = normalizedInvoiceMappingHeader(normalized);
    if (invoiceMappingHeaderHasVatExcludedMarker(text)) return false;
    return text.contains(QString::fromUtf8("з пдв")) ||
           text.contains(QString::fromUtf8("с пдв")) ||
           text.contains(QString::fromUtf8("із пдв")) ||
           text.contains(QString::fromUtf8("включ")) ||
           text.contains(QStringLiteral("incl")) ||
           text.contains(QStringLiteral("included")) ||
           text.contains(QStringLiteral("including")) ||
           text.contains(QStringLiteral("price vat")) ||
           text.contains(QStringLiteral("vat price"));
}

static int invoicePreviewColumnCount(const QList<QStringList>& rows)
{
    int columns = 0;
    for (const QStringList& row : rows) columns = qMax(columns, row.size());
    return columns;
}

static QString invoicePreviewCell(const QList<QStringList>& rows, int row, int col)
{
    if (row < 0 || row >= rows.size() || col < 0 || col >= rows.at(row).size()) return QString();
    return rows.at(row).value(col).trimmed();
}

static QString invoicePreviewColumnLabel(const QList<QStringList>& rows, int headerRow, int col)
{
    QString header = invoicePreviewCell(rows, headerRow, col);
    if (header.isEmpty()) {
        for (int row = 0; row < qMin(rows.size(), 8); ++row) {
            header = invoicePreviewCell(rows, row, col);
            if (!header.isEmpty()) break;
        }
    }
    const int confidence = 0;
    Q_UNUSED(confidence);
    return header.isEmpty()
        ? QString::fromUtf8("Колонка %1").arg(col + 1)
        : QString::fromUtf8("%1 — %2").arg(col + 1).arg(header.left(80));
}

class WorkPartsInvoiceMappingPreviewModel final : public QAbstractTableModel {
public:
    WorkPartsInvoiceMappingPreviewModel(QList<QStringList> rows,
                                        InvoiceImport::InvoiceColumnMappingPreview preview,
                                        QObject* parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(std::move(rows))
        , m_preview(std::move(preview))
    {
        m_firstRow = qMax(0, m_preview.headerRow >= 0 ? m_preview.headerRow : 0);
        m_sampleRows = qMin(14, qMax(0, m_rows.size() - m_firstRow));
        for (const QStringList& row : m_rows) m_columnCount = qMax(m_columnCount, row.size());
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_sampleRows;
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal) {
            const int confidence = m_preview.physicalColumnConfidence.value(section, -1);
            if (role == Qt::DisplayRole) {
                return confidence >= 0
                    ? QString::fromUtf8("%1\n%2%").arg(section + 1).arg(confidence)
                    : QString::number(section + 1);
            }
            if (role == Qt::ToolTipRole) {
                QStringList details;
                details << QString::fromUtf8("Колонка %1").arg(section + 1);
                if (confidence >= 0) details << QString::fromUtf8("Confidence: %1%").arg(confidence);
                if (m_preview.physicalColumnWarnings.contains(section)) {
                    details << m_preview.physicalColumnWarnings.value(section);
                }
                return details.join(QStringLiteral("\n"));
            }
        }
        if (orientation == Qt::Vertical && role == Qt::DisplayRole) return QString::number(section + 1);
        return {};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid()) return {};
        const int sourceRow = m_firstRow + index.row();
        if (sourceRow < 0 || sourceRow >= m_rows.size()) return {};
        const int col = index.column();
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            const QStringList& row = m_rows.at(sourceRow);
            return (col >= 0 && col < row.size()) ? row.at(col).trimmed() : QString();
        }
        if (role == Qt::BackgroundRole) {
            if (col == m_preview.priceWithVatColumn) {
                return QBrush(m_preview.priceWithVatConfirmed ? QColor(210, 244, 220) : QColor(255, 238, 180));
            }
            if (m_preview.physicalColumnWarnings.contains(col) ||
                (m_preview.physicalColumnConfidence.contains(col) && m_preview.physicalColumnConfidence.value(col) < 60)) {
                return QBrush(QColor(255, 238, 180));
            }
            if (sourceRow == m_preview.headerRow) return QBrush(QColor(216, 228, 255));
        }
        if (role == Qt::ToolTipRole) {
            if (m_preview.physicalColumnWarnings.contains(col)) return m_preview.physicalColumnWarnings.value(col);
            if (col == m_preview.priceWithVatColumn) {
                return m_preview.priceWithVatConfirmed
                    ? QString::fromUtf8("Розпізнана колонка 'Ціна з ПДВ'")
                    : QString::fromUtf8("Сумнівна колонка ціни: потрібне ручне підтвердження");
            }
        }
        return {};
    }

private:
    QList<QStringList> m_rows;
    InvoiceImport::InvoiceColumnMappingPreview m_preview;
    int m_firstRow = 0;
    int m_sampleRows = 0;
    int m_columnCount = 0;
};

class WorkPartsInvoiceColumnMappingDialog final : public QDialog {
public:
    WorkPartsInvoiceColumnMappingDialog(const QList<QStringList>& rows,
                                        const InvoiceImport::InvoiceColumnMappingPreview& preview,
                                        const QString& sourceFilePath,
                                        QWidget* parent = nullptr)
        : QDialog(parent)
        , m_rows(rows)
        , m_preview(preview)
        , m_confirmed(preview)
    {
        setWindowTitle(QString::fromUtf8("Preview імпорту накладної"));
        InvoiceDialogSupport::fitOnOpen(this);
        resize(1240, 760);
        setMinimumSize(980, 620);
        setSizeGripEnabled(true);
        setModal(true);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(14, 14, 14, 14);
        root->setSpacing(10);

        auto* detailsScroll = new QScrollArea(this);
        detailsScroll->setWidgetResizable(true);
        auto* detailsHost = new QWidget;
        auto* details = new QVBoxLayout(detailsHost);
        detailsScroll->setWidget(detailsHost);
        root->addWidget(detailsScroll, 1);

        const QString supplier = preview.supplierDisplayName.trimmed().isEmpty()
            ? QString::fromUtf8("не визначено")
            : preview.supplierDisplayName.trimmed();
        const QString priceColumn = preview.priceWithVatColumn >= 0
            ? invoicePreviewColumnLabel(rows, preview.headerRow, preview.priceWithVatColumn)
            : QString::fromUtf8("не знайдено");
        auto* title = new QLabel(QString::fromUtf8(
            "Постачальник: %1\nКолонка 'Ціна з ПДВ': %2\nConfidence: загальний %3%, постачальник %4%, заголовки %5%, типи даних %6%, формула суми %7%, ПДВ-ціна %8%")
                .arg(supplier)
                .arg(priceColumn)
                .arg(preview.totalConfidence)
                .arg(preview.supplierScore)
                .arg(preview.headerScore)
                .arg(preview.dataTypeScore)
                .arg(preview.numericConsistencyScore)
                .arg(preview.vatPriceScore), this);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.96); font-weight: 700; padding: 4px 6px;"));
        details->addWidget(title, 0);

        if (!sourceFilePath.trimmed().isEmpty() || !preview.warnings.isEmpty()) {
            QStringList info;
            if (!sourceFilePath.trimmed().isEmpty()) info << QString::fromUtf8("Файл: %1").arg(QFileInfo(sourceFilePath).fileName());
            if (!preview.warnings.isEmpty()) info << QString::fromUtf8("Попередження: %1").arg(preview.warnings.join(QStringLiteral("; ")));
            auto* warnings = new QLabel(info.join(QStringLiteral("\n")), this);
            warnings->setWordWrap(true);
            warnings->setStyleSheet(QStringLiteral("color: rgba(255,238,180,0.98); padding: 2px 6px;"));
            details->addWidget(warnings, 0);
        }

        auto* mappingFrame = new QFrame(this);
        mappingFrame->setObjectName("invoice_mapping_frame");
        auto* form = new QFormLayout(mappingFrame);
        form->setContentsMargins(10, 8, 10, 8);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(7);

        m_storeEdit = new QLineEdit(preview.supplierDisplayName, this);
        m_storeEdit->setObjectName("invoiceTemplateStore");
        if (m_storeEdit->text().contains(QString::fromUtf8("згідно оригіналу"), Qt::CaseInsensitive))
            m_storeEdit->clear();
        m_storeEdit->setPlaceholderText(QString::fromUtf8("Назва магазину"));
        form->addRow(QString::fromUtf8("Магазин"), m_storeEdit);
        m_splitDescription = new QCheckBox(QString::fromUtf8("Виділяти бренд і OEM зі спільного найменування"), this);
        m_splitDescription->setObjectName("invoiceSplitDescription");
        m_splitDescription->setChecked(preview.splitCombinedDescription);
        form->addRow(QString(), m_splitDescription);

        m_nameCombo = makeColumnCombo(preview.partNameColumn);
        m_articleCombo = makeColumnCombo(preview.articleColumn);
        m_brandCombo = makeColumnCombo(preview.brandColumn);
        m_qtyCombo = makeColumnCombo(preview.quantityColumn);
        m_priceCombo = makeColumnCombo(preview.priceWithVatColumn);
        m_totalCombo = makeColumnCombo(preview.totalWithVatColumn);
        m_dateCombo = makeColumnCombo(preview.dateColumn);
        m_supplierCombo = makeColumnCombo(preview.supplierColumn);

        form->addRow(QString::fromUtf8("Назва запчастини"), m_nameCombo);
        form->addRow(QString::fromUtf8("Артикул / код / каталожний номер"), m_articleCombo);
        form->addRow(QString::fromUtf8("Бренд"), m_brandCombo);
        form->addRow(QString::fromUtf8("Кількість"), m_qtyCombo);
        form->addRow(QString::fromUtf8("Ціна з ПДВ"), m_priceCombo);
        form->addRow(QString::fromUtf8("Сума з ПДВ"), m_totalCombo);
        form->addRow(QString::fromUtf8("Дата документа / доставки"), m_dateCombo);
        form->addRow(QString::fromUtf8("Постачальник"), m_supplierCombo);

        m_confirmVat = new QCheckBox(QString::fromUtf8("Підтверджую, що всі вибрані колонки правильні, а колонка 'Ціна з ПДВ' є саме ціною з ПДВ"), this);
        m_confirmVat->setChecked(preview.priceWithVatConfirmed);
        m_confirmVat->setStyleSheet(QStringLiteral("QCheckBox { color: rgba(255,255,255,0.98); font-weight: 800; padding: 6px; } QCheckBox::indicator { width: 22px; height: 22px; }"));
        form->addRow(QString(), m_confirmVat);
        details->addWidget(mappingFrame, 0);

        m_table = new QTableView(this);
        m_previewModel = new WorkPartsInvoiceMappingPreviewModel(rows, preview, m_table);
        m_table->setModel(m_previewModel);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionMode(QAbstractItemView::NoSelection);
        m_table->setWordWrap(false);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setStretchLastSection(false);
        root->addWidget(m_table, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("Підтвердити mapping"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Скасувати"));
        root->addWidget(buttons, 0);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { acceptMapping(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        setStyleSheet(QStringLiteral(
            "QDialog { background: #56616f; }"
            "QFrame#invoice_mapping_frame { background: rgba(38,48,62,210); border: 1px solid rgba(220,228,238,75); border-radius: 12px; }"
            "QLabel { color: white; }"
            "QComboBox, QLineEdit { min-height: 30px; padding: 3px 8px; background: rgba(245,247,250,245); color: #111827; border-radius: 8px; }"
            "QCheckBox { color: white; }"
            "QTableWidget { background: rgba(238,240,244,245); color: #111827; border: 1px solid rgba(210,220,235,85); gridline-color: rgba(132,142,154,95); }"
            "QHeaderView::section { background: rgba(63,72,86,240); color: white; border: 0px; padding: 7px 6px; }"
            "QPushButton { color: white; font-weight: 700; min-height: 34px; padding: 6px 18px; border-radius: 12px; border: 1px solid rgba(220,228,238,120);"
            "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(106,118,131,235), stop:0.48 rgba(66,76,88,245), stop:0.49 rgba(34,44,58,250), stop:1 rgba(8,16,30,255)); }"));
    }

    InvoiceImport::InvoiceColumnMappingPreview confirmedMapping() const { return m_confirmed; }

private:
    QList<QStringList> m_rows;
    InvoiceImport::InvoiceColumnMappingPreview m_preview;
    InvoiceImport::InvoiceColumnMappingPreview m_confirmed;
    QComboBox* m_nameCombo = nullptr;
    QComboBox* m_articleCombo = nullptr;
    QComboBox* m_brandCombo = nullptr;
    QComboBox* m_qtyCombo = nullptr;
    QComboBox* m_priceCombo = nullptr;
    QComboBox* m_totalCombo = nullptr;
    QComboBox* m_dateCombo = nullptr;
    QComboBox* m_supplierCombo = nullptr;
    QCheckBox* m_confirmVat = nullptr;
    QLineEdit* m_storeEdit = nullptr;
    QCheckBox* m_splitDescription = nullptr;
    QTableView* m_table = nullptr;
    WorkPartsInvoiceMappingPreviewModel* m_previewModel = nullptr;

    QComboBox* makeColumnCombo(int selectedColumn)
    {
        auto* combo = new QComboBox(this);
        combo->addItem(QString::fromUtf8("— не використовувати —"), -1);
        const int columns = invoicePreviewColumnCount(m_rows);
        for (int col = 0; col < columns; ++col) {
            combo->addItem(invoicePreviewColumnLabel(m_rows, m_preview.headerRow, col), col);
        }
        const int index = combo->findData(selectedColumn);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return combo;
    }

    int comboColumn(const QComboBox* combo) const
    {
        return combo ? combo->currentData().toInt() : -1;
    }


    void acceptMapping()
    {
        if (!m_confirmed.templateKey.isEmpty() && m_storeEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("Магазин"),
                QString::fromUtf8("Вкажіть назву магазину для збереження шаблону."));
            m_storeEdit->setFocus();
            return;
        }
        m_confirmed.supplierDisplayName = m_storeEdit->text().trimmed();
        m_confirmed.splitCombinedDescription = m_splitDescription->isChecked();
        m_confirmed.templateApplied = false;
        const int nameCol = comboColumn(m_nameCombo);
        const int articleCol = comboColumn(m_articleCombo);
        const int qtyCol = comboColumn(m_qtyCombo);
        const int priceCol = comboColumn(m_priceCombo);
        if (priceCol < 0 || qtyCol < 0 || nameCol < 0) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Preview імпорту накладної"),
                                 QString::fromUtf8("Для імпорту потрібні назва запчастини, кількість і колонка 'Ціна з ПДВ'."));
            return;
        }
        bool confirmedByPrompt = false;
        if (!m_confirmVat || !m_confirmVat->isChecked()) {
            const auto answer = QMessageBox::question(this,
                QString::fromUtf8("Підтвердження mapping колонок"),
                QString::fromUtf8("Підтвердіть, що всі вибрані колонки правильні, а колонка 'Ціна з ПДВ' дійсно є ціною з ПДВ.\n\n"
                                  "Так — підтвердити mapping і продовжити.\nНі — повернутися до перевірки колонок."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                if (m_confirmVat) m_confirmVat->setFocus(Qt::OtherFocusReason);
                return;
            }
            if (m_confirmVat) m_confirmVat->setChecked(true);
            confirmedByPrompt = true;
        }

        const QString priceHeader = normalizedInvoiceMappingHeader(invoicePreviewCell(m_rows, m_preview.headerRow, priceCol));
        if (invoiceMappingHeaderHasVatExcludedMarker(priceHeader)) {
            const auto answer = QMessageBox::warning(this,
                QString::fromUtf8("Увага: ціна без ПДВ"),
                QString::fromUtf8("Вибрана колонка схожа на 'Ціна без ПДВ'. Автоматично її використовувати заборонено.\n"
                                  "Продовжити тільки якщо ви явно підтверджуєте, що ця ціна вже коректно приведена до ціни з ПДВ."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        } else if (!invoiceMappingHeaderHasVatIncludedMarker(priceHeader)) {
            const auto answer = QMessageBox::question(this,
                QString::fromUtf8("Підтвердження ціни з ПДВ"),
                QString::fromUtf8("Назва вибраної колонки не містить явної ознаки ПДВ.\n"
                                  "Підтверджуєте, що це саме ціна з ПДВ?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }

        m_confirmed.partNameColumn = nameCol;
        m_confirmed.articleColumn = articleCol;
        m_confirmed.brandColumn = comboColumn(m_brandCombo);
        m_confirmed.quantityColumn = qtyCol;
        m_confirmed.priceWithVatColumn = priceCol;
        m_confirmed.totalWithVatColumn = comboColumn(m_totalCombo);
        m_confirmed.dateColumn = comboColumn(m_dateCombo);
        m_confirmed.supplierColumn = comboColumn(m_supplierCombo);
        m_confirmed.priceWithVatConfirmed = true;
        m_confirmed.automaticAllowed = false;
        m_confirmed.manualPreviewRequired = true;
        m_confirmed.manualColumnSelectionRequired = true;
        m_confirmed.vatPriceScore = invoiceMappingHeaderHasVatIncludedMarker(priceHeader) ? 100 : qMax(60, m_confirmed.vatPriceScore);
        m_confirmed.totalConfidence = qMax(60, m_confirmed.totalConfidence);
        accept();
    }
};

static bool confirmWorkPartsInvoiceColumnMapping(QWidget* parent,
                                                 const QList<QStringList>& rows,
                                                 const InvoiceImport::InvoiceColumnMappingPreview& preview,
                                                 const QString& sourceFilePath,
                                                 InvoiceImport::InvoiceColumnMappingPreview* confirmedMapping)
{
    WorkPartsInvoiceColumnMappingDialog dlg(rows, preview, sourceFilePath, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    if (confirmedMapping) *confirmedMapping = dlg.confirmedMapping();
    return true;
}

static bool chooseWorkPartsImportDrafts(QWidget* parent,
                                        const QList<ImportedPartDraft>& parsedDrafts,
                                        InvoiceImport::ExcelReadResult* data, int orderNumber, QTableWidget* existing,
                                        QList<ImportedPartDraft>* selectedDrafts,
                                        bool* manualPreviewUsed)
{
    if (manualPreviewUsed) *manualPreviewUsed = true;
    InvoiceReviewDialog dlg(parsedDrafts, *data, parent, QString::number(orderNumber));
    dlg.commit=[&](QString* error){for(const auto& draft:dlg.selectedDrafts())for(int row=0;row<existing->rowCount();++row){
        auto* attachment=existing->item(row,PartColAttachment);auto* name=existing->item(row,PartColName);auto* oem=existing->item(row,PartColOem);auto* store=existing->item(row,PartColStore);
        if(attachment&&name&&oem&&store&&attachment->data(RoleAttachmentDocumentNumber).toString()==dlg.invoiceNumber()
            &&DateFormats::parseDate(attachment->data(RoleAttachmentDocumentDate).toString()).toString(Qt::ISODate)==dlg.invoiceDate()
            &&name->text().trimmed()==draft.name.trimmed()&&oem->text().trimmed()==draft.oemNumber.trimmed()&&store->text().trimmed().compare(draft.store.trimmed(),Qt::CaseInsensitive)==0){
            *error=QString::fromUtf8("Позиція «%1» цієї накладної вже є в наряді. Зніміть її вибір, щоб уникнути дублювання.").arg(draft.name);return false;}}
        return true;};
    if (dlg.exec() != QDialog::Accepted) return false;
    *selectedDrafts = dlg.selectedDrafts();
    data->documentNumber=dlg.invoiceNumber(); data->documentDate=dlg.invoiceDate();
    return true;
}

} // namespace

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
QRect WorkPartsDialog::partsImportTargetRect() const
{
    if (m_partsTable) {
        const QRect tableRect = ImportFlightAnimation::tableWidgetColumnHintRect(const_cast<WorkPartsDialog*>(this), m_partsTable, PartColName);
        if (tableRect.isValid()) return tableRect;
    }
    return ImportFlightAnimation::widgetRectInHost(const_cast<WorkPartsDialog*>(this), const_cast<QPushButton*>(m_btnImportExcel ? m_btnImportExcel : m_btnAddPart));
}

void WorkPartsDialog::importPartsFromExcel()
{
    if (m_partsTable && m_partsTable->property("csImportBusy").toBool()) {
        qWarning().noquote() << QStringLiteral("WorkPartsDialog import skipped: busy order=") << m_orderNumber;
        logWorkPartsDiagnostic(QStringLiteral("import_skip_busy"), QStringLiteral("part"));
        return;
    }
    auto setImportUiBusy = [this](bool busy) {
        setProperty("csImportBusy", busy);
        if (auto* backup = AppContext::instance().backupManager()) {
            if (busy) BackupPerformanceGuard::hold(backup, 20000);
            else BackupPerformanceGuard::release(backup);
        }
        const auto tuneTable = [busy](QTableWidget* table) {
            if (!table) return;
            table->setProperty("csImportBusy", busy);
            table->setSortingEnabled(false);
            if (busy) {
                flushInlineEditorValueToModel(table);
                table->clearSelection();
            }
            if (table->viewport()) {
                if (busy) table->viewport()->setCursor(Qt::WaitCursor);
                else table->viewport()->unsetCursor();
                table->viewport()->update();
            }
            table->update();
        };

        tuneTable(m_servicesTable);
        tuneTable(m_partsTable);

        if (m_btnImportExcel) {
            m_btnImportExcel->setProperty("csImportBusy", busy);
            m_btnImportExcel->setCursor(busy ? Qt::WaitCursor : Qt::ArrowCursor);
            m_btnImportExcel->setEnabled(!busy);
        }
        if (m_btnOutsource) m_btnOutsource->setEnabled(!busy);
        if (m_btnPdf) m_btnPdf->setEnabled(!busy);
        if (m_btnCloseOrder) m_btnCloseOrder->setEnabled(!busy);
        if (m_btnDialogOk) m_btnDialogOk->setEnabled(!busy);

        if (busy) setCursor(Qt::WaitCursor);
        else unsetCursor();

        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS ui_busy=") << busy
                          << QStringLiteral(" order=") << m_orderNumber;
    };

    auto clearImportApplyingState = [this]() {
        setProperty("csImportApplyingRows", false);
        if (m_partsTable) {
            m_partsTable->setProperty("csImportApplyingRows", false);
            m_partsTable->setUpdatesEnabled(true);
            if (m_partsTable->viewport()) m_partsTable->viewport()->update();
            m_partsTable->update();
        }
    };

    auto releaseImportUi = [setImportUiBusy, clearImportApplyingState]() {
        clearImportApplyingState();
        setImportUiBusy(false);
        if (auto* backup = AppContext::instance().backupManager()) {
            BackupPerformanceGuard::hold(backup, 6000);
        }
    };

    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return;
    }

    const QString filePath = InvoiceDialogSupport::selectFile(
        this,
        QString::fromUtf8("Оберіть накладну Excel/PDF із запчастинами"),
        QString::fromUtf8("Накладні/таблиці (*.pdf *.xlsx *.xls *.xlsm *.xltx *.xltm *.csv *.txt);;PDF (*.pdf);;Excel (*.xlsx *.xls *.xlsm *.xltx *.xltm);;CSV (*.csv *.txt);;Усі файли (*.*)"));
    if (filePath.trimmed().isEmpty()) return;

    logWorkPartsDiagnostic(QStringLiteral("import_start"),
                           QStringLiteral("part"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("file"), QFileInfo(filePath).fileName()}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS begin order=") << m_orderNumber
                      << QStringLiteral(" file=") << filePath;

    const QPointer<WorkPartsDialog> guard(this);
    setImportUiBusy(true);

    runExcelReadAsync(this,
                     QString::fromUtf8("Читання Excel-файлу із запчастинами..."),
                     filePath,
                     [guard, filePath, releaseImportUi](const ExcelReadResult& excelData, QString readError) {
        if (!guard) return;
        auto importStateGuard = qScopeGuard([guard, releaseImportUi]() {
            if (!guard) return;
            releaseImportUi();
        });
        if (readError == WorkPartsDialogExcelSupport::cancelledReadMarker()) return;
        guard->logWorkPartsDiagnostic(QStringLiteral("import_read_callback"),
                                      QStringLiteral("part"),
                                      -1,
                                      -1,
                                      excelData.documentNumber,
                                      {{QStringLiteral("rawRows"), excelData.rows.size()}, {QStringLiteral("error"), readError}});
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS callback order=") << guard->m_orderNumber
                          << QStringLiteral(" rows=") << excelData.rows.size()
                          << QStringLiteral(" error=") << readError;
        if (excelData.rows.isEmpty()) {
            guard->logWorkPartsDiagnostic(QStringLiteral("import_end_empty"), QStringLiteral("part"), -1, -1, excelData.documentNumber, {{QStringLiteral("error"), readError}});
            importStateGuard.dismiss();
            releaseImportUi();
            QMessageBox::warning(guard,
                                 QString::fromUtf8("Імпорт Excel"),
                                 readError.trimmed().isEmpty()
                                     ? QString::fromUtf8("У файлі не знайдено жодного рядка для імпорту.")
                                     : readError);
            return;
        }

        std::function<void(QList<ImportedPartDraft>)> continueImportWithDrafts;
        continueImportWithDrafts = [guard, filePath, releaseImportUi, excelData = InvoiceImport::ExcelReadResult(excelData)](QList<ImportedPartDraft> drafts) mutable {
            if (!guard) return;
            auto importStateGuard = qScopeGuard([guard, releaseImportUi]() {
                if (!guard) return;
                releaseImportUi();
            });
        guard->logWorkPartsDiagnostic(QStringLiteral("import_selection_prompt"),
                                      QStringLiteral("part"),
                                      -1,
                                      -1,
                                      excelData.documentNumber,
                                      {{QStringLiteral("parsedDrafts"), drafts.size()},
                                       {QStringLiteral("mode"), QStringLiteral("ask_all_or_manual")}});

        QList<ImportedPartDraft> selectedDrafts;
        bool manualPreviewUsed = false;
        if (!chooseWorkPartsImportDrafts(guard, drafts, &excelData, guard->m_orderNumber, guard->m_partsTable, &selectedDrafts, &manualPreviewUsed)) {
            guard->logWorkPartsDiagnostic(QStringLiteral("import_cancelled_by_user"),
                                          QStringLiteral("part"),
                                          -1,
                                          -1,
                                          excelData.documentNumber,
                                          {{QStringLiteral("parsedDrafts"), drafts.size()},
                                           {QStringLiteral("manualPreviewUsed"), manualPreviewUsed}});
            importStateGuard.dismiss();
            releaseImportUi();
            return;
        }

        guard->logWorkPartsDiagnostic(manualPreviewUsed
                                          ? QStringLiteral("import_selection_manual_confirmed")
                                          : QStringLiteral("import_selection_all_confirmed"),
                                      QStringLiteral("part"),
                                      -1,
                                      -1,
                                      excelData.documentNumber,
                                      {{QStringLiteral("parsedDrafts"), drafts.size()},
                                       {QStringLiteral("selectedDrafts"), selectedDrafts.size()},
                                       {QStringLiteral("manualPreviewUsed"), manualPreviewUsed}});

        drafts = selectedDrafts;
        if (drafts.isEmpty()) {
            guard->logWorkPartsDiagnostic(QStringLiteral("import_end_no_selected_rows"),
                                          QStringLiteral("part"),
                                          -1,
                                          -1,
                                          excelData.documentNumber,
                                          {{QStringLiteral("parsedDrafts"), selectedDrafts.size()}});
            importStateGuard.dismiss();
            releaseImportUi();
            QMessageBox::warning(guard,
                                 QString::fromUtf8("Імпорт Excel"),
                                 QString::fromUtf8("Не вибрано жодної запчастини для імпорту."));
            return;
        }

        int firstImportedRow = 0;
        QString invoicePdfPath = excelData.canonicalPdfAttachmentPath.trimmed();
        if (invoicePdfPath.isEmpty() && !filePath.trimmed().isEmpty()) {
            const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
            const bool spreadsheetSource = suffix == QStringLiteral("xlsx") || suffix == QStringLiteral("xls") ||
                                           suffix == QStringLiteral("xlsm") || suffix == QStringLiteral("xltx") ||
                                           suffix == QStringLiteral("xltm") || suffix == QStringLiteral("csv") ||
                                           suffix == QStringLiteral("txt");
            if (spreadsheetSource) {
                qWarning().noquote() << QStringLiteral("WorkPartsDialog canonical PDF conversion missing for spreadsheet invoice; original Excel will not be stored/opened as invoice attachment:")
                                     << (excelData.canonicalPdfAttachmentError.trimmed().isEmpty()
                                             ? QStringLiteral("<no details>")
                                             : excelData.canonicalPdfAttachmentError.trimmed());
                releaseImportUi();
                QMessageBox::warning(guard, QString::fromUtf8("Накладна"),
                    excelData.canonicalPdfAttachmentError.trimmed().isEmpty()
                        ? QString::fromUtf8("Імпорт не виконано: не вдалося створити PDF накладної. Повторіть імпорт після усунення помилки.")
                        : excelData.canonicalPdfAttachmentError);
                return;
            } else {
                QString invoicePdfError;
                const QString invoiceSupplierName = drafts.isEmpty() ? QString() : drafts.first().store.trimmed();
                invoicePdfPath = ensureInvoicePdfAttachment(filePath, excelData.documentNumber.trimmed(), excelData.documentDate.trimmed(), invoiceSupplierName, &invoicePdfError);
                if (invoicePdfPath.trimmed().isEmpty()) {
                    qWarning().noquote() << QStringLiteral("WorkPartsDialog invoice PDF conversion failed:") << invoicePdfError;
                }
            }
        }
        const WorkPartsImportedRowsViewModel::ImportBatch importBatch =
            WorkPartsImportedRowsViewModel::buildBatch(guard->m_orderNumber,
                                                       drafts,
                                                       excelData,
                                                       filePath,
                                                       invoicePdfPath);

        auto applyImportedRowsThroughViewModel = [guard, &importBatch, &firstImportedRow, &excelData](QString* applyError) -> bool {
            if (applyError) applyError->clear();
            if (!guard || !guard->m_partsTable) {
                if (applyError) *applyError = QString::fromUtf8("Таблиця запчастин недоступна для імпорту.");
                return false;
            }
            if (!importBatch.isValid()) {
                if (applyError) *applyError = QString::fromUtf8("ViewModel імпорту не містить рядків для додавання.");
                return false;
            }

            // P0 Import p7: one controlled ViewModel/API boundary for imported invoice rows.
            // No caller writes imported cells directly to QTableWidget; all row insertion,
            // role assignment, dirty-state and visual synchronization stay in this single path.
            PerfWatchdog::ScopedTimer applyPerfTimer(QStringLiteral("WorkPartsDialog::applyImportedRowsBatch"), 80);
            const bool previousLoadingParts = guard->m_loadingParts;
            guard->m_loadingParts = true;
            guard->setProperty("csImportApplyingRows", true);
            const QSignalBlocker blocker(guard->m_partsTable);
            guard->m_partsTable->setUpdatesEnabled(false);
            guard->m_partsTable->setProperty("csImportApplyingRows", true);
            auto applyRowsGuard = qScopeGuard([guard, previousLoadingParts]() {
                if (!guard) return;
                guard->m_loadingParts = previousLoadingParts;
                guard->setProperty("csImportApplyingRows", false);
                if (guard->m_partsTable) {
                    guard->m_partsTable->setProperty("csImportApplyingRows", false);
                    guard->m_partsTable->setUpdatesEnabled(true);
                    if (guard->m_partsTable->viewport()) guard->m_partsTable->viewport()->update();
                    guard->m_partsTable->update();
                }
            });
            removeViewportFillRows(guard->m_partsTable);
            for (int row = guard->m_partsTable->rowCount() - 1; row >= 0; --row) {
                if (rowHasHintMarker(guard->m_partsTable, row) || rowIsRemovableBlankPartRow(guard->m_partsTable, row)) {
                    guard->m_partsTable->removeRow(row);
                }
            }

            firstImportedRow = guard->m_partsTable->rowCount();
            guard->bumpWorkPartsTableRevision(QStringLiteral("import_apply_rows_begin"));
            guard->logWorkPartsDiagnostic(QStringLiteral("row_insert_batch_begin"),
                                          QStringLiteral("part"),
                                          firstImportedRow,
                                          -1,
                                          excelData.documentNumber,
                                          {{QStringLiteral("rows"), importBatch.rows.size()}});
            const int importRowCount = importBatch.rows.size();
            // P0 performance: grow the table once and initialize imported rows
            // in-place. Calling insertEmptyPartRow()/insertRow() for every invoice
            // line caused model/layout churn and visible freezes on large imports.
            guard->m_partsTable->setRowCount(firstImportedRow + importRowCount);
            int importOffset = 0;
            for (const WorkPartsImportedRowsViewModel::ImportedPartRow& vmRow : importBatch.rows) {
                const auto& uiRow = vmRow.ui;
                const int row = firstImportedRow + importOffset++;
                for (int c = 0; c < guard->m_partsTable->columnCount(); ++c) {
                    QTableWidgetItem* item = (c == PartColIndex) ? makeImportedPartIndexItem() : makeImportedPartTextItem();
                    if (c == PartColAttachment) item->setText(QString());
                    guard->m_partsTable->setItem(row, c, item);
                }
                if (auto* nameItem = guard->m_partsTable->item(row, PartColName)) {
                    nameItem->setData(RoleIsHint, false);
                }

                // P0 import batch optimization: mark the imported invoice row once,
                // after attachment/invoice metadata is present. The earlier duplicate
                // marker pass caused needless role/font churn for every imported line.
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColOrderedAt, uiRow.orderedAt);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColOem, uiRow.oemNumber);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColName, uiRow.name);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColQty, uiRow.qtyText);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColStore,
                    guard->m_btnCustomerParts->isChecked() ? PartMarkupPolicyService::customerPartsStore() : uiRow.store);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColBrand, uiRow.brandName);
                applyImportedCellValueLikeManualInput(guard->m_partsTable, row, PartColPrice, uiRow.priceText);
                if (auto* priceItem = guard->m_partsTable->item(row, PartColPrice)) {
                    bool priceOk = false;
                    const double purchasePrice = parseLooseNumber(uiRow.priceText, &priceOk);
                    priceItem->setData(RolePurchasePrice, !guard->m_btnCustomerParts->isChecked() && priceOk ? qMax(0.0, purchasePrice) : 0.0);
                    priceItem->setData(RoleManualMarkupOverride, false);
                    priceItem->setData(RoleMarkupPercent, 0.0);
                }
                if (auto* amountItem = guard->m_partsTable->item(row, PartColAmount); amountItem && uiRow.priceText.trimmed().isEmpty()) {
                    amountItem->setText(QString());
                }

                if (auto* nameItem = guard->m_partsTable->item(row, PartColName)) {
                    nameItem->setData(RoleId, 0);
                    nameItem->setData(RolePartManufacturer, uiRow.brandName);
                    nameItem->setData(RoleImportedInvoiceRow, true);
                    nameItem->setData(RoleImportedInvoiceTrace, vmRow.traceId);
                }
                if (auto* attachmentItem = guard->m_partsTable->item(row, PartColAttachment)) {
                    const QString tooltipPath = vmRow.attachmentPath.isEmpty() ? vmRow.attachmentSource : vmRow.attachmentPath;
                    attachmentItem->setText(QString::fromUtf8("📎"));
                    attachmentItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    attachmentItem->setTextAlignment(Qt::AlignCenter);
                    attachmentItem->setData(RoleAttachmentPath, vmRow.attachmentPath);
                    attachmentItem->setData(RoleAttachmentSource, vmRow.attachmentSource);
                    attachmentItem->setData(RoleAttachmentName, vmRow.attachmentName);
                    attachmentItem->setData(RoleAttachmentDocumentNumber, vmRow.documentNumber);
                    attachmentItem->setData(RoleAttachmentDocumentDate, vmRow.documentDate);
                    attachmentItem->setData(RoleImportedInvoiceRow, true);
                    attachmentItem->setData(RoleImportedInvoiceTrace, vmRow.traceId);
                    attachmentItem->setToolTip(attachmentToolTipText(tooltipPath, vmRow.documentNumber, vmRow.documentDate));
                }
                guard->refreshPartInvoiceNumberCell(row);
                markImportedInvoiceRow(guard->m_partsTable, row, vmRow.traceId);

                refreshPartRowClientPricing(guard->m_partsTable, row, PartColPrice);
                if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
                    if (auto* priceItem = guard->m_partsTable->item(row, PartColPrice)) {
                        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=import order=%1 partId=0 trace=%2 name=\"%3\" store=\"%4\" purchase=%5 clientText=%6 activePct=%7 source=policy override=0")
                            .arg(guard->m_orderNumber)
                            .arg(vmRow.traceId)
                            .arg(uiRow.name)
                            .arg(uiRow.store)
                            .arg(priceItem->data(RolePurchasePrice).toDouble(), 0, 'f', 2)
                            .arg(priceItem->text())
                            .arg(priceItem->data(RoleMarkupPercent).toDouble(), 0, 'f', 2);
                    }
                }
                syncPartManufacturerRoleFromRow(guard->m_partsTable, row);
                clearRowHintState(guard->m_partsTable, row);
                setRowEditingVisual(guard->m_partsTable, row);
                syncPartDeliveredCellState(guard->m_partsTable, row);
                // Row-height/font recalculation is intentionally deferred to one batch pass
                // after all imported rows are inserted. Running it per row was one of the
                // visible UI freezes during large invoice imports.
                guard->m_dirtyPartRows.insert(row);
                guard->m_userTouchedPendingContent = true;
                if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
                    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS transfer_to_order_viewmodel order=") << guard->m_orderNumber
                                      << QStringLiteral(" row=") << row
                                      << QStringLiteral(" trace=") << vmRow.traceId
                                      << QStringLiteral(" date=") << clippedImportLogText(uiRow.orderedAt)
                                      << QStringLiteral(" oem=") << clippedImportLogText(uiRow.oemNumber)
                                      << QStringLiteral(" name=") << clippedImportLogText(uiRow.name)
                                      << QStringLiteral(" qty=") << clippedImportLogText(uiRow.qtyText)
                                      << QStringLiteral(" price=") << clippedImportLogText(uiRow.priceText)
                                      << QStringLiteral(" store=") << clippedImportLogText(uiRow.store)
                                      << QStringLiteral(" brand=") << clippedImportLogText(uiRow.brandName)
                                      << QStringLiteral(" doc=") << clippedImportLogText(vmRow.documentNumber)
                                      << QStringLiteral(" docDate=") << clippedImportLogText(vmRow.documentDate);
                }
            }
            guard->bumpWorkPartsTableRevision(QStringLiteral("import_apply_rows_end"));
            guard->logWorkPartsDiagnostic(QStringLiteral("row_insert_batch_end"),
                                          QStringLiteral("part"),
                                          firstImportedRow,
                                          -1,
                                          excelData.documentNumber,
                                          {{QStringLiteral("rows"), importBatch.rows.size()}});
            guard->notifyDraftDirtyChanged();
            return true;
        };

        QString importApplyError;
        if (!applyImportedRowsThroughViewModel(&importApplyError)) {
            guard->logWorkPartsDiagnostic(QStringLiteral("import_end_apply_failed"), QStringLiteral("part"), -1, -1, excelData.documentNumber, {{QStringLiteral("error"), importApplyError}});
            importStateGuard.dismiss();
            releaseImportUi();
            QMessageBox::warning(guard,
                                 QString::fromUtf8("Імпорт Excel"),
                                 importApplyError.trimmed().isEmpty()
                                     ? QString::fromUtf8("Не вдалося застосувати імпортовані рядки.")
                                     : importApplyError);
            return;
        }

        {
            const QSignalBlocker blocker(guard->m_partsTable);
            guard->renumberPartItems();
            guard->repositionInputRows();
            guard->synchronizeAllPairedRowHeights();
        }
        guard->loadHeader();

        ImportFlightAnimation::play(guard,
                                    [guard]() -> QRect {
                                        if (!guard) return {};
                                        return guard->partsImportTargetRect();
                                    },
                                    []() {},
                                    720);

        if (guard->m_partsTable && guard->m_partsTable->rowCount() > 0) {
            const int targetRow = qMin(firstImportedRow, guard->m_partsTable->rowCount() - 1);
            flushInlineEditorValueToModel(guard->m_partsTable);
            guard->m_partsTable->setCurrentCell(targetRow, PartColName);
            if (auto* targetItem = guard->m_partsTable->item(targetRow, PartColName)) {
                guard->m_partsTable->scrollToItem(targetItem, QAbstractItemView::PositionAtCenter);
            }
            if (guard->m_partsTable->columnWidth(PartColIndex) < 49) {
                if (auto* header = guard->m_partsTable->horizontalHeader()) {
                    header->setProperty("csEnforcingPartIndexWidth", true);
                    const QSignalBlocker blocker(header);
                    guard->m_partsTable->setColumnWidth(PartColIndex, 49);
                    header->setProperty("csEnforcingPartIndexWidth", false);
                }
            }
            if (auto* hbar = guard->m_partsTable->horizontalScrollBar()) {
                hbar->setValue(hbar->minimum());
            }
            QPointer<WorkPartsDialog> fontGuard(guard);
            QTimer::singleShot(0, guard, [fontGuard]() {
                if (!fontGuard || !fontGuard->m_partsTable) return;
                refreshCompactMainPartsCellFonts(fontGuard->m_partsTable);
                if (fontGuard->m_partsTable->viewport()) fontGuard->m_partsTable->viewport()->update();
                fontGuard->m_partsTable->update();
            });
        }

        QString importInfo = QString::fromUtf8("Імпортовано %1 рядк(ів) запчастин у поля вводу.\nЗміни будуть збережені після підтвердження наряду або кнопки OK.")
                                 .arg(drafts.size());
        if (!excelData.documentNumber.trimmed().isEmpty()) {
            importInfo += QString::fromUtf8("\nНомер накладної: %1").arg(excelData.documentNumber.trimmed());
        }
        if (!excelData.documentDate.trimmed().isEmpty()) {
            importInfo += QString::fromUtf8("\nДата накладної: %1").arg(excelData.documentDate.trimmed());
        }
        guard->logWorkPartsDiagnostic(QStringLiteral("import_end_success"),
                                      QStringLiteral("part"),
                                      firstImportedRow,
                                      -1,
                                      excelData.documentNumber,
                                      {{QStringLiteral("drafts"), drafts.size()}, {QStringLiteral("firstRow"), firstImportedRow}});
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS applied order=") << guard->m_orderNumber
                          << QStringLiteral(" drafts=") << drafts.size()
                          << QStringLiteral(" firstRow=") << firstImportedRow;
        importStateGuard.dismiss();
        releaseImportUi();
        QMessageBox::information(guard,
                                 QString::fromUtf8("Імпорт Excel"),
                                 importInfo);
        };

        QList<ImportedPartDraft> drafts;
        if (excelData.columnDetectionPerformed) {
            drafts = excelData.parsedPartDrafts;
            readError = excelData.automaticImportError;
        } else {
            readError = QString::fromUtf8("Імпорт зупинено: фоновий аналіз колонок не був виконаний ImportJob.");
        }
        if (drafts.isEmpty()) {
            const QString automaticParseError = readError;
            const InvoiceImport::InvoiceColumnMappingPreview preview = excelData.columnMappingPreview;

            guard->logWorkPartsDiagnostic(QStringLiteral("import_preview_required"),
                                          QStringLiteral("part"),
                                          -1,
                                          -1,
                                          excelData.documentNumber,
                                          {{QStringLiteral("error"), automaticParseError},
                                           {QStringLiteral("confidence"), preview.totalConfidence},
                                           {QStringLiteral("supplierScore"), preview.supplierScore},
                                           {QStringLiteral("vatPriceScore"), preview.vatPriceScore},
                                           {QStringLiteral("priceWithVatConfirmed"), preview.priceWithVatConfirmed}});

            InvoiceImport::InvoiceColumnMappingPreview confirmedMapping;
            if (confirmWorkPartsInvoiceColumnMapping(guard, excelData.rows, preview, filePath, &confirmedMapping)) {
                importStateGuard.dismiss();
                WorkPartsDialogExcelSupport::runConfirmedVatMappingParseAsync(
                    guard,
                    excelData.rows,
                    confirmedMapping,
                    excelData.documentDate,
                    [](const QString& s) { return normalizeSentence(s); },
                    [](QString s, bool* ok) { return parseLooseNumber(s, ok); },
                    [](double v) { return moneyText(v); },
                    [guard, releaseImportUi, excelData, confirmedMapping, continueImportWithDrafts]
                    (const QList<ImportedPartDraft>& parsedDrafts, const QString& manualError) mutable {
                        if (!guard) return;
                        if (parsedDrafts.isEmpty()) {
                            guard->logWorkPartsDiagnostic(QStringLiteral("import_end_manual_mapping_failed"),
                                                          QStringLiteral("part"),
                                                          -1,
                                                          -1,
                                                          excelData.documentNumber,
                                                          {{QStringLiteral("error"), manualError},
                                                           {QStringLiteral("confidence"), confirmedMapping.totalConfidence},
                                                           {QStringLiteral("priceWithVatColumn"), confirmedMapping.priceWithVatColumn}});
                            releaseImportUi();
                            QMessageBox::warning(guard,
                                                 QString::fromUtf8("Імпорт Excel/PDF"),
                                                 manualError.trimmed().isEmpty()
                                                     ? QString::fromUtf8("Після ручного mapping не знайдено коректних рядків із підтвердженою ціною з ПДВ.")
                                                     : manualError);
                            return;
                        }
                        guard->logWorkPartsDiagnostic(QStringLiteral("import_manual_mapping_confirmed_async"),
                                                      QStringLiteral("part"),
                                                      -1,
                                                      -1,
                                                      excelData.documentNumber,
                                                      {{QStringLiteral("parsedDrafts"), parsedDrafts.size()},
                                                       {QStringLiteral("confidence"), confirmedMapping.totalConfidence},
                                                       {QStringLiteral("priceWithVatColumn"), confirmedMapping.priceWithVatColumn}});
                        continueImportWithDrafts(parsedDrafts);
                    });
                return;
            } else {
                guard->logWorkPartsDiagnostic(QStringLiteral("import_end_parse_failed"), QStringLiteral("part"), -1, -1, excelData.documentNumber, {{QStringLiteral("error"), automaticParseError}});
                importStateGuard.dismiss();
                releaseImportUi();
                QMessageBox::warning(guard,
                                     QString::fromUtf8("Імпорт Excel/PDF"),
                                     automaticParseError.trimmed().isEmpty()
                                         ? QString::fromUtf8("Не вдалося автоматично підтвердити колонки. Для імпорту потрібна підтверджена колонка 'Ціна з ПДВ'.")
                                         : automaticParseError);
                return;
            }
        }

        importStateGuard.dismiss();
        continueImportWithDrafts(drafts);
        return;
    });
}
