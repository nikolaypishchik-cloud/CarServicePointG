#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"

#include "core/services/AppRuntimeService.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/PartsQueryService.h"
#include "core/perf/PerfWatchdog.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "ui/common/AdaptiveUi.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QPointer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QDebug>
#include <QAbstractItemView>
#include <QVariant>
#include <QDate>
#include <QBrush>
#include <QStyle>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>
#include <utility>
#include <QWidget>
#include <QTimer>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QVector>
#include <QScrollBar>
#include <QThread>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMap>
#include <QSet>
#include <QStyleOptionHeader>
#include <QStyleOptionButton>


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
                                 const bool paid, const bool partial)
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

    const QColor outlineColor = paid ? QColor(QStringLiteral("#8f9aa7"))
                                     : QColor(partial ? QStringLiteral("#b45309") : QStringLiteral("#a80c0c"));
    const QColor shadowColor = paid ? QColor(66, 74, 83, 36)
                                    : QColor(96, 0, 0, 42);
    const QColor topColor = paid ? QColor(QStringLiteral("#f8fafc"))
                                 : QColor(partial ? QStringLiteral("#ffedd5") : QStringLiteral("#ffd3d3"));
    const QColor midTopColor = paid ? QColor(QStringLiteral("#edf2f6"))
                                    : QColor(partial ? QStringLiteral("#fdba74") : QStringLiteral("#ff9a9a"));
    const QColor midBottomColor = paid ? QColor(QStringLiteral("#d6dde5"))
                                       : QColor(partial ? QStringLiteral("#fb923c") : QStringLiteral("#ec4d4d"));
    const QColor bottomColor = paid ? QColor(QStringLiteral("#9ea8b3"))
                                    : QColor(partial ? QStringLiteral("#d97706") : QStringLiteral("#a60000"));

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
    painter->setPen(QColor(QStringLiteral("#111111")));
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

static QColor colorFromBrushOrColorVariant(const QVariant& value)
{
    const QBrush brush = qvariant_cast<QBrush>(value);
    if (brush.style() != Qt::NoBrush && brush.color().isValid()) return brush.color();
    const QColor color = qvariant_cast<QColor>(value);
    return color;
}

QColor overdueBackgroundForIndex(const QModelIndex& index)
{
    QColor bg = colorFromBrushOrColorVariant(index.data(Qt::BackgroundRole));
    if (bg.isValid()) return bg;
    return overdueBackgroundForSeverity(index.data(kOverdueSeverityRole).toInt());
}

// PartsArchiveTableModel now supplies overdue roles directly; no item mutation is used.


class PartsArchiveRowFillDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

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
        QColor bg = colorFromBrushOrColorVariant(bgVar);
        QColor fg = colorFromBrushOrColorVariant(fgVar);
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
        initStyleOption(&option, index);
        option.state &= ~QStyle::State_HasFocus;
        applyRowFill(painter, option, index);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        prepareOption(painter, opt, index);
        QStyledItemDelegate::paint(painter, opt, index);
    }
};


class PartsPaidStatusDelegate final : public PartsArchiveRowFillDelegate
{
public:
    using PartsArchiveRowFillDelegate::PartsArchiveRowFillDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!painter || index.column() != ColPaid) {
            PartsArchiveRowFillDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        base.state &= ~QStyle::State_HasFocus;
        base.text.clear();
        const QWidget* widget = base.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        if (base.state & QStyle::State_Selected) {
            painter->fillRect(base.rect, kPartsSelectedRowBackground);
            base.backgroundBrush = Qt::NoBrush;
            base.palette.setBrush(QPalette::Base, Qt::NoBrush);
            base.palette.setBrush(QPalette::AlternateBase, Qt::NoBrush);
            base.palette.setBrush(QPalette::Window, Qt::NoBrush);
            base.palette.setBrush(QPalette::Highlight, QBrush(kPartsSelectedRowBackground));
            base.palette.setColor(QPalette::Text, kPartsSelectedRowForeground);
            base.palette.setColor(QPalette::WindowText, kPartsSelectedRowForeground);
            base.palette.setColor(QPalette::HighlightedText, kPartsSelectedRowForeground);
        }
        // Paid column is the only column that must keep the normal cell background.
        // Overdue color is applied to all other visible columns by applyRowVisualState().
//                                                                                                                                                                                                                                                           
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &base, painter, widget);

        const bool paid = index.data(Qt::UserRole + 1).toBool();
        const QString text = index.data(Qt::DisplayRole).toString();
        drawPaidStatusButton(painter, option.rect, option.font, text, paid, index.data(Qt::UserRole + 4).toBool());
    }
};

