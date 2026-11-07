#include "ui/common/dialogs/EmailEnvelopeDialog.h"

#include <QApplication>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

namespace {
QSize centimetersToPixels(QWidget* owner, qreal widthCm, qreal heightCm)
{
    QScreen* screen = nullptr;
    if (owner && owner->windowHandle()) screen = owner->windowHandle()->screen();
    if (!screen && owner) screen = owner->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();

    const qreal dpiX = screen ? screen->logicalDotsPerInchX() : 96.0;
    const qreal dpiY = screen ? screen->logicalDotsPerInchY() : 96.0;
    const qreal inchPerCm = 1.0 / 2.54;
    return QSize(qRound(widthCm * inchPerCm * dpiX), qRound(heightCm * inchPerCm * dpiY));
}

QString buttonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  min-height: 26px;"
        "  border-radius: 12px;"
        "  border: 1px solid rgba(245,248,255,185);"
        "  padding: 3px 10px;"
        "  color: #f7f9ff;"
        "  font-size: 9pt;"
        "  font-weight: 800;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6e7784, stop:0.18 #202833, stop:1 #070a0f);"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #f4f7fb, stop:0.15 #4d5968, stop:1 #0a1018);"
        "}"
        "QPushButton:pressed {"
        "  padding-top: 5px;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #111720, stop:1 #020305);"
        "}");
}
}

class SealedEnvelopeWidget final : public QWidget
{
public:
    explicit SealedEnvelopeWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(92);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setAccountLabel(const QString& account)
    {
        m_account = account.trimmed();
        update();
    }

    void setOpenProgress(qreal progress)
    {
        m_openProgress = qBound<qreal>(0.0, progress, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        QPainterPath shell;
        shell.addRoundedRect(outer, 18.0, 18.0);
        QLinearGradient shellGradient(outer.topLeft(), outer.bottomLeft());
        shellGradient.setColorAt(0.0, QColor(108, 119, 131));
        shellGradient.setColorAt(0.16, QColor(38, 47, 58));
        shellGradient.setColorAt(1.0, QColor(9, 13, 18));
        p.fillPath(shell, shellGradient);

        QPen rim(QColor(235, 241, 248, 170));
        rim.setWidthF(1.2);
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawPath(shell);

        const QRectF envelope = outer.adjusted(24.0, 16.0, -24.0, -16.0);
        const qreal r = 10.0;
        QPainterPath body;
        body.addRoundedRect(envelope, r, r);
        QLinearGradient paper(envelope.topLeft(), envelope.bottomLeft());
        paper.setColorAt(0.0, QColor(248, 250, 254));
        paper.setColorAt(1.0, QColor(192, 202, 215));
        p.fillPath(body, paper);

        p.setPen(QPen(QColor(68, 79, 93, 185), 1.1));
        p.drawPath(body);

        QPainterPath lowerFlaps;
        lowerFlaps.moveTo(envelope.left(), envelope.top() + envelope.height() * 0.35);
        lowerFlaps.lineTo(envelope.center().x(), envelope.bottom() - 5.0);
        lowerFlaps.lineTo(envelope.right(), envelope.top() + envelope.height() * 0.35);
        p.setPen(QPen(QColor(94, 106, 122, 150), 1.0));
        p.drawPath(lowerFlaps);

        const qreal lift = envelope.height() * 0.88 * m_openProgress;
        QPainterPath flap;
        flap.moveTo(envelope.left() + 4.0, envelope.top() + 2.0);
        flap.lineTo(envelope.right() - 4.0, envelope.top() + 2.0);
        flap.lineTo(envelope.center().x(), envelope.top() + envelope.height() * 0.56 - lift);
        flap.closeSubpath();
        QLinearGradient flapGradient(envelope.topLeft(), envelope.bottomLeft());
        flapGradient.setColorAt(0.0, QColor(255, 255, 255));
        flapGradient.setColorAt(1.0, QColor(176, 188, 204));
        p.fillPath(flap, flapGradient);
        p.setPen(QPen(QColor(72, 83, 98, 160), 1.0));
        p.drawPath(flap);

        const int sealAlpha = qBound(0, int((1.0 - m_openProgress) * 235.0), 235);
        if (sealAlpha > 8) {
            const QPointF c(envelope.center().x(), envelope.top() + envelope.height() * 0.50);
            QRadialGradient seal(c, 18.0);
            seal.setColorAt(0.0, QColor(255, 112, 112, sealAlpha));
            seal.setColorAt(1.0, QColor(140, 0, 0, sealAlpha));
            p.setPen(QPen(QColor(255, 238, 238, qMin(255, sealAlpha + 20)), 1.1));
            p.setBrush(seal);
            p.drawEllipse(c, 15.0, 15.0);
        }

        QFont titleFont = font();
        titleFont.setBold(true);
        titleFont.setPointSize(10);
        p.setFont(titleFont);
        p.setPen(QColor(18, 24, 32));
        const QString title = QString::fromUtf8("Новий лист");
        p.drawText(envelope.adjusted(10.0, envelope.height() * 0.43, -10.0, -10.0), Qt::AlignCenter, title);

        if (!m_account.isEmpty()) {
            QFont accountFont = font();
            accountFont.setPointSize(7);
            accountFont.setBold(true);
            p.setFont(accountFont);
            p.setPen(QColor(50, 59, 70));
            p.drawText(envelope.adjusted(8.0, envelope.height() * 0.65, -8.0, -6.0), Qt::AlignCenter, m_account);
        }
    }

private:
    QString m_account;
    qreal m_openProgress = 0.0;
};

EmailEnvelopeDialog::EmailEnvelopeDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Новий лист"));
    setWindowModality(Qt::ApplicationModal);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    const QSize targetSize = centimetersToPixels(parent ? parent : this, 7.0, 4.5);
    setFixedSize(targetSize);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    m_envelope = new SealedEnvelopeWidget(this);
    root->addWidget(m_envelope, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(8, 0, 8, 1);
    buttons->setSpacing(8);

    m_viewButton = new QPushButton(QString::fromUtf8("Переглянути"), this);
    m_postponeButton = new QPushButton(QString::fromUtf8("Відкласти"), this);
    m_viewButton->setCursor(Qt::PointingHandCursor);
    m_postponeButton->setCursor(Qt::PointingHandCursor);
    m_viewButton->setStyleSheet(buttonStyle());
    m_postponeButton->setStyleSheet(buttonStyle());

    buttons->addWidget(m_viewButton, 1);
    buttons->addWidget(m_postponeButton, 1);
    root->addLayout(buttons);

    connect(m_viewButton, &QPushButton::clicked, this, [this]() { playOpenAnimation(); });
    connect(m_postponeButton, &QPushButton::clicked, this, &QDialog::reject);

    QTimer::singleShot(0, this, [this]() {
        raise();
        activateWindow();
    });
}

void EmailEnvelopeDialog::setAccountLabel(const QString& account)
{
    if (m_envelope) m_envelope->setAccountLabel(account);
}

void EmailEnvelopeDialog::playOpenAnimation()
{
    if (!m_envelope) {
        accept();
        return;
    }

    m_viewButton->setEnabled(false);
    m_postponeButton->setEnabled(false);

    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(560);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (m_envelope) m_envelope->setOpenProgress(value.toReal());
    });
    connect(animation, &QVariantAnimation::finished, this, [this, animation]() {
        animation->deleteLater();
        accept();
    });
    animation->start();
}
