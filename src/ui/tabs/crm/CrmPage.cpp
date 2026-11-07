#include "core/crm/CrmPageReadService.h"
#include "core/crm/CrmPageApplicationService.h"
#include "core/services/settings/UiStateSettingsService.h"
#include "core/util/DateFormats.h"
#include "ui/tabs/crm/CrmPage.h"
#include "ui/main/MainWindow.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "core/services/AppRuntimeService.h"
#include "core/perf/PerfWatchdog.h"

#include "core/crm/CrmClientService.h"
#include "core/crm/CrmTaskService.h"
#include "core/crm/CrmTimelineService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/crm/CrmFinancialService.h"
#include "core/crm/VehiclePreviewProvider.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPainterPath>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QLineEdit>
#include <QLayoutItem>

#include <memory>

namespace {

constexpr int kCrmHistoryOrderNumberRole = Qt::UserRole + 1;

struct TableFillGuard {
    explicit TableFillGuard(QTableWidget* table)
        : table(table)
        , sorting(table ? table->isSortingEnabled() : false)
        , scroll(table && table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0)
    {
        if (!table) return;
        blocker = std::make_unique<QSignalBlocker>(table);
        table->setUpdatesEnabled(false);
        table->setSortingEnabled(false);
    }

    ~TableFillGuard()
    {
        if (!table) return;
        table->setSortingEnabled(sorting);
        if (table->verticalScrollBar()) table->verticalScrollBar()->setValue(scroll);
        table->setUpdatesEnabled(true);
    }

    QTableWidget* table = nullptr;
    std::unique_ptr<QSignalBlocker> blocker;
    bool sorting = false;
    int scroll = 0;
};

void setColumnsOnce(QTableWidget* table, const QList<int>& widths)
{
    if (!table) return;
    for (int column = 0; column < widths.size() && column < table->columnCount(); ++column) {
        if (widths.at(column) > 0) table->setColumnWidth(column, widths.at(column));
    }
}

QTableWidgetItem* cell(const QString& text, const QString& uid = QString())
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setData(Qt::UserRole, uid.trimmed());
    return item;
}

QFont crmClientCardFont()
{
    QFont font(QStringLiteral("Times New Roman"));
    font.setPointSize(12);
    font.setWeight(QFont::Normal);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    return font;
}

QFont crmClientCardNameFont()
{
    QFont font(QStringLiteral("Times New Roman"));
    font.setPointSize(14);
    font.setWeight(QFont::DemiBold);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    return font;
}

QFont crmClientCardInfoFont()
{
    QFont font(QStringLiteral("Times New Roman"));
    font.setPointSize(12);
    font.setWeight(QFont::Normal);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    return font;
}

class CrmClientCardDelegate final : public QStyledItemDelegate {
public:
    explicit CrmClientCardDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!painter) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const QRectF cardRect = QRectF(option.rect).adjusted(3.0, 4.0, -3.0, -4.0);
        QPainterPath cardPath;
        cardPath.addRoundedRect(cardRect, 10.0, 10.0);

        const QColor fill = selected ? QColor(42, 55, 74, 240)
                          : hovered ? QColor(45, 55, 68, 235)
                                    : QColor(23, 29, 37, 219);
        const QColor border = selected ? QColor(186, 216, 255, 184)
                            : hovered ? QColor(220, 236, 255, 92)
                                      : QColor(255, 255, 255, 33);
        painter->fillPath(cardPath, fill);
        painter->setPen(QPen(border, 1.0));
        painter->drawPath(cardPath);

        const QStringList lines = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n'));
        QRect textRect = option.rect.adjusted(15, 9, -15, -9);

        painter->setPen(selected ? QColor(255, 255, 255) : QColor(246, 249, 253));
        painter->setFont(crmClientCardNameFont());
        painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), 22), Qt::AlignLeft | Qt::AlignVCenter, lines.value(0));

        painter->setPen(QColor(216, 227, 239));
        painter->setFont(crmClientCardInfoFont());
        painter->drawText(QRect(textRect.left(), textRect.top() + 25, textRect.width(), 19), Qt::AlignLeft | Qt::AlignVCenter, lines.value(1));
        const QRect vehicleRowRect(textRect.left(), textRect.top() + 46, textRect.width(), 19);
        const QString vehicleLine = lines.value(2), vehicleSeparator = QString::fromUtf8(" • "); const int vehicleSeparatorIndex = vehicleLine.indexOf(vehicleSeparator);
        if (vehicleSeparatorIndex >= 0) {
            const QString vehiclePrefix = vehicleLine.left(vehicleSeparatorIndex) + QString::fromUtf8(" •"), vehicleName = vehicleLine.mid(vehicleSeparatorIndex + vehicleSeparator.size()).trimmed();
            const QFontMetrics metrics(painter->font()); const int prefixWidth = qMin(metrics.horizontalAdvance(vehiclePrefix) + 8, vehicleRowRect.width());
            const QRect prefixRect(vehicleRowRect.left(), vehicleRowRect.top(), prefixWidth, vehicleRowRect.height());
            painter->drawText(prefixRect, Qt::AlignLeft | Qt::AlignVCenter, vehiclePrefix);
            const QRect nameRect(vehicleRowRect.left() + prefixWidth, vehicleRowRect.top(), qMax(0, vehicleRowRect.width() - prefixWidth), vehicleRowRect.height());
            painter->setPen(QColor(255, 83, 83));
            painter->drawText(nameRect, Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(vehicleName, Qt::ElideLeft, nameRect.width()));
        } else {
            painter->drawText(vehicleRowRect, Qt::AlignLeft | Qt::AlignVCenter, vehicleLine);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(78);
        return size;
    }
};

void prepareTable(QTableWidget* table, const QStringList& headers, bool premiumDark = false)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    if (premiumDark) {
        table->setStyleSheet(QStringLiteral(
            "QTableWidget{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 rgba(38,45,54,0.92),stop:0.18 rgba(66,76,88,0.64),stop:1 rgba(20,25,31,0.94));"
            "alternate-background-color:rgba(255,255,255,0.035);color:#f6f9fd;"
            "border:1px solid rgba(255,255,255,0.14);border-radius:10px;gridline-color:rgba(255,255,255,0.08);"
            "selection-background-color:rgba(136,170,220,0.34);selection-color:#ffffff;}"
            "QTableWidget::item{background:transparent;color:#f6f9fd;padding:6px;border-bottom:1px solid rgba(255,255,255,0.06);}"
            "QTableWidget::item:selected{background:rgba(136,170,220,0.34);color:#ffffff;}"
            "QHeaderView::section{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 rgba(255,255,255,0.16),stop:0.12 rgba(255,255,255,0.08),stop:1 rgba(30,36,44,0.94));"
            "color:#ffffff;padding:7px;border:1px solid rgba(255,255,255,0.12);font-weight:800;}"));
    } else {
        table->setStyleSheet(QStringLiteral(
            "QTableWidget{background:#ffffff; color:#10161d; border:1px solid rgba(255,255,255,0.16); border-radius:10px; gridline-color:rgba(25,31,39,0.10);}"
            "QHeaderView::section{background:#f3f6fa; color:#111822; padding:6px; border:1px solid rgba(20,28,36,0.08); font-weight:700;}"));
    }
}

QString compactText(const QVariant& v)
{
    const QString text = v.toString().simplified();
    return text.isEmpty() ? QString::fromUtf8("—") : text;
}

