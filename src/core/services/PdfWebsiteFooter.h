#pragma once

#include "infrastructure/db/sqlite/Db.h"
#include "thirdparty/qrcodegen/qrcodegen.hpp"
#include <QPainter>
#include <QUrl>
#include <QImage>

namespace PdfWebsiteFooter {
inline QString url() {
    const QString value = Database::getSetting("settings.reports.pdf.footer_website", QString()).toString().trimmed();
    if (value.isEmpty()) return {};
    const QUrl parsed = QUrl::fromUserInput(value.contains(":") ? value : "https://" + value);
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (parsed.scheme() != "https" && parsed.scheme() != "http")) return {};
    return parsed.toString(QUrl::FullyEncoded);
}
inline QImage qr(const QString& address) {
    if (address.isEmpty()) return {};
    try {
        const auto code = qrcodegen::QrCode::encodeText(address.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = code.getSize();
        QImage image(size + 8, size + 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
            if (code.getModule(x, y)) image.setPixelColor(x + 4, y + 4, Qt::black);
        return image;
    } catch (const std::exception&) { return {}; }
}
inline int height(const QPaintDevice* device) {
    if (url().isEmpty()) return 0;
    const bool includeQr = Database::getSetting("settings.reports.pdf.website_qr", true).toBool();
    return qRound(device->logicalDpiY() * (includeQr ? 26.0 : 8.0) / 25.4);
}
inline void draw(QPainter& painter) {
    const QString address = url();
    if (address.isEmpty()) return;
    painter.save();
    painter.resetTransform();
    const int h = height(painter.device());
    const int margin = qRound(painter.device()->logicalDpiX() * 3.0 / 25.4);
    const int width = painter.device()->width();
    const int top = painter.device()->height() - h;
    int qrWidth = 0;
    if (Database::getSetting("settings.reports.pdf.website_qr", true).toBool()) {
        const QImage image = qr(address);
        if (!image.isNull()) {
            qrWidth = h - margin;
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRect(width - margin - qrWidth, top, qrWidth, qrWidth), image);
        }
    }
    painter.setPen(Qt::black);
    painter.setFont(QFont(QStringLiteral("Times New Roman"), 10));
    painter.drawText(QRect(margin, top, width - 3 * margin - qrWidth, h - margin),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, address);
    painter.restore();
}
}