class PartsArchiveFitTextDelegate final : public PartsArchiveRowFillDelegate
{
public:
    using PartsArchiveRowFillDelegate::PartsArchiveRowFillDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.column() != ColBrand && index.column() != ColArticle
                && index.column() != ColDeliveryDate && index.column() != ColStore
                && index.column() != ColAuto) {
            PartsArchiveRowFillDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt(option);
        prepareOption(painter, opt, index);
        const QString rawText = opt.text.trimmed();
        opt.text.clear();

        const QWidget* widget = opt.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        painter->save();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget).adjusted(2, 0, -4, 0);
        if (textRect.width() <= 4 || rawText.isEmpty()) {
            painter->restore();
            return;
        }

        QFont fittedFont = opt.font;
        const qreal basePt = fittedFont.pointSizeF() > 0.0 ? fittedFont.pointSizeF() : 10.0;
        const qreal minPt = 5.0;
        qreal chosenPt = basePt;
        for (qreal candidate = basePt; candidate >= minPt; candidate -= 0.25) {
            fittedFont.setPointSizeF(candidate);
            if (QFontMetrics(fittedFont).horizontalAdvance(rawText) <= textRect.width()) {
                chosenPt = candidate;
                break;
            }
            chosenPt = candidate;
        }
        const qreal finalPt = qMax(minPt, chosenPt);
        fittedFont.setPointSizeF(finalPt);
        if (finalPt <= minPt + 0.001 && !rawText.isEmpty()) {
            const int hit = ++g_partsFontShrinkClampHitCount;
            if (hit <= 10 || (hit % 50) == 0) {
                qInfo() << "parts font shrink clamp hit" << hit << "minPt" << minPt << "textLen" << rawText.size() << "width" << textRect.width();
            }
        }
        painter->setFont(fittedFont);
        const QColor drawColor = (opt.state & QStyle::State_Selected)
            ? kPartsSelectedRowForeground
            : opt.palette.color(QPalette::Text);
        painter->setPen(drawColor);

        QString drawText = rawText;
        const QFontMetrics fm(fittedFont);
        if (fm.horizontalAdvance(drawText) > textRect.width()) {
            drawText = fm.elidedText(drawText, Qt::ElideRight, textRect.width());
        }

        const Qt::Alignment alignment = index.data(Qt::TextAlignmentRole).isValid()
            ? Qt::Alignment(index.data(Qt::TextAlignmentRole).toInt())
            : (Qt::AlignLeft | Qt::AlignVCenter);
        painter->drawText(textRect, alignment, drawText);
        painter->restore();
    }
};

class PartsStatusHeaderView final : public QHeaderView
{
public:
    using QHeaderView::QHeaderView;

protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        if (logicalIndex != ColDeliveryDate && logicalIndex != ColArticle) {
            QHeaderView::paintSection(painter, rect, logicalIndex);
            return;
        }

        QStyleOptionHeader opt;
        initStyleOption(&opt);
        opt.rect = rect;
        opt.section = logicalIndex;
        opt.text.clear();
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

        painter->save();
        const QRect textRect = rect.adjusted(4, 3, -4, -3);
        QFont labelFont = font();
        labelFont.setPointSizeF(8.0);
        labelFont.setBold(true);
        painter->setFont(labelFont);
        painter->setPen(QColor(QStringLiteral("#1c2430")));
        const QString wrappedText = (logicalIndex == ColDeliveryDate)
            ? QString::fromUtf8("Статус\nвстановл.")
            : QString::fromUtf8("ОЕМ\nномер");
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, wrappedText);
        painter->restore();
    }
};

// PartsArchiveTableModel now creates cells lazily via data(); no cell allocation is used.


QString formatMoneyGrouped(double value)
{
    // Important: keep the grouped string in DisplayRole only. Model DisplayRole is kept as the grouped string so thousand separators are preserved.
    const double rounded = qRound64(value);
    const bool isInteger = qAbs(value - rounded) < 0.000001;
    QString out = FormatUtils::formatNumberSpaces(value, isInteger ? 0 : 2);
    out.replace('.', ',');
    return out;
}