QString detailText(const QVariant& v)
{
    const QString text = v.toString().simplified();
    return text.isEmpty() ? QString::fromUtf8("немає") : text.toHtmlEscaped();
}

QString displayDate(const QVariant& value)
{
    const QString raw = value.toString().simplified();
    if (raw.isEmpty()) return QString::fromUtf8("—");

    const QStringList dateTimeFormats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("dd.MM.yyyy HH:mm:ss"),
        QStringLiteral("dd.MM.yyyy HH:mm")
    };
    for (const QString& format : dateTimeFormats) {
        const QDateTime parsed = QDateTime::fromString(raw, format);
        if (parsed.isValid()) return parsed.date().toString(QStringLiteral("dd.MM.yy"));
    }

    const QStringList dateFormats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy")
    };
    for (const QString& format : dateFormats) {
        const QDate parsed = QDate::fromString(raw.left(format.size()), format);
        if (parsed.isValid()) return parsed.toString(QStringLiteral("dd.MM.yy"));
    }

    const QDateTime isoDateTime = DateFormats::parseDateTime(raw);
    if (isoDateTime.isValid()) return isoDateTime.date().toString(QStringLiteral("dd.MM.yy"));
    const QDate isoDate = DateFormats::parseDate(raw.left(10));
    if (isoDate.isValid()) return isoDate.toString(QStringLiteral("dd.MM.yy"));
    return compactText(raw);
}

QString crmClientTableColumnWidthKey(int column)
{
    return QStringLiteral("settings.crm.clients_table.column_width.%1").arg(column);
}

QString crmShortUid(const QString& uid)
{
    const QString trimmed = uid.trimmed();
    if (trimmed.size() <= 12) return trimmed;
    return trimmed.left(6) + QStringLiteral("...") + trimmed.right(4);
}

int defaultCrmClientTableColumnWidth(int column)
{
    switch (column) {
    case 0: return 220;
    case 1: return 150;
    default: return 120;
    }
}

int minimumCrmClientTableColumnWidth(int column)
{
    switch (column) {
    case 0: return 140;
    case 1: return 110;
    default: return 90;
    }
}

void prepareClientCardsTable(QTableWidget* table)
{
    if (!table) return;
    table->setObjectName(QStringLiteral("crmClientCardsTable"));
    table->setColumnCount(1);
    table->setHorizontalHeaderLabels({QString()});
    table->horizontalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setWordWrap(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(false);
    table->setFocusPolicy(Qt::StrongFocus);
    table->setFont(crmClientCardFont());
    table->setItemDelegate(new CrmClientCardDelegate(table));
    table->setStyleSheet(QStringLiteral(
        "QTableWidget#crmClientCardsTable{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(38,45,54,0.82),stop:0.18 rgba(66,76,88,0.48),stop:1 rgba(16,20,26,0.92));"
        "font-family:'Times New Roman';font-size:12pt;color:#f6f9fd;border:1px solid rgba(255,255,255,0.14);border-radius:10px;gridline-color:transparent;"
        "selection-background-color:transparent;outline:0;padding:7px;}"
        "QTableWidget#crmClientCardsTable::item{background:transparent;color:#f6f9fd;border:none;padding:0;margin:0;}"
        "QTableWidget#crmClientCardsTable::item:hover{background:transparent;border:none;}"
        "QTableWidget#crmClientCardsTable::item:selected{background:transparent;border:none;color:#ffffff;}"
        "QScrollBar:vertical{background:rgba(255,255,255,0.04);width:10px;margin:4px 2px 4px 0;border-radius:5px;}"
        "QScrollBar::handle:vertical{background:rgba(215,228,246,0.34);border-radius:5px;min-height:32px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;background:transparent;}"));
}

QString clientCardText(const QVariant& name, const QVariant& phone, int vehicleCount, const QString& vehicleLabel)
{
    QStringList lines;
    lines << compactText(name);
    lines << QString::fromUtf8("Телефон: %1").arg(compactText(phone));
    const QString vehicleText = vehicleCount > 0
        ? QString::fromUtf8("Авто: %1%2")
              .arg(vehicleCount)
              .arg(vehicleLabel.trimmed().isEmpty() ? QString() : QString::fromUtf8(" • %1").arg(vehicleLabel.trimmed()))
        : QString::fromUtf8("Авто не додано");
    lines << vehicleText;
    return lines.join(QLatin1Char('\n'));
}

QString normalizeEngineDisplay(QString engine, const QString& fuel)
{
    engine = engine.simplified();
    const QString fuelText = fuel.simplified();
    if (engine.isEmpty() || engine == QString::fromUtf8("—")) return QString();
    if (!fuelText.isEmpty() && fuelText != QString::fromUtf8("—")) {
        if (engine.compare(fuelText, Qt::CaseInsensitive) == 0) return QString();
        engine.remove(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(fuelText)),
                                         QRegularExpression::CaseInsensitiveOption));
    }
    engine.remove(QRegularExpression(QString::fromUtf8("\\b(дизель|diesel|бензин|gasoline|petrol|гібрид|hybrid|електро|electric)\\b"),
                                     QRegularExpression::CaseInsensitiveOption));
    engine = engine.simplified();
    engine.remove(QRegularExpression(QStringLiteral("\\s*[,/;+-]\\s*$")));
    engine.remove(QRegularExpression(QStringLiteral("^\\s*[,/;+-]\\s*")));
    return engine.simplified();
}

QLabel* makeCardSectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size:11pt;font-weight:850;color:#ffffff;background:transparent;padding:0 0 4px 0;"));
    return label;
}

QWidget* makeVehiclePreviewCell(const QString& brand, const QString& model, int year, const QString& color, QWidget* parent)
{
    auto* holder = new QWidget(parent);
    holder->setStyleSheet(QStringLiteral("background:transparent;"));
    const QPixmap pixmap = VehiclePreviewProvider::preview(brand, model, year, color, QSize(80, 48));
    if (pixmap.isNull()) return holder;

    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->setSpacing(0);
    auto* preview = new QLabel(holder);
    preview->setFixedSize(80, 48);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    preview->setPixmap(pixmap);
    layout->addWidget(preview, 0, Qt::AlignCenter);
    return holder;
}

