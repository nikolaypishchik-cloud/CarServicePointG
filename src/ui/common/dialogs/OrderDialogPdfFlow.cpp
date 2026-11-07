// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/common/dialogs/OrderDialogModulePrelude.h"
#include "core/services/AccessControlService.h"
#include "core/services/PdfWebsiteFooter.h"
#include "core/util/ManualInputNormalizer.h"
#include <QTextEdit>
#include <QTextLayout>
#include <QScopedValueRollback>
#include "core/services/orders/PartImportTextSupport.h"

namespace {
static QImage applyLogoVisualSettings(const QImage& src, double contrast, double saturation, double opacity)
{
    if (src.isNull()) return src;

    const double safeContrast = qBound(0.0, contrast, 3.0);
    const double safeSaturation = qBound(0.0, saturation, 3.0);
    const double safeOpacity = qBound(0.0, opacity, 1.0);

    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a0 = qAlpha(px);
            if (a0 == 0) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            double r = qRed(px);
            double g = qGreen(px);
            double b = qBlue(px);
            const double gray = 0.299 * r + 0.587 * g + 0.114 * b;
            r = gray + (r - gray) * safeSaturation;
            g = gray + (g - gray) * safeSaturation;
            b = gray + (b - gray) * safeSaturation;
            r = ((r / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            g = ((g / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            b = ((b / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            const int a = qBound(0, qRound(a0 * safeOpacity), 255);
            line[x] = qRgba(qBound(0, qRound(r), 255),
                            qBound(0, qRound(g), 255),
                            qBound(0, qRound(b), 255),
                            a);
        }
    }
    return img;
}
}


void OrderDialog::exportPdfPreviewAndOpen(bool partsReport)
{
    if (!AccessControlService::canAccessOrder( m_orderNumber)) return;
    QScopedValueRollback<QString> recommendationsScope(m_pdfRecommendations, QString());
    if (!partsReport) {
        QMessageBox question(QMessageBox::Question, tr("PDF наряду"), tr("Додати рекомендації?"), QMessageBox::NoButton, this);
        auto* yes = question.addButton(tr("Так"), QMessageBox::YesRole);
        auto* no = question.addButton(tr("Ніт"), QMessageBox::NoRole);
        question.setDefaultButton(no);
        question.exec();
        if (question.clickedButton() == yes) {
            QDialog editor(this);
            editor.setWindowTitle(tr("Рекомендації"));
            editor.resize(700, 420);
            auto* layout = new QVBoxLayout(&editor);
            auto* text = new QTextEdit(&editor);
            text->setAcceptRichText(false);
            text->setFont(QFont(QStringLiteral("Times New Roman"), 14));
            auto* correctionTimer = new QTimer(&editor);
            correctionTimer->setSingleShot(true);
            correctionTimer->setInterval(500);
            connect(text, &QTextEdit::textChanged, correctionTimer, [correctionTimer] { correctionTimer->start(); });
            connect(correctionTimer, &QTimer::timeout, &editor, [text] {
                const QString original = text->toPlainText();
                if (original.isEmpty() || !text->textCursor().atEnd()
                    || !(original.back().isSpace() || QStringLiteral(".,;:!?").contains(original.back()))) return;
                QString corrected = ManualInputNormalizer::normalize(original, ManualInputNormalizer::Role::Text).text.trimmed();
                int spaces = 0;
                while (spaces < original.size() && original.at(original.size() - spaces - 1) == QLatin1Char(' ')) ++spaces;
                corrected += QString(spaces, QLatin1Char(' '));
                if (corrected == original) return;
                auto cursor = text->textCursor();
                cursor.beginEditBlock();
                cursor.select(QTextCursor::Document);
                cursor.insertText(corrected);
                cursor.endEditBlock();
                text->setTextCursor(cursor);
            });
            layout->addWidget(text);
            auto* buttons = new QHBoxLayout;
            auto* add = new QPushButton(tr("Додати"), &editor);
            auto* cancel = new QPushButton(tr("Скасувати"), &editor);
            buttons->addStretch();
            buttons->addWidget(add);
            buttons->addWidget(cancel);
            layout->addLayout(buttons);
            connect(add, &QPushButton::clicked, &editor, [&editor, text]() {
                text->setPlainText(ManualInputNormalizer::normalize(text->toPlainText(), ManualInputNormalizer::Role::Text).text.trimmed());
                editor.accept();
            });
            connect(cancel, &QPushButton::clicked, &editor, &QDialog::reject);
            text->setFocus();
            if (editor.exec() != QDialog::Accepted) return;
            m_pdfRecommendations = text->toPlainText().trimmed();
        } else if (question.clickedButton() != no) {
            return;
        }
    }
    const int previewOrderNumber = m_orderNumber > 0 ? m_orderNumber : (m_num ? m_num->value() : 0);
    const bool hadOverrideCursor = QApplication::overrideCursor() != nullptr;
    if (!hadOverrideCursor) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
    }

    const auto preview = OrderDialogPdfService::exportPreviewAndOpen(
        previewOrderNumber,
        this,
        [this, previewOrderNumber, partsReport](const QString& outFile, QString* errorText) {
            QElapsedTimer timer;
            timer.start();
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Order PDF export started:" << outFile << "order" << previewOrderNumber;
            const bool ok = exportPdfToFile(outFile, errorText, partsReport);
            const qint64 bytes = QFileInfo(outFile).exists() ? QFileInfo(outFile).size() : -1;
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Order PDF export finished:" << outFile << "order" << previewOrderNumber << "bytes" << bytes << "elapsed_ms" << timer.elapsed();
            return ok;
        }, partsReport, currentOpenedAt().date());

    if (!hadOverrideCursor && QApplication::overrideCursor() != nullptr) {
        QApplication::restoreOverrideCursor();
    }

    if (!preview.ok) {
        QMessageBox::warning(this, QString::fromUtf8("PDF"), preview.error);
        return;
    }

    if (!preview.opened) {
        QMessageBox::warning(this,
                             QString::fromUtf8("PDF збережено"),
                             QString::fromUtf8("PDF наряду збережено, але не вдалося відкрити автоматично:\n%1").arg(QDir::toNativeSeparators(preview.outFile)));
    }
}
bool OrderDialog::exportPartsReportToFile(const QString& outFile, QString* errorText)
{
    return exportPdfToFile(outFile, errorText, true);
}

bool OrderDialog::exportCustomerReportToFile(const QString& outFile, const QString& recommendations, QString* errorText)
{
    QScopedValueRollback<QString> scope(m_pdfRecommendations, ManualInputNormalizer::normalize(recommendations, ManualInputNormalizer::Role::Text).text.trimmed());
    return exportPdfToFile(outFile, errorText, false);
}

bool OrderDialog::exportPdfToFile(const QString& outFile, QString* errorText, bool partsReport)
{
    const auto totals=OrderDialogReadQueryService::financialTotals(m_orderNumber);
    if(!totals.ok()) {if(errorText)*errorText=totals.error;return false;}
    exportPdf(outFile, partsReport);
    const QFileInfo fi(outFile);
    if (!fi.exists() || fi.size() <= 0) {
        if (errorText) {
            *errorText = QString::fromUtf8("PDF наряду не був коректно сформований:\n%1")
                .arg(QDir::toNativeSeparators(outFile));
        }
        return false;
    }
    return true;
}
void OrderDialog::exportPdf(const QString& outFile, bool partsReport)
{
    const auto financialTotals=OrderDialogReadQueryService::financialTotals(m_orderNumber);
    if(!financialTotals.ok()) return;
    const double financialWorksTotal=financialTotals.worksCents/100.0;
    const double partsTotalValue=financialTotals.partsCents/100.0;
    QPdfWriter pdf(outFile);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    pdf.setPageMargins(QMarginsF(20, partsReport ? 10 : 7, 10, partsReport ? 10 : 7), QPageLayout::Millimeter);

    QPainter p(&pdf);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect page = p.viewport();
    const int left = 28;                   // widened ~5 mm from left edge
    const int right = page.width() - 1;    // widened to near right edge
    const int width = right - left;
    const int pageBottom = page.height() - (partsReport ? 57 : PdfWebsiteFooter::height(&pdf));
    int y = partsReport ? 44 : 0;

    const auto branding = BrandingSettingsService::loadSnapshot();
    const auto pdfSettings = GeneralSettingsService::loadReportsPdfSettings();
    QString brandName = branding.name.trimmed();
    if (brandName.isEmpty() || brandName == QString::fromUtf8("CarService PointG 2.1.1")
        || brandName == QString::fromUtf8("CarService PointG 2.1.2")) {
        brandName = QStringLiteral("CarServicePointG");
    }
    const QString brandPhone = branding.phone.trimmed().isEmpty() ? QString::fromUtf8("0951425361") : branding.phone;
    const QString pdfFooterText = pdfSettings.footerText.trimmed();
    const QString brandLogoPath = branding.logoPath.trimmed();
    const bool includeBrandLogo = pdfSettings.includeLogo;
    const bool useCustomBrandLogoPosition = pdfSettings.useCustomLogoPosition;
    const double brandLogoPosX = qBound(0.0, branding.logoPosX, 1.0);
    const double brandLogoPosY = qBound(0.0, branding.logoPosY, 1.0);
    const double brandLogoScale = qBound(0.3, branding.logoScale, 3.0);
    const double brandLogoContrast = qBound(0.0, branding.logoContrast, 3.0);
    const double brandLogoSaturation = qBound(0.0, branding.logoSaturation, 3.0);
    const double brandLogoOpacity = qBound(0.0, branding.logoOpacity, 1.0);

    const QString plate = m_plate ? m_plate->text().trimmed() : QString();
    const QString car = m_car ? m_car->text().trimmed() : QString();
    const QString year = (m_year && m_year->value() > 0) ? QString::number(m_year->value()) : QString();
    const QString carDisplay = year.isEmpty() ? car : QString::fromUtf8("%1 (%2)").arg(car, year);
    const QString engine = m_engineCode ? m_engineCode->text().trimmed() : QString();
    QString engineVol = m_engineVolume ? m_engineVolume->text().trimmed() : QString();
    if (!engineVol.isEmpty() && !engineVol.contains(QString::fromUtf8("л"))) engineVol += QString::fromUtf8("л");
    const QString fuel = selectedFuelType();
    const QString color = m_color ? m_color->text().trimmed() : QString();
    const QString odometer = m_odometer ? QString::number(m_odometer->value()) : QString();
    const QString vin = m_vin ? m_vin->text().trimmed() : QString();
    const QString client = (m_client && !m_client->text().trimmed().isEmpty()) ? m_client->text().trimmed() : QString::fromUtf8("-");
    const QString master = m_master ? m_master->currentText() : QString();
    const QDateTime openedDt = currentOpenedAt();
    const QString opened = openedDt.isValid() ? openedDt.toString(QString::fromUtf8("dd.MM.yy HH:mm")) : QString();

    QString closedRaw;
    {
        OrderSnapshotService::OrderSnapshot snapshot;
        if (OrderDialogReadQueryService::loadSnapshotByNumber(m_orderNumber, &snapshot, nullptr)) {
            closedRaw = snapshot.closedAt.trimmed();
        }
    }
    QDateTime closedDt = parseStoredDateTimeFlexible(closedRaw);
    const QString closed = closedDt.isValid() ? closedDt.toString(QString::fromUtf8("dd.MM.yy HH:mm")) : QString();
    QString repairDuration = QString::fromUtf8("-");
    if (openedDt.isValid() && closedDt.isValid()) {
        const QDateTime startDt = openedDt <= closedDt ? openedDt : closedDt;
        const QDateTime endDt = openedDt <= closedDt ? closedDt : openedDt;
        const qint64 totalMin = startDt.secsTo(endDt) / 60;
        const qint64 hh = totalMin / 60;
        const qint64 mm = totalMin % 60;
        repairDuration = QString::fromUtf8("%1:%2").arg(hh).arg(mm, 2, 10, QChar('0'));
    }

    auto money0 = [&](double v) { return QString::fromUtf8("%1 грн").arg(FormatUtils::pdfMoney(v)); };

    const QString detailOrderUid = OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(m_orderNumber);
    const auto printParts = OrderDialogPrintQueryService::loadPartRows(detailOrderUid, partsReport);
    double purchaseTotalVal = 0.0;
    double materialPurchaseTotal = 0.0, materialSaleTotal = 0.0;
    for (const auto& row : printParts) {
        purchaseTotalVal += row.purchaseAmount;
        if (row.stockMaterial) {
            materialPurchaseTotal += row.purchaseAmount;
            materialSaleTotal += partsReport ? qRound64(row.qty * row.price * 100.0) / 100.0 : row.amount;
        }
    }

    auto configuredLogoImage = [&]() {
        if (!includeBrandLogo || brandLogoPath.isEmpty()) return QImage();
        QImage logoImg(brandLogoPath);
        if (logoImg.isNull()) return QImage();
        return applyLogoVisualSettings(logoImg, brandLogoContrast, brandLogoSaturation, brandLogoOpacity);
    };

    auto configuredLogoSize = [&](const QImage& logoImg) {
        QSizeF targetSize(page.width() * 0.26 * brandLogoScale, page.height() * 0.16 * brandLogoScale);
        targetSize.setWidth(qMin(targetSize.width(), page.width() * 0.9));
        targetSize.setHeight(qMin(targetSize.height(), page.height() * 0.8));
        QSizeF imgSize(logoImg.width(), logoImg.height());
        imgSize.scale(targetSize, Qt::KeepAspectRatio);
        const double minW = qMin(190.0 * qMin(brandLogoScale, 1.6), targetSize.width());
        const double minH = qMin(54.0 * qMin(brandLogoScale, 1.6), targetSize.height());
        if (imgSize.width() < minW && logoImg.width() > 0) {
            const double ratio = static_cast<double>(logoImg.height()) / static_cast<double>(logoImg.width());
            imgSize.setWidth(minW);
            imgSize.setHeight(qMin(targetSize.height(), minW * ratio));
        }
        if (imgSize.height() < minH && logoImg.height() > 0) {
            const double ratio = static_cast<double>(logoImg.width()) / static_cast<double>(logoImg.height());
            imgSize.setHeight(minH);
            imgSize.setWidth(qMin(targetSize.width(), minH * ratio));
        }
        return imgSize;
    };

    auto drawConfiguredLogo = [&](std::optional<QRectF> defaultRect = std::nullopt) {
        const QImage logoImg = configuredLogoImage();
        if (logoImg.isNull()) return;

        const QSizeF imgSize = configuredLogoSize(logoImg);
        QRectF logoDrawRect;
        if (useCustomBrandLogoPosition || !defaultRect.has_value()) {
            const double maxX = qMax(0.0, static_cast<double>(page.width()) - imgSize.width());
            const double maxY = qMax(0.0, static_cast<double>(page.height()) - imgSize.height());
            logoDrawRect = QRectF(page.left() + brandLogoPosX * maxX,
                                  page.top() + brandLogoPosY * maxY,
                                  imgSize.width(),
                                  imgSize.height());
        } else {
            logoDrawRect = defaultRect.value();
        }
        p.drawImage(logoDrawRect, logoImg);
    };
    auto thinPen = [&](const QColor& c = QColor(138, 138, 138)) { return QPen(c, 0.5); };

    auto drawTextInRect = [&](const QRect& r, const QString& text, qreal pt, bool bold,
                              Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                              const QColor& color = QColor(24, 24, 24),
                              bool wordWrap = true,
                              const QString& family = QStringLiteral("Times New Roman")) {
        p.save();
        QFont f(family);
        f.setPointSizeF(pt);
        f.setBold(bold);
        p.setFont(f);
        p.setPen(color);
        int flags = int(align) | (wordWrap ? int(Qt::TextWordWrap) : 0);
        QRect drawRect = r.adjusted(0, 1, 0, -1);
        p.drawText(drawRect, flags, text);
        p.restore();
    };

    auto drawBox = [&](const QRect& r, const QColor& fill = Qt::white, const QColor& border = QColor(150, 150, 150)) {
        p.save();
        p.setPen(thinPen(border));
        p.setBrush(fill);
        p.drawRect(r);
        p.restore();
    };

    auto elideToWidth = [&](const QString& text, int pt, bool bold, int maxWidth,
                            const QString& family = QStringLiteral("Times New Roman")) {
        QFont f(family);
        f.setPointSize(pt);
        f.setBold(bold);
        QFontMetrics fm(f, p.device());
        return fm.elidedText(text, Qt::ElideRight, maxWidth);
    };

    auto lineHeight = [&](int pt, bool bold, const QString& family = QStringLiteral("Times New Roman")) {
        QFont f(family);
        f.setPointSize(pt);
        f.setBold(bold);
        return QFontMetrics(f, p.device()).height();
    };

    auto textRectHeight = [&](const QString& text, qreal pt, bool bold, int w,
                              const QString& family = QStringLiteral("Times New Roman")) {
        QFont f(family);
        f.setPointSizeF(pt);
        f.setBold(bold);
        QFontMetricsF fm(f, p.device());
        return qCeil(fm.boundingRect(QRectF(0, 0, qMax(1, w), 4000), Qt::TextWordWrap | Qt::AlignTop, text).height()) + 4;
    };

    struct InfoRow {
        QString label;
        QString value;
        qreal valuePt = 9.0;
        bool valueBold = false;
    };

    const int gap = 18;
    const int colW = (width - gap) / 2;
    const int leftLblW = 126;
    const int rightLblW = 138;
    const int valuePad = 6;
    const int titleH = 22;
    const int rowGap = 7;

    auto buildLeftRows = [&]() -> QVector<InfoRow> {
        if (partsReport) return {
            {QString::fromUtf8("Номер авто:"), plate, 9.5, true},
            {QString::fromUtf8("Марка / модель:"), carDisplay, 9.5, true}
        };
        return {
            {QString::fromUtf8("Номер авто:"), plate, 9.5, true},
            {QString::fromUtf8("Марка / модель:"), carDisplay, 9.5, true},
            {QString::fromUtf8("Двигун:"), engine, 8, false},
            {QString::fromUtf8("Об'єм:"), engineVol, 8, false},
            {QString::fromUtf8("Пальне:"), fuel, 8, false},
            {QString::fromUtf8("Колір:"), color, 8, false},
            {QString::fromUtf8("Одометр:"), odometer, 8, false}
        };
    };
    auto buildRightRows = [&]() -> QVector<InfoRow> {
        if (partsReport) {
            return {
                {QString::fromUtf8("Клієнт:"), client, 8, false},
                {QString::fromUtf8("Майстер:"), master, 8, false}
            };
        }
        return {
            {QString::fromUtf8("Клієнт:"), client, 8, false},
            {QString::fromUtf8("Майстер:"), master, 8, false},
            {QString::fromUtf8("Дата відкриття:"), opened, 8, false},
            {QString::fromUtf8("VIN:"), vin, 8, true},
            {QString::fromUtf8("Роботи:"), money0(financialWorksTotal), 8, false},
            {QString::fromUtf8("Запчастини:"), money0(partsTotalValue), 8, false},
            {QString::fromUtf8("Разом:"), FormatUtils::pdfMoney(qMax(0.0, financialWorksTotal + partsTotalValue - FormatUtils::discountAmount(m_discount->value(), m_discountMode->currentData().toString(), financialWorksTotal))), 8, false}
        };
    };

    auto calcRowsHeight = [&](const QVector<InfoRow>& rows, int valueAreaWidth) {
        int totalH = 0;
        for (const auto& row : rows) {
            const int valueH = qMax(14, textRectHeight(row.value, row.valuePt, row.valueBold, valueAreaWidth));
            const int labelH = qMax(14, lineHeight(8, true, QStringLiteral("Times New Roman")));
            totalH += qMax(valueH, labelH) + rowGap;
        }
        return totalH;
    };

    auto calcInfoBlockHeight = [&]() {
        const auto leftRows = buildLeftRows();
        const auto rightRows = buildRightRows();
        const int leftValueW = colW - 22 - leftLblW - valuePad;
        const int rightValueW = colW - 26 - rightLblW - valuePad;
        const int rowsHeight = qMax(calcRowsHeight(leftRows, leftValueW),
                                    calcRowsHeight(rightRows, rightValueW));
        return qMax(partsReport ? 0 : 235, titleH + 28 + rowsHeight + (partsReport ? 12 : 32));
    };

    auto drawHeaderBlock = [&]() {
        const int headerH = partsReport ? 140 : 152;
        drawBox(QRect(left, y, width, headerH), Qt::white, QColor(160, 160, 160));

        const int pad = 16;
        const int infoW = 387;
        const QRect logoRect(left + pad, y + 10, width - infoW - pad * 3, headerH - 20);
        const QRect infoRect(right - infoW - pad, y + 12, infoW, headerH - 24);
        drawBox(infoRect, QColor(252, 252, 252), QColor(165, 165, 165));

        drawTextInRect(QRect(logoRect.left(), logoRect.top() + 10, logoRect.width(), 28), brandName, 19, true,
                       Qt::AlignHCenter | Qt::AlignVCenter, QColor(18, 18, 18), false);
        drawTextInRect(QRect(logoRect.left(), logoRect.top() + 40, logoRect.width(), 30), QString::fromUtf8("НАРЯД-ЗАМОВЛЕННЯ"), 12, true,
                       Qt::AlignHCenter | Qt::AlignVCenter, QColor(26, 26, 26), false);
        drawTextInRect(QRect(logoRect.left(), logoRect.top() + 74, logoRect.width(), 18), QString::fromUtf8("Сервісний документ"), 9, false,
                       Qt::AlignHCenter | Qt::AlignVCenter, QColor(92, 92, 92), false, QStringLiteral("Times New Roman"));

        const int labelX = infoRect.left() + 14;
        const int valueX = infoRect.left() + 132;
        const int rowH = 26;
        const int rowY = infoRect.top() + 8;
        drawTextInRect(QRect(labelX, rowY + rowH * 0, 110, rowH), QString::fromUtf8("№ наряду"), 10, true,
                       Qt::AlignLeft | Qt::AlignVCenter, QColor(70, 70, 70), false, QStringLiteral("Times New Roman"));
        drawTextInRect(QRect(valueX, rowY + rowH * 0, infoRect.right() - valueX - 10, rowH), QString::number(m_orderNumber), 13, true,
                       Qt::AlignRight | Qt::AlignVCenter, QColor(18, 18, 18), false, QStringLiteral("Times New Roman"));
        drawTextInRect(QRect(labelX, rowY + rowH * 1, 110, rowH), QString::fromUtf8("Дата"), 10, true,
                       Qt::AlignLeft | Qt::AlignTop, QColor(70, 70, 70), false, QStringLiteral("Times New Roman"));
        const QString headerDateText = closed.isEmpty() ? opened : QString::fromUtf8("%1\n%2").arg(opened, closed);
        drawTextInRect(QRect(valueX, rowY + rowH * 1, infoRect.right() - valueX - 12, rowH * 2), headerDateText, 8, true,
                       Qt::AlignRight | Qt::AlignTop, QColor(18, 18, 18), true, QStringLiteral("Times New Roman"));
        const int durationY = rowY + rowH * 3 - (partsReport ? 12 : 0);
        drawTextInRect(QRect(labelX, durationY, 110, rowH), QString::fromUtf8("Тривалість"), 10, true,
                       Qt::AlignLeft | Qt::AlignVCenter, QColor(70, 70, 70), false, QStringLiteral("Times New Roman"));
        drawTextInRect(QRect(valueX, durationY, infoRect.right() - valueX - 12, rowH), repairDuration, 8, true,
                       Qt::AlignRight | Qt::AlignVCenter, QColor(18, 18, 18), false, QStringLiteral("Times New Roman"));

        y += headerH + 12;
    };

    auto drawInfoBlock = [&]() {
        const auto leftRows = buildLeftRows();
        const auto rightRows = buildRightRows();
        const int leftValueW = colW - 22 - leftLblW - valuePad;
        const int rightValueW = colW - 26 - rightLblW - valuePad;
        const int rowsHeight = qMax(calcRowsHeight(leftRows, leftValueW),
                                    calcRowsHeight(rightRows, rightValueW));
        const int blockH = calcInfoBlockHeight();

        const QRect block(left, y, width, blockH);
        drawBox(block, Qt::white, QColor(165, 165, 165));

        const QRect leftCol(left + 12, y + 12, colW - 18, blockH - 22);
        const QRect rightCol(left + colW + gap, y + 12, colW - 20, blockH - 22);

        drawTextInRect(QRect(leftCol.left(), leftCol.top(), leftCol.width(), titleH), QString::fromUtf8("Дані автомобіля"), 12, true,
                       Qt::AlignLeft | Qt::AlignVCenter, QColor(24, 24, 24), false);
        drawTextInRect(QRect(rightCol.left(), rightCol.top(), rightCol.width(), titleH), QString::fromUtf8("Дані клієнта / замовлення"), 12, true,
                       Qt::AlignLeft | Qt::AlignVCenter, QColor(24, 24, 24), false);

        p.save();
        p.setPen(thinPen(QColor(178, 178, 178)));
        p.drawLine(left + colW + gap / 2, y + 12, left + colW + gap / 2, y + blockH - 12);
        p.restore();

        auto drawInfoRows = [&](const QRect& col, const QVector<InfoRow>& rows, int lblW) {
            int yy = col.top() + titleH + 12;
            const int valueW = col.width() - lblW - valuePad;
            for (const auto& row : rows) {
                const int valueH = qMax(15, textRectHeight(row.value, row.valuePt, row.valueBold, valueW));
                const int rowH = qMax(valueH + 2, lineHeight(8, true, QStringLiteral("Times New Roman")) + 3);
                drawTextInRect(QRect(col.left(), yy, lblW, rowH), row.label, 8, true,
                               Qt::AlignLeft | Qt::AlignTop, QColor(70, 70, 70), false, QStringLiteral("Times New Roman"));
                drawTextInRect(QRect(col.left() + lblW + valuePad, yy, valueW, rowH), row.value, row.valuePt, row.valueBold,
                               Qt::AlignLeft | Qt::AlignTop, QColor(20, 20, 20), true, QStringLiteral("Times New Roman"));
                yy += rowH + rowGap;
            }
        };

        drawInfoRows(leftCol, leftRows, leftLblW);
        drawInfoRows(rightCol, rightRows, rightLblW);

        y += blockH + 12;
    };

    auto startContentPage = [&]() {
        y = partsReport ? 44 : 0;
        drawHeaderBlock();
        drawInfoBlock();
    };


    auto mmToPx = [&](double mm) {
        return qMax(1, qRound((mm * pdf.logicalDpiY()) / 25.4));
    };

    int pageNumber = 1;
    int bottomReservePx = 96;
    std::function<void()> onAutoNewPage;
    std::function<void()> finishSimplePage;

    auto startNextContentPage = [&]() {
        if (!partsReport) PdfWebsiteFooter::draw(p);
        pdf.newPage();
        ++pageNumber;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        y = partsReport ? 44 : 0;
        if (onAutoNewPage) onAutoNewPage();
    };

    auto ensurePageSpace = [&](int needed) {
        if (y + needed <= pageBottom - bottomReservePx) return;
        if (finishSimplePage) finishSimplePage();
        startNextContentPage();
    };

    auto drawSectionTitle = [&](const QString& title) {
        ensurePageSpace(40);
        drawTextInRect(QRect(left, y, width, 28), title, 18, true, Qt::AlignLeft | Qt::AlignVCenter,
                       QColor(18, 18, 18), false);
        p.save();
        p.setPen(thinPen(QColor(165, 165, 165)));
        p.drawLine(left, y + 30, right, y + 30);
        p.restore();
        y += 36;
    };

    struct TableCell {
        QString text;
        Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter;
        QColor textColor = QColor(25, 25, 25);
        qreal fontPt = 9.0;
        int minFontPt = 4;
        bool bold = false;
        bool wordWrap = true;
        bool autoShrinkToFit = false;
        QString fontFamily = QStringLiteral("Times New Roman");
    };

    auto scaledWidths = [&](const QVector<int>& baseWidths) {
        QVector<int> result;
        result.reserve(baseWidths.size());
        int baseSum = 0;
        for (int w : baseWidths) baseSum += w;
        if (baseSum <= 0) return baseWidths;
        int used = 0;
        for (int i = 0; i < baseWidths.size(); ++i) {
            int w = (i == baseWidths.size() - 1)
                        ? (width - used)
                        : qMax(40, int(qRound(double(baseWidths[i]) * double(width) / double(baseSum))));
            result.push_back(w);
            used += w;
        }
        if (!result.isEmpty()) result.last() += (width - used);
        return result;
    };

    struct FittedCellFont {
        qreal pointSize = 9.0;
        qreal scaleX = 1.0;
    };

    auto fittedCellFont = [&](const TableCell& cell, int defaultPt, int maxWidth) {
        FittedCellFont fit;
        fit.pointSize = cell.fontPt > 0 ? qreal(cell.fontPt) : qreal(defaultPt);
        if (!cell.autoShrinkToFit || maxWidth <= 1 || cell.text.isEmpty()) {
            return fit;
        }

        const qreal minPt = qMax(4.0, qreal(cell.minFontPt));
        const QString singleLineText = cell.text.simplified();

        auto widthFor = [&](qreal pointSize) {
            QFont f(cell.fontFamily);
            f.setPointSizeF(pointSize);
            f.setBold(cell.bold);
            const QFontMetricsF fm(f, p.device());
            return fm.horizontalAdvance(singleLineText);
        };

        if (cell.wordWrap) {
            for (qreal candidate = fit.pointSize; candidate >= minPt; candidate -= 0.1) {
                if (widthFor(candidate) <= qreal(maxWidth)) {
                    fit.pointSize = candidate;
                    return fit;
                }
            }
            fit.pointSize = minPt;
            return fit;
        }

        for (qreal candidate = fit.pointSize; candidate >= minPt; candidate -= 0.1) {
            if (widthFor(candidate) <= qreal(maxWidth)) {
                fit.pointSize = candidate;
                fit.scaleX = 1.0;
                return fit;
            }
        }

        fit.pointSize = minPt;
        const qreal minWidth = widthFor(minPt);
        if (minWidth > 0.0) {
            fit.scaleX = qMax<qreal>(0.82, qreal(maxWidth) / minWidth);
        }
        return fit;
    };

    auto cellTextHeight = [&](const TableCell& cell, int defaultPt, int maxWidth) {
        const FittedCellFont fitted = fittedCellFont(cell, defaultPt, maxWidth);
        QFont f(cell.fontFamily);
        f.setPointSizeF(fitted.pointSize);
        f.setBold(cell.bold);
        QFontMetricsF fm(f, p.device());
        if (!cell.wordWrap) {
            return qCeil(fm.height()) + 4;
        }
        const int wrap = Qt::TextWordWrap;
        return qCeil(fm.boundingRect(QRectF(0, 0, qMax(1, maxWidth), 4000), wrap | Qt::AlignTop, cell.text).height()) + 8;
    };

    int totalPages = 1;

    auto drawPageNumber = [&]() {
        if (totalPages <= 1) return;
        const int pageNumberW = 120;
        const int pageNumberH = 18;
        const int pageNumberY = page.height() - mmToPx(12.0) - pageNumberH;
        drawTextInRect(QRect(right - pageNumberW, pageNumberY, pageNumberW, pageNumberH),
                       QString::fromUtf8("лист %1").arg(pageNumber), 10, false,
                       Qt::AlignRight | Qt::AlignVCenter, QColor(40, 40, 40), false);
    };

    const int sectionTitleHeight = 36;
    const int summaryW = 385;
    const bool hasMaterials = std::any_of(printParts.cbegin(), printParts.cend(), [](const auto& row) { return row.stockMaterial; });
    // Size the customer summary to its actual rows, without the old unused
    // bottom area. Keep the administration report's separate material box.
    const int summaryRowStep = partsReport ? 42 : 40;
    const int summaryH = partsReport ? 223 + (hasMaterials ? 153 : 0)
                                    : 18 + (hasMaterials ? 4 : 3) * summaryRowStep + 32 + 12;
    const QString masterSignatureText = QString::fromUtf8("Майстер / відповідальна особа");
    const QString clientSignatureText = QString::fromUtf8("Клієнт");
    const int signatureTextH = qMax(20, qMax(
        textRectHeight(masterSignatureText, 9, false, 300, QStringLiteral("Times New Roman")),
        textRectHeight(clientSignatureText, 9, false, 300, QStringLiteral("Times New Roman")))) + 2;
    const int signatureLineTextBlockH = 6 + signatureTextH;
    const int stampTextH = qMax(18, textRectHeight(QString::fromUtf8("М.П."), 8, false, 300,
                                                  QStringLiteral("Times New Roman"))) + 2;

    auto detailedFooterReservePx = [&](bool compactMode) {
        if (partsReport) return mmToPx(2.5) + summaryH + mmToPx(10.0);
        const int footerBottomMarginPx = mmToPx(compactMode ? 3.0 : 8.0);
        const int footerTextH = pdfFooterText.isEmpty() ? 0 : qMax(18, textRectHeight(pdfFooterText, 9, false, width - 8, QStringLiteral("Times New Roman")));
        const int gapTableToSummary = mmToPx(compactMode ? 2.5 : 5.0);
        const int gapSummaryToSign = mmToPx(compactMode ? 4.0 : 24.0);
        const int gapSignToStamp = mmToPx(compactMode ? 2.5 : 34.0);
        const int gapStampToFooter = footerTextH > 0 ? mmToPx(compactMode ? 2.5 : 10.0) : 0;
        return gapTableToSummary + summaryH + gapSummaryToSign + signatureLineTextBlockH
             + gapSignToStamp + stampTextH + gapStampToFooter + footerTextH
             + footerBottomMarginPx + mmToPx(4.0);
    };

    auto simpleFooterReservePx = [&]() {
        return mmToPx(20.0);
    };

    auto drawSimpleFooter = [&]() {
        drawPageNumber();
    };

    finishSimplePage = drawSimpleFooter;

    auto drawFooter = [&](double worksTotalValue, double partsTotalValue) {
        const int signW = 300;
        const int leftSignX = left + 8;
        const int rightSignX = right - signW - 8;

        const int gapTableToSummaryTargetPx = mmToPx(5.0);
        const int gapTableToSummaryMinPx = mmToPx(2.5);
        const int gapSummaryToSignTargetPx = mmToPx(24.0);
        const int gapSummaryToSignMinPx = mmToPx(4.0);
        const int gapSignToStampTargetPx = mmToPx(34.0);
        const int gapSignToStampMinPx = mmToPx(2.5);
        const int footerBottomMarginPx = mmToPx(3.0);
        const int footerTextH = pdfFooterText.isEmpty() ? 0 : qMax(18, textRectHeight(pdfFooterText, 9, false, width - 8, QStringLiteral("Times New Roman")));
        const int gapStampToFooterTargetPx = footerTextH > 0 ? mmToPx(10.0) : 0;
        const int gapStampToFooterMinPx = footerTextH > 0 ? mmToPx(2.5) : 0;
        const int footerY = footerTextH > 0 ? (pageBottom - footerBottomMarginPx - footerTextH) : (pageBottom - footerBottomMarginPx);

        const int maxStampY = footerY - gapStampToFooterMinPx - stampTextH;
        const int maxSignY = maxStampY - gapSignToStampMinPx - signatureLineTextBlockH;
        const int maxSummaryTop = maxSignY - gapSummaryToSignMinPx - summaryH;

        int summaryTop = qMin(y + gapTableToSummaryTargetPx, maxSummaryTop);
        if (partsReport) summaryTop = y + gapTableToSummaryMinPx;
        if (summaryTop < y + gapTableToSummaryMinPx) {
            summaryTop = y + gapTableToSummaryMinPx;
        }

        const QRect summaryRect(right - summaryW, summaryTop, summaryW, summaryH);
        drawBox(QRect(summaryRect.topLeft(), QSize(summaryW, partsReport && hasMaterials ? 223 : summaryH)),
                QColor(252, 252, 252), QColor(158, 158, 158));

        auto drawSummaryRow = [&](int rowIndex, const QString& label, const QString& value, bool bold = false, int pt = 13) {
            const int rowY = summaryRect.top() + 18 + rowIndex * summaryRowStep;
            drawTextInRect(QRect(summaryRect.left() + 18, rowY, 210, 32), label, pt, bold,
                           Qt::AlignLeft | Qt::AlignVCenter, QColor(35, 35, 35), false, QStringLiteral("Times New Roman"));
            drawTextInRect(QRect(summaryRect.right() - 170, rowY, 150, 32), value, pt, bold,
                           Qt::AlignRight | Qt::AlignVCenter, QColor(18, 18, 18), false, QStringLiteral("Times New Roman"));
        };

        const double discountValue = FormatUtils::discountAmount(m_discount->value(), m_discountMode->currentData().toString(), worksTotalValue);
        const double totalValue = FormatUtils::roundMoneyValue(qMax(0.0, worksTotalValue + partsTotalValue - discountValue));
        drawSummaryRow(0, partsReport ? QString::fromUtf8("Закупка") : QString::fromUtf8("Роботи"),
                       FormatUtils::pdfMoney(partsReport ? purchaseTotalVal - materialPurchaseTotal : worksTotalValue));
        drawSummaryRow(1, partsReport ? QString::fromUtf8("Продаж") : QString::fromUtf8("Запчастини"),
                       FormatUtils::pdfMoney(partsTotalValue - materialSaleTotal));
        p.save();
        p.setPen(thinPen(QColor(178, 178, 178)));
        const int summaryLineRaisePx = qMax(1, qRound((2.0 * pdf.logicalDpiY()) / 25.4));
        const int summaryLineY = summaryRect.top() + (partsReport ? 106 - summaryLineRaisePx
                                                                               : 18 + 3 * summaryRowStep - 3);
        p.drawLine(summaryRect.left() + 18, summaryLineY, summaryRect.right() - 18, summaryLineY);
        p.restore();
        if (!partsReport) drawSummaryRow(2, QString::fromUtf8("Знижка"), FormatUtils::pdfMoney(discountValue));
        drawSummaryRow(partsReport ? 2 : 3, partsReport ? QString::fromUtf8("НАЦІНКА") : QString::fromUtf8("РАЗОМ"),
                       FormatUtils::pdfMoney(partsReport ? partsTotalValue - materialSaleTotal - purchaseTotalVal + materialPurchaseTotal : totalValue), true, 13);

        if (partsReport) {
            drawSummaryRow(3, QString::fromUtf8("Роботи"), FormatUtils::pdfMoney(worksTotalValue), true);
            if (hasMaterials) {
                const QRect materialSummary(summaryRect.left(), summaryRect.top() + 231, summaryW, 145);
                drawBox(materialSummary, QColor(252, 252, 252), QColor(158, 158, 158));
                const QStringList labels = {QString::fromUtf8("Склад: закупка"), QString::fromUtf8("Склад: продаж"), QString::fromUtf8("Склад: націнка")};
                const QList<double> values = {materialPurchaseTotal, materialSaleTotal, materialSaleTotal - materialPurchaseTotal};
                for (int row = 0; row < 3; ++row) {
                    const int rowY = materialSummary.top() + 18 + row * 42;
                    drawTextInRect(QRect(materialSummary.left() + 18, rowY, 210, 32), labels.at(row), 10, row == 2,
                        Qt::AlignLeft | Qt::AlignVCenter, QColor(35, 35, 35), false, QStringLiteral("Times New Roman"));
                    drawTextInRect(QRect(materialSummary.right() - 170, rowY, 150, 32), FormatUtils::pdfMoney(values.at(row)), 12, row == 2,
                        Qt::AlignRight | Qt::AlignVCenter, QColor(18, 18, 18), false, QStringLiteral("Times New Roman"));
                }
            }
            drawPageNumber();
            return;
        }

        if (hasMaterials) drawSummaryRow(4, QString::fromUtf8("Матеріали СТО"), FormatUtils::pdfMoney(materialSaleTotal), false, 11);
        int contentBottomY = summaryRect.y() + summaryRect.height();
        if (!m_pdfRecommendations.isEmpty()) {
            int textY = contentBottomY + mmToPx(3.0);
            int blockTop = textY;
            const int padding = mmToPx(2.0);
            auto closeRecommendationBlock = [&]() {
                p.save();p.setBrush(Qt::NoBrush);p.setPen(thinPen(QColor(158,158,158)));
                p.drawRect(QRect(left,blockTop,width,textY + padding - blockTop));p.restore();
                textY += padding;
            };
            QFont recommendationFont(QStringLiteral("Times New Roman"), 10);
            recommendationFont.setItalic(true);
            const int headingH = mmToPx(6.0);
            auto nextRecommendationPage = [&]() {
                totalPages = qMax(totalPages, pageNumber + 1);
                drawPageNumber();
                // Continuations need only the recommendation heading, rather
                // than repeating the whole vehicle/order card on every page.
                {
                    QScopedValueRollback<std::function<void()>> continuation(onAutoNewPage, {});
                    startNextContentPage();
                }
                textY = y;
                blockTop = textY;
            };
            auto heading = [&]() {
                textY += padding;
                p.save();
                QFont headingFont(QStringLiteral("Times New Roman"), 11, QFont::Bold);
                headingFont.setUnderline(true);
                p.setFont(headingFont);
                p.setPen(QColor(24, 24, 24));
                p.drawText(QRect(left + padding, textY, width - 2*padding, headingH), Qt::AlignLeft | Qt::AlignTop,
                           tr("Рекомендації:"));
                p.restore();
                textY += headingH;
            };
            if (textY + headingH + mmToPx(6.0) > maxSignY - gapSummaryToSignMinPx)
                nextRecommendationPage();
            heading();
            for (const QString& paragraph : m_pdfRecommendations.split('\n')) {
                QTextLayout text(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, recommendationFont, p.device());
                QTextOption option;
                option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                text.setTextOption(option);
                text.beginLayout();
                while (true) {
                    auto line = text.createLine();
                    if (!line.isValid()) break;
                    line.setLineWidth(width - 2*padding);
                }
                text.endLayout();
                for (int i = 0; i < text.lineCount(); ++i) {
                    const auto line = text.lineAt(i);
                    const int height = qCeil(line.height()) + 2;
                    if (textY + height + padding > maxSignY - gapSummaryToSignMinPx) {
                        closeRecommendationBlock();
                        nextRecommendationPage();
                        heading();
                    }
                    p.save();
                    p.setPen(QColor(24, 24, 24));
                    line.draw(&p, QPointF(left + padding, textY));
                    p.restore();
                    textY += height;
                }
            }
            closeRecommendationBlock();
            contentBottomY = textY;
        }
        // Never pull footer text back over signatures to fit the page. Move
        // the complete signature/stamp block to a continuation when necessary.
        if (contentBottomY + gapSummaryToSignMinPx > maxSignY) {
            totalPages = qMax(totalPages, pageNumber + 1);
            drawPageNumber();
            QScopedValueRollback<std::function<void()>> continuation(onAutoNewPage, {});
            startNextContentPage();
            contentBottomY = y;
        }
        int signY = qMin(contentBottomY + gapSummaryToSignTargetPx, maxSignY);
        if (signY < contentBottomY + gapSummaryToSignMinPx) {
            signY = contentBottomY + gapSummaryToSignMinPx;
        }

        int stampY = qMin(signY + signatureLineTextBlockH + gapSignToStampTargetPx, maxStampY);
        if (stampY < signY + signatureLineTextBlockH + gapSignToStampMinPx) {
            stampY = signY + signatureLineTextBlockH + gapSignToStampMinPx;
        }

        p.save();
        p.setPen(thinPen(QColor(145, 145, 145)));
        p.drawLine(leftSignX, signY, leftSignX + signW, signY);
        p.drawLine(rightSignX, signY, rightSignX + signW, signY);
        p.restore();
        drawTextInRect(QRect(leftSignX, signY + 6, signW, signatureTextH), masterSignatureText, 9, false,
                       Qt::AlignHCenter | Qt::AlignTop, QColor(85, 85, 85), true, QStringLiteral("Times New Roman"));
        drawTextInRect(QRect(rightSignX, signY + 6, signW, signatureTextH), clientSignatureText, 9, false,
                       Qt::AlignHCenter | Qt::AlignTop, QColor(85, 85, 85), true, QStringLiteral("Times New Roman"));
        drawTextInRect(QRect(leftSignX, stampY, signW, stampTextH), QString::fromUtf8("М.П."), 8, false,
                       Qt::AlignHCenter | Qt::AlignTop, QColor(105, 105, 105), false, QStringLiteral("Times New Roman"));

        const int signaturePage = pageNumber;
        if (!pdfFooterText.isEmpty()) {
            const int footerTextTargetY = stampY + stampTextH + gapStampToFooterTargetPx;
            int footerTextY = qMax(stampY + stampTextH + gapStampToFooterMinPx,
                                   qMin(footerTextTargetY, footerY));
            if (footerTextY + footerTextH > pageBottom - footerBottomMarginPx) {
                totalPages = qMax(totalPages, pageNumber + 1);
                drawPageNumber();
                QScopedValueRollback<std::function<void()>> continuation(onAutoNewPage, {});
                startNextContentPage();
                footerTextY = y;
            }
            // Lay out actual lines, including long user-supplied requisites.
            // A footer larger than one page must flow, never clip or overlap.
            const QFont footerFont(QStringLiteral("Times New Roman"), 9);
            for (const QString& paragraph : pdfFooterText.split('\n')) {
                QTextLayout text(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, footerFont, p.device());
                QTextOption option;
                option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                text.setTextOption(option);
                text.beginLayout();
                while (true) {
                    auto line = text.createLine();
                    if (!line.isValid()) break;
                    line.setLineWidth(width - 8);
                }
                text.endLayout();
                for (int i = 0; i < text.lineCount(); ++i) {
                    const auto line = text.lineAt(i);
                    const int lineHeight = qCeil(line.height()) + 2;
                    if (footerTextY + lineHeight > pageBottom - footerBottomMarginPx) {
                        totalPages = qMax(totalPages, pageNumber + 1);
                        drawPageNumber();
                        QScopedValueRollback<std::function<void()>> continuation(onAutoNewPage, {});
                        startNextContentPage();
                        footerTextY = y;
                    }
                    p.save();
                    p.setPen(QColor(110, 110, 110));
                    line.draw(&p, QPointF(leftSignX, footerTextY));
                    p.restore();
                    footerTextY += lineHeight;
                }
            }
        }

        drawPageNumber();

        const QImage footerLogoImg = configuredLogoImage();
        if (!footerLogoImg.isNull() && m_pdfRecommendations.isEmpty() && pageNumber == signaturePage) {
            const QSizeF footerLogoSize = configuredLogoSize(footerLogoImg);
            const int logoGapPx = mmToPx(10.0);
            const double logoX = leftSignX + qMax(0.0, (signW - footerLogoSize.width()) / 2.0);
            const double logoBottom = qMax(0.0, double(signY - logoGapPx));
            const double logoY = qMax(0.0, logoBottom - footerLogoSize.height());
            drawConfiguredLogo(QRectF(logoX, logoY, footerLogoSize.width(), footerLogoSize.height()));
        }
    };

    auto computeHeaderHeight = [&](const QVector<int>& colWidths, const QStringList& headers) {
        const int headerFontPt = 7;
        int computedHeaderH = 34;
        for (int i = 0; i < colWidths.size() && i < headers.size(); ++i) {
            computedHeaderH = qMax(computedHeaderH, textRectHeight(headers[i], headerFontPt, true, colWidths[i] - 12) + 10);
        }
        return computedHeaderH + 2;
    };

    auto computeRowHeights = [&](const QVector<int>& colWidths, const QVector<QVector<TableCell>>& rows) {
        const int bodyFontPt = 9;
        QVector<int> rowHeights;
        rowHeights.reserve(rows.size());
        for (const auto& row : rows) {
            int rowH = 28;
            for (int c = 0; c < row.size() && c < colWidths.size(); ++c) {
                rowH = qMax(rowH, cellTextHeight(row[c], bodyFontPt, colWidths[c] - 12) + 8);
            }
            rowHeights.push_back(rowH);
        }
        return rowHeights;
    };

    auto drawTableHeaderRow = [&](const QVector<int>& colWidths, const QStringList& headers, int headerH) {
        const int headerFontPt = 7;
        int x = left;
        for (int i = 0; i < colWidths.size() && i < headers.size(); ++i) {
            p.save();
            p.setPen(thinPen(QColor(150, 150, 150)));
            p.setBrush(QColor(242, 242, 242));
            p.drawRect(QRect(x, y, colWidths[i], headerH));
            p.restore();
            drawTextInRect(QRect(x + 6, y + 3, colWidths[i] - 12, headerH - 6), headers[i], headerFontPt, true,
                           Qt::AlignCenter | Qt::AlignVCenter, QColor(28, 28, 28));
            x += colWidths[i];
        }
        y += headerH;
    };

    auto drawTableBodyRow = [&](const QVector<int>& colWidths, const QVector<TableCell>& row, int rowH, int rowIndex) {
        const QColor fill = (rowIndex % 2 == 0) ? QColor(255, 255, 255) : QColor(248, 248, 248);
        int x = left;
        for (int c = 0; c < colWidths.size(); ++c) {
            p.save();
            p.setPen(thinPen(QColor(162, 162, 162)));
            p.setBrush(fill);
            p.drawRect(QRect(x, y, colWidths[c], rowH));
            p.restore();
            if (c < row.size()) {
                const TableCell& cell = row[c];
                const int textWidth = qMax(1, colWidths[c] - 10);
                const FittedCellFont fitted = fittedCellFont(cell, 9, textWidth);
                QString drawText = cell.text;
                p.save();
                QFont fittedFont(cell.fontFamily);
                fittedFont.setPointSizeF(fitted.pointSize);
                fittedFont.setBold(cell.bold);
                p.setFont(fittedFont);
                p.setPen(cell.textColor);
                const QRect drawRect = QRect(x + 5, y + 2, textWidth, rowH - 4).adjusted(0, 1, 0, -1);
                if (!cell.wordWrap) {
                    const QFontMetricsF fm(fittedFont, p.device());
                    const QString singleLineText = drawText.simplified();
                    const qreal naturalWidth = fm.horizontalAdvance(singleLineText);
                    const qreal actualScaleX = (cell.autoShrinkToFit && naturalWidth > qreal(textWidth) && naturalWidth > 0.0)
                        ? qMax<qreal>(0.82, qreal(textWidth) / naturalWidth)
                        : 1.0;
                    const qreal drawnWidth = naturalWidth * actualScaleX;
                    const qreal textHeight = fm.height();
                    const qreal baselineY = drawRect.top() + qMax<qreal>(0.0, (drawRect.height() - textHeight) / 2.0) + fm.ascent();
                    qreal textX = drawRect.left();
                    if (cell.align & Qt::AlignHCenter) {
                        textX = drawRect.left() + qMax<qreal>(0.0, (drawRect.width() - drawnWidth) / 2.0);
                    } else if (cell.align & Qt::AlignRight) {
                        textX = drawRect.left() + qMax<qreal>(0.0, drawRect.width() - drawnWidth);
                    }
                    if (actualScaleX < 0.999) {
                        p.save();
                        p.translate(textX, 0.0);
                        p.scale(actualScaleX, 1.0);
                        p.drawText(QPointF(0.0, baselineY), singleLineText);
                        p.restore();
                    } else if (naturalWidth <= qreal(textWidth) || !cell.autoShrinkToFit) {
                        p.drawText(QPointF(textX, baselineY), singleLineText);
                    } else {
                        drawText = fm.elidedText(drawText, Qt::ElideRight, textWidth);
                        p.drawText(drawRect, int(cell.align) | int(Qt::TextSingleLine), drawText);
                    }
                } else {
                    const int flags = int(cell.align) | int(Qt::TextWordWrap);
                    p.drawText(drawRect, flags, drawText);
                }
                p.restore();
            }
            x += colWidths[c];
        }
        y += rowH;
    };

    auto drawTableSection = [&](const QString& sectionTitle,
                                const QVector<int>& colWidths,
                                const QStringList& headers,
                                const QVector<QVector<TableCell>>& rows,
                                bool finalSection) {
        if (rows.isEmpty()) {
            return;
        }

        const int headerH = computeHeaderHeight(colWidths, headers);
        const QVector<int> rowHeights = computeRowHeights(colWidths, rows);

        auto remainingRowsHeight = [&](int startIndex) {
            int total = 0;
            for (int i = startIndex; i < rowHeights.size(); ++i) {
                total += rowHeights[i];
            }
            return total;
        };

        auto startSectionChunk = [&](int rowIndex) {
            while (true) {
                const bool canFinishOnThisPage = finalSection
                    && (y + sectionTitleHeight + headerH + remainingRowsHeight(rowIndex) + detailedFooterReservePx(true) <= pageBottom);
                bottomReservePx = canFinishOnThisPage ? detailedFooterReservePx(true) : simpleFooterReservePx();
                const int firstRowNeed = rowIndex < rowHeights.size() ? rowHeights[rowIndex] : 0;
                const int minNeed = sectionTitleHeight + headerH + firstRowNeed;
                if (y + minNeed <= pageBottom - bottomReservePx) {
                    break;
                }
                if (finishSimplePage) finishSimplePage();
                startNextContentPage();
            }
            drawSectionTitle(sectionTitle);
            drawTableHeaderRow(colWidths, headers, headerH);
        };

        startSectionChunk(0);

        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const bool canFinishOnThisPage = finalSection
                && (y + remainingRowsHeight(rowIndex) + detailedFooterReservePx(true) <= pageBottom);
            bottomReservePx = canFinishOnThisPage ? detailedFooterReservePx(true) : simpleFooterReservePx();
            if (y + rowHeights[rowIndex] > pageBottom - bottomReservePx) {
                if (finishSimplePage) finishSimplePage();
                startNextContentPage();
                startSectionChunk(rowIndex);
                const bool canFinishAfterBreak = finalSection
                    && (y + remainingRowsHeight(rowIndex) + detailedFooterReservePx(true) <= pageBottom);
                bottomReservePx = canFinishAfterBreak ? detailedFooterReservePx(true) : simpleFooterReservePx();
            }
            drawTableBodyRow(colWidths, rows[rowIndex], rowHeights[rowIndex], rowIndex);
        }
    };
    // Services data
    double worksTotal = 0.0;
    QVector<QVector<TableCell>> serviceRows;
    {
        const auto queryRows = OrderDialogPrintQueryService::loadServiceRows(detailOrderUid);
        for (const auto& row : queryRows) {
            const QString title = row.title.trimmed();
            if (title.isEmpty()) continue;
            const double qty = row.qty;
            const double price = row.price;
            const double amount = qty * price;
            worksTotal += amount;
            if (partsReport) {
                const QString performer = !row.linkedMasters.isEmpty() ? row.linkedMasters
                    : (!row.outsourceCompany.isEmpty() ? row.outsourceCompany : QString::fromUtf8("Не призначено"));
                serviceRows.push_back({
                    {title, Qt::AlignLeft | Qt::AlignVCenter},
                    {performer, Qt::AlignLeft | Qt::AlignVCenter},
                    {FormatUtils::pdfMoney(amount), Qt::AlignRight | Qt::AlignVCenter},
                    {row.hasPayrollMaster ? FormatUtils::pdfMoney(row.payrollAmount) : QString::fromUtf8("—"),
                     Qt::AlignRight | Qt::AlignVCenter}
                });
                for (int column : {2, 3}) {
                    serviceRows.last()[column].wordWrap = false;
                    serviceRows.last()[column].autoShrinkToFit = true;
                }
            } else serviceRows.push_back({
                {title, Qt::AlignLeft | Qt::AlignVCenter},
                {FormatUtils::pdfMoney(price), Qt::AlignCenter | Qt::AlignVCenter},
                {FormatUtils::pdfMoney(amount), Qt::AlignCenter | Qt::AlignVCenter}
            });
        }
    }

    // Parts data
    double partsTotalVal = 0.0;
    QVector<QVector<TableCell>> partRows;
    QVector<QVector<TableCell>> materialRows;
    if (partsReport) {
        for (const auto& row : printParts) {
            const double salePrice = row.price;
            const double saleAmount = qRound64(row.qty * salePrice * 100.0) / 100.0;
            partsTotalVal += saleAmount;
            const QString percent = row.purchasePrice > 0.0
                ? QString::number((salePrice / row.purchasePrice - 1.0) * 100.0, 'f', 2) + QStringLiteral(" %")
                : QString::fromUtf8("—");
            partRows.push_back({
                {row.purchaseDate, Qt::AlignCenter | Qt::AlignVCenter},
                {row.invoiceNumber, Qt::AlignLeft | Qt::AlignVCenter},
                {row.partNo, Qt::AlignLeft | Qt::AlignVCenter},
                {row.name, Qt::AlignLeft | Qt::AlignVCenter},
                {row.store, Qt::AlignLeft | Qt::AlignVCenter},
                {FormatUtils::smartQty(row.qty), Qt::AlignCenter | Qt::AlignVCenter},
                {FormatUtils::pdfMoney(row.purchasePrice), Qt::AlignRight | Qt::AlignVCenter},
                {FormatUtils::pdfMoney(salePrice), Qt::AlignRight | Qt::AlignVCenter},
                {FormatUtils::pdfMoney(saleAmount), Qt::AlignRight | Qt::AlignVCenter},
                {percent, Qt::AlignRight | Qt::AlignVCenter}
            });
            for (int column : {1, 2, 6, 7, 8}) {
                partRows.last()[column].wordWrap = false;
                partRows.last()[column].autoShrinkToFit = true;
            }
            for (auto& cell : partRows.last()) cell.fontPt = 7.5;
            if (row.customerSupplied) {
                for (int column : {6, 7, 8, 9}) partRows.last()[column].text.clear();
            }
            if (row.stockMaterial) materialRows.append(partRows.takeLast());
        }
        if (partRows.isEmpty() && materialRows.isEmpty()) {
            partRows.push_back({{QString()}, {QString()}, {QString()}, {QString::fromUtf8("Запчастини не додані")},
                               {QString()}, {QString()}, {QString()}, {QString()}, {QString()}, {QString()}});
            for (auto& cell : partRows.last()) cell.fontPt = 7.5;
        }
    } else {
        struct PartPrintRow {
            QString partNo;
            QString brand;
            QString name;
            double qty = 0.0;
            double price = 0.0;
            double amount = 0.0;
            bool stockMaterial = false;
            bool customerSupplied = false;
        };

        QVector<PartPrintRow> rawPartRows;
        const auto& queryRows = printParts;
        for (const auto& row : queryRows) {
            const QString partNo = row.partNo;
            const QString brand = row.brand;
            const QString name = row.name;
            if (name.isEmpty()) continue;
            const double qty = row.qty;
            const double price = row.price;
            const double amount = row.amount;
            partsTotalVal += amount;
            rawPartRows.push_back({partNo, brand, name, qty, price, amount, row.stockMaterial, row.customerSupplied});
        }

        for (const auto& row : rawPartRows) {
            QVector<TableCell> printableRow;

            TableCell nameCell;
            const QString partName = row.name.trimmed();
            const QString brandName = row.brand.trimmed();
            nameCell.text = brandName.isEmpty()
                ? partName
                : QStringLiteral("%1 %2").arg(partName, brandName).simplified();
            nameCell.align = Qt::AlignLeft | Qt::AlignVCenter;
            if (row.customerSupplied) nameCell.text += QString::fromUtf8(" (З/Ч клієнта)");
            printableRow.push_back(nameCell);

            printableRow.push_back({QString::number(row.qty, 'f', 1), Qt::AlignCenter | Qt::AlignVCenter});
            printableRow.push_back({row.customerSupplied ? QString() : FormatUtils::pdfMoney(row.price), Qt::AlignCenter | Qt::AlignVCenter});
            printableRow.push_back({row.customerSupplied ? QString() : FormatUtils::pdfMoney(row.amount), Qt::AlignCenter | Qt::AlignVCenter});
            if (row.stockMaterial) materialRows.push_back(printableRow);
            else partRows.push_back(printableRow);
        }
    }

    const QVector<int> serviceColWidths = partsReport ? scaledWidths({600, 315, 180, 180}) : scaledWidths({1010, 135, 130});
    const QStringList serviceHeaders = partsReport ? QStringList{QString::fromUtf8("Робота"),
                                        QString::fromUtf8("Майстер"), QString::fromUtf8("Вартість\n(грн)"),
                                        QString::fromUtf8("ЗП\n(грн)")} : QStringList{QString::fromUtf8("Назва"),
                                        QString::fromUtf8("Ціна\n(грн)"), QString::fromUtf8("Сума\n(грн)")};
    const QVector<int> legacyPartsColWidths = scaledWidths({840, 90, 105, 115});
    QVector<int> partsColWidths = {
        legacyPartsColWidths[0],
        legacyPartsColWidths[1],
        legacyPartsColWidths[2],
        legacyPartsColWidths[3]
    };
    if (partsReport) {
        const auto previous = scaledWidths({165, 340, 175, 80, 145, 145, 115});
        partsColWidths = {mmToPx(15), mmToPx(20), previous[0] - mmToPx(4), 0,
                          previous[2] - mmToPx(8), mmToPx(10), mmToPx(12), mmToPx(12),
                          mmToPx(8) * 2 + mmToPx(4), mmToPx(15)};
        int assigned = 0;
        for (const int columnWidth : partsColWidths) assigned += columnWidth;
        partsColWidths[3] = width - assigned;
    }
    const QStringList partsHeaders = partsReport ? QStringList{
        QString::fromUtf8("Дата"), QString::fromUtf8("Накладна"),
        QString::fromUtf8("OEM"), QString::fromUtf8("Назва запчастини"), QString::fromUtf8("Магазин"),
        QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Ціна закупки\n(грн)"),
        QString::fromUtf8("Ціна\nпродажу\n(грн)"), QString::fromUtf8("Сума продажу\n(грн)"), QString::fromUtf8("Доданий %")
    } : QStringList{
        QString::fromUtf8("Найменування запчастини"),
        QString::fromUtf8("Кіл-ть"),
        QString::fromUtf8("Ціна\n(грн)"),
        QString::fromUtf8("Сума\n(грн)")
    };

    startContentPage();
    onAutoNewPage = startContentPage;

    const int compactInterSectionGapPx = mmToPx(2.5);

    auto simulateTotalPages = [&](int contentStartY) {
        int simulatedPages = 1;
        int simulatedY = contentStartY;
        int simulatedBottomReservePx = 96;

        auto simulatedStartNextPage = [&]() {
            ++simulatedPages;
            simulatedY = contentStartY;
        };

        auto simulatedSimpleFooterReservePx = [&]() {
            return mmToPx(20.0);
        };

        auto computeRemainingHeight = [](const QVector<int>& heights, int startIndex) {
            int total = 0;
            for (int i = startIndex; i < heights.size(); ++i) total += heights[i];
            return total;
        };

        auto simulateTableSection = [&](const QVector<int>& colWidths,
                                        const QStringList& headers,
                                        const QVector<QVector<TableCell>>& rows,
                                        bool finalSection) {
            if (rows.isEmpty()) return;

            const int headerH = computeHeaderHeight(colWidths, headers);
            const QVector<int> rowHeights = computeRowHeights(colWidths, rows);

            auto startSectionChunk = [&](int rowIndex) {
                while (true) {
                    const bool canFinishOnThisPage = finalSection
                        && (simulatedY + sectionTitleHeight + headerH + computeRemainingHeight(rowHeights, rowIndex) + detailedFooterReservePx(true) <= pageBottom);
                    simulatedBottomReservePx = canFinishOnThisPage ? detailedFooterReservePx(true) : simulatedSimpleFooterReservePx();
                    const int firstRowNeed = rowIndex < rowHeights.size() ? rowHeights[rowIndex] : 0;
                    const int minNeed = sectionTitleHeight + headerH + firstRowNeed;
                    if (simulatedY + minNeed <= pageBottom - simulatedBottomReservePx) break;
                    simulatedStartNextPage();
                }
                simulatedY += sectionTitleHeight + headerH;
            };

            startSectionChunk(0);

            for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
                const bool canFinishOnThisPage = finalSection
                    && (simulatedY + computeRemainingHeight(rowHeights, rowIndex) + detailedFooterReservePx(true) <= pageBottom);
                simulatedBottomReservePx = canFinishOnThisPage ? detailedFooterReservePx(true) : simulatedSimpleFooterReservePx();
                if (simulatedY + rowHeights[rowIndex] > pageBottom - simulatedBottomReservePx) {
                    simulatedStartNextPage();
                    startSectionChunk(rowIndex);
                    const bool canFinishAfterBreak = finalSection
                        && (simulatedY + computeRemainingHeight(rowHeights, rowIndex) + detailedFooterReservePx(true) <= pageBottom);
                    simulatedBottomReservePx = canFinishAfterBreak ? detailedFooterReservePx(true) : simulatedSimpleFooterReservePx();
                }
                simulatedY += rowHeights[rowIndex];
            }
        };

        if (!serviceRows.isEmpty()) {
            simulateTableSection(serviceColWidths, serviceHeaders, serviceRows, partRows.isEmpty() && materialRows.isEmpty());
        }
        if (!partRows.isEmpty()) {
            if (!serviceRows.isEmpty()) simulatedY += compactInterSectionGapPx;
            simulateTableSection(partsColWidths, partsHeaders, partRows, materialRows.isEmpty());
            simulatedY += mmToPx(1.0);
        }
        if (!materialRows.isEmpty()) {
            simulatedY += compactInterSectionGapPx;
            QStringList materialHeaders = partsHeaders;
            materialHeaders[partsReport ? 3 : 0] = QString::fromUtf8("Матеріал");
            simulateTableSection(partsColWidths, materialHeaders, materialRows, true);
            simulatedY += mmToPx(1.0);
        }
        return simulatedPages;
    };

    totalPages = simulateTotalPages(y);

    if (!serviceRows.isEmpty()) {
        drawTableSection(QString::fromUtf8("Роботи"), serviceColWidths, serviceHeaders, serviceRows, partRows.isEmpty() && materialRows.isEmpty());
    }

    if (!partRows.isEmpty()) {
        if (!serviceRows.isEmpty()) {
            y += compactInterSectionGapPx;
        }
        drawTableSection(QString::fromUtf8("Запчастини"), partsColWidths, partsHeaders, partRows, materialRows.isEmpty());
        y += mmToPx(1.0);
    }

    if (!materialRows.isEmpty()) {
        y += compactInterSectionGapPx;
        QStringList materialHeaders = partsHeaders;
        materialHeaders[partsReport ? 3 : 0] = QString::fromUtf8("Матеріал");
        drawTableSection(QString::fromUtf8("Матеріали зі складу СТО"), partsColWidths, materialHeaders, materialRows, true);
        y += mmToPx(1.0);
    }
    bottomReservePx = detailedFooterReservePx(true);
    drawFooter(worksTotal, partsTotalVal);
    if (!partsReport) PdfWebsiteFooter::draw(p);
    p.end();
}
