#include "core/util/DateFormats.h"
#include "ui/common/dialogs/ReceivedScansDialog.h"

#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QAbstractTableModel>
#include <QDate>
#include <QDebug>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <utility>
#include <QFrame>

namespace {

QString safeText(QString value, const QString& fallback = QString())
{
    value = value.trimmed();
    return value.isEmpty() ? fallback : value;
}

constexpr int kColDate = 0;
constexpr int kColOrder = 1;
constexpr int kColOem = 2;
constexpr int kColName = 3;
constexpr int kColQty = 4;
constexpr int kColPrice = 5;
constexpr int kColStore = 6;
constexpr int kColumnCount = 7;

class ReceivedScansTableModel final : public QAbstractTableModel
{
public:
    explicit ReceivedScansTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : kColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
        static const QStringList headers = {QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"),
                                            QString::fromUtf8("OEM номер"), QString::fromUtf8("Замовлення"),
                                            QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Ціна"),
                                            QString::fromUtf8("Магазин")};
        return section >= 0 && section < headers.size() ? headers.at(section) : QVariant();
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || index.column() < 0 || index.column() >= kColumnCount) return QVariant();
        if (role == Qt::DisplayRole || role == Qt::EditRole) return m_rows.at(index.row()).value(index.column());
        if (role == Qt::ToolTipRole) return QString();
        return QVariant();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || index.column() < 0 || index.column() >= kColumnCount) return false;
        if (role != Qt::EditRole && role != Qt::DisplayRole) return false;
        QStringList row = m_rows.at(index.row());
        while (row.size() < kColumnCount) row.append(QString());
        row[index.column()] = value.toString();
        m_rows[index.row()] = row;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        if (!index.isValid()) return Qt::NoItemFlags;
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }

    void setRows(QVector<QStringList> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

    QString cellText(int row, int column) const
    {
        if (row < 0 || row >= m_rows.size() || column < 0 || column >= kColumnCount) return QString();
        return m_rows.at(row).value(column).trimmed();
    }

private:
    QVector<QStringList> m_rows;
};

class ReceivedScansItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static QColor cellFillColor(const QStyleOptionViewItem& option)
    {
        return option.state.testFlag(QStyle::State_Selected)
            ? QColor(QStringLiteral("#dce8fb"))
            : QColor(QStringLiteral("#ffffff"));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (auto* table = qobject_cast<QTableView*>(parent())) {
            const int editingRow = table->property("inlineEditingRow").toInt();
            const int editingCol = table->property("inlineEditingCol").toInt();
            const bool isEditingCell = table->property("inlineEditorActive").toBool() &&
                                       editingRow == index.row() && editingCol == index.column();
            if (isEditingCell) {
                painter->save();
                painter->fillRect(option.rect, cellFillColor(option));
                painter->setPen(QColor(QStringLiteral("#c7cfdb")));
                painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
                painter->restore();
                return;
            }
        }
        QStyledItemDelegate::paint(painter, option, index);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto* edit = new QLineEdit(parent);
        edit->setFrame(false);
        edit->setAttribute(Qt::WA_Hover, false);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit{"
            "background:#ffffff;"
            "color:#111827;"
            "selection-background-color:#bfd3f6;"
            "selection-color:#0f172a;"
            "border:none;"
            "padding:0 2px;"
            "font-size:12pt;"
            "}"
        ));
        edit->installEventFilter(const_cast<ReceivedScansItemDelegate*>(this));
        if (auto* table = qobject_cast<QTableView*>(this->parent())) {
            table->setProperty("inlineEditorActive", true);
            table->setProperty("inlineEditingRow", index.row());
            table->setProperty("inlineEditingCol", index.column());
            QObject::connect(edit, &QObject::destroyed, table, [table]() {
                table->setProperty("inlineEditorActive", false);
                table->setProperty("inlineEditingRow", -1);
                table->setProperty("inlineEditingCol", -1);
                table->viewport()->update();
            });
            table->viewport()->update();
        }
        return edit;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
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
        if (auto* edit = qobject_cast<QLineEdit*>(obj)) {
            if (ev->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                    emit commitData(edit);
                    emit closeEditor(edit, QAbstractItemDelegate::NoHint);
                    return true;
                }
                if (ke->key() == Qt::Key_Escape) {
                    emit closeEditor(edit, QAbstractItemDelegate::RevertModelCache);
                    return true;
                }
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        editor->setGeometry(option.rect.adjusted(6, 1, -6, -1));
    }
};

} // namespace