void makeMouseTransparent(QWidget* widget)
{
    if (!widget) return;
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

QLabel* makeVehicleTextLabel(const QString& text, const QString& style, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet(style);
    label->setWordWrap(true);
    makeMouseTransparent(label);
    return label;
}

void applyVehicleStatusButtonState(QPushButton* button, bool primary, int vehicleCount)
{
    if (!button) return;
    button->setObjectName(primary ? QStringLiteral("crmVehiclePrimaryButton") : QStringLiteral("crmVehicleAdditionalButton"));
    button->setText(primary ? QString::fromUtf8("Основний") : QString::fromUtf8("Додатковий"));
    button->setProperty("vehiclePrimary", primary);
    button->setFixedWidth(primary ? 92 : 104);
    button->setToolTip(primary && vehicleCount <= 1 ? QString::fromUtf8("Єдине авто клієнта має залишатися основним") : QString());
    button->setStyleSheet(primary
        ? QStringLiteral("QPushButton{background:rgba(195,219,255,0.94);color:#16345c;border:1px solid rgba(229,240,255,0.96);border-radius:10px;padding:6px 10px;font-weight:900;}QPushButton:hover{border:1px solid #ffffff;}")
        : QStringLiteral("QPushButton{background:rgba(30,37,46,0.82);color:#dbe6f3;border:1px solid rgba(255,255,255,0.22);border-radius:10px;padding:6px 10px;font-weight:850;}QPushButton:hover{background:rgba(58,70,84,0.90);border:1px solid rgba(220,236,255,0.62);}"));
}

QPushButton* makeVehicleCard(const QString& vehicleUid,
                             const QString& brand,
                             const QString& model,
                             const QString& displayName,
                             int year,
                             const QString& engine,
                             const QString& fuel,
                             const QString& plate,
                             int mileage,
                             const QString& color,
                             bool primary,
                             int vehicleCount,
                             QWidget* parent)
{
    auto* card = new QPushButton(parent);
    card->setObjectName(QStringLiteral("crmVehicleCard"));
    card->setCheckable(true);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("vehicleUid", vehicleUid);
    card->setMinimumHeight(102);
    card->setStyleSheet(QStringLiteral(
        "QPushButton#crmVehicleCard{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 rgba(255,255,255,0.16),stop:0.14 rgba(255,255,255,0.06),stop:1 rgba(10,13,18,0.34));"
        "border:1px solid rgba(255,255,255,0.14);border-radius:14px;text-align:left;padding:0;}"
        "QPushButton#crmVehicleCard:hover{border:1px solid rgba(220,236,255,0.46);background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 rgba(255,255,255,0.20),stop:0.20 rgba(255,255,255,0.07),stop:1 rgba(18,24,32,0.48));}"
        "QPushButton#crmVehicleCard:checked{border:1px solid rgba(174,207,255,0.70);background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 rgba(190,214,255,0.22),stop:0.20 rgba(255,255,255,0.08),stop:1 rgba(11,16,24,0.54));}"));

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(14);

    const QPixmap vehiclePreview = VehiclePreviewProvider::preview(brand, model, displayName, year, color, QSize(120, 70));
    if (!vehiclePreview.isNull()) {
        auto* preview = new QLabel(card);
        preview->setFixedSize(120, 70);
        preview->setAlignment(Qt::AlignCenter);
        preview->setPixmap(vehiclePreview);
        preview->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
        makeMouseTransparent(preview);
        row->addWidget(preview, 0, Qt::AlignVCenter);
    }

    auto* details = new QVBoxLayout();
    details->setSpacing(5);
    const QString title = (brand + QLatin1Char(' ') + model).simplified();
    details->addWidget(makeVehicleTextLabel(title.isEmpty() ? QString::fromUtf8("Автомобіль") : title,
                                            QStringLiteral("font-size:15pt;font-weight:900;color:#ffffff;background:transparent;"),
                                            card));

    QStringList specs;
    if (year > 0) specs << QString::number(year);
    const QString engineSpec = normalizeEngineDisplay(engine, fuel);
    if (!engineSpec.isEmpty()) specs << engineSpec;
    if (!fuel.isEmpty() && fuel != QString::fromUtf8("—")) specs << fuel;
    details->addWidget(makeVehicleTextLabel(specs.isEmpty() ? QString::fromUtf8("Характеристики не вказані") : specs.join(QString::fromUtf8(" • ")),
                                            QStringLiteral("font-size:10pt;font-weight:750;color:#d8e3ef;background:transparent;"),
                                            card));

    QStringList identity;
    if (!plate.isEmpty() && plate != QString::fromUtf8("—")) identity << plate;
    identity << (mileage > 0 ? QString::fromUtf8("%1 км").arg(mileage) : QString::fromUtf8("Пробіг не вказано"));
    details->addWidget(makeVehicleTextLabel(identity.join(QString::fromUtf8(" • ")),
                                            QStringLiteral("font-size:9.5pt;font-weight:700;color:#aebdce;background:transparent;"),
                                            card));
    row->addLayout(details, 1);

    auto* statusActions = new QVBoxLayout();
    statusActions->setSpacing(6);

    auto* statusButton = new QPushButton(primary ? QString::fromUtf8("Основний") : QString::fromUtf8("Додатковий"), card);
    statusButton->setObjectName(primary ? QStringLiteral("crmVehiclePrimaryButton") : QStringLiteral("crmVehicleAdditionalButton"));
    statusButton->setProperty("vehicleStatusButton", true);
    statusButton->setCursor(Qt::PointingHandCursor);
    statusButton->setMinimumHeight(30);
    applyVehicleStatusButtonState(statusButton, primary, vehicleCount);
    statusActions->addWidget(statusButton);
    statusActions->addStretch(1);
    row->addLayout(statusActions, 0);

    return card;
}

void applyCrmActionButtonStyle(QPushButton* button)
{
    if (!button) return;
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(36);
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(255,255,255,0.18),stop:0.10 rgba(255,255,255,0.08),"
        "stop:0.11 rgba(63,72,82,0.90),stop:0.55 rgba(42,48,56,0.94),"
        "stop:1 rgba(26,31,37,0.96));color:#f8f9fb;"
        "border:1px solid rgba(255,255,255,0.16);border-radius:11px;"
        "padding:7px 14px;font-weight:650;}"
        "QPushButton:hover{border:1px solid rgba(220,236,255,0.95);}"
        "QPushButton:pressed{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 rgba(24,28,34,0.99),stop:1 rgba(8,10,14,0.99));}"
        "QPushButton:disabled{color:rgba(245,248,252,0.52);"
        "border:1px solid rgba(255,255,255,0.18);background:rgba(35,41,49,0.44);}"));
}

} // namespace

