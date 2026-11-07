// Extracted from src/ui/tabs/orders/WorkPartsInlineEditorController.h for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

        if (focused != table && isChildOf(focused, table->viewport())) {
            QWidget* candidate = comboAncestor(focused);
            const QPoint topLeft = candidate->mapTo(table->viewport(), QPoint(0, 0));
            const QRect candidateRect(topLeft, candidate->size());
            if (candidateRect.intersects(cellRect)) return candidate;
        }
    }

    const auto widgets = table->viewport()->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
    for (QWidget* widget : widgets) {
        if (!widget || !widget->isVisible()) continue;
        if (!isChildOf(widget, table->viewport())) continue;
        if (qobject_cast<QAbstractItemView*>(widget)) continue;

        QWidget* candidate = nullptr;
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            candidate = combo;
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            bool insideCombo = false;
            for (QObject* p = edit->parent(); p; p = p->parent()) {
                if (qobject_cast<QComboBox*>(p)) {
                    insideCombo = true;
                    break;
                }
            }
            if (!insideCombo) candidate = edit;
        }
        if (!candidate) continue;

        const QPoint topLeft = candidate->mapTo(table->viewport(), QPoint(0, 0));
        const QRect candidateRect(topLeft, candidate->size());
        if (candidateRect.intersects(cellRect)) {
            return candidate;
        }
    }
    return nullptr;
}

bool flushInlineEditorValueToModel(QTableWidget* table)
{
    if (!table || !table->property("inlineEditorActive").toBool()) return true;
    const QPersistentModelIndex editingIndex = inlineEditingPersistentIndex(table);
    const int row = editingIndex.isValid() ? editingIndex.row() : -1;
    const int col = editingIndex.isValid() ? editingIndex.column() : -1;
    if (!editingIndex.isValid() || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) {
        resetInlineEditorStateForTable(table, "flush_invalid_cell");
        return false;
    }

    logInlineEditorDiagnostic(table, QStringLiteral("editor_commit_begin"), row, col, QStringLiteral("flushInlineEditorValueToModel"), true);

    auto* item = table->item(row, col);
    if (!item || isSpacerItem(item)) {
        resetInlineEditorStateForTable(table, "flush_invalid_item");
        return false;
    }

    QWidget* editor = visibleInlineEditorForCell(table, row, col);
    if (!editor) {
        if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
            qWarning() << "WorkPartsDialog stale inline editor flag cleared"
                       << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                       << "row" << row << "col" << col;
        }
        resetInlineEditorStateForTable(table, "flush_no_visible_editor");
        return false;
    }
    QWidget* input = editor;
    if (auto* combo = qobject_cast<QComboBox*>(editor)) input = combo->lineEdit();
    ManualInputCorrectionFilter::correct(input);
    const bool keepUserSpelling = input && input->property("inputCorrectionSuppressed").toBool();
    QString value;
    bool hasValue = false;
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        value = combo->currentText();
        hasValue = true;
    } else if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
        value = edit->text();
        hasValue = true;
    }
    if (!hasValue) return true;

    if (table->columnCount() == 6) {
        if (col == ServiceColTitle && !keepUserSpelling) value = canonicalizeServiceTitle(value);
        else if (col == ServiceColMaster) value = normalizeSentence(value);
        else value = value.trimmed();
    } else if (isMainOrderPartsTable(table)) {
        if (col == PartColName && !keepUserSpelling) value = normalizePartSentence(value);
        else if (col == PartColStore || col == PartColBrand) value = normalizeSentence(value);
        else value = value.trimmed();
        if (col == PartColOrderedAt) {
            if (value.isEmpty() && rowHasMeaningfulPartData(table, row)) {
                value = currentOrderDateDisplay();
            } else if (!value.isEmpty()) {
                value = displayDateFromStored(storedDateFromDisplay(value));
            }
        }
    } else {
        value = value.trimmed();
    }

    if (item->text() == value) {
        logInlineEditorDiagnostic(table, QStringLiteral("editor_commit_close"), row, col, QStringLiteral("no_change"), true);
        return true;
    }
    if (QAbstractItemModel* model = table->model()) {
        const QModelIndex index = model->index(row, col);
        model->setData(index, value, Qt::EditRole);
        logInlineEditorDiagnostic(table, QStringLiteral("editor_commit_close"), row, col, QStringLiteral("model_setData"), true);
    } else {
        logInlineEditorDiagnostic(table, QStringLiteral("editor_commit_close"), row, col, QStringLiteral("missing_model"), false);
    }
    return true;
}