int rowHeightForText(const QFontMetrics& fm, const QString& text, int width, int minRowHeight)
{
    const int safeWidth = qMax(1, width);
    const QRect bounds = fm.boundingRect(QRect(0, 0, safeWidth, 10000),
                                         Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                                         text);
    return qMax(minRowHeight, bounds.height() + 12);
}

bool confirmUnpayPart(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QString::fromUtf8("Скасування оплати"));
    dlg.setModal(true);
    dlg.setSizeGripEnabled(false);
    dlg.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dlg.setObjectName(QStringLiteral("cancelPaidDialog"));
    dlg.setStyleSheet(QStringLiteral(
        "QDialog#cancelPaidDialog {"
        " background:#202930;"
        " border:1px solid #69747d;"
        " border-radius:0px;"
        "}"
        "QLabel { color:#ffffff; background:transparent; }"
        "QPushButton {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " color:#ffffff;"
        " border:1px solid #84919d;"
        " border-radius:0px;"
        " padding:0 18px;"
        " min-width:112px;"
        " min-height:34px;"
        "}"
        "QPushButton:hover {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        "}"
        "QPushButton:pressed {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        "}"
    ));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    auto* icon = new QLabel(&dlg);
    const QPixmap iconPx = dlg.style()->standardIcon(QStyle::SP_MessageBoxQuestion).pixmap(20, 20);
    icon->setPixmap(iconPx);
    icon->setFixedSize(20, 20);
    icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    row->addWidget(icon, 0, Qt::AlignTop);

    auto* label = new QLabel(QString::fromUtf8("Скасувати оплату цієї запчастини?"), &dlg);
    QFont labelFont = label->font();
    labelFont.setBold(true);
    labelFont.setPointSizeF(10.8);
    label->setFont(labelFont);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setMinimumWidth(0);
    label->setMaximumWidth(280);
    row->addWidget(label, 1);

    root->addLayout(row);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(10);
    buttons->addStretch(1);

    auto* yesButton = new QPushButton(QString::fromUtf8("Так"), &dlg);
    auto* noButton = new QPushButton(QString::fromUtf8("Ні"), &dlg);
    yesButton->setMinimumSize(104, 34);
    noButton->setMinimumSize(104, 34);
    yesButton->setAutoDefault(false);
    yesButton->setDefault(false);
    noButton->setAutoDefault(false);
    noButton->setDefault(true);
    QObject::connect(yesButton, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(noButton, &QPushButton::clicked, &dlg, &QDialog::reject);
    buttons->addWidget(yesButton);
    buttons->addWidget(noButton);
    root->addLayout(buttons);

    root->setSizeConstraint(QLayout::SetFixedSize);
    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint().expandedTo(QSize(408, 126)));

    if (parent) {
        const QRect host = parent->window()->frameGeometry();
        dlg.move(host.center() - QPoint(dlg.width() / 2, dlg.height() / 2));
    }

    return dlg.exec() == QDialog::Accepted;
}