CrmPage::CrmPage(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("background: transparent; border: none; color:#f7fbff;"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QString::fromUtf8("CRM: клієнти та авто"), this);
    title->setStyleSheet(QStringLiteral("font-size:18pt; font-weight:800; color:#f7fbff;"));
    header->addWidget(title);
    m_summary = new QLabel(this);
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_summary->setStyleSheet(QStringLiteral("font-size:11pt; font-weight:700; color:#d8e2ed;"));
    header->addWidget(m_summary, 1);
    m_createOrder = new QPushButton(QString::fromUtf8("Створити наряд"), this);
    m_createAppointment = new QPushButton(QString::fromUtf8("Записати на ремонт"), this);
    applyCrmActionButtonStyle(m_createOrder);
    applyCrmActionButtonStyle(m_createAppointment);
    header->addWidget(m_createOrder);
    header->addWidget(m_createAppointment);
    root->addLayout(header);

    auto* searchLine = new QHBoxLayout();
    auto* searchLabel = new QLabel(QString::fromUtf8("Пошук:"), this);
    searchLabel->setStyleSheet(QStringLiteral("font-weight:700; color:#f7fbff;"));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QString::fromUtf8("ПІБ, телефон, номер авто, VIN, марка/модель"));
    m_search->setStyleSheet(QStringLiteral("QLineEdit{background:#ffffff; color:#111822; border-radius:9px; padding:7px 10px;}"));
    auto* refreshButton = new QPushButton(QString::fromUtf8("Оновити CRM"), this);
    applyCrmActionButtonStyle(refreshButton);
    searchLine->addWidget(searchLabel);
    searchLine->addWidget(m_search, 1);
    searchLine->addWidget(refreshButton);
    root->addLayout(searchLine);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    auto* left = new QVBoxLayout();
    auto* clientsTitle = new QLabel(QString::fromUtf8("Клієнти"), this);
    clientsTitle->setStyleSheet(QStringLiteral("font-size:12pt; font-weight:800; color:#f7fbff;"));
    left->addWidget(clientsTitle);
    m_clients = new QTableWidget(this);
    prepareClientCardsTable(m_clients);
    left->addWidget(m_clients, 1);
    body->addLayout(left, 2);

    auto* right = new QVBoxLayout();
    right->setSpacing(0);
    m_clientCard = new QWidget(this);
    m_clientCard->setObjectName(QStringLiteral("crmClientCard"));
    m_clientCard->setAttribute(Qt::WA_StyledBackground, true);
    m_clientCard->setStyleSheet(QStringLiteral(
        "QWidget#crmClientCard{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(255,255,255,0.14),stop:0.055 rgba(255,255,255,0.24),"
        "stop:0.12 rgba(102,115,132,0.82),stop:0.42 rgba(42,51,62,0.96),"
        "stop:1 rgba(18,23,29,0.98));border:1px solid rgba(255,255,255,0.22);"
        "border-radius:16px;color:#f7fbff;}"
        "QTabWidget::pane{border:1px solid rgba(255,255,255,0.12);border-radius:12px;"
        "background:rgba(8,11,15,0.16);top:-1px;}"
        "QTabBar::tab{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(255,255,255,0.16),stop:0.12 rgba(255,255,255,0.08),stop:1 rgba(32,38,46,0.90));"
        "color:#dfe8f4;border:1px solid rgba(255,255,255,0.14);border-bottom:none;"
        "border-top-left-radius:9px;border-top-right-radius:9px;padding:8px 13px;margin-right:2px;font-weight:750;}"
        "QTabBar::tab:selected{color:#ffffff;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(255,255,255,0.28),stop:0.12 rgba(255,255,255,0.13),stop:1 rgba(54,63,75,0.92));"
        "border:1px solid rgba(220,236,255,0.34);border-bottom:none;}"));
    auto* cardLayout = new QVBoxLayout(m_clientCard);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setTextFormat(Qt::RichText);
    m_detail->setAttribute(Qt::WA_StyledBackground, false);
    m_detail->setStyleSheet(QStringLiteral(
        "QLabel{background:transparent;color:#f7fbff;border:none;padding:0;font-weight:700;}"));
    cardLayout->addWidget(m_detail);
    m_debt = new QLabel(this);
    m_debt->setWordWrap(true);
    m_debt->setTextFormat(Qt::RichText);
    m_debt->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_debt->setOpenExternalLinks(false);
    connect(m_debt, &QLabel::linkActivated, this, [this](const QString&) { openClientDebt(); });
    m_debt->setStyleSheet(QStringLiteral(
        "QLabel{background:rgba(8,11,15,0.28);color:#ffffff;border:1px solid rgba(255,255,255,0.10);"
        "border-radius:12px;padding:10px;font-weight:800;}"));
    cardLayout->addWidget(m_debt);

    m_sections = new QTabWidget(this);
    m_sections->setDocumentMode(true);

    auto* vehiclesTab = new QWidget(this);
    auto* vehiclesLayout = new QVBoxLayout(vehiclesTab);
    vehiclesLayout->setContentsMargins(10, 10, 10, 10);
    vehiclesLayout->setSpacing(8);
    vehiclesLayout->addWidget(makeCardSectionTitle(QString::fromUtf8("Автомобілі клієнта"), vehiclesTab));

    auto* vehicleScroll = new QScrollArea(vehiclesTab);
    vehicleScroll->setWidgetResizable(true);
    vehicleScroll->setFrameShape(QFrame::NoFrame);
    vehicleScroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(38,45,54,0.54),stop:0.18 rgba(66,76,88,0.34),stop:1 rgba(15,19,25,0.58));"
        "border:1px solid rgba(255,255,255,0.10);border-radius:12px;}"
        "QScrollBar:vertical{background:rgba(255,255,255,0.04);width:10px;margin:4px 2px 4px 0;border-radius:5px;}"
        "QScrollBar::handle:vertical{background:rgba(215,228,246,0.34);border-radius:5px;min-height:32px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;background:transparent;}"));
    auto* vehicleContent = new QWidget(vehicleScroll);
    vehicleContent->setObjectName(QStringLiteral("crmVehicleList"));
    vehicleContent->setStyleSheet(QStringLiteral("QWidget#crmVehicleList{background:transparent;}"));
    m_vehicleList = new QVBoxLayout(vehicleContent);
    m_vehicleList->setContentsMargins(12, 12, 12, 12);
    m_vehicleList->setSpacing(10);
    m_vehicleList->addStretch(1);
    vehicleScroll->setWidget(vehicleContent);
    vehiclesLayout->addWidget(vehicleScroll, 1);
    m_sections->addTab(vehiclesTab, QString::fromUtf8("Автомобілі"));

    m_history = new QTableWidget(this);
    prepareTable(m_history, {QString::fromUtf8("Наряд"), QString::fromUtf8("Дата"), QString::fromUtf8("Авто"), QString::fromUtf8("Статус"), QString::fromUtf8("Сума")}, true);
    setColumnsOnce(m_history, {90, 82, 190, 120, 105});
    auto* historyTab = new QWidget(this);
    auto* historyLayout = new QVBoxLayout(historyTab);
    historyLayout->setContentsMargins(10, 10, 10, 10);
    historyLayout->addWidget(makeCardSectionTitle(QString::fromUtf8("Історія замовлень"), historyTab));
    historyLayout->addWidget(m_history, 1);
    m_sections->addTab(historyTab, QString::fromUtf8("Історія"));

    auto* notesTab = new QWidget(this);
    auto* eventsBlock = new QVBoxLayout(notesTab);
    eventsBlock->setContentsMargins(10, 10, 10, 10);
    eventsBlock->addWidget(makeCardSectionTitle(QString::fromUtf8("Нотатки та події"), notesTab));
    m_events = new QTableWidget(this);
    prepareTable(m_events, {QString::fromUtf8("Дата"), QString::fromUtf8("Подія"), QString::fromUtf8("Опис")}, true);
    setColumnsOnce(m_events, {135, 170, 360});
    eventsBlock->addWidget(m_events, 1);
    m_addNote = new QPushButton(QString::fromUtf8("Додати нотатку"), this);
    applyCrmActionButtonStyle(m_addNote);
    eventsBlock->addWidget(m_addNote, 0, Qt::AlignRight);
    m_sections->addTab(notesTab, QString::fromUtf8("Нотатки"));

    auto* tasksTab = new QWidget(this);
    auto* tasksBlock = new QVBoxLayout(tasksTab);
    tasksBlock->setContentsMargins(10, 10, 10, 10);
    tasksBlock->addWidget(makeCardSectionTitle(QString::fromUtf8("Задачі клієнта"), tasksTab));
    m_tasks = new QTableWidget(this);
    prepareTable(m_tasks, {QString::fromUtf8("Задача"), QString::fromUtf8("Статус"), QString::fromUtf8("Термін"), QString::fromUtf8("UID")}, true);
    setColumnsOnce(m_tasks, {280, 90, 135, 0});
    tasksBlock->addWidget(m_tasks, 1);
    auto* taskActions = new QHBoxLayout();
    m_addTask = new QPushButton(QString::fromUtf8("Додати задачу"), this);
    m_completeTask = new QPushButton(QString::fromUtf8("Виконано"), this);
    applyCrmActionButtonStyle(m_addTask);
    applyCrmActionButtonStyle(m_completeTask);
    taskActions->addWidget(m_addTask);
    taskActions->addWidget(m_completeTask);
    tasksBlock->addLayout(taskActions);
    m_sections->addTab(tasksTab, QString::fromUtf8("Задачі"));

    cardLayout->addWidget(m_sections, 1);
    right->addWidget(m_clientCard, 1);

    body->addLayout(right, 4);
    root->addLayout(body, 1);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(320);

    connect(refreshButton, &QPushButton::clicked, this, [this]() { refresh(true); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->start();
    });
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() { refreshClients(); });
    connect(m_clients, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_populatingClients || m_selectingClient) return;
        selectClientByUid(clientUidForRow(row), SelectionReason::UserSelection, false);
    });
    connect(m_clients, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (m_populatingClients || m_selectingClient) return;
        if (m_clients && m_clients->item(row, 0)) return;
        const QString uid = clientUidForRow(row);
        qInfo() << "CRM_SELECT_CLICK" << "row=" << row << "uid=" << crmShortUid(uid);
        selectClientByUid(uid, SelectionReason::UserClick, false);
    });
    connect(m_clients, &QTableWidget::itemClicked, this, [this](QTableWidgetItem* item) {
        if (m_populatingClients || m_selectingClient || !item) return;
        const QString uid = clientUidForItem(item);
        qInfo() << "CRM_SELECT_CLICK" << "row=" << item->row() << "uid=" << crmShortUid(uid);
        selectClientByUid(uid, SelectionReason::UserClick, false);
    });
    connect(m_clients, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_populatingClients || m_selectingClient || !m_clients) return;
        int row = m_clients->currentRow();
        if (row < 0 && m_clients->selectionModel()) {
            const QModelIndexList selectedRows = m_clients->selectionModel()->selectedRows();
            if (!selectedRows.isEmpty()) row = selectedRows.constFirst().row();
        }
        selectClientByUid(clientUidForRow(row), SelectionReason::UserSelection, false);
    });
    if (m_clients->selectionModel()) {
        connect(m_clients->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current, const QModelIndex&) {
            if (m_populatingClients || m_selectingClient || !current.isValid()) return;
            selectClientByUid(clientUidForRow(current.row()), SelectionReason::UserSelection, false);
        });
    }
    connect(m_createOrder, &QPushButton::clicked, this, [this]() { requestNewOrder(); });
    connect(m_createAppointment, &QPushButton::clicked, this, [this]() { requestRepairBookingTask(); });
    connect(m_history, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { openHistoryOrderRow(row); });
    connect(m_addNote, &QPushButton::clicked, this, [this]() { addClientNote(); });
    connect(m_addTask, &QPushButton::clicked, this, [this]() { addClientTask(); });
    connect(m_completeTask, &QPushButton::clicked, this, [this]() { markSelectedTaskCompleted(); });
    qInfo() << "CRM_CONNECT_OK client_cards";

    refresh();
}

