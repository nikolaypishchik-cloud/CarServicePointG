// Extracted from src/ui/tabs/parts/PartsPageModulePrelude.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

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
    explicit PartsPaidStatusDelegate(QObject* parent = nullptr)
        : PartsArchiveRowFillDelegate(parent)
    {
    }

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
        const bool paymentActionsEnabled = index.data(Qt::UserRole + 3).toBool();
        const QString text = index.data(Qt::DisplayRole).toString();
        drawPaidStatusButton(painter, option.rect, option.font, text, paid, paymentActionsEnabled, index.data(Qt::UserRole + 4).toBool());
    }
};

class PartsArchiveFitTextDelegate final : public PartsArchiveRowFillDelegate
{
public:
    explicit PartsArchiveFitTextDelegate(QObject* parent = nullptr)
        : PartsArchiveRowFillDelegate(parent)
    {
    }

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
    explicit PartsStatusHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QHeaderView(orientation, parent)
    {
    }

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
