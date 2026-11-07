// Extracted from src/ui/tabs/parts/PartsPageModulePrelude.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

namespace {
static std::atomic_int g_partsFontShrinkClampHitCount{0};
constexpr int ColId = 0;
constexpr int ColDate = 1;
constexpr int ColDeliveryDate = 2;
constexpr int ColStore = 3;
constexpr int ColArticle = 4;
constexpr int ColBrand = 5;
constexpr int ColName = 6;
constexpr int ColQty = 7;
constexpr int ColPrice = 8;
constexpr int ColAmount = 9;
constexpr int ColOrder = 10;
constexpr int ColAuto = 11;
constexpr int ColPaid = 12;
constexpr int kPaidButtonWidth = 108;
constexpr int kPaidButtonHeight = 34;
constexpr int kPaidButtonMinWidth = 92;
constexpr int kPaidButtonSideInset = 6;
constexpr int kPaidColumnMinWidth = kPaidButtonMinWidth + (kPaidButtonSideInset * 2);
constexpr int kPaidColumnDefaultWidth = 120;
constexpr auto kHeaderStateKey = "parts_page/header_state_v6";
constexpr auto kHeaderVisualOrderKey = "parts_page/header_visual_order_v6";
constexpr auto kHeaderWidthsPrefix = "parts_page/header_width_v6";
constexpr auto kDateRangeFromKey = "parts_page/date_range/from_v1";
constexpr auto kDateRangeToKey = "parts_page/date_range/to_v1";
constexpr int kOverdueSeverityRole = Qt::UserRole + 200;
const QColor kPartsSelectedRowBackground(QStringLiteral("#D7E6FA"));
const QColor kPartsSelectedRowForeground(QStringLiteral("#111111"));

static void syncPaidColumnWidgetsWidth(QTableView* table)
{
    // P1 large-table guard: Paid column is item-based now, not a QWidget/QPushButton per row.
    // Keep this helper as a cheap viewport refresh for existing width-change call sites.
    if (table && table->viewport()) table->viewport()->update();
}

static void drawPaidStatusButton(QPainter* painter,
                                 const QRect& cellRect,
                                 const QFont& baseFont,
                                 const QString& text,
                                 const bool paid,
                                 const bool enabled,
                                 const bool partial)
{
    if (!painter) return;

    const int vInset = qMax(2, (cellRect.height() - kPaidButtonHeight) / 2);
    const int availableWidth = qMax(0, cellRect.width() - (kPaidButtonSideInset * 2));

    QFont buttonFont(baseFont);
    buttonFont.setBold(true);
    if (buttonFont.pointSizeF() > 0.0) buttonFont.setPointSizeF(buttonFont.pointSizeF() + 0.35);

    const QFontMetrics fm(buttonFont);
    const int desiredWidth = qBound(kPaidButtonMinWidth, fm.horizontalAdvance(text) + 4, kPaidButtonWidth);
    const int buttonWidth = qMin(availableWidth, desiredWidth);
    const int buttonHeight = qMin(kPaidButtonHeight, cellRect.height() - (vInset * 2));
    QRect buttonRect(cellRect.left() + kPaidButtonSideInset,
                     cellRect.top() + vInset,
                     buttonWidth,
                     buttonHeight);
    buttonRect.moveLeft(cellRect.left() + (cellRect.width() - buttonRect.width()) / 2);

    const QRectF outerRect = QRectF(buttonRect).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = qMax<qreal>(8.0, outerRect.height() / 2.15);

    const QColor outlineColor = enabled ? (paid ? QColor("#8f9aa7") : QColor(partial ? "#b45309" : "#a80c0c")) : QColor("#7e8995");
    const QColor shadowColor = enabled ? (paid ? QColor(66, 74, 83, 36) : QColor(96, 0, 0, 42)) : QColor(48, 56, 64, 22);
    const QColor topColor = enabled ? (paid ? QColor("#f8fafc") : QColor(partial ? "#ffedd5" : "#ffd3d3")) : QColor("#d9dfe5");
    const QColor midTopColor = enabled ? (paid ? QColor("#edf2f6") : QColor(partial ? "#fdba74" : "#ff9a9a")) : QColor("#c0c8d0");
    const QColor midBottomColor = enabled ? (paid ? QColor("#d6dde5") : QColor(partial ? "#fb923c" : "#ec4d4d")) : QColor("#a6b0bb");
    const QColor bottomColor = enabled ? (paid ? QColor("#9ea8b3") : QColor(partial ? "#d97706" : "#a60000")) : QColor("#87929d");

    QPainterPath shadowPath;
    shadowPath.addRoundedRect(outerRect.translated(0.0, 1.2), radius, radius);

    QPainterPath buttonPath;
    buttonPath.addRoundedRect(outerRect, radius, radius);

    QLinearGradient fill(outerRect.topLeft(), outerRect.bottomLeft());
    fill.setColorAt(0.00, topColor);
    fill.setColorAt(0.30, midTopColor);
    fill.setColorAt(0.60, midBottomColor);
    fill.setColorAt(1.00, bottomColor);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(Qt::NoPen);
    painter->setBrush(shadowColor);
    painter->drawPath(shadowPath);

    painter->setBrush(fill);
    painter->drawPath(buttonPath);

    painter->save();
    painter->setClipPath(buttonPath);
    QRectF glossRect = outerRect.adjusted(1.5, 1.5, -1.5, -outerRect.height() * 0.48);
    QLinearGradient gloss(glossRect.topLeft(), glossRect.bottomLeft());
    gloss.setColorAt(0.0, QColor(255, 255, 255, paid ? 210 : 175));
    gloss.setColorAt(0.55, QColor(255, 255, 255, paid ? 105 : 65));
    gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->setBrush(gloss);
    painter->drawRoundedRect(glossRect, qMax<qreal>(4.0, radius - 2.5), qMax<qreal>(4.0, radius - 2.5));

    QRectF lowerShadeRect = outerRect.adjusted(1.5, outerRect.height() * 0.46, -1.5, -1.5);
    QLinearGradient lowerShade(lowerShadeRect.topLeft(), lowerShadeRect.bottomLeft());
    lowerShade.setColorAt(0.0, QColor(0, 0, 0, paid ? 10 : 14));
    lowerShade.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter->setBrush(lowerShade);
    painter->drawRoundedRect(lowerShadeRect, qMax<qreal>(4.0, radius - 2.5), qMax<qreal>(4.0, radius - 2.5));
    painter->restore();

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(outlineColor, 1.15));
    painter->drawPath(buttonPath);