void CrmPage::loadClientCardForRow(int row, const QString& reason)
{
    Q_UNUSED(reason);
    selectClientByUid(clientUidForRow(row), SelectionReason::Programmatic, false);
}

void CrmPage::loadSelectedClientCard(const QString& reason)
{
    if (!m_clients) {
        qWarning() << "CRM client card not opened: clients table missing" << reason;
        loadClientDetails(QString());
        return;
    }

    int row = m_clients->currentRow();
    if (row < 0 && m_clients->selectionModel()) {
        const QModelIndexList selectedRows = m_clients->selectionModel()->selectedRows();
        if (!selectedRows.isEmpty()) row = selectedRows.constFirst().row();
    }
    loadClientCardForRow(row, reason);
}

bool CrmPage::selectClientByUid(const QString& clientUid, SelectionReason reason, bool allowFirstRow)
{
    if (!m_clients) {
        loadClientDetails(QString());
        return false;
    }

    const QString wantedUid = clientUid.trimmed();
    int rowToSelect = -1;
    if (!wantedUid.isEmpty()) {
        for (int row = 0; row < m_clients->rowCount(); ++row) {
            if (clientUidForRow(row) == wantedUid) {
                rowToSelect = row;
                break;
            }
        }
    }
    if (rowToSelect < 0 && allowFirstRow && m_clients->rowCount() > 0) {
        for (int row = 0; row < m_clients->rowCount(); ++row) {
            if (!clientUidForRow(row).isEmpty()) {
                rowToSelect = row;
                break;
            }
        }
    }
    if (rowToSelect < 0) {
        qInfo() << "CRM client card placeholder: no selectable client" << crmShortUid(wantedUid);
        const bool oldSelecting = m_selectingClient;
        m_selectingClient = true;
        {
            QSignalBlocker blocker(m_clients);
            if (m_clients->selectionModel()) {
                QSignalBlocker selectionBlocker(m_clients->selectionModel());
                m_clients->selectionModel()->clearSelection();
                m_clients->selectionModel()->clearCurrentIndex();
            }
            m_clients->clearSelection();
            m_clients->setCurrentCell(-1, -1);
        }
        m_selectingClient = oldSelecting;
        loadClientDetails(QString());
        if (m_clients->viewport()) m_clients->viewport()->update();
        return false;
    }

    const QString resolvedUid = clientUidForRow(rowToSelect);
    if (resolvedUid.isEmpty()) {
        qWarning() << "CRM client card not opened: resolved row has empty client_uid" << rowToSelect;
        loadClientDetails(QString());
        if (m_clients->viewport()) m_clients->viewport()->update();
        return false;
    }

    bool rowAlreadySelected = m_clients->currentRow() == rowToSelect;
    if (rowAlreadySelected && m_clients->selectionModel()) {
        const QModelIndex index = m_clients->model()->index(rowToSelect, 0);
        rowAlreadySelected = m_clients->selectionModel()->isSelected(index);
    }
    if (rowAlreadySelected && m_currentClientUid == resolvedUid) {
        if (m_clients->viewport()) m_clients->viewport()->update();
        return true;
    }

    const bool oldSelecting = m_selectingClient;
    m_selectingClient = true;
    {
        QSignalBlocker blocker(m_clients);
        m_clients->setCurrentCell(rowToSelect, 0);
        if (m_clients->selectionModel()) {
            QSignalBlocker selectionBlocker(m_clients->selectionModel());
            const QModelIndex index = m_clients->model()->index(rowToSelect, 0);
            m_clients->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_clients->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            m_clients->selectRow(rowToSelect);
        }
    }
    m_selectingClient = oldSelecting;

    loadClientDetails(resolvedUid);

    const QString selectedRowUid = clientUidForRow(m_clients->currentRow());
    if (selectedRowUid != m_currentClientUid) {
        qWarning() << "CRM_SELECT_MISMATCH"
                   << "row_uid=" << crmShortUid(selectedRowUid)
                   << "selected_uid=" << crmShortUid(m_currentClientUid)
                   << "row=" << m_clients->currentRow();
        QSignalBlocker blocker(m_clients);
        QSignalBlocker selectionBlocker(m_clients->selectionModel());
        m_clients->clearSelection();
        m_clients->setCurrentCell(-1, -1);
        return false;
    }
    qInfo() << "CRM_SELECT_APPLY"
            << "uid=" << crmShortUid(resolvedUid)
            << "row=" << rowToSelect
            << "reason=" << static_cast<int>(reason);
    if (m_clients->viewport()) m_clients->viewport()->update();
    return true;
}