class WordStyleInlineEditor final : public QLineEdit {
public:
    explicit WordStyleInlineEditor(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {
        setObjectName(QStringLiteral("inlineWordEditor"));
        setFrame(false);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setAttribute(Qt::WA_MacShowFocusRect, false);
        setAutoFillBackground(true);
        setTextMargins(0, 0, 0, 0);
        setContentsMargins(0, 0, 0, 0);
        setContextMenuPolicy(Qt::DefaultContextMenu);
        QPalette pal = palette();
        pal.setColor(QPalette::Text, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::Base, QColor(QStringLiteral("#eef4ff")));
        pal.setColor(QPalette::Window, QColor(QStringLiteral("#eef4ff")));
        setPalette(pal);
        setStyleSheet(QString::fromUtf8(
            "QLineEdit#inlineWordEditor {"
            " background: #eef4ff;"
            " background-color: #eef4ff;"
            " border: none;"
            " border-radius: 0px;"
            " outline: none;"
            " padding: 0px;"
            " margin: 0px;"
            " min-height: 0px;"
            " color: #111111;"
            " selection-background-color: rgba(110, 160, 255, 90);"
            " selection-color: #111111;"
            "}"
            "QLineEdit#inlineWordEditor:focus {"
            " background: #eef4ff;"
            " background-color: #eef4ff;"
            " border: none;"
            " border-radius: 0px;"
            " outline: none;"
            "}"
        ));
    }

    void setEditorBackground(const QColor& color)
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Base, color);
        pal.setColor(QPalette::Window, color);
        setPalette(pal);
        const QString bg = color.name(QColor::HexRgb);
        setStyleSheet(QString::fromUtf8(
            "QLineEdit#inlineWordEditor {"
            " background: %1;"
            " background-color: %1;"
            " border: none;"
            " border-radius: 0px;"
            " outline: none;"
            " padding: 0px;"
            " margin: 0px;"
            " min-height: 0px;"
            " color: #111111;"
            " selection-background-color: rgba(110, 160, 255, 90);"
            " selection-color: #111111;"
            "}"
            "QLineEdit#inlineWordEditor:focus {"
            " background: %1;"
            " background-color: %1;"
            " border: none;"
            " border-radius: 0px;"
            " outline: none;"
            "}"
        ).arg(bg));
    }
};

