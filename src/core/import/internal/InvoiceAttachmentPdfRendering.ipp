// PDF drawing and atomic publication helpers for InvoiceAttachmentPdfService.
static QRect invoiceScaledRect(QPdfWriter& pdf, qreal x, qreal y, qreal w, qreal h)
{
    const qreal sx = pdf.width() / 510.0;
    const qreal sy = pdf.height() / 740.0;
    return QRect(qRound(x * sx), qRound(y * sy), qRound(w * sx), qRound(h * sy));
}

static void drawInvoiceTemplateText(QPainter& painter,
                                    const QRect& rect,
                                    const QString& text,
                                    const QFont& font,
                                    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter,
                                    int paddingX = 2,
                                    int paddingY = 1)
{
    if (text.trimmed().isEmpty()) return;
    painter.save();
    painter.setFont(font);
    painter.setPen(Qt::black);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(alignment);
    painter.drawText(rect.adjusted(paddingX, paddingY, -paddingX, -paddingY), text, option);
    painter.restore();
}

static void drawInvoiceTemplateFittedText(QPainter& painter,
                                          const QRect& rect,
                                          const QString& text,
                                          QFont font,
                                          Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter,
                                          int paddingX = 2,
                                          int paddingY = 1,
                                          qreal minPointSize = 8.0)
{
    if (text.trimmed().isEmpty()) return;
    const QRect textRect = rect.adjusted(paddingX, paddingY, -paddingX, -paddingY);
    const QString normalized = text.simplified();
    qreal pointSize = font.pointSizeF() > 0.0 ? font.pointSizeF() : qreal(font.pointSize());
    if (pointSize <= 0.0) pointSize = 12.0;
    for (qreal candidate = pointSize; candidate >= minPointSize; candidate -= 0.5) {
        font.setPointSizeF(candidate);
        const QRect br = QFontMetrics(font).boundingRect(textRect, Qt::TextWordWrap | alignment, normalized);
        if (br.width() <= textRect.width() && br.height() <= textRect.height()) {
            drawInvoiceTemplateText(painter, rect, normalized, font, alignment, paddingX, paddingY);
            return;
        }
    }
    font.setPointSizeF(minPointSize);
    drawInvoiceTemplateText(painter, rect, normalized, font, alignment, paddingX, paddingY);
}

static void drawInvoiceTemplateFittedSingleLineText(QPainter& painter,
                                                    const QRect& rect,
                                                    const QString& text,
                                                    QFont font,
                                                    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter,
                                                    int paddingX = 2,
                                                    int paddingY = 1,
                                                    qreal minPointSize = 8.0)
{
    if (text.trimmed().isEmpty()) return;
    const QRect textRect = rect.adjusted(paddingX, paddingY, -paddingX, -paddingY);
    const QString normalized = text.simplified();
    qreal pointSize = font.pointSizeF() > 0.0 ? font.pointSizeF() : qreal(font.pointSize());
    if (pointSize <= 0.0) pointSize = 12.0;
    for (qreal candidate = pointSize; candidate >= minPointSize; candidate -= 0.5) {
        font.setPointSizeF(candidate);
        const QFontMetrics fm(font);
        if (fm.horizontalAdvance(normalized) <= textRect.width() && fm.height() <= textRect.height()) {
            drawInvoiceTemplateText(painter, rect, normalized, font, alignment, paddingX, paddingY);
            return;
        }
    }
    font.setPointSizeF(minPointSize);
    drawInvoiceTemplateText(painter, rect, normalized, font, alignment, paddingX, paddingY);
}

static int invoiceTemplateTextHeight(const QFontMetrics& fm, int width, const QString& text, int paddingX = 4, int paddingY = 4)
{
    const QRect br = fm.boundingRect(QRect(0, 0, qMax(1, width - paddingX * 2), 2000),
                                     Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                                     text.trimmed());
    return qMax(fm.height() + paddingY * 2, br.height() + paddingY * 2);
}