void CrmPage::restoreClientTableColumnWidths()
{
    if (!m_clients) return;
    QSignalBlocker blocker(m_clients->horizontalHeader());
    for (int col = 0; col < m_clients->columnCount(); ++col) {
        const int minWidth = minimumCrmClientTableColumnWidth(col);
        const QVariant stored = UiStateSettingsService::loadValue(crmClientTableColumnWidthKey(col), QVariant());
        const int width = stored.isValid()
            ? qMax(minWidth, stored.toInt())
            : defaultCrmClientTableColumnWidth(col);
        m_clients->setColumnWidth(col, width);
    }
}

void CrmPage::saveClientTableColumnWidth(int logicalIndex, int width)
{
    if (!m_clients || logicalIndex < 0 || logicalIndex >= m_clients->columnCount()) return;
    const int normalizedWidth = qMax(minimumCrmClientTableColumnWidth(logicalIndex), width);
    UiStateSettingsService::saveValue(crmClientTableColumnWidthKey(logicalIndex), normalizedWidth);
    if (width < normalizedWidth) m_clients->setColumnWidth(logicalIndex, normalizedWidth);
}

void CrmPage::refresh()
{
    refresh(false);
}

bool CrmPage::runBackfill(QString* statusText, QString* errorText)
{
    CrmClientService::BackfillResult result;
    if (!CrmPageApplicationService::backfill(&result, errorText)) return false;
    if (statusText) {
        *statusText = QString::fromUtf8("CRM оновлено: клієнтів %1/%2, авто %3/%4, нарядів %5")
                          .arg(result.clientsCreated)
                          .arg(result.clientsUpdated)
                          .arg(result.vehiclesCreated)
                          .arg(result.vehiclesUpdated)
                          .arg(result.ordersLinked);
    }
    return true;
}

