#include "ui/common/widgets/UnpaidBannerWidget.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QKeyEvent>

UnpaidBannerWidget::UnpaidBannerWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(qRound(28.0 * logicalDpiY() / 25.4));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QString::fromUtf8("Деталізація боргів"));
    setAccessibleName(QString::fromUtf8("Деталізація боргів"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

}

void UnpaidBannerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) emit clicked();
    QWidget::mouseReleaseEvent(event);
}

void UnpaidBannerWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Space) {
        emit clicked();
        event->accept();
    } else QWidget::keyPressEvent(event);
}

void UnpaidBannerWidget::setDebt(bool hasDebt, const QString& qtyText, const QString& sumText)
{
    const bool debtChanged = (m_hasDebt != hasDebt);

    m_hasDebt = hasDebt;
    m_qty = qtyText;
    m_sum = sumText;

    if (debtChanged || isVisible()) {
        update();
    }
}

void UnpaidBannerWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0, 0, -1, -1);

    // smoother, more uniform background fill
    QLinearGradient body(r.topLeft(), r.bottomLeft());
    if (m_hasDebt) {
        body.setColorAt(0.00, QColor(224, 58, 58));
        body.setColorAt(0.45, QColor(198, 24, 24));
        body.setColorAt(1.00, QColor(168, 12, 12));
    } else {
        body.setColorAt(0.00, QColor(182, 182, 182));
        body.setColorAt(0.45, QColor(154, 154, 154));
        body.setColorAt(1.00, QColor(126, 126, 126));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(body);
    p.setPen(QPen(Qt::black, 1.0));
    p.drawRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);

    // text: centered and auto-fitted to avoid clipping in the paid state
    const QRectF paidRect(r.left() + 22, r.top() + 18, r.width() - 44, r.height() - 36);
    const QRectF debtTitleRect(r.left() + 14, r.top() + 14, r.width() - 28, 30);
    const QRectF debtBodyRect(r.left() + 18, r.top() + 46, r.width() - 36, r.height() - 56);

    p.setPen(QColor(0, 0, 0));

    if (m_hasDebt) {
        QFont titleFont = font();
        titleFont.setBold(true);
        titleFont.setPointSize(12);
        p.setFont(titleFont);
        p.drawText(debtTitleRect,
                   Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                   QString::fromUtf8("НЕ СПЛАЧЕНО"));

        QFont infoFont = font();
        infoFont.setBold(true);
        infoFont.setPointSize(13);
        p.setFont(infoFont);
        const QString info = QString::fromUtf8("Боргів: %1\nСума: %2 грн").arg(m_qty, m_sum);
        for(int pt=13;pt>=9;--pt) {
            infoFont.setPointSize(pt);
            if(QFontMetrics(infoFont).boundingRect(debtBodyRect.toRect(),Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap,info).height()<=debtBodyRect.height()) break;
        }
        p.setFont(infoFont);
        p.drawText(debtBodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, info);
    } else {
        const QString paidText = QString::fromUtf8("Усі придбання\nоплачені");
        QFont paidFont = font();
        paidFont.setBold(true);
        paidFont.setPointSize(13);

        for (int pt = 13; pt >= 9; --pt) {
            paidFont.setPointSize(pt);
            QFontMetrics fm(paidFont);
            QRect br = fm.boundingRect(paidRect.toRect(), Qt::AlignCenter | Qt::TextWordWrap, paidText);
            if (br.width() <= paidRect.width() && br.height() <= paidRect.height()) {
                break;
            }
        }

        p.setFont(paidFont);
        p.drawText(paidRect, Qt::AlignCenter | Qt::TextWordWrap, paidText);
    }
}