static QFont fittedCellFont(const QFont& baseFont, const QString& text, int availableWidth, qreal minPt = 5.0)
{
    QFont fitted = baseFont;
    const QString trimmedText = text.trimmed();
    const qreal saneMinPt = qMax<qreal>(5.0, minPt);
    const qreal basePt = fitted.pointSizeF() > 0.0 ? fitted.pointSizeF() : 10.0;
    if (availableWidth <= 4 || trimmedText.isEmpty()) {
        fitted.setPointSizeF(qMax(saneMinPt, basePt));
        return fitted;
    }

    // P1 performance: this helper is used by delegates/row-height sync and was
    // recalculating QFontMetrics loops for the same text/column width on every
    // paint/edit pass.  Keep a small thread-local cache keyed by the actual
    // rendering inputs.  The cache is deliberately bounded and self-clearing so
    // long sessions with many different invoice rows cannot grow unbounded.
    thread_local QHash<QString, qreal> fittedPointSizeCache;
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(baseFont.family())
        .arg(baseFont.bold() ? 1 : 0)
        .arg(baseFont.italic() ? 1 : 0)
        .arg(basePt, 0, 'f', 2)
        .arg(saneMinPt, 0, 'f', 2)
        .arg(availableWidth)
        .arg(trimmedText);
    const auto cachedIt = fittedPointSizeCache.constFind(cacheKey);
    if (cachedIt != fittedPointSizeCache.constEnd()) {
        fitted.setPointSizeF(*cachedIt);
        return fitted;
    }

    qreal chosenPt = qMax(basePt, saneMinPt);
    for (qreal candidate = qMax(basePt, saneMinPt); candidate >= saneMinPt; candidate -= 0.25) {
        fitted.setPointSizeF(candidate);
        if (QFontMetrics(fitted).horizontalAdvance(trimmedText) <= availableWidth) {
            chosenPt = candidate;
            break;
        }
        chosenPt = candidate;
    }
    const qreal finalPt = qMax(saneMinPt, chosenPt);
    fitted.setPointSizeF(finalPt);
    if (fittedPointSizeCache.size() > 4096) {
        fittedPointSizeCache.clear();
    }
    fittedPointSizeCache.insert(cacheKey, finalPt);
    if (finalPt <= saneMinPt + 0.001 && !trimmedText.isEmpty()) {
        const int hit = ++g_fontShrinkClampHitCount;
        if (hit <= 10 || (hit % 50) == 0) {
            qCDebug(csWorkPartsTraceLog) << "font shrink clamp hit" << hit << "minPt" << saneMinPt << "textLen" << trimmedText.size() << "width" << availableWidth;
        }
    }
    return fitted;
}

static QFont fittedWrappedNameCellFont(const QFont& baseFont, const QString& text, int availableWidth)
{
    QFont fitted = baseFont;
    const qreal basePt = fitted.pointSizeF() > 0.0 ? fitted.pointSizeF() : 10.0;
    const qreal minPt = qMax<qreal>(6.0, basePt - 2.0);
    if (availableWidth <= 8 || text.trimmed().isEmpty()) {
        fitted.setPointSizeF(basePt);
        return fitted;
    }

    const QStringList words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int longestWordWidth = 0;
    for (const QString& word : words) {
        longestWordWidth = qMax(longestWordWidth, QFontMetrics(fitted).horizontalAdvance(word));
    }
    if (QFontMetrics(fitted).horizontalAdvance(text) <= availableWidth || longestWordWidth <= availableWidth) {
        fitted.setPointSizeF(basePt);
        return fitted;
    }

    for (qreal candidate = basePt - 0.5; candidate >= minPt; candidate -= 0.5) {
        fitted.setPointSizeF(candidate);
        bool fitsLongestWord = true;
        for (const QString& word : words) {
            if (QFontMetrics(fitted).horizontalAdvance(word) > availableWidth) {
                fitsLongestWord = false;
                break;
            }
        }
        if (fitsLongestWord) return fitted;
    }
    fitted.setPointSizeF(minPt);
    return fitted;
}


static constexpr int RoleCompactFontSignature = Qt::UserRole + 191;

static quint64 compactMainPartsFontSignature(QTableWidget* table, int row, int column)
{
    if (!table || row < 0 || row >= table->rowCount() || column < 0 || column >= table->columnCount()) return 0;
    const auto* item = table->item(row, column);
    QString signature;
    signature.reserve(128);
    signature += QString::number(column);
    signature += QLatin1Char('|');
    signature += QString::number(table->columnWidth(column));
    signature += QLatin1Char('|');
    signature += QString::number(table->font().pointSizeF(), 'f', 2);
    signature += QLatin1Char('|');
    signature += item ? item->text().trimmed().left(160) : QString();
    signature += QLatin1Char('|');
    signature += item && item->font().underline() ? QLatin1Char('u') : QLatin1Char('-');
    return static_cast<quint64>(qHash(signature));
}

static QPair<int, int> bufferedVisibleMainPartsRows(QTableWidget* table, int bufferRows = 30)
{
    if (!isMainOrderPartsTable(table) || table->rowCount() <= 0) return qMakePair(0, -1);
    int first = table->rowAt(0);
    int last = table->rowAt(qMax(0, table->viewport() ? table->viewport()->height() - 1 : table->height() - 1));
    if (first < 0) first = table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0;
    if (last < 0) last = qMin(table->rowCount() - 1, first + 60);
    first = qBound(0, first - bufferRows, table->rowCount() - 1);
    last = qBound(0, last + bufferRows, table->rowCount() - 1);
    return qMakePair(first, last);
}