static bool renderInvoiceTablePdfUnpublished(const QString& outputPath,
                                  const QString& title,
                                  const QString& sourcePath,
                                  const QString& documentNumber,
                                  const QString& documentDate,
                                  const QList<QStringList>& rows,
                                  const QString& supplierName,
                                  QString* errorText)
{
    Q_UNUSED(title);
    Q_UNUSED(sourcePath);

    const InvoiceTemplateData data = buildInvoiceTemplateData(documentNumber, documentDate, rows);
    if (data.items.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("У файлі не знайдено рядків накладної для створення PDF за шаблоном.");
        return false;
    }

    QPdfWriter pdf(outputPath);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(300);

    QPainter painter(&pdf);
    if (!painter.isActive()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити PDF накладної.");
        return false;
    }

    painter.fillRect(QRect(0, 0, pdf.width(), pdf.height()), Qt::white);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QPen(Qt::black, 0));

    QFont companyFont(QStringLiteral("Times New Roman"), 12);
    QFont subtitleFont(QStringLiteral("Times New Roman"), 11, QFont::Bold);
    subtitleFont.setItalic(true);
    QFont labelFont(QStringLiteral("Arial"), 8);
    QFont valueFont(QStringLiteral("Arial"), 9, QFont::Bold);
    QFont smallValueFont(QStringLiteral("Arial"), 8, QFont::Bold);
    QFont tableHeaderFont(QStringLiteral("Arial"), 7, QFont::Bold);
    QFont tableBodyFont(QStringLiteral("Arial"), 8);
    QFont brandBodyFont(QStringLiteral("Arial"), 7);
    QFont totalFont(QStringLiteral("Arial"), 10, QFont::Bold);
    QFont footerFont(QStringLiteral("Arial"), 8);
    QFont noteFont(QStringLiteral("Arial"), 5);

    auto rr = [&](qreal x, qreal y, qreal w, qreal h) { return invoiceScaledRect(pdf, x, y, w, h); };

    const QString companyName = detectInvoiceCompanyName(documentNumber, sourcePath, rows, supplierName);
    drawInvoiceTemplateFittedSingleLineText(painter, rr(30, 5, 255, 22), companyName, companyFont, Qt::AlignLeft | Qt::AlignVCenter, 0, 0, 8.0);
    if (!data.documentNumber.isEmpty()) {
        drawInvoiceTemplateText(painter,
                                rr(410, 4, 90, 16),
                                data.documentNumber,
                                QFont(QStringLiteral("Times New Roman"), 12),
                                Qt::AlignRight | Qt::AlignVCenter,
                                0,
                                0);
    }

    QString subtitle = QString::fromUtf8("Видаткова накладна");
    if (!data.documentNumber.isEmpty()) subtitle += QString::fromUtf8(" №%1").arg(data.documentNumber);
    if (!data.documentDate.isEmpty()) subtitle += QString::fromUtf8(" від %1").arg(data.documentDate);
    drawInvoiceTemplateText(painter, rr(48, 31, 260, 18), subtitle, subtitleFont);

    drawInvoiceTemplateText(painter, rr(31, 62, 60, 14), QString::fromUtf8("Замовник :"), labelFont);
    drawInvoiceTemplateText(painter, rr(117, 59, 170, 20), data.customerName, valueFont);

    drawInvoiceTemplateText(painter, rr(31, 80, 40, 14), QString::fromUtf8("Код :"), labelFont);
    drawInvoiceTemplateText(painter, rr(117, 77, 90, 18), data.customerCode, valueFont);

    drawInvoiceTemplateText(painter, rr(31, 97, 40, 14), QString::fromUtf8("тел. :"), labelFont);
    drawInvoiceTemplateText(painter, rr(117, 94, 110, 18), data.customerPhone, valueFont);

    drawInvoiceTemplateText(painter, rr(31, 114, 64, 14), QString::fromUtf8("Замовлення :"), labelFont);
    drawInvoiceTemplateText(painter, rr(117, 111, 90, 18), data.orderReference, valueFont);

    QString debtLabel = QString::fromUtf8("Борг всього:");
    if (!data.totalDebt.isEmpty()) debtLabel += QString::fromUtf8(" %1 грн").arg(data.totalDebt);
    drawInvoiceTemplateText(painter, rr(226, 104, 125, 16), debtLabel, labelFont);

    QString ratesText;
    if (!data.eurRate.isEmpty()) ratesText += QString::fromUtf8("EUR: %1").arg(data.eurRate);
    if (!data.usdRate.isEmpty()) {
        if (!ratesText.isEmpty()) ratesText += QString::fromUtf8("; ");
        ratesText += QString::fromUtf8("USD: %1").arg(data.usdRate);
    }
    drawInvoiceTemplateText(painter, rr(420, 104, 80, 16), ratesText, smallValueFont, Qt::AlignLeft | Qt::AlignVCenter);

    const bool includeCurrencyColumn = invoiceCanonicalTemplateHasCurrencyColumn(data);
    const QVector<int> xBase = includeCurrencyColumn
        ? QVector<int>{28, 44, 119, 227, 314, 343, 363, 411, 461, 503}
        : QVector<int>{28, 44, 119, 227, 314, 343, 363, 424, 503};
    QVector<int> x;
    x.reserve(xBase.size());
    for (int value : xBase) x.append(rr(value, 0, 0, 0).x());

    const int headerTop = rr(0, 129, 0, 0).y();
    const int headerHeight = qMax(24, rr(0, 20, 0, 0).y());
    const int footerTop = rr(0, 588, 0, 0).y();
    const int footerReserveHeight = rr(0, 152, 0, 0).y();

    const QList<QStringList> canonicalRows = buildCanonicalInvoiceRows(data, includeCurrencyColumn);
    const QStringList headers = canonicalRows.isEmpty() ? invoiceCanonicalTemplateHeaders(includeCurrencyColumn) : canonicalRows.first();

    const int leftTextPaddingPx = qMax(1, qRound((pdf.resolution() * 1.5) / 72.0));

    auto drawRowCells = [&](int y, int rowHeight, const QStringList& values, bool header) {
        for (int c = 0; c + 1 < x.size(); ++c) {
            const QRect cell(x[c], y, x[c + 1] - x[c], rowHeight);
            painter.drawRect(cell);
            const QFont& font = header ? tableHeaderFont : (c == 3 ? brandBodyFont : tableBodyFont);
            Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter;
            if (c == 0 || c >= 4) align = Qt::AlignCenter;

            int paddingX = 2;
            if (c == 1 || c == 2 || c == 3) {
                paddingX = leftTextPaddingPx;
            }

            drawInvoiceTemplateText(painter, cell, c < values.size() ? values.at(c) : QString(), font, align, paddingX, 1);
        }
    };

    drawRowCells(headerTop, headerHeight, headers, true);

    auto drawInvoicePageHeader = [&]() {
        drawInvoiceTemplateFittedSingleLineText(painter, rr(30, 5, 255, 22), companyName, companyFont, Qt::AlignLeft | Qt::AlignVCenter, 0, 0, 8.0);
        if (!data.documentNumber.isEmpty()) {
            drawInvoiceTemplateText(painter,
                                    rr(410, 4, 90, 16),
                                    data.documentNumber,
                                    QFont(QStringLiteral("Times New Roman"), 12),
                                    Qt::AlignRight | Qt::AlignVCenter,
                                    0,
                                    0);
        }
        drawInvoiceTemplateText(painter, rr(48, 31, 320, 18), subtitle, subtitleFont);
    };

    auto drawInvoiceFooter = [&](int startY) {
        const int totalEuroY = startY + rr(0, 3, 0, 0).y();
        const int totalBlockY = totalEuroY + rr(0, 20, 0, 0).y();
        if (includeCurrencyColumn) {
            drawInvoiceTemplateText(painter, QRect(rr(463, 0, 40, 14).x(), totalEuroY, rr(463, 0, 40, 14).width(), rr(463, 0, 40, 14).height()), data.totalEuro, tableBodyFont, Qt::AlignRight | Qt::AlignVCenter, 1, 0);
        }
        drawInvoiceTemplateText(painter, QRect(rr(284, 0, 112, 18).x(), totalBlockY, rr(284, 0, 112, 18).width(), rr(284, 0, 112, 18).height()), QString::fromUtf8("Всього до сплати:"), totalFont, Qt::AlignLeft | Qt::AlignVCenter);
        drawInvoiceTemplateText(painter, QRect(rr(392, 0, 93, 18).x(), totalBlockY, rr(392, 0, 93, 18).width(), rr(392, 0, 93, 18).height()),
                                data.totalPayable.isEmpty() ? QString() : QString::fromUtf8("%1 грн").arg(data.totalPayable),
                                totalFont,
                                Qt::AlignLeft | Qt::AlignVCenter);

        painter.setFont(footerFont);
        const int lineY = totalBlockY + rr(0, 44, 0, 0).y();
        painter.drawLine(rr(31, 0, 0, 0).x(), lineY, rr(145, 0, 0, 0).x(), lineY);
        painter.drawLine(rr(334, 0, 0, 0).x(), lineY, rr(448, 0, 0, 0).x(), lineY);
        drawInvoiceTemplateText(painter, QRect(rr(31, 0, 80, 12).x(), lineY - rr(0, 12, 0, 0).y(), rr(31, 0, 80, 12).width(), rr(31, 0, 80, 12).height()), QString::fromUtf8("Відпустив"), footerFont);
        drawInvoiceTemplateText(painter, QRect(rr(333, 0, 80, 12).x(), lineY - rr(0, 12, 0, 0).y(), rr(333, 0, 80, 12).width(), rr(333, 0, 80, 12).height()), QString::fromUtf8("Отримав"), footerFont);

        const int infoY = lineY + rr(0, 27, 0, 0).y();
        painter.drawLine(rr(31, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y(), rr(73, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y());
        painter.drawLine(rr(103, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y(), rr(173, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y());
        painter.drawLine(rr(299, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y(), rr(350, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y());
        painter.drawLine(rr(412, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y(), rr(485, 0, 0, 0).x(), infoY + rr(0, 14, 0, 0).y());
        drawInvoiceTemplateText(painter, QRect(rr(31, 0, 40, 12).x(), infoY, rr(31, 0, 40, 12).width(), rr(31, 0, 40, 12).height()), QString::fromUtf8("Дата"), footerFont, Qt::AlignLeft | Qt::AlignVCenter);
        drawInvoiceTemplateText(painter, QRect(rr(115, 0, 40, 12).x(), infoY, rr(115, 0, 40, 12).width(), rr(115, 0, 40, 12).height()), QString::fromUtf8("ФІО"), footerFont, Qt::AlignLeft | Qt::AlignVCenter);
        drawInvoiceTemplateText(painter, QRect(rr(302, 0, 45, 12).x(), infoY, rr(302, 0, 45, 12).width(), rr(302, 0, 45, 12).height()), QString::fromUtf8("Сума"), footerFont, Qt::AlignLeft | Qt::AlignVCenter);
        drawInvoiceTemplateText(painter, QRect(rr(417, 0, 50, 12).x(), infoY, rr(417, 0, 50, 12).width(), rr(417, 0, 50, 12).height()), QString::fromUtf8("Підпис"), footerFont, Qt::AlignLeft | Qt::AlignVCenter);

        const QString note = QString::fromUtf8("Своїм підписом Замовник підтверджує відповідність Товару умовам комплектності, кількості та асортименту, відсутність претензій до зовнішніх характеристик Товару");
        drawInvoiceTemplateText(painter, QRect(rr(30, 0, 450, 10).x(), infoY + rr(0, 38, 0, 0).y(), rr(30, 0, 450, 10).width(), rr(30, 0, 450, 10).height()), note, noteFont, Qt::AlignLeft | Qt::AlignVCenter, 0, 0);
    };

    int y = headerTop + headerHeight;
    const QFontMetrics bodyFm(tableBodyFont);
    const QFontMetrics brandFm(brandBodyFont);
    int index = 0;
    while (index < data.items.size()) {
        const InvoiceTemplateItem& item = data.items.at(index);
        const QStringList values = (index + 1 < canonicalRows.size())
            ? canonicalRows.at(index + 1)
            : invoiceCanonicalTemplateRow(index, item, includeCurrencyColumn);

        int rowHeight = qMax(20, rr(0, 20, 0, 0).y());
        rowHeight = qMax(rowHeight, invoiceTemplateTextHeight(bodyFm, x[2] - x[1], item.oem));
        rowHeight = qMax(rowHeight, invoiceTemplateTextHeight(bodyFm, x[3] - x[2], item.name));
        rowHeight = qMax(rowHeight, invoiceTemplateTextHeight(brandFm, x[4] - x[3], item.brand));

        if (y + rowHeight + footerReserveHeight > pdf.height()) {
            if (!pdf.newPage()) {
                if (errorText) *errorText = QString::fromUtf8("Не вдалося створити наступну сторінку PDF.");
                return false;
            }
            painter.fillRect(QRect(0, 0, pdf.width(), pdf.height()), Qt::white);
            painter.setPen(QPen(Qt::black, 0));
            drawInvoicePageHeader();
            drawRowCells(headerTop, headerHeight, headers, true);
            y = headerTop + headerHeight;
        }

        drawRowCells(y, rowHeight, values, false);
        y += rowHeight;
        ++index;
    }

    if (y + footerReserveHeight > pdf.height()) {
        if (!pdf.newPage()) {
            if (errorText) *errorText = QString::fromUtf8("Не вдалося створити сторінку підсумків PDF.");
            return false;
        }
        painter.fillRect(QRect(0, 0, pdf.width(), pdf.height()), Qt::white);
        painter.setPen(QPen(Qt::black, 0));
        drawInvoicePageHeader();
        y = rr(0, 120, 0, 0).y();
    }

    drawInvoiceFooter(y);

    return painter.end();
}

static bool renderInvoiceTablePdf(const QString& outputPath, const QString& title, const QString& sourcePath,
                                  const QString& documentNumber, const QString& documentDate,
                                  const QList<QStringList>& rows, const QString& supplierName, QString* errorText)
{
    return publishInvoicePdf(outputPath, [&](const QString& path) {
        return renderInvoiceTablePdfUnpublished(path, title, sourcePath, documentNumber, documentDate, rows, supplierName, errorText);
    }, errorText);
}

static bool renderImageInvoicePdfUnpublished(const QString& outputPath,
                                  const QString& title,
                                  const QString& sourcePath,
                                  const QString& documentNumber,
                                  const QString& documentDate,
                                  QString* errorText)
{
    QImageReader reader(sourcePath);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати зображення накладної для створення PDF.");
        return false;
    }

    QPdfWriter pdf(outputPath);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter painter(&pdf);
    if (!painter.isActive()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити PDF накладної.");
        return false;
    }

    const int margin = invoiceMmToPx(&pdf, 12.0);
    int y = margin;
    QFont titleFont(QStringLiteral("Times New Roman"), 14, QFont::Bold);
    QFont metaFont(QStringLiteral("Times New Roman"), 10);
    painter.setFont(titleFont);
    painter.drawText(QRect(margin, y, pdf.width() - margin * 2, invoiceMmToPx(&pdf, 8.0)), Qt::AlignHCenter | Qt::AlignVCenter, title);
    y += invoiceMmToPx(&pdf, 8.0);
    painter.setFont(metaFont);
    const QString displayNumber = normalizedInvoiceDisplayNumber(documentNumber);
    if (!displayNumber.isEmpty()) {
        painter.drawText(QRect(margin, y, pdf.width() - margin * 2, invoiceMmToPx(&pdf, 5.0)), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8("Накладна №: %1").arg(displayNumber));
        y += invoiceMmToPx(&pdf, 4.8);
    }
    const QString displayDate = normalizedInvoiceDisplayDate(documentDate, documentNumber);
    if (!displayDate.isEmpty()) {
        painter.drawText(QRect(margin, y, pdf.width() - margin * 2, invoiceMmToPx(&pdf, 5.0)), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8("Дата: %1").arg(displayDate));
        y += invoiceMmToPx(&pdf, 4.8);
    }
    y += invoiceMmToPx(&pdf, 2.0);
    QRect target(margin, y, pdf.width() - margin * 2, pdf.height() - y - margin);
    const QSize scaled = image.size().scaled(target.size(), Qt::KeepAspectRatio);
    const QRect imageRect(QPoint(target.center().x() - scaled.width() / 2, target.top()), scaled);
    painter.drawImage(imageRect, image);
    return painter.end();
}

static bool renderImageInvoicePdf(const QString& outputPath, const QString& title, const QString& sourcePath,
                                  const QString& documentNumber, const QString& documentDate, QString* errorText)
{
    return publishInvoicePdf(outputPath, [&](const QString& path) {
        return renderImageInvoicePdfUnpublished(path, title, sourcePath, documentNumber, documentDate, errorText);
    }, errorText);
}

