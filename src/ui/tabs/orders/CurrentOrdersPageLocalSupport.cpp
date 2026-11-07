#include "ui/common/ContextMenuAppearance.h"
#include "ui/common/dialogs/InvoiceReviewDialog.h"
#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/CurrentOrdersPageModulePrelude.h"
#include "core/import/UniversalInvoiceColumnDetector.h"
#include "ui/common/dialogs/InvoiceColumnMappingEditor.h"
#include "ui/common/dialogs/InvoiceDialogSupport.h"
#include <QScrollArea>

#include <optional>

#include <QAbstractTableModel>
#include <QLineEdit>
#include <QCheckBox>
#include <QBrush>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>

namespace CurrentOrdersSupport {
int modelColumnByHeader(const QAbstractItemModel* model, const QString& header)
{
    if (!model) return -1;
    for (int col = 0; col < model->columnCount(); ++col) {
        if (model->headerData(col, Qt::Horizontal).toString().trimmed() == header) return col;
    }
    return -1;
}

static QString currentOrdersStatusDisplay(QString value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("open") || normalized == QString::fromUtf8("відкритий") || normalized == QString::fromUtf8("відкрито")) {
        return QString::fromUtf8("Відкритий");
    }
    if (normalized == QStringLiteral("in_progress") || normalized == QString::fromUtf8("в роботі")) {
        return QString::fromUtf8("В роботі");
    }
    if (normalized == QStringLiteral("closed") || normalized == QString::fromUtf8("закритий") || normalized == QString::fromUtf8("закрито")) {
        return QString::fromUtf8("Закритий");
    }
    if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled") || normalized == QString::fromUtf8("скасований") || normalized == QString::fromUtf8("скасовано")) {
        return QString::fromUtf8("Скасований");
    }
    return value;
}

static QString normalizedWorkOrderStatus(QString value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("open") || normalized == QString::fromUtf8("відкритий") || normalized == QString::fromUtf8("відкрито")) {
        return QStringLiteral("open");
    }
    if (normalized == QStringLiteral("in_progress") || normalized == QString::fromUtf8("в роботі") || normalized == QString::fromUtf8("у роботі") || normalized == QString::fromUtf8("в процесі")) {
        return QStringLiteral("in_progress");
    }
    if (normalized == QStringLiteral("closed") || normalized == QString::fromUtf8("закритий") || normalized == QString::fromUtf8("закрито")) {
        return QStringLiteral("closed");
    }
    if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled") || normalized == QString::fromUtf8("скасований") || normalized == QString::fromUtf8("скасовано")) {
        return QStringLiteral("cancelled");
    }
    return normalized;
}


void publishCurrentOrdersBackupEvent(const QString& operationType,
                                            int orderNumber,
                                            const QString& entityType,
                                            const QString& entityId,
                                            const QString& description,
                                            const QStringList& changedTables)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("CurrentOrdersPage"),
                                         QStringLiteral("current_orders"),
                                         entityType,
                                         entityId,
                                         AppRuntimeService::resolveOrderUid(orderNumber),
                                         orderNumber,
                                         operationType,
                                         description,
                                         changedTables),
        nullptr);
}

using ImportedPartDraft = WorkPartsDialogExcelSupport::ImportedPartDraft;
using ExcelReadResult = WorkPartsDialogExcelSupport::ExcelReadResult;

static QString normalizedCurrentOrdersInvoiceMappingHeader(QString text)
{
    return text.trimmed().toLower()
        .replace(QChar(0x00A0), QLatin1Char(' '))
        .replace(QString::fromUtf8("ндс"), QString::fromUtf8("пдв"))
        .replace(QString::fromUtf8("vat"), QString::fromUtf8("пдв"))
        .replace(QString::fromUtf8("tax"), QString::fromUtf8("пдв"));
}

static bool currentOrdersInvoiceMappingHeaderHasVatExcludedMarker(const QString& normalized)
{
    const QString text = normalizedCurrentOrdersInvoiceMappingHeader(normalized);
    return text.contains(QString::fromUtf8("без пдв")) ||
           text.contains(QString::fromUtf8("без  пдв")) ||
           text.contains(QStringLiteral("excl")) ||
           text.contains(QStringLiteral("without"));
}