static void refreshCompactMainPartsCellFontsForRow(QTableWidget* table, int row)
{
    if (!isMainOrderPartsTable(table) || row < 0 || row >= table->rowCount()) return;

    const auto applyFittedFont = [table, row](int column, bool preserveUnderline = false) {
        auto* item = table->item(row, column);
        if (!item) return;
        const quint64 signature = compactMainPartsFontSignature(table, row, column);
        if (item->data(RoleCompactFontSignature).toULongLong() == signature) return;

        QFont font = item->font();
        if (preserveUnderline) {
            const bool underline = font.underline();
            font = fittedCellFont(table->font(), item->text().trimmed(), qMax(26, table->columnWidth(column) - 8), 5.0);
            font.setUnderline(underline);
        } else {
            font = fittedCellFont(table->font(), item->text().trimmed(), qMax(26, table->columnWidth(column) - 8), 5.0);
        }
        item->setFont(font);
        item->setData(RoleCompactFontSignature, QVariant::fromValue(signature));
    };

    applyFittedFont(PartColOem);
    applyFittedFont(PartColStore);
    applyFittedFont(PartColBrand);
    applyFittedFont(PartColInvoiceNumber, true);
}

void refreshCompactMainPartsCellFonts(QTableWidget* table)
{
    if (!isMainOrderPartsTable(table)) return;
    const QSignalBlocker blocker(table);
    const bool updatesWereEnabled = table->updatesEnabled();
    table->setUpdatesEnabled(false);
    // P1 fitted-font lazy cache: do not recompute compact fonts for all rows
    // after rebuild/import/resize. The visible range is updated now; off-screen
    // rows are corrected when they become visible or are edited.
    const auto range = bufferedVisibleMainPartsRows(table);
    for (int row = range.first; row <= range.second; ++row) {
        refreshCompactMainPartsCellFontsForRow(table, row);
    }
    table->setUpdatesEnabled(updatesWereEnabled);
    if (table->viewport()) table->viewport()->update();
    table->update();
}

static constexpr const char* kCanonicalEditorCommitInProgressProperty = "csCanonicalEditorCommitInProgress";

static bool editorBelongsToTableViewport(QTableWidget* table, QWidget* editor)
{
    if (!table || !editor || !table->viewport()) return false;
    for (QWidget* w = editor; w; w = w->parentWidget()) {
        if (w == table->viewport()) return true;
    }
    return false;
}

class FlatTableItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static QColor cellFillColor(const QStyleOptionViewItem& option)
    {
        return option.state.testFlag(QStyle::State_Selected)
            ? QColor(QStringLiteral("#eef4ff"))
            : QColor(255, 255, 255, 240);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (auto* table = qobject_cast<QTableWidget*>(parent())) {
            const bool isEditingCell = table->property("inlineEditorActive").toBool() &&
                                       inlineEditingMatches(table, index.row(), index.column());
            if (isEditingCell) {
                painter->save();
                painter->fillRect(option.rect, cellFillColor(option));
                QColor rightBorder(115, 126, 140);
                rightBorder.setAlphaF(0.34);
                QColor bottomBorder(115, 126, 140);
                bottomBorder.setAlphaF(0.16);
                painter->setPen(rightBorder);
                painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
                painter->setPen(bottomBorder);
                painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
                painter->restore();
                return;
            }
            if ((isMainOrderServicesTable(table) && index.column() == ServiceColTitle)
                || (isMainOrderPartsTable(table) && (index.column() == PartColName
                    || index.column() == PartColOem
                    || index.column() == PartColStore
                    || index.column() == PartColBrand
                    || index.column() == PartColInvoiceNumber))) {
                QStyleOptionViewItem opt(option);
                initStyleOption(&opt, index);
                const QString rawText = opt.text.trimmed();
                opt.text.clear();

                const QWidget* widget = opt.widget;
                QStyle* style = widget ? widget->style() : QApplication::style();
                style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

                painter->save();
                QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget).adjusted(4, 2, -4, -2);
                if (textRect.width() > 4 && !rawText.isEmpty()) {
                    QColor textColor = opt.palette.color((opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
                    const QVariant fg = index.data(Qt::ForegroundRole);
                    if (fg.canConvert<QBrush>()) {
                        const QColor candidate = qvariant_cast<QBrush>(fg).color();
                        if (candidate.isValid()) textColor = candidate;
                    }
                    painter->setPen(textColor);

                    const bool wrappedNameColumn = (isMainOrderServicesTable(table) && index.column() == ServiceColTitle)
                        || (isMainOrderPartsTable(table) && index.column() == PartColName);
                    QTextOption textOption;
                    textOption.setWrapMode(wrappedNameColumn ? QTextOption::WordWrap : QTextOption::NoWrap);
                    textOption.setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

                    QFont drawFont = opt.font;
                    if (wrappedNameColumn) {
                        drawFont = fittedWrappedNameCellFont(opt.font, rawText, textRect.width());
                    } else if (index.column() == PartColOem || index.column() == PartColStore
                        || index.column() == PartColBrand || index.column() == PartColInvoiceNumber) {
                        drawFont = fittedCellFont(opt.font, rawText, textRect.width(), 5.0);
                    }
                    painter->setFont(drawFont);

                    if (wrappedNameColumn) {
                        painter->drawText(QRectF(textRect), rawText, textOption);
                    } else {
                        const QString drawText = QFontMetrics(drawFont).elidedText(rawText, Qt::ElideRight, textRect.width());
                        painter->drawText(QRectF(textRect), drawText, textOption);
                    }
                }
                painter->restore();
                return;
            }


            if (isMainOrderPartsTable(table) && index.column() == PartColDelivered) {
                painter->save();
                painter->fillRect(option.rect, cellFillColor(option));

                if (!shouldShowPartDeliveredCheckbox(table, index.row())) {
                    QColor rightBorder(115, 126, 140);
                    rightBorder.setAlphaF(0.34);
                    QColor bottomBorder(115, 126, 140);
                    bottomBorder.setAlphaF(0.16);
                    painter->setPen(rightBorder);
                    painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
                    painter->setPen(bottomBorder);
                    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
                    painter->restore();
                    return;
                }

                QColor rightBorder(115, 126, 140);
                rightBorder.setAlphaF(0.34);
                QColor bottomBorder(115, 126, 140);
                bottomBorder.setAlphaF(0.16);
                painter->setPen(rightBorder);
                painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
                painter->setPen(bottomBorder);
                painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

                const QRect cell = option.rect.adjusted(0, 0, -1, -1);
                const int side = qMin(20, qMax(16, qMin(cell.width(), cell.height()) - 12));
                QRect box(cell.center().x() - side / 2, cell.center().y() - side / 2, side, side);

                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setPen(QPen(QColor(QStringLiteral("#111111")), 1.25));
                painter->setBrush(QColor(255, 255, 255, 248));
                painter->drawRoundedRect(box, 3, 3);

                const Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                if (state == Qt::Checked) {
                    QPen markPen(QColor(QStringLiteral("#111111")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                    painter->setPen(markPen);
                    const QPoint p1(box.left() + side / 4, box.center().y());
                    const QPoint p2(box.left() + side / 2 - 1, box.bottom() - side / 4);
                    const QPoint p3(box.right() - side / 5, box.top() + side / 4);
                    painter->drawLine(p1, p2);
                    painter->drawLine(p2, p3);
                }

                painter->restore();
                return;
            }
        }
        QStyledItemDelegate::paint(painter, option, index);
    }

    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        if (auto* table = qobject_cast<QTableWidget*>(parent())) {
            if (table->objectName() == QStringLiteral("orderPartsTable") && index.column() == PartColDelivered) {
                if (!model || !index.isValid()) return false;
                if (!shouldShowPartDeliveredCheckbox(table, index.row())) return false;
                const Qt::ItemFlags flags = model->flags(index);
                if (!(flags & Qt::ItemIsEnabled) || !(flags & Qt::ItemIsUserCheckable)) return false;

                switch (event ? event->type() : QEvent::None) {
                case QEvent::MouseButtonPress: {
                    auto* mouseEvent = static_cast<QMouseEvent*>(event);
                    if (mouseEvent->button() != Qt::LeftButton) return false;
                    return option.rect.adjusted(1, 1, -1, -1).contains(mouseEvent->pos());
                }
                case QEvent::MouseButtonRelease: {
                    auto* mouseEvent = static_cast<QMouseEvent*>(event);
                    if (mouseEvent->button() != Qt::LeftButton) return false;
                    if (!option.rect.adjusted(1, 1, -1, -1).contains(mouseEvent->pos())) return false;
                    const Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                    return model->setData(index, state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
                }
                case QEvent::MouseButtonDblClick:
                    return true;
                case QEvent::KeyPress: {
                    auto* keyEvent = static_cast<QKeyEvent*>(event);
                    if (keyEvent->key() != Qt::Key_Space && keyEvent->key() != Qt::Key_Select) return false;
                    const Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                    return model->setData(index, state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
                }
                default:
                    break;
                }
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    bool commitInlineEditorCanonical(QWidget* editor,
                                     QTableWidget* table,
                                     const QString& reason,
                                     int key,
                                     const QPersistentModelIndex& fallbackIndex = QPersistentModelIndex())
    {
        if (!table || !editor) return false;
        if (editor->property(kCanonicalEditorCommitInProgressProperty).toBool()) return true;
        if (!editorBelongsToTableViewport(table, editor)) {
            resetInlineEditorStateForTable(table, "canonical_commit_orphan_editor");
            return false;
        }

        QPersistentModelIndex editorIndex = editorWidgetPersistentIndex(editor, table);
        if (!editorIndex.isValid() && persistentIndexBelongsToTable(table, fallbackIndex)) {
            editorIndex = fallbackIndex;
        }
        const int row = editorIndex.isValid() ? editorIndex.row() : table->currentRow();
        const int col = editorIndex.isValid() ? editorIndex.column() : table->currentColumn();

        logInlineEditorDiagnostic(table, QStringLiteral("editor_commit_canonical"), row, col, reason, true);
        editor->setProperty(kCanonicalEditorCommitInProgressProperty, true);
        markInlineEditorClosing(editor);
        QPointer<QWidget> safeEditor(editor);
        emit commitData(editor);
        logExcelNavigation(table,
                           QStringLiteral("EDITOR_COMMIT_CANONICAL"),
                           key,
                           row,
                           col,
                           row,
                           col,
                           QStringLiteral(" reason=%1 result=commitData")
                               .arg(reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason));
        if (safeEditor && editorBelongsToTableViewport(table, safeEditor.data())) {
            emit closeEditor(safeEditor.data(), QAbstractItemDelegate::NoHint);
            logInlineEditorDiagnostic(table, QStringLiteral("editor_close"), row, col, QStringLiteral("delegate_closeEditor"), true);
        } else {
            resetInlineEditorStateForTable(table, "canonical_commit_editor_destroyed_after_commit");
        }
        if (safeEditor) safeEditor->setProperty(kCanonicalEditorCommitInProgressProperty, false);
        return true;
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (auto* table = qobject_cast<QTableWidget*>(this->parent())) {
            const bool serviceMasterCell = table->columnCount() == 6 && index.column() == ServiceColMaster;
            const bool partStoreCell = isMainOrderPartsTable(table) && index.column() == PartColStore;
            if (serviceMasterCell || partStoreCell) {
                QComboBox* combo = nullptr;
                if (serviceMasterCell) {
                    auto* masterCombo = new ArrowComboBox(parent);
                    masterCombo->setStyleSheet(whiteMasterComboStyle());
                    masterCombo->setFrame(false);
                    combo = masterCombo;
                } else {
                    auto* storeCombo = new QComboBox(parent);
                    storeCombo->setEditable(true);
                    storeCombo->setInsertPolicy(QComboBox::NoInsert);
                    storeCombo->setDuplicatesEnabled(false);
                    storeCombo->setFrame(false);
                    storeCombo->setAttribute(Qt::WA_Hover, false);
                    storeCombo->setMaxVisibleItems(12);
                    storeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                    storeCombo->setMinimumContentsLength(12);
                    storeCombo->setMinimumWidth(260);
                    const QStringList storeNames = loadPartStoreNamesFromSettings();
                    storeCombo->addItems(storeNames);
                    if (auto* line = storeCombo->lineEdit()) {
                        line->setClearButtonEnabled(false);
                        line->setPlaceholderText(QString::fromUtf8("Магазин"));
                        line->setCursorMoveStyle(Qt::VisualMoveStyle);
                        line->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    }
                    auto* completer = new QCompleter(storeNames, storeCombo);
                    completer->setCaseSensitivity(Qt::CaseInsensitive);
                    completer->setFilterMode(Qt::MatchContains);
                    completer->setCompletionMode(QCompleter::PopupCompletion);
                    if (completer->popup()) {
                        completer->popup()->setStyleSheet(QStringLiteral(
                            "background:#ffffff;"
                            "color:#111111;"
                            "border:1px solid #c7cfdb;"
                            "selection-background-color:#dce8fb;"
                            "selection-color:#111111;"
                        ));
                    }
                    storeCombo->setCompleter(completer);
                    storeCombo->setStyleSheet(QStringLiteral(
                        "QComboBox{" 
                        "background:#ffffff;"
                        "color:#111111;"
                        "border:1px solid #9fb2c8;"
                        "border-radius:4px;"
                        "padding:0 22px 0 4px;"
                        "margin:0;"
                        "font-size:12pt;"
                        "selection-background-color:#bfd3f6;"
                        "selection-color:#111111;"
                        "}"
                        "QComboBox::drop-down{"
                        "width:18px;"
                        "border:none;"
                        "background: transparent;"
                        "subcontrol-origin: padding;"
                        "subcontrol-position: top right;"
                        "}"
                        "QComboBox QAbstractItemView{"
                        "background:#ffffff;"
                        "color:#111111;"
                        "border:1px solid #c7cfdb;"
                        "selection-background-color:#dce8fb;"
                        "selection-color:#111111;"
                        "}"
                        "QLineEdit{"
                        "background: transparent;"
                        "color:#111111;"
                        "selection-background-color:#bfd3f6;"
                        "selection-color:#111111;"
                        "border:none;"
                        "padding:0 4px;"
                        "margin:0;"
                        "font-size:12pt;"
                        "}"
                    ));
                    if (auto* line = storeCombo->lineEdit()) {
                        QObject::connect(line, &QLineEdit::textEdited, storeCombo, [storeCombo](const QString& text) {
                            if (auto* c = storeCombo->completer()) {
                                c->setCompletionPrefix(text);
                                c->complete();
                            }
                        });
                    }
                    combo = storeCombo;
                }

                combo->installEventFilter(const_cast<FlatTableItemDelegate*>(this));
                if (combo->lineEdit()) combo->lineEdit()->installEventFilter(const_cast<FlatTableItemDelegate*>(this));
                table->setProperty("inlineEditorActive", true);
                setInlineEditingIndex(table, index);
                setEditorWidgetIndex(combo, index);
                if (combo->lineEdit()) setEditorWidgetIndex(combo->lineEdit(), index);
                const QPersistentModelIndex editorIndex(index);
                QObject::connect(combo, &QObject::destroyed, table, [table, editorIndex]() {
                    clearInlineEditorStateIfMatches(table, editorIndex);
                });
                QObject::connect(combo, qOverload<int>(&QComboBox::activated), combo, [delegate = const_cast<FlatTableItemDelegate*>(this), comboGuard = QPointer<QComboBox>(combo), ownerTable = QPointer<QTableWidget>(table)](int) {
                    auto* combo = comboGuard.data();
                    if (!combo || combo->property("csEditorClosing").toBool()) return;
                    delegate->commitInlineEditorCanonical(combo, ownerTable.data(), QStringLiteral("combo_activated"), 0);
                });
                const QString openReason = table->property("editorOpenReason").toString();
                if (serviceMasterCell && openReason == QStringLiteral("mouse")) {
                    QTimer::singleShot(0, combo, [combo]() { combo->showPopup(); });
                } else if (partStoreCell && combo->lineEdit()) {
                    QTimer::singleShot(0, combo, [combo]() {
                        if (auto* line = combo->lineEdit()) {
                            line->setFocus(Qt::OtherFocusReason);
                            line->setCursorPosition(line->text().size());
                            line->deselect();
                        }
                    });
                }
                table->viewport()->update();
                return combo;
            }
        }

        auto* edit = new WordStyleInlineEditor(parent);
        const bool isOrderedDateCell = [this, &index]() {
            if (auto* table = qobject_cast<QTableWidget*>(this->parent())) {
                return isMainOrderPartsTable(table) && index.column() == PartColOrderedAt;
            }
            return false;
        }();
        if (isOrderedDateCell) {
            edit->setObjectName(QStringLiteral("inlineDateEditor"));
            edit->setInputMask(QStringLiteral("00.00.00;_"));
            edit->setPlaceholderText(QStringLiteral("ДД.ММ.РР"));
            QFont dateFont = edit->font();
            dateFont.setPointSize(qMax(11, dateFont.pointSize() > 0 ? dateFont.pointSize() : 11));
            dateFont.setBold(true);
            edit->setFont(dateFont);
            edit->setAlignment(Qt::AlignCenter);
            edit->setStyleSheet(QString::fromUtf8(
                "QLineEdit#inlineDateEditor {"
                " background:#ffffff;"
                " color:#111111;"
                " border:1px solid rgba(77,121,199,0.95);"
                " border-radius:4px;"
                " padding:1px 4px;"
                " margin:0px;"
                " selection-background-color:#dce9ff;"
                " selection-color:#111111;"
                "}"
                "QLineEdit#inlineDateEditor:focus {"
                " background:#ffffff;"
                " color:#111111;"
                " border:2px solid rgba(77,121,199,1.0);"
                " padding:0px 3px;"
                "}"
            ));
        }
        QPalette pal = option.palette;
        pal.setColor(QPalette::Text, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#111111")));
        pal.setColor(QPalette::Base, cellFillColor(option));
        pal.setColor(QPalette::Window, cellFillColor(option));
        edit->setPalette(pal);
        edit->setEditorBackground(cellFillColor(option));
        edit->installEventFilter(const_cast<FlatTableItemDelegate*>(this));
        if (auto* table = qobject_cast<QTableWidget*>(this->parent())) {
            table->setProperty("inlineEditorActive", true);
            setInlineEditingIndex(table, index);
            setEditorWidgetIndex(edit, index);
            const QPersistentModelIndex editorIndex(index);
            QObject::connect(edit, &QObject::destroyed, table, [table, editorIndex]() {
                clearInlineEditorStateIfMatches(table, editorIndex);
            });
            table->viewport()->update();
        }
        return edit;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            if (auto* table = qobject_cast<QTableWidget*>(this->parent()); table && table->columnCount() == 6 && index.column() == ServiceColMaster) {
                populateMasterEditor(combo, index.data(Qt::EditRole).toString());
            } else {
                const QString raw = normalizePartSentence(index.data(Qt::EditRole).toString());
                combo->setEditText(raw);
                if (combo->lineEdit()) {
                    combo->lineEdit()->setCursorPosition(raw.size());
                    combo->lineEdit()->deselect();
                }
            }
            return;
        }

        auto* edit = qobject_cast<QLineEdit*>(editor);
        if (!edit) {
            QStyledItemDelegate::setEditorData(editor, index);
            return;
        }
        const QString raw = index.data(Qt::EditRole).toString();
        const bool isHint = index.data(RoleIsHint).toBool() || isServiceHintValue(raw) || isPartHintValue(raw);
        edit->setProperty("clearOnFirstMouseClick", false);
        QString openReason = QStringLiteral("keyboard");