void CrmPage::refresh(bool showStatus)
{
    if (m_refreshInProgress) {
        if (showStatus) m_refreshPending = true;
        return;
    }

    m_refreshInProgress = true;
    setProperty("pageDataLoading", true);
    const quint64 generation = ++m_refreshGeneration;
    const bool runBackfillNow = !m_backfillCompletedThisSession || showStatus;
    QPointer<CrmPage> guard(this);

    auto* thread = QThread::create([guard, generation, showStatus, runBackfillNow]() {
        PerfWatchdog::ScopedTimer timer(QStringLiteral("CRM background refresh"), 150);
        const auto result=CrmPageReadService::refresh(runBackfillNow);
        AppRuntimeService::closeThreadConnection();
        QMetaObject::invokeMethod(qApp, [guard, generation, showStatus, result]() {
            if (!guard || guard->m_refreshGeneration != generation) return;
            guard->m_refreshInProgress = false;
            guard->setProperty("pageDataLoading", false);
            if (result.backfillRan && result.backfillOk) guard->m_backfillCompletedThisSession = true;

            if (guard->m_summary) {
                const QString suffix = result.backfillOk && !result.backfillStatus.isEmpty()
                    ? QString::fromUtf8("  •  %1").arg(result.backfillStatus)
                    : QString();
                guard->m_summary->setText(QString::fromUtf8("Клієнтів: %1  •  Авто: %2  •  Задач: %3  •  Оновлено: %4%5")
                                              .arg(result.clientsCount)
                                              .arg(result.vehiclesCount)
                                              .arg(result.tasksCount)
                                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yy HH:mm:ss")),
                                                   suffix));
            }

            guard->refreshClients();
            if (showStatus) {
                if (result.backfillOk) {
                    QMessageBox::information(guard, QString::fromUtf8("CRM"), result.backfillStatus.isEmpty() ? QString::fromUtf8("CRM оновлено.") : result.backfillStatus);
                } else {
                    const QString error = !result.backfillError.trimmed().isEmpty() ? result.backfillError : result.dbError;
                    QMessageBox::warning(guard, QString::fromUtf8("CRM"), QString::fromUtf8("Не вдалося оновити CRM: %1").arg(error));
                }
            }

            if (guard->m_refreshPending) {
                guard->m_refreshPending = false;
                guard->refresh(false);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void CrmPage::refreshClients()
{
    PerfWatchdog::ScopedTimer timer(QStringLiteral("CRM client list"), 150);
    const QString term = m_search ? m_search->text().trimmed() : QString();
    const auto data=CrmPageReadService::clients(term);
    if(!data.error.isEmpty()) {
        if(m_summary)m_summary->setText(QString::fromUtf8("Не вдалося завантажити клієнтів: %1").arg(data.error));
        return;
    }
    QString previousUid = selectedClientUid();

    if (m_clients) {
        {
            m_populatingClients = true;
            TableFillGuard fillGuard(m_clients);
            m_clients->setRowCount(0);
            m_clients->setColumnCount(1);
            QSet<QString> seenClientUids;
            {
                for (const auto& q:data.clients) {
                    const int row = m_clients->rowCount();
                    m_clients->insertRow(row);
                    const QString uid = q.value(0).toString().trimmed();
                    auto* item = cell(clientCardText(q.value(1), q.value(2), q.value(3).toInt(), q.value(4).toString()), uid);
                    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    item->setData(Qt::FontRole, crmClientCardFont());
                    item->setData(Qt::UserRole + 1, q.value(1).toString().trimmed());
                    item->setData(Qt::UserRole + 2, q.value(2).toString().trimmed());
                    if (uid.isEmpty()) {
                        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                        qWarning() << "CRM client list item has empty client_uid" << "row" << row;
                    } else if (seenClientUids.contains(uid)) {
                        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                        qWarning() << "CRM client list item has duplicate client_uid" << "row" << row << "uid" << crmShortUid(uid);
                    } else {
                        seenClientUids.insert(uid);
                    }
                    m_clients->setItem(row, 0, item);
                    m_clients->setRowHeight(row, 78);
                }
            }
            if (m_clients->rowCount() == 0) {
                const int row = m_clients->rowCount();
                m_clients->insertRow(row);
                auto* empty = cell(QString::fromUtf8("Клієнтів не знайдено\nЗмініть пошук або оновіть CRM"));
                empty->setFlags(empty->flags() & ~Qt::ItemIsSelectable);
                empty->setTextAlignment(Qt::AlignCenter);
                empty->setData(Qt::FontRole, crmClientCardFont());
                m_clients->setItem(row, 0, empty);
                m_clients->setRowHeight(row, 86);
            }
            m_clients->resizeColumnToContents(0);
            if (m_clients->horizontalHeader()) {
                m_clients->horizontalHeader()->setStretchLastSection(true);
            }
            m_populatingClients = false;
        }
        selectClientByUid(previousUid, SelectionReason::RefreshRestore, true);
        qInfo() << "CRM_REFRESH"
                << "clients=" << m_clients->rowCount()
                << "selected=" << crmShortUid(selectedClientUid());
    }
}

void CrmPage::loadClientDetails(const QString& clientUid)
{
    PerfWatchdog::ScopedTimer timer(QStringLiteral("CRM client details"), 150);
    QElapsedTimer stages; stages.start();
    const auto checkpoint = [&stages](const char* stage) {
        PerfWatchdog::noteSlowOperation(QString::fromLatin1(stage), stages.restart(), 100);
    };
    // Invalidate the previous card before reading a different client. A failed
    // read must never leave actions targeting the previously displayed client.
    m_currentClientUid.clear();
    m_currentVehicleUid.clear();
    for (auto* action : {m_createOrder, m_createAppointment, m_addNote, m_addTask, m_completeTask}) {
        if (action) action->setEnabled(false);
    }

    if (m_detail) {
        m_detail->setText(QString::fromUtf8(
            "<div style='font-size:17pt;font-weight:900;color:#ffffff;'>Оберіть клієнта, щоб переглянути картку</div>"
            "<div style='margin-top:6px;color:#9fb0c4;'>Контакти, статистика, автомобілі та історія з'являться після вибору клієнта зі списку.</div>"));
    }
    if (m_debt) m_debt->setText(QString());
    clearVehicleCards();
    TableFillGuard historyGuard(m_history);
    TableFillGuard eventsGuard(m_events);
    TableFillGuard tasksGuard(m_tasks);
    if (m_history) m_history->setRowCount(0);
    if (m_events) m_events->setRowCount(0);
    if (m_tasks) m_tasks->setRowCount(0);
    if (clientUid.trimmed().isEmpty()) return;
    const auto data = CrmPageReadService::details(clientUid.trimmed());
    if (!data.error.isEmpty()) {
        const QString error = QString::fromUtf8("Не вдалося завантажити картку: %1").arg(data.error);
        if (m_summary) m_summary->setText(error);
        if (m_detail) m_detail->setText(error.toHtmlEscaped());
        return;
    }
    m_currentClientUid = clientUid.trimmed();

    const auto& client=data.client;
    bool clientFound = false;
    if (!client.isEmpty() && m_detail) {
        clientFound = true;
        const QString altPhone = client.value(4).toString().trimmed();
        const QString phones = altPhone.isEmpty()
            ? detailText(client.value(3))
            : QString::fromUtf8("%1 <span style='color:#d8e2ed'>/</span> %2").arg(detailText(client.value(3)), altPhone.toHtmlEscaped());
        const QString loyalty = detailText(client.value(5));
        const QString discount = QString::number(client.value(6).toDouble(), 'f', 1);
        m_detail->setText(QString::fromUtf8(
            "<div style='font-size:18pt;font-weight:900;color:#ffffff;background:transparent;border:none;'>%1</div>"
            "<table width='100%' cellspacing='0' cellpadding='0' style='margin-top:10px;background-color:transparent;background:transparent;color:#f7fbff;border:none;'>"
            "<tr>"
            "<td width='50%' valign='top' align='left' style='padding:0 7px 0 0;background-color:transparent;background:transparent;border:none;'>"
            "<div style='min-height:52px;height:52px;background-color:transparent;background:transparent;border:none;padding:10px 12px;'>"
            "<div style='color:#c8d4e2;font-size:11.5pt;font-weight:800;background:transparent;border:none;'>Телефон</div>"
            "<div style='font-size:11.5pt;font-weight:850;color:#ffffff;background:transparent;border:none;'>%2</div>"
            "</div>"
            "</td>"
            "<td width='50%' valign='top' align='left' style='padding:0 0 0 7px;background-color:transparent;background:transparent;border:none;'>"
            "<div style='min-height:52px;height:52px;background-color:transparent;background:transparent;border:none;padding:10px 12px;'>"
            "<div style='color:#c8d4e2;font-size:11.5pt;font-weight:800;background:transparent;border:none;'>Лояльність</div>"
            "<div style='font-size:11.5pt;font-weight:850;color:#ffffff;background:transparent;border:none;'>%3  •  знижка %4%</div>"
            "</div>"
            "</td>"
            "</tr>"
            "</table>")
                              .arg(detailText(client.value(0)),
                                   phones,
                                   loyalty,
                                   discount));
    }
    if (!clientFound) {
        qWarning() << "CRM client card not opened: client_uid not found" << m_currentClientUid;
        m_currentClientUid.clear();
        return;
    }
    qInfo() << "CRM loaded detail client uid" << m_currentClientUid;
    for (auto* action : {m_createOrder, m_createAppointment, m_addNote, m_addTask, m_completeTask}) {
        if (action) action->setEnabled(true);
    }

    const auto& financial=data.financial;
    checkpoint("CRM detail aggregates");
    if (m_debt) {
        if (!financial.ok()) {
            m_debt->setText(QStringLiteral("Не вдалося обчислити фінанси: %1").arg(financial.error.toHtmlEscaped()));
        } else {
            m_debt->setText(QStringLiteral(
                "Нарядів: %1 · Вартість нарядів: %2 · Отримано за журналом: %3<br>"
                "<a href='debt' style='color:#ffdd99;'>Розрахунковий залишок: %4</a> · Переплата за журналом: %5<br>"
                "Історичні клієнтські оплати не обліковувалися. Залишки потребують звірки; оплати постачальникам не враховуються.%6")
                .arg(QString::number(financial.orders.size()), CrmFinancialService::money(financial.totalCents),
                     CrmFinancialService::money(financial.receivedCents), CrmFinancialService::money(financial.outstandingCents),
                     CrmFinancialService::money(financial.overpaidCents),
                     financial.ledgerAvailable ? QString() : QStringLiteral(" Журнал клієнтських оплат відсутній.")));
        }
    }

    if (m_vehicleList) {
        const int vehicleCountForClient=data.vehicles.size();
        int vehicleIndex=0;
        for (const auto& vehicles:data.vehicles) {
            const QString vehicleUid = vehicles.value(0).toString();
            const QString brand = vehicles.value(1).toString();
            const QString model = vehicles.value(2).toString();
            const QString displayName = vehicles.value(3).toString();
            const QString color = vehicles.value(9).toString();
            const QString engine = compactText(vehicles.value(5));
            const QString fuel = compactText(vehicles.value(6));
            const int mileage = vehicles.value(8).toInt();
            const bool primary = vehicles.value(10).toInt() != 0 || (vehicleIndex == 0 && vehicleCountForClient == 1);
            auto* card = makeVehicleCard(vehicleUid,
                                         brand,
                                         model,
                                         displayName,
                                         vehicles.value(4).toInt(),
                                         engine,
                                         fuel,
                                         compactText(vehicles.value(7)),
                                         mileage,
                                         color,
                                         primary,
                                         vehicleCountForClient,
                                         m_vehicleList->parentWidget());
            connect(card, &QPushButton::clicked, this, [this, vehicleUid]() { selectVehicleUid(vehicleUid); });
            const auto statusButtons = card->findChildren<QPushButton*>();
            for (QPushButton* statusButton : statusButtons) {
                if (!statusButton || !statusButton->property("vehicleStatusButton").toBool()) continue;
                connect(statusButton, &QPushButton::clicked, this, [this, vehicleUid, statusButton]() {
                    setVehiclePrimaryStatus(vehicleUid, !statusButton->property("vehiclePrimary").toBool());
                });
            }
            m_vehicleCards.append(card);
            m_vehicleList->addWidget(card);
            if (vehicleIndex == 0) selectVehicleUid(vehicleUid);
            ++vehicleIndex;
        }
        qInfo() << "CRM vehicles for selected client" << m_currentClientUid << vehicleIndex;
        if (vehicleIndex == 0) {
            m_vehiclePlaceholder = new QLabel(QString::fromUtf8("У клієнта ще немає автомобілів"), m_vehicleList->parentWidget());
            m_vehiclePlaceholder->setAlignment(Qt::AlignCenter);
            m_vehiclePlaceholder->setMinimumHeight(96);
            m_vehiclePlaceholder->setStyleSheet(QStringLiteral(
                "background:rgba(255,255,255,0.07);color:#d8e3ef;border:1px solid rgba(255,255,255,0.12);"
                "border-radius:12px;font-size:11pt;font-weight:800;padding:18px;"));
            m_vehicleList->addWidget(m_vehiclePlaceholder);
        }
        m_vehicleList->addStretch(1);
    }

    checkpoint("CRM vehicle cards");
    QHash<QString, qint64> financialTotals;
    if (financial.ok())
        for (const auto& order : financial.orders) financialTotals.insert(order.uid, order.totalCents);
    if (m_history) {
        for (const auto& history:data.history) {
            const int row = m_history->rowCount();
            m_history->insertRow(row);
            const QString orderUid = history.value(5).toString();
            const QString orderNumberText = history.value(0).toString();
            const int orderNumber = orderNumberText.trimmed().toInt();
            auto makeHistoryCell = [&](const QString& text) {
                auto* item = cell(text, orderUid);
                item->setData(kCrmHistoryOrderNumberRole, orderNumber);
                return item;
            };
            m_history->setItem(row, 0, makeHistoryCell(orderNumberText));
            m_history->setItem(row, 1, makeHistoryCell(displayDate(history.value(1))));
            m_history->setItem(row, 2, makeHistoryCell(compactText(history.value(2))));
            m_history->setItem(row, 3, makeHistoryCell(compactText(history.value(3))));
            m_history->setItem(row, 4, makeHistoryCell(financialTotals.contains(orderUid)
                ? CrmFinancialService::money(financialTotals.value(orderUid)) : QStringLiteral("Потрібна перевірка")));
        }
    }

    checkpoint("CRM order history");
    if (m_events) {
        for (const auto& events:data.events) {
            const int row = m_events->rowCount();
            m_events->insertRow(row);
            m_events->setItem(row, 0, cell(compactText(events.value(0))));
            m_events->setItem(row, 1, cell(compactText(events.value(1))));
            m_events->setItem(row, 2, cell(compactText(events.value(2))));
        }
    }

    if (m_tasks) {
        for (const auto& tasks:data.tasks) {
            const int row = m_tasks->rowCount();
            m_tasks->insertRow(row);
            const QString taskUid = tasks.value(0).toString();
            m_tasks->setItem(row, 0, cell(compactText(tasks.value(1)), taskUid));
            m_tasks->setItem(row, 1, cell(compactText(tasks.value(2)), taskUid));
            m_tasks->setItem(row, 2, cell(compactText(tasks.value(3)), taskUid));
            m_tasks->setItem(row, 3, cell(taskUid, taskUid));
        }
    }
}

void CrmPage::clearVehicleCards()
{
    m_currentVehicleUid.clear();
    m_vehicleCards.clear();
    m_vehiclePlaceholder = nullptr;
    if (!m_vehicleList) return;
    while (QLayoutItem* item = m_vehicleList->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
}

void CrmPage::selectVehicleUid(const QString& vehicleUid)
{
    m_currentVehicleUid = vehicleUid.trimmed();
    for (QPushButton* card : m_vehicleCards) {
        if (!card) continue;
        card->setChecked(card->property("vehicleUid").toString().trimmed() == m_currentVehicleUid);
    }
}

void CrmPage::setVehiclePrimaryStatus(const QString& vehicleUid, bool primary)
{
    const QString clientUid = selectedClientUid();
    if (clientUid.isEmpty() || vehicleUid.trimmed().isEmpty()) return;
    if (!primary) {
        selectVehicleUid(vehicleUid);
        return;
    }
    QString err;
    if (!CrmPageApplicationService::setPrimaryVehicle(clientUid, vehicleUid, primary, &err)) {
        if (!err.trimmed().isEmpty()) QMessageBox::information(this, QString::fromUtf8("CRM"), err);
        return;
    }
    const QString restoreVehicleUid = vehicleUid.trimmed();
    updateVehicleStatusButtons();
    selectVehicleUid(restoreVehicleUid);
}

void CrmPage::updateVehicleStatusButtons()
{
    const QString clientUid = selectedClientUid();
    if (clientUid.isEmpty() || m_vehicleCards.isEmpty()) return;

    const auto statuses = CrmVehicleService::primaryStatuses(clientUid);
    if (!statuses.succeeded()) {
        qWarning() << "CRM vehicle status refresh failed" << clientUid << statuses.error;
        return;
    }
    const auto& primaryByVehicle = statuses.vehicles;

    const int vehicleCount = primaryByVehicle.size();
    for (QPushButton* card : m_vehicleCards) {
        if (!card) continue;
        const QString cardVehicleUid = card->property("vehicleUid").toString().trimmed();
        const bool primary = primaryByVehicle.value(cardVehicleUid, false);
        const auto buttons = card->findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            if (!button || !button->property("vehicleStatusButton").toBool()) continue;
            applyVehicleStatusButtonState(button, primary, vehicleCount);
        }
    }
}

void CrmPage::addClientNote()
{
    if (selectedClientUid().isEmpty()) return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, QString::fromUtf8("Нотатка CRM"), QString::fromUtf8("Текст нотатки:"), QString(), &ok).trimmed();
    if (!ok || text.isEmpty()) return;
    QString err;
    if (!CrmPageApplicationService::addNote(selectedClientUid(), selectedVehicleUid(), text, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("CRM"), err);
        return;
    }
    loadClientDetails(selectedClientUid());
}

void CrmPage::openClientDebt()
{
    const auto financial = CrmPageApplicationService::financial(selectedClientUid());
    if (!financial.ok()) {
        QMessageBox::warning(this, QStringLiteral("Розрахунковий залишок"), financial.error);
        return;
    }
    QList<QPair<int,QString>> orders;
    QStringList labels;
    for (const auto& order : financial.orders) {
        if (order.balanceCents() <= 0) continue;
        orders.append({order.number,order.uid});
        labels.append(QStringLiteral("Наряд №%1 • %2 • %3 (за журналом)")
                      .arg(QString::number(order.number),order.car,CrmFinancialService::money(order.balanceCents())));
    }
    if (orders.isEmpty()) { loadClientDetails(selectedClientUid()); return; }
    int index=0;
    if (orders.size()>1) {
        bool ok=false;
        const QString choice=QInputDialog::getItem(this,QString::fromUtf8("Наряди із залишком за журналом"),
                                                   QString::fromUtf8("Історичні оплати невідомі; потрібна звірка. Оберіть наряд:"),labels,0,false,&ok);
        if (!ok) return;
        index=labels.indexOf(choice);
        if (index<0) return;
    }
    const auto result=CurrentOrdersCoordinator::openExistingOrderEditor(this,orders[index].first,orders[index].second);
    if (!result.launched) QMessageBox::warning(this,QString::fromUtf8("Наряд"),result.errorText);
    else if (result.accepted) loadClientDetails(selectedClientUid());
}