ReceivedScansDialog::ReceivedScansDialog(const SyncPayloadUtils::SyncPayload& payload,
                                         int defaultOrderNumber,
                                         QWidget* parent)
    : QDialog(parent)
    , m_payload(payload)
    , m_sourceRows(payload.parts)
{
    setWindowTitle(QString::fromUtf8("Прийняті скани"));
    resize(1180, 680);
    setModal(true);
    setStyleSheet(QStringLiteral(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(34,45,58,0.98),stop:1 rgba(20,28,37,0.98));}"
        "QLabel{color:#eef4fc;}"
        "QFrame#card{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.10);border-radius:14px;}"
        "QTableView{background:rgba(255,255,255,0.94);color:#111827;gridline-color:#c7cfdb;border:1px solid rgba(255,255,255,0.12);border-radius:10px;}"
        "QTableView::item:selected{background:#dce8fb;color:#0f172a;}"
        "QHeaderView::section{background:#f2f4f8;color:#111827;padding:8px 6px;border:none;border-right:1px solid #d7dee8;border-bottom:1px solid #d7dee8;font-weight:700;}"
        "QPushButton{min-height:42px;border-radius:12px;padding:0 18px;font-weight:700;}"
        "QPushButton#importBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2ca168,stop:1 #1f7d52);color:#f8fffb;border:1px solid rgba(255,255,255,0.12);}"
        "QPushButton#cancelBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #334155,stop:1 #1f2937);color:#f3f7fb;border:1px solid rgba(255,255,255,0.10);}"
    ));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);

    auto* title = new QLabel(QString::fromUtf8("Прийняті скани для перевірки перед імпортом"), this);
    title->setStyleSheet(QStringLiteral("font-size:18pt;font-weight:800;color:#f7fbff;"));
    outer->addWidget(title);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* cardL = new QVBoxLayout(card);
    cardL->setContentsMargins(14, 14, 14, 14);
    cardL->setSpacing(10);

    QStringList meta;
    if (!payload.documentNumber.trimmed().isEmpty()) meta << QString::fromUtf8("Документ: %1").arg(payload.documentNumber.trimmed());
    if (!payload.documentDate.trimmed().isEmpty()) meta << QString::fromUtf8("Дата: %1").arg(displayDateText(payload.documentDate));
    if (!payload.supplier.trimmed().isEmpty()) meta << QString::fromUtf8("Постачальник: %1").arg(payload.supplier.trimmed());
    if (!payload.notes.trimmed().isEmpty()) meta << QString::fromUtf8("Нотатка: %1").arg(payload.notes.trimmed());
    auto* metaLabel = new QLabel(meta.join(QStringLiteral("    •    ")), card);
    metaLabel->setWordWrap(true);
    metaLabel->setStyleSheet(QStringLiteral("font-size:12pt;color:#dfe9f5;"));
    cardL->addWidget(metaLabel);

    m_hint = new QLabel(QString::fromUtf8("Перевір дані після OCR. Усі колонки редагуються вручну. 'Дата' за замовчуванням ставиться поточна, 'Наряд' визначає цільовий наряд, 'OEM номер' бере номер деталі з накладної, 'Магазин' імпортується з парсера / постачальника."), card);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#d5e1ef;background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.08);border-radius:10px;padding:10px;"));
    cardL->addWidget(m_hint);

    m_table = new QTableView(card);
    m_tableModel = new ReceivedScansTableModel(m_table);
    m_table->setModel(m_tableModel);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColDate, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColOrder, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColOem, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColQty, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColPrice, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColStore, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(false);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed | QAbstractItemView::SelectedClicked);
    m_table->setItemDelegate(new ReceivedScansItemDelegate(m_table));
    m_table->setMinimumHeight(420);
    cardL->addWidget(m_table, 1);

    populateRows(defaultOrderNumber);
    outer->addWidget(card, 1);

    auto* buttonsRow = new QHBoxLayout();
    buttonsRow->addStretch(1);
    auto* cancelBtn = new QPushButton(QString::fromUtf8("Скасувати"), this);
    cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    auto* importBtn = new QPushButton(QString::fromUtf8("Імпорт в наряд"), this);
    importBtn->setObjectName(QStringLiteral("importBtn"));
    buttonsRow->addWidget(cancelBtn);
    buttonsRow->addWidget(importBtn);
    outer->addLayout(buttonsRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(importBtn, &QPushButton::clicked, this, &ReceivedScansDialog::handleImport);
}