static bool currentOrdersInvoiceMappingHeaderHasVatIncludedMarker(const QString& normalized)
{
    const QString text = normalizedCurrentOrdersInvoiceMappingHeader(normalized);
    if (currentOrdersInvoiceMappingHeaderHasVatExcludedMarker(text)) return false;
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

static int currentOrdersInvoicePreviewColumnCount(const QList<QStringList>& rows)
{
    int columns = 0;
    for (const QStringList& row : rows) columns = qMax(columns, row.size());
    return columns;
}

static QString currentOrdersInvoicePreviewCell(const QList<QStringList>& rows, int row, int col)
{
    if (row < 0 || row >= rows.size() || col < 0 || col >= rows.at(row).size()) return QString();
    return rows.at(row).value(col).trimmed();
}

static QString currentOrdersInvoicePreviewColumnLabel(const QList<QStringList>& rows, int headerRow, int col)
{
    QString header = currentOrdersInvoicePreviewCell(rows, headerRow, col);
    if (header.isEmpty()) {
        for (int row = 0; row < qMin(rows.size(), 8); ++row) {
            header = currentOrdersInvoicePreviewCell(rows, row, col);
            if (!header.isEmpty()) break;
        }
    }
    const int confidence = 0;
    Q_UNUSED(confidence);
    return header.isEmpty()
        ? QString::fromUtf8("Колонка %1").arg(col + 1)
        : QString::fromUtf8("%1 — %2").arg(col + 1).arg(header.left(80));
}

class CurrentOrdersInvoiceMappingPreviewModel final : public QAbstractTableModel {
public:
    CurrentOrdersInvoiceMappingPreviewModel(const QList<QStringList>& rows,
                                            const InvoiceImport::InvoiceColumnMappingPreview& preview,
                                            QObject* parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_preview(preview)
    {
        m_firstRow = qMax(0, preview.headerRow);
        m_sampleRows = qMin(24, qMax(0, rows.size() - m_firstRow));
        m_columnCount = currentOrdersInvoicePreviewColumnCount(rows);
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
        if (orientation != Qt::Horizontal) return {};
        if (role == Qt::DisplayRole) {
            const QString label = currentOrdersInvoicePreviewColumnLabel(m_rows, m_preview.headerRow, section);
            const int confidence = m_preview.physicalColumnConfidence.value(section, -1);
            return confidence >= 0 ? QStringLiteral("%1\n%2%").arg(label).arg(confidence) : label;
        }
        if (role == Qt::ToolTipRole) {
            QStringList details;
            if (m_preview.physicalColumnConfidence.contains(section)) {
                details << QString::fromUtf8("Confidence колонки: %1%").arg(m_preview.physicalColumnConfidence.value(section));
            }
            if (m_preview.physicalColumnWarnings.contains(section)) {
                details << m_preview.physicalColumnWarnings.value(section);
            }
            return details.join(QStringLiteral("\n"));
        }
        return {};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid()) return {};
        const int sourceRow = m_firstRow + index.row();
        if (sourceRow < 0 || sourceRow >= m_rows.size()) return {};
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            return m_rows.at(sourceRow).value(index.column()).trimmed();
        }
        if (role == Qt::BackgroundRole) {
            if (index.row() == 0 && sourceRow == m_preview.headerRow) return QColor(214, 232, 255);
            if (index.column() == m_preview.priceWithVatColumn) {
                return m_preview.priceWithVatConfirmed ? QColor(214, 255, 226) : QColor(255, 238, 190);
            }
            const int confidence = m_preview.physicalColumnConfidence.value(index.column(), 100);
            if (confidence < 60) return QColor(255, 226, 226);
            if (confidence < 85) return QColor(255, 244, 204);
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

class CurrentOrdersInvoiceColumnMappingDialog final : public QDialog {
public:
    CurrentOrdersInvoiceColumnMappingDialog(const QList<QStringList>& rows,
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
        auto* detailsScroll = new QScrollArea(this);
        detailsScroll->setWidgetResizable(true);
        auto* detailsHost = new QWidget;
        auto* details = new QVBoxLayout(detailsHost);
        detailsScroll->setWidget(detailsHost);
        root->addWidget(detailsScroll, 1);
        root->setContentsMargins(14, 14, 14, 14);
        root->setSpacing(10);

        const QString supplier = preview.supplierDisplayName.trimmed().isEmpty()
            ? QString::fromUtf8("не визначено")
            : preview.supplierDisplayName.trimmed();
        const QString priceColumn = preview.priceWithVatColumn >= 0
            ? currentOrdersInvoicePreviewColumnLabel(rows, preview.headerRow, preview.priceWithVatColumn)
            : QString::fromUtf8("не знайдено");
        auto* title = new QLabel(QString::fromUtf8(
            "Поточні наряди / імпорт накладної\nПостачальник: %1\nКолонка 'Ціна з ПДВ': %2\nConfidence: загальний %3%, постачальник %4%, заголовки %5%, типи даних %6%, формула суми %7%, ПДВ-ціна %8%")
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

        QStringList info;
        if (!sourceFilePath.trimmed().isEmpty()) info << QString::fromUtf8("Файл: %1").arg(QFileInfo(sourceFilePath).fileName());
        if (!preview.warnings.isEmpty()) info << QString::fromUtf8("Попередження: %1").arg(preview.warnings.join(QStringLiteral("; ")));
        if (!info.isEmpty()) {
            auto* warnings = new QLabel(info.join(QStringLiteral("\n")), this);
            warnings->setWordWrap(true);
            warnings->setStyleSheet(QStringLiteral("color: rgba(255,238,180,0.98); padding: 2px 6px;"));
            details->addWidget(warnings, 0);
        }

        auto* mappingFrame = new QFrame(this);
        mappingFrame->setObjectName("current_orders_invoice_mapping_frame");
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
        m_previewModel = new CurrentOrdersInvoiceMappingPreviewModel(rows, preview, m_table);
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
            "QFrame#current_orders_invoice_mapping_frame { background: rgba(38,48,62,210); border: 1px solid rgba(220,228,238,75); border-radius: 12px; }"
            "QLabel { color: white; }"
            "QComboBox, QLineEdit { min-height: 30px; padding: 3px 8px; background: rgba(245,247,250,245); color: #111827; border-radius: 8px; }"
            "QCheckBox { color: white; }"
            "QTableView { background: rgba(238,240,244,245); color: #111827; border: 1px solid rgba(210,220,235,85); gridline-color: rgba(132,142,154,95); }"
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
    CurrentOrdersInvoiceMappingPreviewModel* m_previewModel = nullptr;

    QComboBox* makeColumnCombo(int selectedColumn)
    {
        auto* combo = new QComboBox(this);
        combo->addItem(QString::fromUtf8("— не використовувати —"), -1);
        const int columns = currentOrdersInvoicePreviewColumnCount(m_rows);
        for (int col = 0; col < columns; ++col) {
            combo->addItem(currentOrdersInvoicePreviewColumnLabel(m_rows, m_preview.headerRow, col), col);
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

        const QString priceHeader = normalizedCurrentOrdersInvoiceMappingHeader(currentOrdersInvoicePreviewCell(m_rows, m_preview.headerRow, priceCol));
        if (currentOrdersInvoiceMappingHeaderHasVatExcludedMarker(priceHeader)) {
            const auto answer = QMessageBox::warning(this,
                QString::fromUtf8("Увага: ціна без ПДВ"),
                QString::fromUtf8("Вибрана колонка схожа на 'Ціна без ПДВ'. Автоматично її використовувати заборонено.\n"
                                  "Продовжити тільки якщо ви явно підтверджуєте, що ця ціна вже коректно приведена до ціни з ПДВ."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        } else if (!currentOrdersInvoiceMappingHeaderHasVatIncludedMarker(priceHeader)) {
            const auto answer = QMessageBox::question(this,
                QString::fromUtf8("Підтвердження ціни з ПДВ"),
                QString::fromUtf8("Назва вибраної колонки не містить явної ознаки ПДВ.\n"
                                  "Підтверджуєте, що це саме ціна з ПДВ?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }

        m_confirmed.partNameColumn = nameCol;
        m_confirmed.articleColumn = comboColumn(m_articleCombo);
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
        m_confirmed.vatPriceScore = currentOrdersInvoiceMappingHeaderHasVatIncludedMarker(priceHeader) ? 100 : qMax(60, m_confirmed.vatPriceScore);
        m_confirmed.totalConfidence = qMax(60, m_confirmed.totalConfidence);
        accept();
    }
};

bool confirmCurrentOrdersInvoiceColumnMapping(QWidget* parent,
                                                    const QList<QStringList>& rows,
                                                    const InvoiceImport::InvoiceColumnMappingPreview& preview,
                                                    const QString& sourceFilePath,
                                                    InvoiceImport::InvoiceColumnMappingPreview* confirmedMapping)
{
    CurrentOrdersInvoiceColumnMappingDialog dlg(rows, preview, sourceFilePath, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    if (confirmedMapping) *confirmedMapping = dlg.confirmedMapping();
    return true;
}


class CurrentOrdersWordStyleDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static QColor colorFromBrushOrColorVariant(const QVariant& value)
    {
        const QBrush brush = qvariant_cast<QBrush>(value);
        if (brush.style() != Qt::NoBrush && brush.color().isValid()) return brush.color();
        return qvariant_cast<QColor>(value);
    }

    static QColor cellFillColor(const QStyleOptionViewItem& option, const QModelIndex& index)
    {
        if (option.state.testFlag(QStyle::State_Selected)) return QColor(QStringLiteral("#dce8fb"));
        const QColor overdueBg = colorFromBrushOrColorVariant(index.data(Qt::BackgroundRole));
        if (overdueBg.isValid()) return overdueBg;
        return QColor(QStringLiteral("#ffffff"));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const QColor fill = cellFillColor(option, index);
        const QColor explicitFg = colorFromBrushOrColorVariant(index.data(Qt::ForegroundRole));
        painter->save();
        painter->fillRect(opt.rect, fill);
        painter->restore();

        // Match the stable v2.1.1 painting path: draw the overdue/order fill
        // ourselves and keep the style engine from repainting the normal panel
        // back to white. Selection still uses the same fill through Highlight.
        opt.backgroundBrush = Qt::NoBrush;
        opt.palette.setBrush(QPalette::Base, Qt::NoBrush);
        opt.palette.setBrush(QPalette::AlternateBase, Qt::NoBrush);
        opt.palette.setBrush(QPalette::Window, Qt::NoBrush);
        opt.palette.setBrush(QPalette::Highlight, QBrush(fill));
        if (explicitFg.isValid()) {
            opt.palette.setColor(QPalette::Text, explicitFg);
            opt.palette.setColor(QPalette::WindowText, explicitFg);
            opt.palette.setColor(QPalette::HighlightedText, explicitFg);
        }

        if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
            const int editingRow = view->property("inlineEditingRow").toInt();
            const int editingCol = view->property("inlineEditingCol").toInt();
            const bool isEditingCell = view->property("inlineEditorActive").toBool() &&
                                       editingRow == index.row() && editingCol == index.column();
            if (isEditingCell) {
                painter->save();
                painter->fillRect(option.rect, fill);
                painter->setPen(QColor(QStringLiteral("#c7cfdb")));
                painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
                painter->restore();
                return;
            }
        }
        QStyledItemDelegate::paint(painter, opt, index);
        // Keep the urgency marker visible without hiding row selection.
        if (option.state.testFlag(QStyle::State_Selected)
            && colorFromBrushOrColorVariant(index.data(Qt::BackgroundRole)).isValid()) {
            painter->save();
            painter->fillRect(QRect(opt.rect.left(), opt.rect.bottom() - 2, opt.rect.width(), 3),
                              QColor(QStringLiteral("#c62828")));
            painter->restore();
        }
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString editorBg = cellFillColor(option, index).name();
        if (index.column() == 3) {
            auto* combo = new QComboBox(parent);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->setFrame(false);
            combo->setAttribute(Qt::WA_Hover, false);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
            combo->setAutoFillBackground(true);
            combo->addItems(PartImportTextSupport::knownStoreNames());
            combo->setStyleSheet(QStringLiteral(
                "QComboBox{"
                "background: %1;"
                "color:#111827;"
                "border:none;"
                "padding:0 18px 0 1px;"
                "margin:0;"
                "font-size:12pt;"
                "}"
                "QComboBox::drop-down{"
                "width:18px;"
                "border:none;"
                "background: transparent;"
                "}"
                "QComboBox QAbstractItemView{"
                "background:#ffffff;"
                "color:#111827;"
                "border:1px solid #c7cfdb;"
                "selection-background-color:#dce8fb;"
                "selection-color:#0f172a;"
                "}"
                "QLineEdit{"
                "background: %1;"
                "color:#111827;"
                "selection-background-color:#bfd3f6;"
                "selection-color:#0f172a;"
                "border:none;"
                "padding:0 1px;"
                "margin:0;"
                "font-size:12pt;"
                "}"
            ).arg(editorBg));
            QPalette pal = combo->palette();
            pal.setColor(QPalette::Base, cellFillColor(option, index));
            pal.setColor(QPalette::Button, cellFillColor(option, index));
            pal.setColor(QPalette::Window, cellFillColor(option, index));
            pal.setColor(QPalette::Text, QColor(QStringLiteral("#111827")));
            pal.setColor(QPalette::ButtonText, QColor(QStringLiteral("#111827")));
            combo->setPalette(pal);
            if (auto* line = combo->lineEdit()) {
                line->setAutoFillBackground(true);
                line->setPalette(pal);
                line->setFrame(false);
            }
            combo->installEventFilter(const_cast<CurrentOrdersWordStyleDelegate*>(this));
            if (combo->lineEdit()) combo->lineEdit()->installEventFilter(const_cast<CurrentOrdersWordStyleDelegate*>(this));
            markEditorActive(index, combo);
            return combo;
        }

        auto* edit = new QLineEdit(parent);
        edit->setFrame(false);
        edit->setAttribute(Qt::WA_Hover, false);
        edit->setAutoFillBackground(true);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit{"
            "background: %1;"
            "color:#111827;"
            "selection-background-color:#bfd3f6;"
            "selection-color:#0f172a;"
            "border:none;"
            "padding:0 1px;"
            "margin:0;"
            "font-size:12pt;"
            "}"
            "QLineEdit:focus{"
            "background: %1;"
            "border:none;"
            "outline:none;"
            "}"
        ).arg(editorBg));
        QPalette pal = edit->palette();
        pal.setColor(QPalette::Base, cellFillColor(option, index));
        pal.setColor(QPalette::Window, cellFillColor(option, index));
        pal.setColor(QPalette::Text, QColor(QStringLiteral("#111827")));
        edit->setPalette(pal);
        edit->installEventFilter(const_cast<CurrentOrdersWordStyleDelegate*>(this));
        markEditorActive(index, edit);
        return edit;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            const QString raw = PartImportTextSupport::titleCaseWords(index.data(Qt::EditRole).toString());
            combo->setEditText(raw);
            if (combo->lineEdit()) {
                combo->lineEdit()->setCursorPosition(raw.size());
                combo->lineEdit()->deselect();
            }
            return;
        }

        auto* edit = qobject_cast<QLineEdit*>(editor);
        if (!edit) {
            QStyledItemDelegate::setEditorData(editor, index);
            return;
        }
        const QString raw = index.data(Qt::EditRole).toString();
        edit->setText(raw);
        edit->setCursorPosition(raw.size());
        edit->deselect();
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            const QString value = PartImportTextSupport::titleCaseWords(combo->currentText());
            model->setData(index, value, Qt::EditRole);
            model->setData(index, value, Qt::DisplayRole);
            model->setData(index, QString(), Qt::ToolTipRole);
            return;
        }

        auto* edit = qobject_cast<QLineEdit*>(editor);
        if (!edit) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }
        model->setData(index, edit->text(), Qt::EditRole);
        model->setData(index, edit->text(), Qt::DisplayRole);
        model->setData(index, QString(), Qt::ToolTipRole);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        if (auto* combo = qobject_cast<QComboBox*>(obj)) {
            if (ev->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                    emit commitData(combo);
                    emit closeEditor(combo, QAbstractItemDelegate::NoHint);
                    return true;
                }
                if (ke->key() == Qt::Key_Escape) {
                    emit closeEditor(combo, QAbstractItemDelegate::RevertModelCache);
                    return true;
                }
            }
        }
        if (auto* edit = qobject_cast<QLineEdit*>(obj)) {
            if (ev->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                QWidget* editorWidget = edit;
                if (auto* combo = qobject_cast<QComboBox*>(edit->parentWidget())) editorWidget = combo;
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                    emit commitData(editorWidget);
                    emit closeEditor(editorWidget, QAbstractItemDelegate::NoHint);
                    return true;
                }
                if (ke->key() == Qt::Key_Escape) {
                    emit closeEditor(editorWidget, QAbstractItemDelegate::RevertModelCache);
                    return true;
                }
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }

private:
    void markEditorActive(const QModelIndex& index, QWidget* editor) const
    {
        if (auto* view = qobject_cast<QAbstractItemView*>(this->parent())) {
            view->setProperty("inlineEditorActive", true);
            view->setProperty("inlineEditingRow", index.row());
            view->setProperty("inlineEditingCol", index.column());
            QObject::connect(editor, &QObject::destroyed, view, [view]() {
                view->setProperty("inlineEditorActive", false);
                view->setProperty("inlineEditingRow", -1);
                view->setProperty("inlineEditingCol", -1);
                if (view->viewport()) view->viewport()->update();
            });
            if (view->viewport()) view->viewport()->update(view->visualRect(index));
        }
    }
};


class CurrentOrdersExcelImportDialog final : public InvoiceReviewDialog {
public:
    CurrentOrdersExcelImportDialog(const QString& sourceFilePath, const InvoiceImport::ExcelReadResult& data,
                                  const QList<ImportedPartDraft>& drafts, QWidget* parent)
        : InvoiceReviewDialog(drafts, data, parent), m_sourceFilePath(sourceFilePath),
          m_canonicalPdfAttachmentPath(data.canonicalPdfAttachmentPath), m_canonicalPdfAttachmentError(data.canonicalPdfAttachmentError) {
        commit = [this](QString* error) {
            m_invoiceNumber=invoiceNumber(); m_invoiceDate=invoiceDate();
            QList<CurrentOrdersImportDraftRow> rows;
            for(int r:selectedRows()) { CurrentOrdersImportDraftRow row;
                row.orderedAt=m_invoiceDate;row.deliveryDate=text(r,1);row.orderNumberText=text(r,2);
                row.store=text(r,3);row.oemNumber=text(r,4);row.name=text(r,5);row.brandName=text(r,6);
                row.qtyText=text(r,7);row.priceText=text(r,8);rows.append(row);
            }
            return importRows(rows,error);
        };
    }
private:
    QString m_sourceFilePath,m_canonicalPdfAttachmentPath,m_canonicalPdfAttachmentError,m_invoiceNumber,m_invoiceDate;
    bool importRows(const QList<CurrentOrdersImportDraftRow>& rows, QString* errorText) const
    {
        const auto validation = CurrentOrdersCoordinator::buildInvoiceImportRows(rows);
        if (!validation.success) {
            if (errorText) *errorText = validation.errorText;
            return false;
        }

        CurrentOrdersPage* const page = qobject_cast<CurrentOrdersPage*>(parentWidget());

        QPointer<ImportFlightAnimation::LoopHandle> flightHandle;
        if (page) {
            const int targetOrderNumber = validation.uniqueOrderNumbers.size() == 1 ? validation.uniqueOrderNumbers.constFirst() : -1;
            flightHandle = ImportFlightAnimation::startLoop(
                page,
                [page, targetOrderNumber]() -> QRect {
                    return page ? page->partsCellRectForOrder(targetOrderNumber) : QRect();
                },
                1250,
                860);
        }

        auto stopFlight = [&flightHandle]() {
            if (flightHandle) flightHandle->stop(true);
        };
        CurrentOrdersInvoiceImportResult importResult;
        CurrentOrdersInvoiceAttachmentSpec attachmentSpec;
        const QString sourceSuffix = QFileInfo(m_sourceFilePath).suffix().trimmed().toLower();
        const bool spreadsheetSource = sourceSuffix == QStringLiteral("xlsx") || sourceSuffix == QStringLiteral("xls") ||
                                       sourceSuffix == QStringLiteral("xlsm") || sourceSuffix == QStringLiteral("xltx") ||
                                       sourceSuffix == QStringLiteral("xltm") || sourceSuffix == QStringLiteral("csv") ||
                                       sourceSuffix == QStringLiteral("txt");
        const QString attachmentPath = !m_canonicalPdfAttachmentPath.trimmed().isEmpty()
            ? m_canonicalPdfAttachmentPath.trimmed()
            : (spreadsheetSource ? QString() : m_sourceFilePath);
        if (spreadsheetSource && attachmentPath.trimmed().isEmpty()) {
            if (errorText) {
                *errorText = m_canonicalPdfAttachmentError.trimmed().isEmpty()
                    ? QString::fromUtf8("Excel-накладну не збережено: PDF у єдиному форматі ще не підготовлено.")
                    : m_canonicalPdfAttachmentError.trimmed();
            }
            return false;
        }
        attachmentSpec.sourceFilePath = attachmentPath;
        attachmentSpec.displayName = QFileInfo(attachmentPath).fileName();
        for (const auto& serviceRow : validation.serviceRows) {
            const QString store = serviceRow.store.trimmed();
            if (!store.isEmpty()) {
                attachmentSpec.storeName = store;
                break;
            }
        }
        attachmentSpec.invoiceNumber = m_invoiceNumber;
        attachmentSpec.invoiceDate = m_invoiceDate;

        const bool ok = CurrentOrdersCoordinator::importInvoiceRows(validation.serviceRows,
                                                                    attachmentSpec,
                                                                    &importResult,
                                                                    errorText);
        stopFlight();
        if (!ok) return false;

        for (int orderNumber : importResult.affectedOrderNumbers) {
            publishCurrentOrdersBackupEvent(QStringLiteral("parts_import_invoice"),
                                            orderNumber,
                                            QStringLiteral("invoice_import"),
                                            QString::number(orderNumber),
                                            QString::fromUtf8("Імпортовано запчастини з накладної у наряд №%1").arg(orderNumber),
                                            {QStringLiteral("work_order_parts"), QStringLiteral("part_attachments"), QStringLiteral("work_orders")});
        }
        return true;
    }

};

void prepareActionMenu(QMenu& menu)
{
    ContextMenuAppearance::apply(menu);
}

#include "ui/tabs/orders/internal/CurrentOrdersDisplayProxy.ipp"

QAbstractItemDelegate* createWordStyleDelegate(QObject* parent)
{
    return new CurrentOrdersWordStyleDelegate(parent);
}

QSortFilterProxyModel* createDisplayProxy(QObject* parent)
{
    return new CurrentOrdersDisplayProxy(parent);
}

std::unique_ptr<InvoiceReviewDialog> createInvoiceReview(const QString& path,
    const ExcelReadResult& data, const QList<ImportedPartDraft>& drafts, QWidget* parent)
{
    return std::make_unique<CurrentOrdersExcelImportDialog>(path, data, drafts, parent);
}

bool reviewInvoice(const QString& path, const ExcelReadResult& data,
                   const QList<ImportedPartDraft>& drafts, QWidget* parent)
{
    auto dialog = createInvoiceReview(path, data, drafts, parent);
    return dialog->exec() == QDialog::Accepted;
}
}