    painter->setFont(buttonFont);
    painter->setPen(enabled ? QColor(QStringLiteral("#111111")) : QColor(QStringLiteral("#5b6570")));
    painter->drawText(buttonRect.adjusted(2, 0, -2, 0), Qt::AlignCenter | Qt::TextSingleLine, text);
    painter->restore();
}


QColor overdueBackgroundForSeverity(int overdueSeverity)
{
    if (overdueSeverity >= 2) return QColor(255, 160, 160);
    if (overdueSeverity == 1) return QColor(255, 235, 120);
    return QColor();
}

QColor overdueForegroundForSeverity(int overdueSeverity)
{
    return overdueSeverity > 0 ? QColor(Qt::black) : QColor();
}

static QColor partsColorFromBrushOrColorVariant(const QVariant& value)
{
    const QBrush brush = qvariant_cast<QBrush>(value);
    if (brush.style() != Qt::NoBrush && brush.color().isValid()) return brush.color();
    return qvariant_cast<QColor>(value);
}

QColor overdueBackgroundForIndex(const QModelIndex& index)
{
    QColor bg = partsColorFromBrushOrColorVariant(index.data(Qt::BackgroundRole));
    if (bg.isValid()) return bg;
    return overdueBackgroundForSeverity(index.data(kOverdueSeverityRole).toInt());
}

// PartsArchiveTableModel now supplies overdue roles directly; no item mutation is used.


class PartsArchiveRowFillDelegate : public QStyledItemDelegate
{
public:
    explicit PartsArchiveRowFillDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

protected:
    static void applyRowFill(QPainter* painter, QStyleOptionViewItem& opt, const QModelIndex& index)
    {
        if (!index.isValid() || index.column() == ColPaid || !painter) return;

        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, kPartsSelectedRowBackground);
            opt.backgroundBrush = Qt::NoBrush;
            opt.palette.setBrush(QPalette::Base, Qt::NoBrush);
            opt.palette.setBrush(QPalette::AlternateBase, Qt::NoBrush);
            opt.palette.setBrush(QPalette::Window, Qt::NoBrush);
            opt.palette.setBrush(QPalette::Highlight, QBrush(kPartsSelectedRowBackground));
            opt.palette.setColor(QPalette::Text, kPartsSelectedRowForeground);
            opt.palette.setColor(QPalette::WindowText, kPartsSelectedRowForeground);
            opt.palette.setColor(QPalette::HighlightedText, kPartsSelectedRowForeground);
            opt.palette.setColor(QPalette::ButtonText, kPartsSelectedRowForeground);
            return;
        }

        QVariant bgVar = index.data(Qt::BackgroundRole);
        QVariant fgVar = index.data(Qt::ForegroundRole);
        QColor bg = partsColorFromBrushOrColorVariant(bgVar);
        QColor fg = partsColorFromBrushOrColorVariant(fgVar);
        if (!bg.isValid()) {
            const int severity = index.data(kOverdueSeverityRole).toInt();
            if (severity >= 2) bg = QColor(255, 160, 160);
            else if (severity == 1) bg = QColor(255, 235, 120);
            if (severity > 0 && !fg.isValid()) fg = QColor(Qt::black);
        }
        if (!bg.isValid()) return;

        painter->fillRect(opt.rect, bg);
        // Keep the manual overdue fill visible under QSS-backed item views.
        // QStyleSheetStyle can repaint the panel over opt.backgroundBrush in the
        // normal state; leave Base/Window transparent and use Highlight only for
        // selected overdue rows.
        opt.backgroundBrush = Qt::NoBrush;
        opt.palette.setBrush(QPalette::Base, Qt::NoBrush);
        opt.palette.setBrush(QPalette::AlternateBase, Qt::NoBrush);
        opt.palette.setBrush(QPalette::Window, Qt::NoBrush);
        opt.palette.setBrush(QPalette::Highlight, QBrush(bg));
        if (fg.isValid()) {
            opt.palette.setColor(QPalette::Text, fg);
            opt.palette.setColor(QPalette::WindowText, fg);
            opt.palette.setColor(QPalette::HighlightedText, fg);
            opt.palette.setColor(QPalette::ButtonText, fg);
        }
    }


    void prepareOption(QPainter* painter, QStyleOptionViewItem& option, const QModelIndex& index) const
    {