QString ReceivedScansDialog::displayDateText(const QString& isoDate)
{
    const QString trimmed = isoDate.trimmed();
    if (trimmed.isEmpty()) return QString();
    const QDate iso = DateFormats::parseDate(trimmed);
    if (iso.isValid()) return iso.toString(QStringLiteral("dd.MM.yy"));
    const QStringList formats = {QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd.MM.yy"), QStringLiteral("dd/MM/yyyy"), QStringLiteral("dd/MM/yy")};
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(trimmed, fmt);
        if (d.isValid()) return d.toString(QStringLiteral("dd.MM.yy"));
    }
    return trimmed;
}

QString ReceivedScansDialog::normalizeDateText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return QDate::currentDate().toString(Qt::ISODate);
    const QDate iso = DateFormats::parseDate(trimmed);
    if (iso.isValid()) return iso.toString(Qt::ISODate);
    const QStringList formats = {QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd.MM.yy"), QStringLiteral("dd/MM/yyyy"), QStringLiteral("dd/MM/yy")};
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(trimmed, fmt);
        if (d.isValid()) return d.toString(Qt::ISODate);
    }
    return trimmed;
}

QString ReceivedScansDialog::numberText(double value)
{
    if (qFuzzyIsNull(value)) return QStringLiteral("0");
    QString text = QString::number(value, 'f', 4);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.'))) {
        if (text.endsWith('.')) { text.chop(1); break; }
        text.chop(1);
    }
    return text;
}

double ReceivedScansDialog::parseLooseDouble(const QString& text, bool* ok)
{
    QString s = text.trimmed();
    s.remove(QChar(' '));
    s.replace(QChar(','), QChar('.'));
    bool localOk = false;
    const double value = s.toDouble(&localOk);
    if (ok) *ok = localOk;
    return value;
}

void ReceivedScansDialog::populateRows(int defaultOrderNumber)
{
    // Pass10 performance closure: import/OCR preview is snapshot-model driven,
    // not item-object row-by-row allocation while opening the preview.
    auto* model = static_cast<ReceivedScansTableModel*>(m_tableModel);
    QVector<QStringList> rows;
    rows.reserve(m_sourceRows.size());
    const int payloadOrder = m_payload.targetOrderNumber();
    const int effectiveOrder = defaultOrderNumber > 0 ? defaultOrderNumber : payloadOrder;
    const QString todayText = QDate::currentDate().toString(QStringLiteral("dd.MM.yy"));
    const QString fallbackStore = safeText(m_payload.supplier);
    for (const auto& source : std::as_const(m_sourceRows)) {
        const QString displayName = safeText(source.name, safeText(source.partNumber, source.note));
        const QString displayStore = safeText(source.store, fallbackStore);
        rows.push_back({todayText,
                        effectiveOrder > 0 ? QString::number(effectiveOrder) : QString(),
                        source.partNumber.trimmed(),
                        displayName,
                        numberText(source.qty),
                        numberText(source.purchasePrice > 0.0 ? source.purchasePrice : source.clientPrice),
                        displayStore});
    }
    model->setRows(std::move(rows));
    if (model->rowCount() > 0) m_table->setCurrentIndex(model->index(0, kColOrder));
}