QString smoothPaidButtonStyle(bool paid)
{
    if (paid) {
        return QString::fromUtf8(
            "QPushButton {"
            " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "   stop:0 rgba(255,255,255,0.98),"
            "   stop:0.10 rgba(248,250,252,0.98),"
            "   stop:0.22 rgba(232,236,240,0.98),"
            "   stop:0.52 rgba(189,196,203,0.98),"
            "   stop:0.78 rgba(144,151,158,0.98),"
            "   stop:1 rgba(92,99,106,0.99));"
            " color:#111111;"
            " border:1px solid rgba(255,255,255,0.92);"
            " border-radius:13px;"
            " font-weight:800;"
            " padding:1px 10px 3px 10px;"
            " min-width:80px;"
            " }"
            "QPushButton:hover {"
            " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "   stop:0 rgba(255,255,255,1.0),"
            "   stop:0.12 rgba(252,253,254,1.0),"
            "   stop:0.26 rgba(238,242,246,1.0),"
            "   stop:0.55 rgba(198,205,212,1.0),"
            "   stop:0.82 rgba(153,160,167,1.0),"
            "   stop:1 rgba(102,109,116,1.0));"
            " }"
            "QPushButton:pressed {"
            " background:qlineargradient(x1:0,y1:1,x2:0,y2:0,"
            "   stop:0 rgba(245,247,249,0.98),"
            "   stop:0.18 rgba(220,225,230,0.98),"
            "   stop:0.55 rgba(166,173,180,0.98),"
            "   stop:1 rgba(110,117,124,0.99));"
            " padding:4px 10px 0px 10px;"
            " }");
    }
    return QString::fromUtf8(
        "QPushButton {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 rgba(255,239,239,0.99),"
        "   stop:0.10 rgba(255,203,203,0.99),"
        "   stop:0.24 rgba(255,126,126,0.99),"
        "   stop:0.58 rgba(226,61,61,1.0),"
        "   stop:0.82 rgba(171,25,25,1.0),"
        "   stop:1 rgba(116,10,10,1.0));"
        " color:#111111;"
        " border:1px solid rgba(255,255,255,0.92);"
        " border-radius:13px;"
        " font-weight:800;"
        " padding:1px 10px 3px 10px;"
        " min-width:80px;"
        " }"
        "QPushButton:hover {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 rgba(255,248,248,1.0),"
        "   stop:0.12 rgba(255,219,219,1.0),"
        "   stop:0.28 rgba(255,145,145,1.0),"
        "   stop:0.60 rgba(236,73,73,1.0),"
        "   stop:0.84 rgba(182,31,31,1.0),"
        "   stop:1 rgba(126,14,14,1.0));"
        " }"
        "QPushButton:pressed {"
        " background:qlineargradient(x1:0,y1:1,x2:0,y2:0,"
        "   stop:0 rgba(255,230,230,0.99),"
        "   stop:0.18 rgba(255,170,170,0.99),"
        "   stop:0.52 rgba(220,52,52,1.0),"
        "   stop:1 rgba(122,12,12,1.0));"
        " padding:4px 10px 0px 10px;"
        " }");
}

QString buttonStyle(bool paid)
{
    return smoothPaidButtonStyle(paid);
}

// Row lookup and overdue coloring are model-driven through prepared indexes and roles.


}

PartsPage::PartsPage(QWidget* parent)
    : QWidget(parent)
{
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(350);
    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });
    m_rowHeightsDebounce = new QTimer(this);
    m_rowHeightsDebounce->setSingleShot(true);
    m_rowHeightsDebounce->setInterval(35);
    connect(m_rowHeightsDebounce, &QTimer::timeout, this, [this]() { updateRowHeights(); });
    m_debtBannerDebounce = new QTimer(this);
    m_debtBannerDebounce->setSingleShot(true);
    m_debtBannerDebounce->setInterval(120);
    connect(m_debtBannerDebounce, &QTimer::timeout, this, [this]() { updateDebtBanner(); });
    buildUi();
    // MainWindow triggers the first refresh when the page is opened.
    // Avoid duplicate initial loading from the constructor.
}

PartsPage::~PartsPage()
{
    cancelActiveRefreshForOwnerShutdown("destructor");
    saveTableHeaderState();
    saveDateRangeState();
}

void PartsPage::closeEvent(QCloseEvent* event)
{
    AsyncTaskHandle::cancel(m_debtCancel);
    m_debtRefreshQueued=false;
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Parts);
    if (m_refreshWorker) {
        m_refreshWorker->requestInterruption();
    }
    saveTableHeaderState();
    saveDateRangeState();
    QWidget::closeEvent(event);
}

void PartsPage::cancelActiveRefreshForOwnerShutdown(const char* reason)
{
    AsyncTaskHandle::cancel(m_debtCancel);
    m_debtRefreshQueued=false;
    if(m_debtWorker) QObject::disconnect(m_debtWorker,nullptr,this,nullptr);
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Parts);
    m_refreshQueued = false;
    m_refreshInProgress = false;

    if (m_refreshWorker) {
        qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE parts_page_owner_shutdown reason=")
                             << QString::fromLatin1(reason ? reason : "unknown")
                             << QStringLiteral(" generation=") << m_refreshGeneration;
        QObject::disconnect(m_refreshWorker, nullptr, this, nullptr);
        m_refreshWorker->requestInterruption();
        m_refreshWorker.clear();
    }
}