QList<ReceivedScansDialog::ImportRow> ReceivedScansDialog::importRows() const
{
    QList<ImportRow> out;
    auto* model = static_cast<ReceivedScansTableModel*>(m_tableModel);
    for (int rowIndex = 0; rowIndex < model->rowCount() && rowIndex < m_sourceRows.size(); ++rowIndex) {
        const QString dateText = model->cellText(rowIndex, kColDate);
        const QString orderText = model->cellText(rowIndex, kColOrder);
        const QString oemText = model->cellText(rowIndex, kColOem);
        const QString nameText = model->cellText(rowIndex, kColName);
        const QString qtyText = model->cellText(rowIndex, kColQty);
        const QString priceText = model->cellText(rowIndex, kColPrice);
        const QString storeText = model->cellText(rowIndex, kColStore);

        bool orderOk = false;
        const int orderNumber = orderText.toInt(&orderOk);
        bool qtyOk = false;
        const double qty = parseLooseDouble(qtyText, &qtyOk);
        bool priceOk = false;
        const double price = parseLooseDouble(priceText, &priceOk);
        if (!orderOk || orderNumber <= 0 || !qtyOk || qty <= 0.0 || !priceOk || price < 0.0) continue;

        ImportRow importRow;
        importRow.orderNumber = orderNumber;
        importRow.row = m_sourceRows.at(rowIndex);
        importRow.row.orderedAt = normalizeDateText(dateText);
        importRow.row.partNumber = oemText;
        importRow.row.name = nameText;
        importRow.row.store = storeText;
        importRow.row.qty = qty;
        PartRowPayloadInput payloadInput;
        payloadInput.orderedAt = importRow.row.orderedAt;
        payloadInput.deliveryDate = QDate::currentDate().toString(Qt::ISODate);
        payloadInput.store = importRow.row.store;
        payloadInput.partNumber = importRow.row.partNumber;
        payloadInput.name = importRow.row.name;
        payloadInput.note = importRow.row.note;
        payloadInput.manufacturer = importRow.row.manufacturer;
        payloadInput.qty = qty;
        payloadInput.purchasePrice = price;
        payloadInput.sourceType = QStringLiteral("scan");
        importRow.row = PartRowPayloadBuilderService::build(payloadInput);
        importRow.row.deliveryDate = payloadInput.deliveryDate;
        importRow.row.orderNumber = orderNumber;
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("PART_PRICING_SCAN_IMPORT")
                              << QStringLiteral(" orderNumber=") << orderNumber
                              << QStringLiteral(" partName=") << importRow.row.name
                              << QStringLiteral(" store=") << importRow.row.store
                              << QStringLiteral(" qty=") << importRow.row.qty
                              << QStringLiteral(" purchasePrice=") << importRow.row.purchasePrice
                              << QStringLiteral(" clientPrice=") << importRow.row.clientPrice
                              << QStringLiteral(" markupPercent=") << importRow.row.markupPercent;
        }
        out.push_back(importRow);
    }
    return out;
}


void ReceivedScansDialog::handleImport()
{
    QStringList errors;
    auto* model = static_cast<ReceivedScansTableModel*>(m_tableModel);
    for (int rowIndex = 0; rowIndex < model->rowCount(); ++rowIndex) {
        const QString dateText = model->cellText(rowIndex, kColDate);
        const QString orderText = model->cellText(rowIndex, kColOrder);
        const QString nameText = model->cellText(rowIndex, kColName);
        const QString qtyText = model->cellText(rowIndex, kColQty);
        const QString priceText = model->cellText(rowIndex, kColPrice);

        bool orderOk = false;
        const int orderNumber = orderText.toInt(&orderOk);
        if (!orderOk || orderNumber <= 0) errors << QString::fromUtf8("Рядок %1: вкажи коректний номер наряду.").arg(rowIndex + 1);
        if (nameText.trimmed().isEmpty()) errors << QString::fromUtf8("Рядок %1: заповни колонку 'Замовлення'.").arg(rowIndex + 1);
        bool qtyOk = false;
        const double qty = parseLooseDouble(qtyText, &qtyOk);
        if (!qtyOk || qty <= 0.0) errors << QString::fromUtf8("Рядок %1: кіл-ть має бути більшою за 0.").arg(rowIndex + 1);
        bool priceOk = false;
        const double price = parseLooseDouble(priceText, &priceOk);
        if (!priceOk || price < 0.0) errors << QString::fromUtf8("Рядок %1: ціна введена некоректно.").arg(rowIndex + 1);
        if (normalizeDateText(dateText).trimmed().isEmpty()) errors << QString::fromUtf8("Рядок %1: дата введена некоректно.").arg(rowIndex + 1);
    }

    if (!errors.isEmpty()) {
        QMessageBox::warning(this,
                             QString::fromUtf8("Прийняті скани"),
                             errors.join(QStringLiteral("\n")));
        return;
    }
    accept();
}
